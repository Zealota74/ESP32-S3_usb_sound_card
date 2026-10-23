/***************************************************************************
 *  ESP32-S3 USB Audio DAC  (USB Audio Class 1.0 -> FIFO -> I2S -> PCM5102)
 *
 *  audio.*      - cały podsystem audio (źródło, FIFO, drift, głośność, wyjście I2S)
 *  input.*      - enkoder, przyciski, pilot IR (wspólny timer), bez wiedzy o reszcie
 *  controls.*   - wejścia -> akcje (audio, UI, menu)
 *  audio_ui.*   - audio -> UI (głośność, limit, widmo, liczniki)
 *  menu_oled.*  - logika okienek menu
 *  ui_oled.*    - wyświetlacz
 *  ten plik     - inicjalizacja i pętla główna, diagnostyka
 ***************************************************************************/
#include <Arduino.h>

#include "src/audio/audio.h"
#include "input.h"
#include "ui_oled.h"
#include "controls.h"
#include "audio_ui.h"

/***************************************************************************
 *                              KONFIGURACJA
 ***************************************************************************/
#define AUDIO_DIAG_SERIAL   0        /* 1 = co sekundę statystyki audio na Serial (źródło/FIFO fps, FIFO %, fs, blocks/s) */
#define DIAG_PERIOD_MS      1000

/* Głośność po starcie w dB (zakres od minimum do 0, 0 dB = pełna).
 * Ustawiana w źródle audio przed podłączeniem do hosta. Jeśli źródło jej nie
 * przyjmie, ta sama wartość działa jako awaryjny limit do pierwszej zmiany
 * głośności z Windows lub dwuklika. */
#define STARTUP_VOL_DB      (-20)

#if AUDIO_DIAG_SERIAL
static void diagPrint( uint32_t now );
#endif

// ========================================================================================
void setup() {
	AUDIO_Config_t cfg;
	int err;

	Serial.begin( 115200 );
	delay( 1000 );
	Serial.println( "ESP32-S3 USB AUDIO DAC (PCM5102)" );

	UI_Init();
	INPUT_init();

	cfg.startup_vol_db = (int8_t)STARTUP_VOL_DB;
	err = AUDIO_Init( &cfg );
	if ( err != AUDIO_OK ) {
		Serial.printf( "AUDIO init failed: %s\n", AUDIO_ErrorText( err ) );
		for ( ;; )
			delay( 1000 );
	}

	AUDIOUI_Init( STARTUP_VOL_DB );
	CONTROLS_Init();                  /* po AUDIO_Init(): dwuklik zależy od limitu startowego */
	Serial.println( "System ready" );
}
// ----------------------------------------------------------------------------------------
void loop() {
	uint32_t now = millis();

	INPUT_proc();                     /* enkoder + przyciski + IR (callbacki w controls.cpp) */
	AUDIO_Poll( now );                /* źródło, głośność, limit startowy, widmo */
	AUDIOUI_Sync( now );              /* audio -> UI */
	UI_Loop( now );

#if AUDIO_DIAG_SERIAL
	diagPrint( now );
#endif
	delay( 1 );
}
// ========================================================================================
/******************************************************************************************
 *                                       DIAGNOSTYKA
 *****************************************************************************************/
#if AUDIO_DIAG_SERIAL
static void diagPrint( uint32_t now ) {
	static uint32_t tLast = 0, prevSrcBytes = 0, prevConsumed = 0, prevBlocks = 0;
	AUDIO_Stats_t s;
	uint32_t dt = now - tLast;
	uint32_t srcFps, outFps, blocksPerS;

	if ( dt < DIAG_PERIOD_MS ) return;
	tLast = now;

	AUDIO_GetStats( &s, 1 );

	srcFps = ( s.src.bytes - prevSrcBytes ) / AUDIO_BYTES_PER_FRAME * 1000UL / dt;
	outFps = ( s.consumed_frames - prevConsumed ) * 1000UL / dt;
	blocksPerS = ( s.blocks - prevBlocks ) * 1000UL / dt;
	prevSrcBytes = s.src.bytes;
	prevConsumed = s.consumed_frames;
	prevBlocks = s.blocks;

	Serial.printf(
		"%s | fs %lu | blocks/s %lu | src in %lu fps, FIFO out %lu fps | FIFO %lu%% (min %lu%% max %lu%%) | "
		"korekta %+ld ppm | OV %lu UF %lu short %lu | src age %lu ms | out err %lu\n",
		s.playing ? "PLAY" : "IDLE",
		(unsigned long)s.sample_rate,
		(unsigned long)blocksPerS,
		(unsigned long)srcFps, (unsigned long)outFps,
		(unsigned long)( s.fifo_bytes * 100UL / s.fifo_size ),
		(unsigned long)( s.fifo_min * 100UL / s.fifo_size ),
		(unsigned long)( s.fifo_max * 100UL / s.fifo_size ),
		(long)s.drift_ppm,
		(unsigned long)s.overruns,
		(unsigned long)s.underruns,
		(unsigned long)s.src.short_writes,
		(unsigned long)s.src.age_ms,
		(unsigned long)s.out_errors );
}
#endif
// ========================================================================================
