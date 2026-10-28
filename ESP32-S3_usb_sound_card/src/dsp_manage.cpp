/*
 * dsp_manage.cpp - zarządca modułów DSP (kod niezależny od platformy).
 *
 * Łączy tor audio (audio_manage.h) z analizatorem widma (DSP/spectrum.h):
 *  - rejestruje tap, którym wątek wyjścia karmi bufor widma (SPECTRUM_Feed),
 *  - w loop() (DSP_Poll) śledzi częstotliwość próbkowania i liczy ramki widma,
 *  - mierzy koszt silnika widma (us na hop).
 * Nic z USB, I2S ani Arduino.
 */
#include <stdint.h>

#include "dsp_manage.h"
#include "audio_manage.h"
#include "DSP/dsp_config.h"
#include "DSP/spectrum.h"

static_assert( DSP_SPECTRUM_MAX_BARS == SPECTRUM_MAX_BARS, "DSP_SPECTRUM_MAX_BARS musi byc rowne SPECTRUM_MAX_BARS" );

/* Widmo: liczone w loop (DSP_Poll), tylko gdy włączone. */
static bool     s_spectrumOn = false;
static uint32_t s_spectrumSeq = 0, s_spectrumReadSeq = 0;
static uint32_t s_spectrumRate = 0;              /* częstotliwość, dla której skonfigurowano widmo */
static uint32_t s_fftUs = 0;                 /* czas silnika widma na jeden hop próbek (us), średnia z okna */
static uint32_t s_fftAccUs = 0;              /* okno uśredniania: suma czasów SPECTRUM_Process ... */
static uint32_t s_fftAccN = 0;               /* ... i suma próbek audio, które objęły te wywołania */

// ---------------------------------------------------------------------------------------------
/* Zaczyna nowe okno pomiaru czasu widma (po zmianie liczby słupków, częstotliwości, włączeniu widma). */
static void fftStatReset( void ) {
	s_fftAccUs = 0;
	s_fftAccN = 0;
}
// ---------------------------------------------------------------------------------------------
/* Wołane w wątku wyjścia audio (tap). Bez printów i blokowania! Tu można dopiąć kolejne bloki DSP. */
static void dspTap( const int16_t * stereo, uint32_t frames ) {
	SPECTRUM_Feed( stereo, frames );
}
// ---------------------------------------------------------------------------------------------
void DSP_Init( void ) {
	s_spectrumRate = AUDIO_GetSampleRate();

	s_spectrumOn = false;
	s_spectrumSeq = s_spectrumReadSeq = 0;
	s_fftUs = 0;
	fftStatReset();

	SPECTRUM_Init( s_spectrumRate );
	AUDIO_SetTap( dspTap );   /* na końcu: bufor widma musi być gotowy, zanim wątek wyjścia zacznie go karmić */
}
// ---------------------------------------------------------------------------------------------
void DSP_Poll( void ) {
	uint32_t rate = AUDIO_GetSampleRate();

	/* widmo podąża za rzeczywistą częstotliwością wyjścia (SPECTRUM_* wołamy tylko z loop) */
	if ( rate != s_spectrumRate ) {
		s_spectrumRate = rate;
		SPECTRUM_SetRate( s_spectrumRate );
		fftStatReset();
	}

	if ( s_spectrumOn ) {
		uint32_t t0 = AUDIO_TimeUs();
		if ( SPECTRUM_Process() ) {
			/* Czas jednego wywołania zależy od zaległości próbek (silnik IIR liczy wszystkie próbki od ostatniego
			 * wywołania, a okres loop() zmienia się z zapełnieniem ekranu), więc uśredniamy w oknie ok. 0,5 s
			 * i normalizujemy do jednego hopa: us na SPECTRUM_HOP próbek. Budżet = hop / fs (np. 21,3 ms @48 kHz). */
			s_fftAccUs += AUDIO_TimeUs() - t0;
			s_fftAccN  += SPECTRUM_GetLastBlock();
			s_spectrumSeq++;
			if ( s_fftAccN >= s_spectrumRate / 2 ) {
				s_fftUs = (uint32_t)( (uint64_t)s_fftAccUs * SPECTRUM_HOP / s_fftAccN );
				fftStatReset();
			}
		}
	}
}
// ---------------------------------------------------------------------------------------------
void DSP_SpectrumEnable( int on ) {
	if ( on && !s_spectrumOn ) {
		SPECTRUM_Reset();
		fftStatReset();
		s_spectrumReadSeq = s_spectrumSeq;
	}
	s_spectrumOn = ( on != 0 );
}
// ---------------------------------------------------------------------------------------------
void DSP_SpectrumSetBars( int bars ) {
	SPECTRUM_SetBars( bars );
	fftStatReset();
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumGetBars( void ) {
	return SPECTRUM_GetBars();
}
// ---------------------------------------------------------------------------------------------
void DSP_SpectrumCycleBars( int dir ) {
	static const int opts[] = { 7, 16, 32, 64 };
	const int n = (int)( sizeof( opts ) / sizeof( opts[ 0 ] ) );
	int cur = SPECTRUM_GetBars();
	int idx = 1;

	for ( int i = 0; i < n; i++ ) {
		if ( opts[ i ] == cur ) idx = i;
	}
	idx = ( idx + ( dir >= 0 ? 1 : n - 1 ) ) % n;
	SPECTRUM_SetBars( opts[ idx ] );
	fftStatReset();
}
// ---------------------------------------------------------------------------------------------
void DSP_SpectrumFallStep( int dir ) {
	SPECTRUM_SetFall( SPECTRUM_GetFall() + ( dir >= 0 ? 1 : -1 ) );
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumGetFallMs( void ) {
	return (int)SPECTRUM_GetFallMs();
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumFallIndex( void ) {
	return SPECTRUM_GetFall();
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumFallSteps( void ) {
	return SPECTRUM_FallSteps();
}
// ---------------------------------------------------------------------------------------------
int DSP_GetSpectrum( DSP_Spectrum_t * out ) {
	if ( s_spectrumSeq == s_spectrumReadSeq ) return 0;
	s_spectrumReadSeq = s_spectrumSeq;
	out->bars = (uint8_t)SPECTRUM_GetBars();
	SPECTRUM_GetFrame( out->level, out->peak );
	return 1;
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumGetEngine( void ) {
	return SPECTRUM_GetEngine();
}
// ---------------------------------------------------------------------------------------------
void DSP_SpectrumSetEngine( int e ) {
	SPECTRUM_SetEngine( e );
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumEngineCount( void ) {
	return SPECTRUM_ENGINE_COUNT;
}
// ---------------------------------------------------------------------------------------------
const char * DSP_SpectrumEngineName( int e ) {
	return SPECTRUM_EngineName( e );
}
// ---------------------------------------------------------------------------------------------
uint32_t DSP_SpectrumGetUs( void ) {
	return s_fftUs;
}
