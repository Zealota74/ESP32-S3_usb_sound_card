/*
 * audio_ui.cpp - most audio -> UI. Szczegóły: audio_ui.h.
 */
#include <Arduino.h>

#include "src/audio_manage.h"
#include "src/dsp_manage.h"

#include "ui_oled.h"
#include "ui_bars.h"
#include "audio_ui.h"

#define VOL_DIAG_SERIAL     0        /* 1 = log "VOL host=... -> ...%" przy zmianie głośności */
#define OLED_PERIOD_MS      100      /* odświeżanie liczników ekranu STAT */

static_assert( BARS_MAX == DSP_SPECTRUM_MAX_BANDS, "BARS_MAX (ui_bars.h) musi byc rowne DSP_SPECTRUM_MAX_BANDS" );
static_assert( UI_SPEC_MAX_BARS == DSP_SPECTRUM_MAX_BANDS, "UI_SPEC_MAX_BARS (ui_oled.h) musi byc rowne DSP_SPECTRUM_MAX_BANDS" );

static int s_startupDb = 0;

// ---------------------------------------------------------------------------------------------
void AUDIOUI_Init( int startup_vol_db ) {
	s_startupDb = startup_vol_db;
	UI_SetGuard( AUDIO_GuardActive(), (float)s_startupDb );
}
// ---------------------------------------------------------------------------------------------
static void syncVolume( void ) {
	AUDIO_Volume_t vol;
	int guard;

	AUDIO_GetVolume( &vol );
	guard = AUDIO_GuardActive();
	UI_SetGuard( guard, guard ? (float)s_startupDb : 0.0f );

#if VOL_DIAG_SERIAL
	static int lastHost = 12345, lastMute = -1;
	if ( (int)vol.host_db != lastHost || vol.mute != lastMute ) { /* log przy każdej zmianie głośności */
		lastHost = (int)vol.host_db;
		lastMute = vol.mute;
		Serial.printf( "VOL host=%d dB -> lvl %d (win %d %%) gain=%.4f mute=%d\n",
		               (int)vol.host_db, vol.host_level, vol.host_percent, (double)vol.gain, vol.mute );
	}
#endif
	UI_SetVolume( vol.level, vol.level_max, vol.mute ); /* liniowo 0..level_max, jeden tick = jeden krok */
	UI_SetHostDb( (int)vol.host_db );
}
// ---------------------------------------------------------------------------------------------
static void syncSpectrum( uint32_t now ) {
	static uint32_t tOled = 0;
	static int s_wasViz = 0;
	DSP_Bands_t sp;
	int viz = ( UI_GetScreen() == UI_SCREEN_SPECTRUM );

	/* widmo liczone tylko na ekranie SPECTRUM (analiza w DSP_Poll, w tym samym wątku) */
	if ( viz && !s_wasViz ) BARS_Reset();            /* po włączeniu ekranu słupki startują od zera */
	s_wasViz = viz;
	DSP_SpectrumEnable( viz );
	if ( viz && DSP_GetBandPower( &sp ) ) {
		uint8_t level[ BARS_MAX ], peak[ BARS_MAX ];

		BARS_Update( sp.power, sp.channels * sp.bands, now );
		BARS_Get( level, peak );
		UI_SetSpectrum( level, peak, sp.bands );     /* rysowany jest pierwszy kanał (ekran jest na razie jednokanałowy) */
	}

	if ( now - tOled >= OLED_PERIOD_MS ) {
		AUDIO_Stats_t st;

		tOled = now;
		AUDIO_GetStats( &st, 0 );
		UI_SetDebug( st.blocks, st.fifo_bytes, st.src.packets, DSP_SpectrumGetUs() );
	}
}
// =============================================================================================
void AUDIOUI_Sync( uint32_t now ) {
	syncVolume();
	syncSpectrum( now );
}
