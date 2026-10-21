/*
 * audio.cpp - rdzeń podsystemu audio (kod niezależny od platformy).
 *
 * Łączy: źródło (audio_source.h) -> FIFO -> drift -> mnożnik głośności -> wyjście (audio_out.h).
 * Nie zawiera nic z USB, I2S ani Arduino.
 */
#include <math.h>
#include <string.h>

#include "audio.h"
#include "audio_config.h"
#include "audio_out.h"
#include "audio_source.h"
#include "audio_spectrum.h"
#include "fifo.h"
#include "drift.h"

#if AUDIO_SOURCE == AUDIO_SOURCE_USB
  #define AUDIO_SOURCE_OPS   (&AUDIO_SRC_USB)
#else
  #error "Nieobsługiwane AUDIO_SOURCE w audio_config.h"
#endif

/* ---- stan ---- */
static uint8_t  s_fifoStorage[ AUDIO_FIFO_SIZE_BYTES ];
static FIFO_t   s_fifo;
static DRIFT_t  s_drift;
static const AUDIO_Source_t * s_src = NULL;

/* Zapisuje task wyjścia, czyta loop (liczniki diagnostyczne). */
static volatile uint32_t s_blocks = 0;
static volatile uint32_t s_underruns = 0;
static volatile uint32_t s_fifoMin = 0xFFFFFFFFUL;
static volatile uint32_t s_fifoMax = 0;
static volatile bool     s_playing = false;

/* Głośność: mnożnik od źródła (cisza do pierwszego odczytu) i limit startowy. */
static volatile bool  s_guardActive = true;
static volatile float s_gainTarget = 0.0f;
static volatile float s_guardGain = 1.0f;

/* Widmo: liczone w loop (AUDIO_Poll), tylko gdy włączone. */
static bool     s_specOn = false;
static uint32_t s_specSeq = 0, s_specReadSeq = 0;
static uint32_t s_fftUs = 0;

/* Cache głośności (odświeżany w AUDIO_Poll) i znacznik czasu ostatniego odświeżenia. */
static AUDIO_Volume_t s_vol;
static uint32_t       s_tVol = 0;

