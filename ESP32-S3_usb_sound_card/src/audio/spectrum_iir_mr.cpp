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
 */
#include <math.h>
#include <string.h>

#include "spectrum_engine.h"
#include "spectrum_biquad.h"

#ifndef AUDIO_SPECTRUM_IIR_SECTIONS
#define AUDIO_SPECTRUM_IIR_SECTIONS   2
#endif
#ifndef AUDIO_SPECTRUM_IIR_TAU_CYCLES
#define AUDIO_SPECTRUM_IIR_TAU_CYCLES 1.0f
#endif
#ifndef AUDIO_SPECTRUM_IIR_TAU_MIN_MS
#define AUDIO_SPECTRUM_IIR_TAU_MIN_MS 10
#endif
#ifndef AUDIO_SPECTRUM_IIR_TRIM_DB
#define AUDIO_SPECTRUM_IIR_TRIM_DB    0.0f
#endif

#define SECTIONS      ( AUDIO_SPECTRUM_IIR_SECTIONS )
#if SECTIONS < 1 || SECTIONS > 4
  #error "AUDIO_SPECTRUM_IIR_SECTIONS musi być z zakresu 1..4"
#endif
#define CHUNK         256                    /* próbek na wejściu stopnia 0 w jednym przebiegu */
#define MAX_BACKLOG   ( SPEC_RING_SIZE / 2 )
#define HANN_ENBW     1.5
#define MR_MAX_STAGES 10                     /* fs/512 przy najgłębszym stopniu */
#define MR_F_EDGE     0.40                   /* pasmo należy do stopnia, jeśli f_hi <= MR_F_EDGE * fs_stopnia */
#define LP_SECTIONS   3

/* Filtr decymatora (znormalizowany do fs stopnia): eliptyczny 6. rzędu, pasmo 0..0,2 fs (zafalowanie 0,05 dB,
 * wzmocnienie 1.0 w DC, żeby dziewięć kolejnych stopni nie osłabiało basu), zaporowe >= 55 dB od 0,30 fs. */
static const SpecBq_t kLpProto[ LP_SECTIONS ] = {
	{ 0.027153495081800694, 0.046953505889913517, 0.027153495081800701, -0.74358447649174342, 0.20562973773178161, 0, 0 },
	{ 1, 0.7059801393346562, 0.99999999999999989, -0.57119155259768717, 0.52229498984464728, 0, 0 },
	{ 1, 0.23027647949452978, 0.99999999999999989, -0.45835101446408744, 0.84897939370800557, 0, 0 },
};

typedef struct {
	SpecBp_t sec[ SECTIONS ];
	float    alpha, beta;
	float    e;
	uint8_t  stage;               /* stopień decymacji, na którym liczone jest pasmo */
} MrBand_t;

static MrBand_t s_mb[ SPEC_MAX_BARS ];
static int      s_bars = 0;
static int      s_stages = 1;                              /* liczba używanych stopni */
static SpecBq_t s_lp[ MR_MAX_STAGES ][ LP_SECTIONS ];       /* decymator ze stopnia k na k+1 */
static uint8_t  s_phase[ MR_MAX_STAGES ];                   /* indeks (0/1) następnej próbki do zachowania */
static float    s_x[ CHUNK ];                               /* wejście bieżącego stopnia */
static float    s_t[ CHUNK ];                               /* wyjście decymatora przed odrzuceniem próbek */
static float    s_y[ CHUNK ];                               /* wyjścia kolejnych sekcji jednego pasma */
static float    s_gain = 2.0f;

#ifdef AUDIO_SPECTRUM_TEST
uint32_t g_dbgWorkMr = 0;
#endif

