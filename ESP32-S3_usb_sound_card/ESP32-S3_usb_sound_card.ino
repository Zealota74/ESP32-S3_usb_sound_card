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
#define VOL_DIAG_SERIAL     0        /* 1 = log "VOL host=... -> ...%" przy zmianie głośności */
#define KEY_DIAG_SERIAL     1        /* 1 = log akcji klawiszy A-D / pilota na Serial */
#define DIAG_PERIOD_MS      1000
#define OLED_PERIOD_MS      100

/* Głośność po starcie w dB (zakres od minimum do 0, 0 dB = pełna).
 * Ustawiana w źródle audio przed podłączeniem do hosta. Jeśli źródło jej nie
 * przyjmie, ta sama wartość działa jako awaryjny limit do pierwszej zmiany
 * głośności z Windows lub dwuklika. */
#define STARTUP_VOL_DB      (-20)

/***************************************************************************
 *                              PROTOTYPY
 ***************************************************************************/
static void volumeProc( uint32_t now );
static void spectrumProc( uint32_t now );
#if AUDIO_DIAG_SERIAL
static void diagPrint( uint32_t now );
#endif
static void onEncRotate( int steps );
static void onEncClick( void );
static void onEncLongPress( void );
static void onEncDoubleClick( void );
static void onKeyRight( void );
static void onKeyUp( void );
static void onKeyLeft( void );
static void onKeyDown( void );

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
	INPUT_attach_action( onEncClick );
	INPUT_attach_long_press( onEncLongPress );
	INPUT_attach_keyA( onKeyRight );
	INPUT_attach_keyB( onKeyUp );
	INPUT_attach_keyC( onKeyLeft );
	INPUT_attach_keyD( onKeyDown );

	cfg.startup_vol_db = (int8_t)STARTUP_VOL_DB;
	err = AUDIO_Init( &cfg );
	if ( err != AUDIO_OK ) {
		Serial.printf( "AUDIO init failed: %s\n", AUDIO_ErrorText( err ) );
		for ( ;; )
			delay( 1000 );
	}

	UI_SetGuard( AUDIO_GuardActive(), (float)STARTUP_VOL_DB );
	if ( AUDIO_GuardActive() ) {
		INPUT_attach_double_click( onEncDoubleClick ); /* dwuklik zwalnia awaryjny limit */
	}
	Serial.println( "System ready" );
}
// -----------------------------------------------------------------------------------------
void loop() {
	uint32_t now = millis();
	
	INPUT_proc();         /* enkoder + przyciski (callbacki) */
	volumeProc( now );
	spectrumProc( now );
	UI_Loop(now);

#if AUDIO_DIAG_SERIAL
	diagPrint( now );
#endif
	delay( 1 );
}
// ========================================================================================

