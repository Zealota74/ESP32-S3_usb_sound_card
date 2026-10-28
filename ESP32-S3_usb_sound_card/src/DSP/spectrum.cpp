/*
 * spectrum.cpp - analizator widma: część wspólna dla wszystkich silników.
 *
 * Tu leży: bufor kołowy (SPECTRUM_Feed), podział na pasma logarytmiczne, wybór silnika,
 * oraz obróbka słupków (moc -> dB -> 0..255, opadanie, kreska szczytu).
 * Silniki liczą wyłącznie moc pasm: spectrum_fft.cpp (FFT), spectrum_iir.cpp (bank IIR).
 */
#include <math.h>
#include <string.h>

#include "spectrum.h"
#include "dsp_config.h"
#include "spectrum_engine.h"

int16_t                  g_spectrumRing[ SPECTRUM_RING_SIZE ];
static uint32_t          s_wr = 0;          /* licznik zapisanych próbek */
static uint32_t          s_lastWr = 0;      /* wartość s_wr przy ostatniej analizie */
static uint32_t          s_lastBlock = 0;   /* ile próbek objęło ostatnie wywołanie silnika */

static const SpectrumEngine_t * const kEngine[ SPECTRUM_ENGINE_COUNT ] = { &g_spectrumEngineFft, &g_spectrumEngineIir, &g_spectrumEngineIirMr };
static int s_engine = SPECTRUM_ENGINE_DEFAULT;

static uint32_t   s_fs = 44100;
static SpectrumBand_t s_band[ SPECTRUM_MAX_BARS ];
static float      s_bandPow[ SPECTRUM_MAX_BARS ];   /* moc pasm z silnika (sinus pełnej skali = 1.0) */
static int        s_bars = 16;

static const uint16_t kFallMs[] = SPECTRUM_FALL_MS_LIST;
#define FALL_STEPS ( (int)( sizeof( kFallMs ) / sizeof( kFallMs[ 0 ] ) ) )
#define HOP        ( SPECTRUM_HOP )
static int    s_fallIdx = SPECTRUM_FALL_DEFAULT;
static int    s_fallPerFrame = 12;     /* opadanie słupka na ramkę (skala 0..255) */
static int    s_level[ SPECTRUM_MAX_BARS ];
static int    s_peak[ SPECTRUM_MAX_BARS ];
static int    s_hold[ SPECTRUM_MAX_BARS ];

#ifdef SPECTRUM_TEST
const float * SPECTRUM_DebugBandPower( void ) { return s_bandPow; }
#endif