// ---------------------------------------------------------------------------------------------
static void mrInit( void ) {
	/* brak ciężkich tablic */
}
// ---------------------------------------------------------------------------------------------
static void mrReset( void ) {
	for ( int b = 0; b < s_bars; b++ ) {
		for ( int s = 0; s < SECTIONS; s++ ) s_mb[ b ].sec[ s ].z1 = s_mb[ b ].sec[ s ].z2 = 0.0f;
		s_mb[ b ].e = 0.0f;
	}
	for ( int k = 0; k < MR_MAX_STAGES; k++ ) {
		for ( int j = 0; j < LP_SECTIONS; j++ ) s_lp[ k ][ j ].z1 = s_lp[ k ][ j ].z2 = 0.0f;
		s_phase[ k ] = 0;
	}
}
// ---------------------------------------------------------------------------------------------
static void mrConfigure( uint32_t fs, const SpecBand_t * band, int bars ) {
	double bwScale, enbwRatio;
	float tauMin = (float)AUDIO_SPECTRUM_IIR_TAU_MIN_MS * 0.001f;

	SpecBp_CascadeScale( SECTIONS, &bwScale, &enbwRatio );
	s_gain = (float)( 2.0 * HANN_ENBW / enbwRatio * pow( 10.0, (double)AUDIO_SPECTRUM_IIR_TRIM_DB / 10.0 ) );

	for ( int k = 0; k < MR_MAX_STAGES; k++ ) memcpy( s_lp[ k ], kLpProto, sizeof( kLpProto ) );

	s_bars = bars;
	s_stages = 1;
	for ( int b = 0; b < bars; b++ ) {
		int k = 0;
		double fsk;
		float tau = (float)AUDIO_SPECTRUM_IIR_TAU_CYCLES / band[ b ].f0;
		double beta;

		/* najgłębszy stopień, w którym pasmo mieści się pod MR_F_EDGE * fs_k */
		while ( k < MR_MAX_STAGES - 1 && (double)band[ b ].f_hi <= MR_F_EDGE * (double)fs / (double)( 1 << ( k + 1 ) ) ) k++;
		fsk = (double)fs / (double)( 1 << k );
		s_mb[ b ].stage = (uint8_t)k;
		if ( k + 1 > s_stages ) s_stages = k + 1;

		if ( tau < tauMin ) tau = tauMin;
		for ( int s = 0; s < SECTIONS; s++ ) {
			SpecBp_Design( &s_mb[ b ].sec[ s ], fsk, (double)band[ b ].f_lo, (double)band[ b ].f_hi, bwScale );
		}
		beta = exp( -1.0 / ( fsk * (double)tau ) );
		s_mb[ b ].beta = (float)beta;
		s_mb[ b ].alpha = (float)( 1.0 - beta );
		s_mb[ b ].e = 0.0f;
	}
	mrReset();
}
// ---------------------------------------------------------------------------------------------
static void runBand( MrBand_t * bd, const float * x, int cnt ) {
	const float * in = x;

	for ( int k = 0; k < SECTIONS; k++ ) {
		SpecBp_Run( &bd->sec[ k ], in, s_y, cnt );
		in = s_y;
	}
	bd->e = SpecEnv_Run( bd->e, bd->alpha, bd->beta, s_y, cnt );
#ifdef AUDIO_SPECTRUM_TEST
	g_dbgWorkMr += (uint32_t)cnt * SECTIONS;
#endif
}
// ---------------------------------------------------------------------------------------------
/* Stopień k -> k+1: filtr dolnoprzepustowy, potem co druga próbka (z pamiętaną fazą). Wynik w s_x. Zwraca liczbę próbek. */
static int decimate( int k, int cnt ) {
	const float * in = s_x;
	int ph = s_phase[ k ], m = 0;

	for ( int j = 0; j < LP_SECTIONS; j++ ) {
		SpecBq_Run( &s_lp[ k ][ j ], in, s_t, cnt );
		in = s_t;
	}
#ifdef AUDIO_SPECTRUM_TEST
	g_dbgWorkMr += (uint32_t)cnt * LP_SECTIONS;
#endif
	for ( int i = ph; i < cnt; i += 2 ) s_x[ m++ ] = s_t[ i ];   /* zapis nie wyprzedza odczytu: m <= i */
	s_phase[ k ] = (uint8_t)( ( ph + cnt ) & 1 );
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

		for ( int i = 0; i < cnt; i++ ) {
			s_x[ i ] = (float)g_specRing[ ( from + done + (uint32_t)i ) & SPEC_RING_MASK ] * ( 1.0f / 32768.0f );
		}
		for ( int k = 0; k < s_stages && cnt > 0; k++ ) {
			for ( int b = 0; b < s_bars; b++ ) {
				if ( s_mb[ b ].stage == k ) runBand( &s_mb[ b ], s_x, cnt );
			}
			if ( k < s_stages - 1 ) cnt = decimate( k, cnt );
		}
	}
	if ( (uint32_t)( SpecRing_Wr() - from ) > (uint32_t)SPEC_RING_SIZE ) {   /* producent nadpisał dane w trakcie liczenia */
		mrReset();
		return 0;
	}
	for ( int b = 0; b < s_bars; b++ ) power[ b ] = s_gain * s_mb[ b ].e;
	return 1;
}
// ---------------------------------------------------------------------------------------------
const SpecEngine_t g_specEngineIirMr = { "IIR-MR", mrInit, mrConfigure, mrReset, mrProcess };