// ---------------------------------------------------------------------------------------------
uint32_t AUDIO_Push( const uint8_t * data, uint32_t bytes ) {
	return FIFO_Write( &s_fifo, data, bytes );
}
// ---------------------------------------------------------------------------------------------
/* Wołane przez wyjście w jego wątku/przerwaniu. Bez printów i blokowania! */
static void audioFill( int16_t * out, uint32_t frames, void * ctx ) {
	static bool  playing = false;
	static float gPrev = 0.0f;
	uint32_t level = FIFO_Available( &s_fifo );
	float gNew;

	(void)ctx;

	if ( level < s_fifoMin ) s_fifoMin = level;
	if ( level > s_fifoMax ) s_fifoMax = level;

	/* start dopiero po napełnieniu FIFO do połowy (prefill) */
	if ( !playing && level >= AUDIO_FIFO_TARGET_BYTES ) {
		playing = true;
		DRIFT_Start( &s_drift );
	}

	if ( playing ) {
		uint32_t n = DRIFT_Process( &s_drift, out, frames );
		if ( n < frames ) { /* underrun: wygaś ogon i zrób ponowny prefill */
			DRIFT_FadeOutTail( out, n, frames );
			playing = false;
			s_underruns++;
		}
	} else {
		memset( out, 0, frames * AUDIO_BYTES_PER_FRAME ); /* strumień nigdy się nie urywa */
	}
	s_playing = playing;

	SPEC_Feed( out, frames ); /* tap dla widma: po drift, przed głośnością (widmo nie zależy od poziomu) */

	/* głośność (źródło + limit startowy): jeden mnożnik z rampą w obrębie bloku (bez trzasków) */
	gNew = s_gainTarget * ( s_guardActive ? s_guardGain : 1.0f );
	if ( !( gPrev == 1.0f && gNew == 1.0f ) ) {
		float step = ( gNew - gPrev ) / (float)frames;
		float g = gPrev;
		for ( uint32_t k = 0; k < frames; k++ ) {
			g += step;
			out[ k * 2 ]     = (int16_t)lrintf( (float)out[ k * 2 ] * g );
			out[ k * 2 + 1 ] = (int16_t)lrintf( (float)out[ k * 2 + 1 ] * g );
		}
	}
	gPrev = gNew;

	s_blocks++;
}
// ---------------------------------------------------------------------------------------------
int AUDIO_Init( const AUDIO_Config_t * cfg ) {
	AUDIO_OutConfig_t oc;
	int accepted;

	if ( cfg == NULL ) return AUDIO_ERR_PARAM;
	s_src = AUDIO_SOURCE_OPS;
	if ( s_src->init == NULL ) return AUDIO_ERR_PARAM;

	if ( !FIFO_Init( &s_fifo, s_fifoStorage, AUDIO_FIFO_SIZE_BYTES, AUDIO_BYTES_PER_FRAME ) ||
	     !DRIFT_Init( &s_drift, &s_fifo, AUDIO_FIFO_TARGET_BYTES ) ) {
		return AUDIO_ERR_FIFO;
	}

	SPEC_Init( AUDIO_SAMPLE_RATE );

	oc.sample_rate  = AUDIO_SAMPLE_RATE;
	oc.channels     = AUDIO_CHANNELS;
	oc.bits         = AUDIO_BITS;
	oc.block_frames = AUDIO_BLOCK_FRAMES;
	if ( !AUDIO_OUT_Init( &oc ) ) return AUDIO_ERR_OUT;

	/* głośność startowa: źródło, a przy niepowodzeniu awaryjny limit w torze audio */
	s_guardGain = powf( 10.0f, (float)cfg->startup_vol_db / 20.0f );
	accepted = s_src->init( AUDIO_Push, cfg->startup_vol_db );
	s_guardActive = !accepted;
	s_gainTarget = ( s_src->get_volume != NULL ) ? 0.0f : 1.0f; /* bez głośności ze źródła: bez tłumienia */

	if ( !AUDIO_OUT_Start( audioFill, NULL ) ) return AUDIO_ERR_OUT;
	return AUDIO_OK;
}
// ---------------------------------------------------------------------------------------------
const char * AUDIO_ErrorText( int err ) {
	switch ( err ) {
		case AUDIO_OK:        return "ok";
		case AUDIO_ERR_PARAM: return "bad config";
		case AUDIO_ERR_FIFO:  return "FIFO/DRIFT init failed";
		case AUDIO_ERR_OUT:   return "output (I2S) failed";
		default:              return "unknown";
	}
}
// ---------------------------------------------------------------------------------------------
void AUDIO_Poll( uint32_t now_ms ) {
	if ( s_src == NULL ) return;

	if ( s_src->poll != NULL ) s_src->poll();

	if ( s_src->get_volume != NULL && now_ms - s_tVol >= AUDIO_VOL_POLL_MS ) {
		s_tVol = now_ms;
		s_src->get_volume( &s_vol );
		if ( s_guardActive && s_vol.ctrl_count > 0 ) { /* host odezwał się: zwolnij limit */
			s_guardActive = false;
		}
		/* mute strony sterującej wycisza też wyjście (wcześniej tylko zmieniał napis na OLED) */
		s_gainTarget = s_vol.mute ? 0.0f : s_vol.gain;
	}

	if ( s_specOn ) {
		uint32_t t0 = AUDIO_OUT_TimeUs();
		if ( SPEC_Process() ) {
			s_fftUs = AUDIO_OUT_TimeUs() - t0;
			s_specSeq++;
		}
	}
}
// ---------------------------------------------------------------------------------------------
void AUDIO_GetVolume( AUDIO_Volume_t * v ) {
	*v = s_vol;
}
// ---------------------------------------------------------------------------------------------
void AUDIO_VolumeStep( int32_t ticks ) {
	if ( s_src != NULL && s_src->volume_step != NULL ) s_src->volume_step( ticks );
}
// ---------------------------------------------------------------------------------------------
void AUDIO_MuteToggle( void ) {
	if ( s_src != NULL && s_src->mute_toggle != NULL ) s_src->mute_toggle();
}
// ---------------------------------------------------------------------------------------------
void AUDIO_MediaKey( AUDIO_MediaKey_t key ) {
	if ( s_src != NULL && s_src->media_key != NULL ) s_src->media_key( key );
}
// ---------------------------------------------------------------------------------------------
int AUDIO_GuardActive( void ) {
	return s_guardActive ? 1 : 0;
}
// ---------------------------------------------------------------------------------------------
void AUDIO_GuardRelease( void ) {
	s_guardActive = false;
}
// ---------------------------------------------------------------------------------------------
void AUDIO_SpectrumEnable( int on ) {
	if ( on && !s_specOn ) {
		SPEC_Reset();
		s_specReadSeq = s_specSeq;
	}
	s_specOn = ( on != 0 );
}
// ---------------------------------------------------------------------------------------------
void AUDIO_SpectrumSetBars( int bars ) {
	SPEC_SetBars( bars );
}
// ---------------------------------------------------------------------------------------------
int AUDIO_SpectrumGetBars( void ) {
	return SPEC_GetBars();
}
// ---------------------------------------------------------------------------------------------
void AUDIO_SpectrumCycleBars( int dir ) {
	static const int opts[] = { 7, 16, 32 };
	const int n = (int)( sizeof( opts ) / sizeof( opts[ 0 ] ) );
	int cur = SPEC_GetBars();
	int idx = 1;

	for ( int i = 0; i < n; i++ ) {
		if ( opts[ i ] == cur ) idx = i;
	}
	idx = ( idx + ( dir >= 0 ? 1 : n - 1 ) ) % n;
	SPEC_SetBars( opts[ idx ] );
}
// ---------------------------------------------------------------------------------------------
void AUDIO_SpectrumFallStep( int dir ) {
	SPEC_SetFall( SPEC_GetFall() + ( dir >= 0 ? 1 : -1 ) );
}
// ---------------------------------------------------------------------------------------------
int AUDIO_SpectrumGetFallMs( void ) {
	return (int)SPEC_GetFallMs();
}
// ---------------------------------------------------------------------------------------------
int AUDIO_SpectrumFallIndex( void ) {
	return SPEC_GetFall();
}
// ---------------------------------------------------------------------------------------------
int AUDIO_SpectrumFallSteps( void ) {
	return SPEC_FallSteps();
}
// ---------------------------------------------------------------------------------------------
int AUDIO_GetSpectrum( AUDIO_Spectrum_t * out ) {
	if ( s_specSeq == s_specReadSeq ) return 0;
	s_specReadSeq = s_specSeq;
	out->bars = (uint8_t)SPEC_GetBars();
	SPEC_GetFrame( out->level, out->peak );
	return 1;
}
// ---------------------------------------------------------------------------------------------
void AUDIO_GetStats( AUDIO_Stats_t * out, int reset_minmax ) {
	uint32_t fMin = s_fifoMin, fMax = s_fifoMax;
	bool playing = s_playing;

	if ( fMin == 0xFFFFFFFFUL ) fMin = 0;
	if ( reset_minmax ) {
		s_fifoMin = 0xFFFFFFFFUL;
		s_fifoMax = 0;
	}

	out->playing = playing ? 1 : 0;
	out->blocks = s_blocks;
	out->underruns = s_underruns;
	out->overruns = FIFO_GetOverruns( &s_fifo );
	out->out_errors = AUDIO_OUT_GetErrors();
	out->fifo_bytes = FIFO_Available( &s_fifo );
	out->fifo_size = AUDIO_FIFO_SIZE_BYTES;
	out->fifo_min = fMin;
	out->fifo_max = fMax;
	out->consumed_frames = DRIFT_GetConsumedFrames( &s_drift );
	out->fft_us = s_fftUs;
	out->drift_ppm = playing ? (int32_t)( ( DRIFT_GetRatio( &s_drift ) - 1.0f ) * 1000000.0f ) : 0;

	memset( &out->src, 0, sizeof( out->src ) );
	if ( s_src != NULL && s_src->get_stats != NULL ) s_src->get_stats( &out->src );
}