// ========================================================================================
static void volumeProc( uint32_t now ) {
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
static void spectrumProc( uint32_t now ) {
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

// ========================================================================================
/******************************************************************************************
 *                    CALLBACKI WEJŚĆ (enkoder / przyciski / pilot IR)
 *
 * Warstwa dolna (ekran STAT / SPECTRUM):
 *   enkoder obrót = głośność, klik = mute, dwuklik = zwolnienie limitu startowego,
 *   długie naciśnięcie enkodera = przełączenie ekranu STAT <-> SPECTRUM,
 *   A (Right) / C (Left) = następny / poprzedni utwór.
 * Warstwa górna (okienko menu, znika po UI_MENU_MS bezczynności):
 *   B (Up) / D (Down) = poprzednie / następne okienko, pierwsze naciśnięcie tylko otwiera ostatnie,
 *   enkoder obrót = zmiana wartości w okienku (zamiast głośności).
 * Klawisze A..D i pilot IR wywołują te same funkcje (mapowanie kodów IR w input.h).
 *****************************************************************************************/
typedef enum { MENU_BARS_CNT = 0, MENU_BRIGHT, MENU_ENGINE, MENU_BARS_FALL, MENU_COUNT } Menu_t;
static int s_menuItem = MENU_BARS_CNT;       /* ostatnio używane okienko */
// ---------------------------------------------------------------------------------------
static void menuShow( void ) {
	char t[ 20 ], v[ 16 ];
	int pos = 0, max = 0;

	switch ( s_menuItem ) {
		case MENU_BARS_CNT:
			snprintf( t, sizeof( t ), "%d/%d SPECTRUM BARS", MENU_BARS_CNT + 1, MENU_COUNT );
			snprintf( v, sizeof( v ), "%d", AUDIO_SpectrumGetBars() );
			break;
		case MENU_BRIGHT:
			snprintf( t, sizeof( t ), "%d/%d BRIGHTNESS", MENU_BRIGHT + 1, MENU_COUNT );
			snprintf( v, sizeof( v ), "%d/%d", UI_GetBrightness() + 1, UI_BRIGHT_LEVELS );
			pos = UI_GetBrightness();
			max = UI_BRIGHT_LEVELS - 1;
			break;
		case MENU_ENGINE:
			snprintf( t, sizeof( t ), "%d/%d ANALYSIS", MENU_ENGINE + 1, MENU_COUNT );
			snprintf( v, sizeof( v ), "%s", AUDIO_SpectrumEngineName( AUDIO_SpectrumGetEngine() ) );
			break;
		case MENU_BARS_FALL:
		default:
			snprintf( t, sizeof( t ), "%d/%d SPECTRUM FALL", MENU_BARS_FALL + 1, MENU_COUNT );
			snprintf( v, sizeof( v ), "%lu ms", (unsigned long)AUDIO_SpectrumGetFallMs() );
			pos = AUDIO_SpectrumFallIndex();
			max = AUDIO_SpectrumFallSteps() - 1;     /* pasek: dalej w prawo = szybsze opadanie */
			break;
	}
	UI_ShowMenu( t, v, pos, max );
}
// ---------------------------------------------------------------------------------------
static void menuMove( int dir ) {        /* pierwsze naciśnięcie tylko otwiera, kolejne przełączają cyklicznie */
	if ( UI_MenuOpen() ) s_menuItem = ( s_menuItem + dir + MENU_COUNT ) % MENU_COUNT;
	menuShow();
}
// ---------------------------------------------------------------------------------------
static void menuChange( int steps ) {
	int dir = ( steps > 0 ) ? 1 : -1;

	switch ( s_menuItem ) {
		case MENU_BARS_CNT:
			AUDIO_SpectrumCycleBars( dir );
			break;
		case MENU_BRIGHT:
			UI_BrightnessStep( steps );      /* bez zawijania: skok z max na min byłby nieprzyjemny */
			break;
		case MENU_ENGINE: {
			int n = AUDIO_SpectrumEngineCount();
			AUDIO_SpectrumSetEngine( ( AUDIO_SpectrumGetEngine() + dir + n ) % n );
			break;
		}
		case MENU_BARS_FALL:
			AUDIO_SpectrumFallStep( dir );   /* z ograniczeniem do zakresu, bez zawijania */
			break;
		default:
			break;
	}
	menuShow();                              /* nadpisuje OSD jasności i odnawia czas okienka */
}
// ---------------------------------------------------------------------------------------
static void onEncRotate( int steps ) {
	if ( UI_MenuOpen() ) menuChange( steps );
	else AUDIO_VolumeStep( steps );
}
static void onEncClick( void )       { AUDIO_MuteToggle(); }
static void onEncLongPress( void )   { UI_NextScreen(); }                                  /* STAT <-> SPECTRUM */
static void onEncDoubleClick( void ) { AUDIO_GuardRelease(); UI_SetGuard( 0, 0.0f ); }     /* zwolnij limit */
// ---------------------------------------------------------------------------------------
static void onKeyUp( void )   { menuMove( -1 ); }   /* B: poprzednie okienko */
static void onKeyDown( void ) { menuMove( +1 ); }   /* D: następne okienko */
// ---------------------------------------------------------------------------------------
static void onKeyRight( void ) {                    /* A */
	AUDIO_MediaKey( AUDIO_MEDIA_NEXT );
#if KEY_DIAG_SERIAL
	Serial.println( "KEY_A / IR: następny utwór" );
#endif
}
// ---------------------------------------------------------------------------------------
static void onKeyLeft( void ) {                     /* C */
	AUDIO_MediaKey( AUDIO_MEDIA_PREV );
#if KEY_DIAG_SERIAL
	Serial.println( "KEY_C / IR: poprzedni utwór" );
#endif
}
// ========================================================================================
