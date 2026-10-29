/*
 * audio_manage.cpp - zarządca toru audio (kod niezależny od platformy).
 *
 * Łączy: źródło (audio_source.h) -> FIFO -> drift -> [tap dla DSP] -> mnożnik głośności -> wyjście (audio_out.h).
 * Nie zawiera nic z USB, I2S, Arduino ani analizy widma.
 */
#include <math.h>
#include <string.h>

#include "audio_manage.h"
#include "AUDIO/audio_clock.h"
#include "AUDIO/audio_config.h"
#include "AUDIO/audio_out.h"
#include "AUDIO/audio_source.h"
#include "AUDIO/fifo.h"
#include "AUDIO/drift.h"

#if AUDIO_SOURCE == AUDIO_SOURCE_USB
  #define AUDIO_SOURCE_OPS   (&AUDIO_SRC_USB)
#else
  #error "Nieobsługiwane AUDIO_SOURCE w audio_config.h"
#endif

static const uint32_t kRates[] = AUDIO_SAMPLE_RATE_LIST;
#define RATE_COUNT  ( (int)( sizeof( kRates ) / sizeof( kRates[ 0 ] ) ) )

/* FIFO musi mieć zapas ponad poziom docelowy także przy największej częstotliwości. */
static_assert( ( AUDIO_SAMPLE_RATE_MAX / 1000 ) * AUDIO_BYTES_PER_FRAME * AUDIO_FIFO_TARGET_MS * 3 / 2 <= AUDIO_FIFO_SIZE_BYTES,
               "AUDIO_FIFO_SIZE_BYTES za male dla AUDIO_SAMPLE_RATE_MAX i AUDIO_FIFO_TARGET_MS" );

/* ---- stan ---- */
static uint8_t  s_fifoStorage[ AUDIO_FIFO_SIZE_BYTES ];
static FIFO_t   s_fifo;
static DRIFT_t  s_drift;
static const AUDIO_Source_t * s_src = NULL;

/* Częstotliwość: s_rate = używana przez wyjście (zapisuje tylko task wyjścia),
 * s_rateReq = żądana (zapisuje loop), reszta = pomocnicze po stronie loop. */
static volatile uint32_t s_rate = AUDIO_SAMPLE_RATE;
static volatile uint32_t s_rateReq = AUDIO_SAMPLE_RATE;
static uint32_t s_srcRateSeen = AUDIO_SAMPLE_RATE;
static bool     s_run = false;   /* tylko task wyjścia: trwa odtwarzanie (po prefillu) */
static uint32_t s_fifoTarget = AUDIO_FIFO_TARGET_BYTES;

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

/* Tap dla DSP: ustawia loop (AUDIO_SetTap), wywołuje task wyjścia. Wskaźnik 32-bitowy, zapis atomowy. */
static AUDIO_TapFn volatile s_tap = NULL;

/* Cache głośności (odświeżany w AUDIO_Poll) i znacznik czasu ostatniego odświeżenia. */
static AUDIO_Volume_t s_vol;
static uint32_t       s_tVol = 0;

