/***************************************************************************
 *  ESP32-S3 USB Audio DAC  (USB Audio Class 1.0 -> FIFO -> I2S -> PCM5102)
 *
 *  audio.*   - cały podsystem audio (źródło, FIFO, drift, głośność, wyjście I2S)
 *  input.*   - enkoder, przyciski, pilot IR (wspólny timer)
 *  ui_oled.* - wyświetlacz
 *  ten plik  - klej: wejścia -> audio, audio -> UI, diagnostyka
 ***************************************************************************/
#include <Arduino.h>

#include "src/audio/audio.h"
#include "input.h"
#include "ui_oled.h"

/***************************************************************************
 *                              KONFIGURACJA
 ***************************************************************************/
#define AUDIO_DIAG_SERIAL   0        /* 1 = co sekundę statystyki audio na Serial (źródło/FIFO fps, FIFO %) */
#define VOL_DIAG_SERIAL     1        /* 1 = log "VOL host=... -> ...%" przy zmianie głośności */
#define KEY_DIAG_SERIAL     1        /* 1 = log akcji klawiszy A-D / pilota na Serial */
#define DIAG_PERIOD_MS      1000
#define OLED_PERIOD_MS      100

/* Głośność po starcie w dB (zakres od minimum do 0, 0 dB = pełna).
 * Ustawiana w źródle audio przed podłączeniem do hosta. Jeśli źródło jej nie
 * przyjmie, ta sama wartość działa jako awaryjny limit do pierwszej zmiany
 * głośności z Windows lub dwuklika. */
#define STARTUP_VOL_DB      (-20)

// ========================================================================================
void setup() {
	AUDIO_Config_t cfg;
	int err;

	Serial.begin( 115200 );
	delay( 1000 );
	Serial.println( "ESP32-S3 USB AUDIO DAC (PCM5102)" );

	UI_Init();
	INPUT_init();
	INPUT_attach_rotate( onEncRotate );
	INPUT_attach_action( onBtnClick );
	INPUT_attach_long_press( onBtnLongPress );
	INPUT_attach_keyA( onBtnKeyA );
	INPUT_attach_keyB( onBtnKeyB );
	INPUT_attach_keyC( onBtnKeyC );
	INPUT_attach_keyD( onBtnKeyD );
	INPUT_attach_keyA_long( onBtnKeyALong );
	INPUT_attach_keyB_long( onBtnKeyBLong );
	INPUT_attach_keyC_long( onBtnKeyCLong );
	INPUT_attach_keyD_long( onBtnKeyDLong );

	cfg.startup_vol_db = (int8_t)STARTUP_VOL_DB;
	err = AUDIO_Init( &cfg );
	if ( err != AUDIO_OK ) {
		Serial.printf( "AUDIO init failed: %s\n", AUDIO_ErrorText( err ) );
		for ( ;; )
			delay( 1000 );
	}

	UI_SetGuard( AUDIO_GuardActive(), (float)STARTUP_VOL_DB );
	if ( AUDIO_GuardActive() ) {
		INPUT_attach_double_click( onBtnDoubleClick ); /* dwuklik zwalnia awaryjny limit */
	}
	Serial.println( "System ready" );
}
// -----------------------------------------------------------------------------------------
void loop() {
	uint32_t now = millis();
	
	INPUT_proc();         /* enkoder + przyciski (callbacki) */
	VOL_proc(now);
	SPECTRUM_proc(now);
	UI_Loop(now);

#if AUDIO_DIAG_SERIAL
	diagPrint( now );
#endif
	delay( 1 );
}
// ========================================================================================

