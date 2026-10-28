/*
 * spectrum_iir_mr.cpp - silnik banku filtrów IIR z decymacją oktawową (etap 2, "multirate").
 *
 * Te same pasma i te same filtry pasmowe co w spectrum_iir.cpp, ale pasmo liczone jest na strumieniu
 * zdecymowanym tak głęboko, jak pozwala jego górna krawędź: stopień k pracuje z fs / 2^k. Pasmo trafia do
 * najgłębszego stopnia, w którym f_hi <= MR_F_EDGE * fs_k. Dzięki temu:
 *  - koszt pasma maleje 2x na każdy stopień w dół (koszt całego banku ~ kilka pasm zamiast wszystkich),
 *  - filtr basowy pracuje przy f0/fs_k rzędu 0,1..0,4 zamiast 0,0005 (dużo lepsza numeryka float).
 *
 * Przejście ze stopnia k na k+1: dolnoprzepustowy filtr eliptyczny 6. rzędu (3 sekcje, te same współczynniki
 * dla każdego stopnia, bo znormalizowane do fs_k), potem odrzucenie co drugiej próbki. Przepuszcza do 0,2 fs_k
 * (zafalowanie 0,05 dB), tłumi > 55 dB od 0,3 fs_k, czyli to, co po decymacji wpadłoby w pasma stopnia k+1.
 *
 * Skala wyjścia i detektor energii identyczne jak w spectrum_iir.cpp. Faza decymacji jest zapamiętana
 * między wywołaniami, więc dowolna liczba próbek w bloku jest dopuszczalna.
 *
 * Kanały (stereo): stan pasm i decymatorów jest osobny dla każdego kanału (kanały x pasma <= SPECTRUM_MAX_BANDS),
 * współczynniki te same. Koszt decymatorów rośnie z liczbą kanałów, koszt pasm z ich łączną liczbą.
 */
#include <math.h>
#include <string.h>

#include "spectrum_engine.h"
#include "spectrum_biquad.h"

#ifndef SPECTRUM_IIR_SECTIONS
#define SPECTRUM_IIR_SECTIONS   2
#endif
#ifndef SPECTRUM_IIR_TAU_CYCLES
#define SPECTRUM_IIR_TAU_CYCLES 1.0f
#endif
#ifndef SPECTRUM_IIR_TAU_MIN_MS
#define SPECTRUM_IIR_TAU_MIN_MS 10
#endif
#ifndef SPECTRUM_IIR_TRIM_DB
#define SPECTRUM_IIR_TRIM_DB    0.0f
#endif

#define SECTIONS      ( SPECTRUM_IIR_SECTIONS )
#if SECTIONS < 1 || SECTIONS > 4
  #error "SPECTRUM_IIR_SECTIONS musi być z zakresu 1..4"
#endif
#define CHUNK         256                    /* próbek na wejściu stopnia 0 w jednym przebiegu */
#define MAX_BACKLOG   ( SPECTRUM_RING_SIZE / 2 )
#define HANN_ENBW     1.5
#define MR_MAX_STAGES 10                     /* fs/512 przy najgłębszym stopniu */
#define MR_F_EDGE     0.40                   /* pasmo należy do stopnia, jeśli f_hi <= MR_F_EDGE * fs_stopnia */
#define LP_SECTIONS   3

/* Filtr decymatora (znormalizowany do fs stopnia): eliptyczny 6. rzędu, pasmo 0..0,2 fs (zafalowanie 0,05 dB,
 * wzmocnienie 1.0 w DC, żeby dziewięć kolejnych stopni nie osłabiało basu), zaporowe >= 55 dB od 0,30 fs. */
static const SpectrumBq_t kLpProto[ LP_SECTIONS ] = {
	{ 0.027153495081800694, 0.046953505889913517, 0.027153495081800701, -0.74358447649174342, 0.20562973773178161, 0, 0 },
	{ 1, 0.7059801393346562, 0.99999999999999989, -0.57119155259768717, 0.52229498984464728, 0, 0 },
	{ 1, 0.23027647949452978, 0.99999999999999989, -0.45835101446408744, 0.84897939370800557, 0, 0 },
};