// ---------------------------------------------------------------------------------------------
/* Poziom docelowy FIFO dla danej częstotliwości: stały czas AUDIO_FIFO_TARGET_MS, pełne ramki. */
static uint32_t fifoTargetBytes( uint32_t rate ) {
	uint32_t b = (uint32_t)( (uint64_t)rate * AUDIO_BYTES_PER_FRAME * AUDIO_FIFO_TARGET_MS / 1000ULL );

	return b - ( b % AUDIO_BYTES_PER_FRAME );
}
// ---------------------------------------------------------------------------------------------
static bool rateSupported( uint32_t rate ) {
	for ( int i = 0; i < RATE_COUNT; i++ ) {
		if ( kRates[ i ] == rate ) return true;
	}
	return false;
}
// ---------------------------------------------------------------------------------------------
uint32_t AUDIO_Push( const uint8_t * data, uint32_t bytes ) {
	return FIFO_Write( &s_fifo, data, bytes );
}
// ---------------------------------------------------------------------------------------------
/* Wołane przez wyjście w jego wątku/przerwaniu. Bez printów i blokowania! */
static void audioFill( int16_t * out, uint32_t frames, void * ctx ) {
	static float gPrev = 0.0f;
	uint32_t level, req;
	float gNew;

	(void)ctx;

	/* Zmiana częstotliwości (np. host przełączył format): robimy ją tylko tu, w wątku wyjścia,
	 * bo zmienia zegar I2S i stan konsumenta FIFO. Ten blok wyjdzie jako cisza (po zmianie nie ma prefillu). */
	req = s_rateReq;
	if ( req != s_rate ) {
		if ( AUDIO_OUT_SetSampleRate( req ) ) {
			s_rate = req;
			s_fifoTarget = fifoTargetBytes( req );
			FIFO_Flush( &s_fifo );
			DRIFT_SetTarget( &s_drift, s_fifoTarget );
			s_run = false;
			s_fifoMin = 0xFFFFFFFFUL;
			s_fifoMax = 0;
		} else {
			s_rateReq = s_rate; /* wyjście odrzuciło zmianę: zostajemy przy dotychczasowej */
		}
	}

	level = FIFO_Available( &s_fifo );
	if ( level < s_fifoMin ) s_fifoMin = level;
	if ( level > s_fifoMax ) s_fifoMax = level;

	/* start dopiero po napełnieniu FIFO do połowy (prefill) */
	if ( !s_run && level >= s_fifoTarget ) {
		s_run = true;
		DRIFT_Start( &s_drift );
	}

	if ( s_run ) {
		uint32_t n = DRIFT_Process( &s_drift, out, frames );
		if ( n < frames ) { /* underrun: wygaś ogon i zrób ponowny prefill */
			DRIFT_FadeOutTail( out, n, frames );
			s_run = false;
			s_underruns++;
		}
	} else {
		memset( out, 0, frames * AUDIO_BYTES_PER_FRAME ); /* strumień nigdy się nie urywa */
	}
	s_playing = s_run;

	{ /* tap dla DSP: po drift, przed głośnością (analiza nie zależy od poziomu) */
		AUDIO_TapFn tap = s_tap;

		if ( tap != NULL ) tap( out, frames );
	}

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

	s_rate = s_rateReq = s_srcRateSeen = AUDIO_SAMPLE_RATE;
	s_fifoTarget = fifoTargetBytes( s_rate );

	if ( !FIFO_Init( &s_fifo, s_fifoStorage, AUDIO_FIFO_SIZE_BYTES, AUDIO_BYTES_PER_FRAME ) ||
	     !DRIFT_Init( &s_drift, &s_fifo, s_fifoTarget ) ) {
		return AUDIO_ERR_FIFO;
	}

	oc.sample_rate  = s_rate;
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

	/* częstotliwość od źródła: reagujemy tylko na jej zmianę (odrzucona nie jest ponawiana w pętli) */
	if ( s_src->get_sample_rate != NULL ) {
		uint32_t r = s_src->get_sample_rate();

		if ( r != s_srcRateSeen ) {
			s_srcRateSeen = r;
			if ( rateSupported( r ) ) s_rateReq = r;
		}
	}

	if ( s_src->get_volume != NULL && now_ms - s_tVol >= AUDIO_VOL_POLL_MS ) {
		s_tVol = now_ms;
		s_src->get_volume( &s_vol );
		if ( s_guardActive && s_vol.ctrl_count > 0 ) { /* host odezwał się: zwolnij limit */
			s_guardActive = false;
		}
		/* mute strony sterującej wycisza też wyjście (wcześniej tylko zmieniał napis na OLED) */
		s_gainTarget = s_vol.mute ? 0.0f : s_vol.gain;
	}
}
// ---------------------------------------------------------------------------------------------
uint32_t AUDIO_GetSampleRate( void ) {
	return s_rate;
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
	out->sample_rate = s_rate;
	out->drift_ppm = playing ? (int32_t)( ( DRIFT_GetRatio( &s_drift ) - 1.0f ) * 1000000.0f ) : 0;

	memset( &out->src, 0, sizeof( out->src ) );
	if ( s_src != NULL && s_src->get_stats != NULL ) s_src->get_stats( &out->src );
}
// ---------------------------------------------------------------------------------------------
void AUDIO_SetTap( AUDIO_TapFn fn ) {
	s_tap = fn;
}
// ---------------------------------------------------------------------------------------------
uint32_t AUDIO_TimeUs( void ) {
	return AUDIO_CLOCK_TimeUs();
}