// ========================================================================================
void VOL_proc( uint32_t now ) {
	AUDIO_Volume_t vol;
	int guard;
	AUDIO_Poll( now );    /* źródło, głośność, limit startowy */

	AUDIO_GetVolume( &vol );
	guard = AUDIO_GuardActive();
	UI_SetGuard( guard, guard ? (float)STARTUP_VOL_DB : 0.0f );

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
// ----------------------------------------------------------------------------------------
void SPECTRUM_proc( uint32_t now ) {
	static uint32_t tOled = 0;

	/* widmo: liczone tylko na ekranie SPECTRUM (FFT w AUDIO_Poll, czyli w tym wątku) */
	AUDIO_Spectrum_t sp;
	int viz = ( UI_GetScreen() == UI_SCREEN_SPECTRUM );

	AUDIO_SpectrumEnable( viz );
	if ( viz && AUDIO_GetSpectrum( &sp ) ) UI_SetSpectrum( sp.level, sp.peak, sp.bars );
	
	if ( now - tOled >= OLED_PERIOD_MS ) {
		AUDIO_Stats_t st;
		tOled = now;
		AUDIO_GetStats( &st, 0 );
		UI_SetDebug( st.blocks, st.fifo_bytes, st.src.packets, st.fft_us );
	}
}
// ----------------------------------------------------------------------------------------
/******************************************************************************************
 *                                       DIAGNOSTYKA
 *****************************************************************************************/
#if AUDIO_DIAG_SERIAL
static void diagPrint( uint32_t now ) {
	static uint32_t tLast = 0, prevSrcBytes = 0, prevConsumed = 0;
	AUDIO_Stats_t s;
	uint32_t dt = now - tLast;
	uint32_t srcFps, outFps;

	if ( dt < DIAG_PERIOD_MS ) return;
	tLast = now;

	AUDIO_GetStats( &s, 1 );

	srcFps = ( s.src.bytes - prevSrcBytes ) / AUDIO_BYTES_PER_FRAME * 1000UL / dt;
	outFps = ( s.consumed_frames - prevConsumed ) * 1000UL / dt;
	prevSrcBytes = s.src.bytes;
	prevConsumed = s.consumed_frames;

	Serial.printf(
		"%s | src in %lu fps, FIFO out %lu fps | FIFO %lu%% (min %lu%% max %lu%%) | "
		"korekta %+ld ppm | OV %lu UF %lu short %lu | src age %lu ms | out err %lu\n",
		s.playing ? "PLAY" : "IDLE",
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

// ========================================================================================
/******************************************************************************************
 *                    CALLBACKI WEJŚĆ (enkoder / przycisk)
 *****************************************************************************************/
static void onEncRotate( int steps ) { AUDIO_VolumeStep( steps ); }                       /* tick = jeden poziom głośności */
static void onBtnClick( void ) { AUDIO_MuteToggle(); }
static void onBtnLongPress( void ) { UI_NextScreen(); }                                   /* STAT <-> SPECTRUM */
static void onBtnDoubleClick( void ) { AUDIO_GuardRelease(); UI_SetGuard( 0, 0.0f ); }    /* zwolnij limit */


/* Klawisze KEY_A..KEY_D i pilot IR wywołują te same funkcje (mapowanie kodów IR w input.h).
* Krótkie naciśnięcie (działa w każdym ekranie): A = następny utwór, C = poprzedni utwór,
*                                                B = jaśniej, D = ciemniej.
* Długie naciśnięcie (tylko w ekranie SPECTRUM):  A/C = liczba słupków 7/16/32 (w przód/w tył),
*                                                B/D = szybsze/wolniejsze opadanie słupków.
* Każda zmiana pokazuje OSD (warstwa nad ekranem), które samo znika po UI_OSD_MS.
*/
static void osdBars( void ) {
	char v[ 8 ];
	snprintf( v, sizeof( v ), "%d", AUDIO_SpectrumGetBars() );
	UI_ShowOsd( "SPECTRUM BARS", v, 0, 0 );
}
// ---------------------------------------------------------------------------------------
static void osdFall( void ) {
	char v[ 16 ];
	snprintf( v, sizeof( v ), "%d ms", AUDIO_SpectrumGetFallMs() );
	UI_ShowOsd( "SPECTRUM FALL", v, AUDIO_SpectrumFallIndex(), AUDIO_SpectrumFallSteps() - 1 );
}
// ---------------------------------------------------------------------------------------
static void onBtnKeyA( void ) {
	AUDIO_MediaKey( AUDIO_MEDIA_NEXT );
#if KEY_DIAG_SERIAL
	Serial.println( "KEY_A / IR: następny utwór" );
#endif
}
// ---------------------------------------------------------------------------------------
static void onBtnKeyC( void ) {
	AUDIO_MediaKey( AUDIO_MEDIA_PREV );
#if KEY_DIAG_SERIAL
	Serial.println( "KEY_C / IR: poprzedni utwór" );
#endif
}
// ---------------------------------------------------------------------------------------
static void onBtnKeyB( void ) {
	UI_BrightnessStep( +1 );
#if KEY_DIAG_SERIAL
	Serial.printf( "KEY_B / IR: jasność OLED %d/%d\n", UI_GetBrightness() + 1, UI_BRIGHT_LEVELS );
#endif
}
// ---------------------------------------------------------------------------------------
static void onBtnKeyD( void ) {
	UI_BrightnessStep( -1 );
#if KEY_DIAG_SERIAL
	Serial.printf( "KEY_D / IR: jasność OLED %d/%d\n", UI_GetBrightness() + 1, UI_BRIGHT_LEVELS );
#endif
}
// ---------------------------------------------------------------------------------------
static void onBtnKeyALong( void ) {
	if ( UI_GetScreen() != UI_SCREEN_SPECTRUM ) return;
	AUDIO_SpectrumCycleBars( +1 );
	osdBars();
#if KEY_DIAG_SERIAL
	Serial.printf( "KEY_A (długo): widmo %d słupków\n", AUDIO_SpectrumGetBars() );
#endif
}
// ---------------------------------------------------------------------------------------
static void onBtnKeyCLong( void ) {
	if ( UI_GetScreen() != UI_SCREEN_SPECTRUM ) return;
	AUDIO_SpectrumCycleBars( -1 );
	osdBars();
#if KEY_DIAG_SERIAL
	Serial.printf( "KEY_C (długo): widmo %d słupków\n", AUDIO_SpectrumGetBars() );
#endif
}
// ---------------------------------------------------------------------------------------
static void onBtnKeyBLong( void ) {
	if ( UI_GetScreen() != UI_SCREEN_SPECTRUM ) return;
	AUDIO_SpectrumFallStep( +1 );
	osdFall();
#if KEY_DIAG_SERIAL
	Serial.printf( "KEY_B (długo): opadanie słupków %d ms\n", AUDIO_SpectrumGetFallMs() );
#endif
}
// ---------------------------------------------------------------------------------------
static void onBtnKeyDLong( void ) {
	if ( UI_GetScreen() != UI_SCREEN_SPECTRUM ) return;
	AUDIO_SpectrumFallStep( -1 );
	osdFall();
#if KEY_DIAG_SERIAL
	Serial.printf( "KEY_D (długo): opadanie słupków %d ms\n", AUDIO_SpectrumGetFallMs() );
#endif
}
// ========================================================================================