// ---------------------------------------------------------------------------------------------
uint32_t SpectrumRing_Wr( void ) {
	return s_wr;
}
// ---------------------------------------------------------------------------------------------
/* Pasma logarytmiczne F_LOW..F_HIGH (górna krawędź ograniczona do 0,49 fs). */
static void buildBands( void ) {
	float fLow = (float)SPECTRUM_F_LOW;
	float fHigh = (float)SPECTRUM_F_HIGH;
	float fMax = 0.49f * (float)s_fs;
	float ratio;

	if ( fHigh > fMax ) fHigh = fMax;
	ratio = fHigh / fLow;

	for ( int b = 0; b < s_bars; b++ ) {
		float fl = fLow * powf( ratio, (float)b / (float)s_bars );
		float fh = fLow * powf( ratio, (float)( b + 1 ) / (float)s_bars );

		s_band[ b ].f_lo = fl;
		s_band[ b ].f_hi = fh;
		s_band[ b ].f0 = sqrtf( fl * fh );
	}
}
// ---------------------------------------------------------------------------------------------
/* Pasma + konfiguracja aktywnego silnika (zeruje jego stan). */
static void applyConfig( void ) {
	buildBands();
	kEngine[ s_engine ]->configure( s_fs, s_band, s_bars );
}
// ---------------------------------------------------------------------------------------------
static void computeFall( void ) {
	float frame_s = (float)HOP / (float)s_fs;                       /* czas jednej ramki */
	float full_s = (float)kFallMs[ s_fallIdx ] * 0.001f;            /* czas opadania z pełnej skali */
	int f = (int)( 255.0f * frame_s / full_s + 0.5f );

	s_fallPerFrame = ( f < 1 ) ? 1 : f;
}
// ---------------------------------------------------------------------------------------------
int SPECTRUM_FallSteps( void ) {
	return FALL_STEPS;
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_SetFall( int idx ) {
	if ( idx < 0 ) idx = 0;
	if ( idx > FALL_STEPS - 1 ) idx = FALL_STEPS - 1;
	s_fallIdx = idx;
	computeFall();
}
// ---------------------------------------------------------------------------------------------
int SPECTRUM_GetFall( void ) {
	return s_fallIdx;
}
// ---------------------------------------------------------------------------------------------
uint32_t SPECTRUM_GetFallMs( void ) {
	return kFallMs[ s_fallIdx ];
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_Init( uint32_t sample_rate ) {
	if ( s_engine < 0 || s_engine >= SPECTRUM_ENGINE_COUNT ) s_engine = SPECTRUM_ENGINE_FFT;
	for ( int e = 0; e < SPECTRUM_ENGINE_COUNT; e++ ) kEngine[ e ]->init();
	memset( g_spectrumRing, 0, sizeof( g_spectrumRing ) );
	s_wr = 0;
	s_lastWr = 0;
	s_fs = sample_rate ? sample_rate : 44100;
	SPECTRUM_SetFall( s_fallIdx );   /* przelicz krok opadania dla bieżącej częstotliwości */
	applyConfig();
	SPECTRUM_Reset();
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_SetRate( uint32_t sample_rate ) {
	if ( sample_rate == 0 || sample_rate == s_fs ) return;
	s_fs = sample_rate;
	computeFall();
	applyConfig();
	SPECTRUM_Reset();
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_Feed( const int16_t * stereo, uint32_t frames ) {
	uint32_t wr = s_wr;

	for ( uint32_t i = 0; i < frames; i++ ) {
		g_spectrumRing[ ( wr + i ) & SPECTRUM_RING_MASK ] = (int16_t)( ( (int32_t)stereo[ i * 2 ] + (int32_t)stereo[ i * 2 + 1 ] ) >> 1 );
	}
	s_wr = wr + frames;
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_SetBars( int bars ) {
	if ( bars < 1 ) bars = 1;
	if ( bars > SPECTRUM_MAX_BARS ) bars = SPECTRUM_MAX_BARS;
	if ( bars == s_bars ) return;
	s_bars = bars;
	applyConfig();
	SPECTRUM_Reset();
}
// ---------------------------------------------------------------------------------------------
int SPECTRUM_GetBars( void ) {
	return s_bars;
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_SetEngine( int engine ) {
	if ( engine < 0 ) engine = 0;
	if ( engine > SPECTRUM_ENGINE_COUNT - 1 ) engine = SPECTRUM_ENGINE_COUNT - 1;
	if ( engine == s_engine ) return;
	s_engine = engine;
	applyConfig();
	SPECTRUM_Reset();
}
// ---------------------------------------------------------------------------------------------
int SPECTRUM_GetEngine( void ) {
	return s_engine;
}
// ---------------------------------------------------------------------------------------------
const char * SPECTRUM_EngineName( int engine ) {
	if ( engine < 0 || engine >= SPECTRUM_ENGINE_COUNT ) return "?";
	return kEngine[ engine ]->name;
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_Reset( void ) {
	memset( s_level, 0, sizeof( s_level ) );
	memset( s_peak, 0, sizeof( s_peak ) );
	memset( s_hold, 0, sizeof( s_hold ) );
	kEngine[ s_engine ]->reset();
	s_lastWr = SpectrumRing_Wr();   /* analiza zaczyna od próbek, które przyjdą po resecie */
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_Resync( void ) {
	memset( g_spectrumRing, 0, sizeof( g_spectrumRing ) );   /* okno FFT nie może mieszać próbek sprzed i po przerwie */
	kEngine[ s_engine ]->reset();
	s_lastWr = s_wr;   /* analiza zaczyna od próbek, które przyjdą po przerwie; słupki zostają */
}
// ---------------------------------------------------------------------------------------------
/* Moc pasm -> słupki: dB, skala 0..255, szybkie narastanie, wolne opadanie, kreska szczytu. */
static void updateBars( void ) {
	for ( int b = 0; b < s_bars; b++ ) {
		float db = 10.0f * log10f( s_bandPow[ b ] + 1e-12f );
		int target, lvl = s_level[ b ];

		target = (int)( ( db - (float)SPECTRUM_DB_FLOOR ) * ( 255.0f / (float)( SPECTRUM_DB_TOP - SPECTRUM_DB_FLOOR ) ) + 0.5f );
		if ( target < 0 ) target = 0;
		if ( target > 255 ) target = 255;

		/* szybkie narastanie, wolne opadanie */
		if ( target >= lvl ) lvl = target;
		else {
			lvl -= s_fallPerFrame;
			if ( lvl < target ) lvl = target;
		}
		s_level[ b ] = lvl;

		/* kreska szczytu: podtrzymanie, potem wolne opadanie */
		if ( lvl >= s_peak[ b ] ) {
			s_peak[ b ] = lvl;
			s_hold[ b ] = SPECTRUM_PEAK_HOLD;
		} else if ( s_hold[ b ] > 0 ) {
			s_hold[ b ]--;
		} else {
			s_peak[ b ] -= SPECTRUM_PEAK_FALL;
			if ( s_peak[ b ] < lvl ) s_peak[ b ] = lvl;
		}
	}
}
// ---------------------------------------------------------------------------------------------
int SPECTRUM_Process( void ) {
	uint32_t wr = SpectrumRing_Wr();
	uint32_t from = s_lastWr;
	int ok;

	if ( (uint32_t)( wr - from ) < (uint32_t)HOP ) return 0;
	s_lastBlock = wr - from;
	ok = kEngine[ s_engine ]->process( from, wr, s_bandPow );
	s_lastWr = wr;
	if ( !ok ) return 0;

	updateBars();
	return 1;
}
// ---------------------------------------------------------------------------------------------
/* Benchmark: ten sam kod co SPECTRUM_Process, ale na ostatnich HOP próbkach i bez wpływu na słupki. */
int SPECTRUM_Bench( void ) {
	uint32_t wr = SpectrumRing_Wr();

	return kEngine[ s_engine ]->process( wr - (uint32_t)HOP, wr, s_bandPow );
}
// ---------------------------------------------------------------------------------------------
uint32_t SPECTRUM_GetLastBlock( void ) {
	return s_lastBlock;
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_GetFrame( uint8_t * level, uint8_t * peak ) {
	for ( int b = 0; b < s_bars; b++ ) {
		level[ b ] = (uint8_t)s_level[ b ];
		peak[ b ] = (uint8_t)s_peak[ b ];
	}
}
