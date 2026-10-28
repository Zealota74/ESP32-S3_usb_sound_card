/*
 * spectrum_iir.cpp - silnik banku filtrów IIR (etap 1: jedna częstotliwość próbkowania dla wszystkich pasm).
 *
 * Każde pasmo = kaskada 1..4 identycznych pasmowych biquadów (transformacja dwuliniowa z pre-warpingiem
 * krawędzi), potem detektor energii: y^2 wygładzone filtrem jednobiegunowym o stałej czasowej zależnej od f0.
 * Klocki DSP: spectrum_biquad.*. Współczynniki liczone w double przy konfiguracji, wykonanie w float.
 *
 * Rozdzielczość pasma jest stała względnie (stałe Q) i niezależna od długości okna, więc bas ma tyle
 * samo rozdzielczości co soprany. Ceną jest opóźnienie rosnące ku basowi (ok. Q/(pi*f0)).
 *
 * Skala wyjścia dopasowana do FFT dla szumu (suma mocy w paśmie); poprawka: SPECTRUM_IIR_TRIM_DB.
 * Pamięć: ok. 4 kB dla 64 pasm x 2 sekcje. Koszt rośnie liniowo z liczbą pasm (każde pasmo na pełnej fs);
 * tańsza wersja z decymacją: spectrum_iir_mr.cpp.
 *
 * Uwaga: filtry potrzebują KAŻDEJ próbki (stan), więc zaległości większe niż połowa bufora kołowego
 * kończą się resetem stanu i pominięciem ramki.
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

#define SECTIONS   ( SPECTRUM_IIR_SECTIONS )
#if SECTIONS < 1 || SECTIONS > 4
  #error "SPECTRUM_IIR_SECTIONS musi być z zakresu 1..4"
#endif
#define CHUNK      256                       /* próbek przetwarzanych jednym przebiegiem przez pasma */
#define MAX_BACKLOG ( SPECTRUM_RING_SIZE / 2 )   /* większe zaległości = nie nadążamy, reset */
#define HANN_ENBW  1.5                       /* równoważna szerokość szumowa okna Hanna w binach */

typedef struct {
	SpectrumBp_t sec[ SECTIONS ];
	float    alpha, beta;         /* detektor: e = beta * e + alpha * y^2, beta = 1 - alpha */
	float    e;                   /* wygładzona energia (sinus pełnej skali -> 0.5) */
} IirBand_t;

static IirBand_t s_ib[ SPECTRUM_MAX_BARS ];
static int       s_bars = 0;
static float     s_x[ CHUNK ];    /* próbki bloku przeskalowane do +-1.0 */
static float     s_y[ CHUNK ];    /* wyjście kolejnych sekcji filtru jednego pasma */
static float     s_gain = 2.0f;   /* energia detektora -> moc w skali widma (patrz iirConfigure) */

#ifdef SPECTRUM_TEST
uint32_t g_dbgWorkIir = 0;        /* licznik pracy: próbki x sekcje */
#endif

// ---------------------------------------------------------------------------------------------
static void iirInit( void ) {
	/* brak ciężkich tablic */
}
// ---------------------------------------------------------------------------------------------
static void iirReset( void ) {
	for ( int b = 0; b < s_bars; b++ ) {
		for ( int s = 0; s < SECTIONS; s++ ) s_ib[ b ].sec[ s ].z1 = s_ib[ b ].sec[ s ].z2 = 0.0f;
		s_ib[ b ].e = 0.0f;
	}
}
// ---------------------------------------------------------------------------------------------
static void iirConfigure( uint32_t fs, const SpectrumBand_t * band, int bars ) {
	double bwScale, enbwRatio;
	float tauMin = (float)SPECTRUM_IIR_TAU_MIN_MS * 0.001f;

	SpectrumBp_CascadeScale( SECTIONS, &bwScale, &enbwRatio );

	/* Normalizacja: szum biały w paśmie daje ten sam poziom co suma mocy binów FFT, czyli wygląd słupków
	 * jest zgodny po przełączeniu silników. Suma binów FFT z oknem Hanna zawyża moc o ENBW okna = 1.5
	 * (+1,76 dB; dotyczy też sinusa), a bank IIR mierzy energię wprost, więc tę samą stałą dokładamy tu.
	 * 2.0 = sinus A: <y^2> = A^2/2. Sinus w środku pasma wypada przy 2 sekcjach ok. 0,9 dB poniżej FFT.
	 * TRIM_DB koryguje całość. */
	s_gain = (float)( 2.0 * HANN_ENBW / enbwRatio * pow( 10.0, (double)SPECTRUM_IIR_TRIM_DB / 10.0 ) );

	s_bars = bars;
	for ( int b = 0; b < bars; b++ ) {
		float tau = (float)SPECTRUM_IIR_TAU_CYCLES / band[ b ].f0;
		double beta;

		if ( tau < tauMin ) tau = tauMin;
		for ( int k = 0; k < SECTIONS; k++ ) {
			SpectrumBp_Design( &s_ib[ b ].sec[ k ], (double)fs, (double)band[ b ].f_lo, (double)band[ b ].f_hi, bwScale );
		}
		beta = exp( -1.0 / ( (double)fs * (double)tau ) );
		s_ib[ b ].beta = (float)beta;
		s_ib[ b ].alpha = (float)( 1.0 - beta );
		s_ib[ b ].e = 0.0f;
	}
}
// ---------------------------------------------------------------------------------------------
/* Jedno pasmo przez blok: kolejne sekcje (przebiegi po całym bloku), potem detektor. */
static void runBand( IirBand_t * bd, const float * x, int cnt ) {
	const float * in = x;

	for ( int k = 0; k < SECTIONS; k++ ) {
		SpectrumBp_Run( &bd->sec[ k ], in, s_y, cnt );
		in = s_y;
	}
	bd->e = SpectrumEnv_Run( bd->e, bd->alpha, bd->beta, s_y, cnt );
#ifdef SPECTRUM_TEST
	g_dbgWorkIir += (uint32_t)cnt * SECTIONS;
#endif
}
// ---------------------------------------------------------------------------------------------
static int iirProcess( uint32_t from, uint32_t to, float * power ) {
	uint32_t n = to - from;

	if ( n > (uint32_t)MAX_BACKLOG ) {      /* nie nadążamy: ciągłość próbek przepadła */
		iirReset();
		return 0;
	}
	for ( uint32_t done = 0; done < n; done += CHUNK ) {
		int cnt = (int)( ( n - done > CHUNK ) ? CHUNK : ( n - done ) );

		for ( int i = 0; i < cnt; i++ ) {
			s_x[ i ] = (float)g_spectrumRing[ ( from + done + (uint32_t)i ) & SPECTRUM_RING_MASK ] * ( 1.0f / 32768.0f );
		}
		for ( int b = 0; b < s_bars; b++ ) runBand( &s_ib[ b ], s_x, cnt );
	}
	if ( (uint32_t)( SpectrumRing_Wr() - from ) > (uint32_t)SPECTRUM_RING_SIZE ) {   /* producent nadpisał dane w trakcie liczenia */
		iirReset();
		return 0;
	}
	for ( int b = 0; b < s_bars; b++ ) power[ b ] = s_gain * s_ib[ b ].e;
	return 1;
}
// ---------------------------------------------------------------------------------------------
const SpectrumEngine_t g_spectrumEngineIir = { "IIR", iirInit, iirConfigure, iirReset, iirProcess };