typedef struct {
	SpectrumBp_t sec[ SECTIONS ];
	float    alpha, beta;
	float    e;
	uint8_t  stage;               /* stopień decymacji, na którym liczone jest pasmo */
} MrBand_t;

static MrBand_t s_mb[ SPECTRUM_MAX_BANDS ];                  /* indeks: kanał * s_bands + pasmo */
static int      s_bands = 0;
static int      s_channels = 1;
static int      s_stages = 1;                              /* liczba używanych stopni */
static SpectrumBq_t s_lp[ SPECTRUM_MAX_CHANNELS ][ MR_MAX_STAGES ][ LP_SECTIONS ];   /* decymator ze stopnia k na k+1, osobny na kanał */
static uint8_t  s_phase[ SPECTRUM_MAX_CHANNELS ][ MR_MAX_STAGES ];                   /* indeks (0/1) następnej próbki do zachowania */
static float    s_x[ CHUNK ];                               /* wejście bieżącego stopnia */
static float    s_t[ CHUNK ];                               /* wyjście decymatora przed odrzuceniem próbek */
static float    s_y[ CHUNK ];                               /* wyjścia kolejnych sekcji jednego pasma */
static float    s_gain = 2.0f;

#ifdef SPECTRUM_TEST
uint32_t g_dbgWorkMr = 0;
#endif

// ---------------------------------------------------------------------------------------------
static void mrInit( void ) {
	/* brak ciężkich tablic */
}
// ---------------------------------------------------------------------------------------------
static void mrReset( void ) {
	for ( int b = 0; b < s_bands * s_channels; b++ ) {
		for ( int s = 0; s < SECTIONS; s++ ) s_mb[ b ].sec[ s ].z1 = s_mb[ b ].sec[ s ].z2 = 0.0f;
		s_mb[ b ].e = 0.0f;
	}
	for ( int ch = 0; ch < SPECTRUM_MAX_CHANNELS; ch++ ) {
		for ( int k = 0; k < MR_MAX_STAGES; k++ ) {
			for ( int j = 0; j < LP_SECTIONS; j++ ) s_lp[ ch ][ k ][ j ].z1 = s_lp[ ch ][ k ][ j ].z2 = 0.0f;
			s_phase[ ch ][ k ] = 0;
		}
	}
}
// ---------------------------------------------------------------------------------------------
static void mrConfigure( uint32_t fs, const SpectrumBand_t * band, int bands, int channels ) {
	double bwScale, enbwRatio;
	float tauMin = (float)SPECTRUM_IIR_TAU_MIN_MS * 0.001f;

	SpectrumBp_CascadeScale( SECTIONS, &bwScale, &enbwRatio );
	s_gain = (float)( 2.0 * HANN_ENBW / enbwRatio * pow( 10.0, (double)SPECTRUM_IIR_TRIM_DB / 10.0 ) );

	for ( int ch = 0; ch < SPECTRUM_MAX_CHANNELS; ch++ ) {
		for ( int k = 0; k < MR_MAX_STAGES; k++ ) memcpy( s_lp[ ch ][ k ], kLpProto, sizeof( kLpProto ) );
	}

	s_bands = bands;
	s_channels = channels;
	s_stages = 1;
	for ( int b = 0; b < bands; b++ ) {
		int k = 0;
		double fsk;
		float tau = (float)SPECTRUM_IIR_TAU_CYCLES / band[ b ].f0;
		double beta;

		/* najgłębszy stopień, w którym pasmo mieści się pod MR_F_EDGE * fs_k */
		while ( k < MR_MAX_STAGES - 1 && (double)band[ b ].f_hi <= MR_F_EDGE * (double)fs / (double)( 1 << ( k + 1 ) ) ) k++;
		fsk = (double)fs / (double)( 1 << k );
		s_mb[ b ].stage = (uint8_t)k;
		if ( k + 1 > s_stages ) s_stages = k + 1;

		if ( tau < tauMin ) tau = tauMin;
		for ( int s = 0; s < SECTIONS; s++ ) {
			SpectrumBp_Design( &s_mb[ b ].sec[ s ], fsk, (double)band[ b ].f_lo, (double)band[ b ].f_hi, bwScale );
		}
		beta = exp( -1.0 / ( fsk * (double)tau ) );
		s_mb[ b ].beta = (float)beta;
		s_mb[ b ].alpha = (float)( 1.0 - beta );
		s_mb[ b ].e = 0.0f;
	}
	for ( int ch = 1; ch < channels; ch++ ) memcpy( &s_mb[ ch * bands ], &s_mb[ 0 ], (size_t)bands * sizeof( MrBand_t ) );   /* te same filtry, osobny stan */
	mrReset();
}
// ---------------------------------------------------------------------------------------------
static void runBand( MrBand_t * bd, const float * x, int cnt ) {
	const float * in = x;

	for ( int k = 0; k < SECTIONS; k++ ) {
		SpectrumBp_Run( &bd->sec[ k ], in, s_y, cnt );
		in = s_y;
	}
	bd->e = SpectrumEnv_Run( bd->e, bd->alpha, bd->beta, s_y, cnt );
#ifdef SPECTRUM_TEST
	g_dbgWorkMr += (uint32_t)cnt * SECTIONS;
#endif
}
// ---------------------------------------------------------------------------------------------
/* Kanał ch, stopień k -> k+1: filtr dolnoprzepustowy, potem co druga próbka (z pamiętaną fazą). Wynik w s_x.
 * Zwraca liczbę próbek. */
