/*
 * spectrum.cpp - analizator widma: część wspólna dla wszystkich silników.
 *
 * Tu leży: bufor kołowy (SPECTRUM_Feed), podział na pasma logarytmiczne i wybór silnika.
 * Silniki liczą wyłącznie moc pasm: spectrum_fft.cpp (FFT), spectrum_iir.cpp (bank IIR), spectrum_iir_mr.cpp.
 * Wygląd słupków (dB, opadanie, kreska szczytu) nie należy do DSP: ui_bars.* w warstwie UI.
 */
#include <math.h>
#include <string.h>

#include "spectrum.h"
#include "dsp_config.h"
#include "spectrum_engine.h"

int16_t                  g_spectrumRing[ SPECTRUM_MAX_CHANNELS ][ SPECTRUM_RING_SIZE ];
static uint32_t          s_wr = 0;          /* licznik zapisanych próbek */
static uint32_t          s_lastWr = 0;      /* wartość s_wr przy ostatniej analizie */
static uint32_t          s_lastBlock = 0;   /* ile próbek objęło ostatnie wywołanie silnika */

static const SpectrumEngine_t * const kEngine[ SPECTRUM_ENGINE_COUNT ] = { &g_spectrumEngineFft, &g_spectrumEngineIir, &g_spectrumEngineIirMr };
static int s_engine = SPECTRUM_ENGINE_DEFAULT;

static uint32_t   s_fs = 44100;
static SpectrumBand_t s_band[ SPECTRUM_MAX_BANDS ];
static float      s_bandPow[ SPECTRUM_MAX_BANDS ];   /* moc pasm z silnika (sinus pełnej skali = 1.0) */
static int        s_bands = 16;                      /* pasma na kanał */
static int        s_channels = 1;                   /* 1 = miks mono, 2 = L i R osobno; kanały x pasma <= SPECTRUM_MAX_BANDS */

#define HOP        ( SPECTRUM_HOP )

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

	for ( int b = 0; b < s_bands; b++ ) {
		float fl = fLow * powf( ratio, (float)b / (float)s_bands );
		float fh = fLow * powf( ratio, (float)( b + 1 ) / (float)s_bands );

		s_band[ b ].f_lo = fl;
		s_band[ b ].f_hi = fh;
		s_band[ b ].f0 = sqrtf( fl * fh );
	}
}
// ---------------------------------------------------------------------------------------------
/* Pasma + konfiguracja aktywnego silnika (zeruje jego stan). */
static void applyConfig( void ) {
	buildBands();
	kEngine[ s_engine ]->configure( s_fs, s_band, s_bands, s_channels );
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_Init( uint32_t sample_rate ) {
	if ( s_engine < 0 || s_engine >= SPECTRUM_ENGINE_COUNT ) s_engine = SPECTRUM_ENGINE_FFT;
	for ( int e = 0; e < SPECTRUM_ENGINE_COUNT; e++ ) kEngine[ e ]->init();
	memset( g_spectrumRing, 0, sizeof( g_spectrumRing ) );
	s_wr = 0;
	s_lastWr = 0;
	s_fs = sample_rate ? sample_rate : 44100;
	applyConfig();
	SPECTRUM_Reset();
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_SetRate( uint32_t sample_rate ) {
	if ( sample_rate == 0 || sample_rate == s_fs ) return;
	s_fs = sample_rate;
	applyConfig();
	SPECTRUM_Reset();
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_Feed( const int16_t * stereo, uint32_t frames ) {
	uint32_t wr = s_wr;

#if SPECTRUM_MAX_CHANNELS > 1
	if ( s_channels > 1 ) {
		for ( uint32_t i = 0; i < frames; i++ ) {
			g_spectrumRing[ 0 ][ ( wr + i ) & SPECTRUM_RING_MASK ] = stereo[ i * 2 ];
			g_spectrumRing[ 1 ][ ( wr + i ) & SPECTRUM_RING_MASK ] = stereo[ i * 2 + 1 ];
		}
	} else
#endif
	{
		for ( uint32_t i = 0; i < frames; i++ ) {
			g_spectrumRing[ 0 ][ ( wr + i ) & SPECTRUM_RING_MASK ] = (int16_t)( ( (int32_t)stereo[ i * 2 ] + (int32_t)stereo[ i * 2 + 1 ] ) >> 1 );
		}
	}
	s_wr = wr + frames;
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_Configure( int channels, int bands ) {
	int maxBands;

	if ( channels < 1 ) channels = 1;
	if ( channels > SPECTRUM_MAX_CHANNELS ) channels = SPECTRUM_MAX_CHANNELS;
	maxBands = SPECTRUM_MAX_BANDS / channels;
	if ( bands < 1 ) bands = 1;
	if ( bands > maxBands ) bands = maxBands;
	if ( channels == s_channels && bands == s_bands ) return;
	if ( channels != s_channels ) memset( g_spectrumRing, 0, sizeof( g_spectrumRing ) );   /* bufory zmieniają znaczenie (miks <-> L/R) */
	s_channels = channels;
	s_bands = bands;
	applyConfig();
	SPECTRUM_Reset();
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_SetBands( int bands ) {
	SPECTRUM_Configure( s_channels, bands );
}
// ---------------------------------------------------------------------------------------------
int SPECTRUM_GetBands( void ) {
	return s_bands;
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_SetChannels( int channels ) {
	SPECTRUM_Configure( channels, s_bands );   /* liczba pasm zostanie ograniczona, jeśli się nie mieści */
}
// ---------------------------------------------------------------------------------------------
int SPECTRUM_GetChannels( void ) {
	return s_channels;
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
	kEngine[ s_engine ]->reset();
	s_lastWr = SpectrumRing_Wr();   /* analiza zaczyna od próbek, które przyjdą po resecie */
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_Resync( void ) {
	memset( g_spectrumRing, 0, sizeof( g_spectrumRing ) );   /* okno FFT nie może mieszać próbek sprzed i po przerwie */
	kEngine[ s_engine ]->reset();
	s_lastWr = s_wr;   /* analiza zaczyna od próbek, które przyjdą po przerwie */
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
	return ok ? 1 : 0;
}
// ---------------------------------------------------------------------------------------------
/* Benchmark: ten sam kod co SPECTRUM_Process, ale na ostatnich HOP próbkach i bez wpływu na kolejne ramki. */
int SPECTRUM_Bench( void ) {
	uint32_t wr = SpectrumRing_Wr();

	return kEngine[ s_engine ]->process( wr - (uint32_t)HOP, wr, s_bandPow );
}
// ---------------------------------------------------------------------------------------------
uint32_t SPECTRUM_GetLastBlock( void ) {
	return s_lastBlock;
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_GetPower( float * power ) {
	memcpy( power, s_bandPow, (size_t)( s_channels * s_bands ) * sizeof( float ) );
}