static int decimate( int ch, int k, int cnt ) {
	const float * in = s_x;
	int ph = s_phase[ ch ][ k ], m = 0;

	for ( int j = 0; j < LP_SECTIONS; j++ ) {
		SpectrumBq_Run( &s_lp[ ch ][ k ][ j ], in, s_t, cnt );
		in = s_t;
	}
#ifdef SPECTRUM_TEST
	g_dbgWorkMr += (uint32_t)cnt * LP_SECTIONS;
#endif
	for ( int i = ph; i < cnt; i += 2 ) s_x[ m++ ] = s_t[ i ];   /* zapis nie wyprzedza odczytu: m <= i */
	s_phase[ ch ][ k ] = (uint8_t)( ( ph + cnt ) & 1 );
	return m;
}
// ---------------------------------------------------------------------------------------------
static int mrProcess( uint32_t from, uint32_t to, float * power ) {
	uint32_t n = to - from;

	if ( n > (uint32_t)MAX_BACKLOG ) {      /* nie nadążamy: ciągłość próbek przepadła */
		mrReset();
		return 0;
	}
	for ( uint32_t done = 0; done < n; done += CHUNK ) {
		int cnt = (int)( ( n - done > CHUNK ) ? CHUNK : ( n - done ) );

		for ( int ch = 0; ch < s_channels; ch++ ) {
			const int16_t * ring = g_spectrumRing[ ch ];
			MrBand_t * mb = &s_mb[ ch * s_bands ];
			int c = cnt;

			for ( int i = 0; i < cnt; i++ ) {
				s_x[ i ] = (float)ring[ ( from + done + (uint32_t)i ) & SPECTRUM_RING_MASK ] * ( 1.0f / 32768.0f );
			}
			for ( int k = 0; k < s_stages && c > 0; k++ ) {
				for ( int b = 0; b < s_bands; b++ ) {
					if ( mb[ b ].stage == k ) runBand( &mb[ b ], s_x, c );
				}
				if ( k < s_stages - 1 ) c = decimate( ch, k, c );
			}
		}
	}
	if ( (uint32_t)( SpectrumRing_Wr() - from ) > (uint32_t)SPECTRUM_RING_SIZE ) {   /* producent nadpisał dane w trakcie liczenia */
		mrReset();
		return 0;
	}
	for ( int b = 0; b < s_bands * s_channels; b++ ) power[ b ] = s_gain * s_mb[ b ].e;
	return 1;
}
// ---------------------------------------------------------------------------------------------
const SpectrumEngine_t g_spectrumEngineIirMr = { "IIR-MR", mrInit, mrConfigure, mrReset, mrProcess };
