/***************************************************************************
 *  ESP32-S3 USB Audio DAC  (USB Audio Class 1.0 -> FIFO -> I2S -> PCM5102)
 *
 *  usb_soundcard.*  - odbiór PCM z USB do FIFO
 *  fifo.*           - bufor kołowy SPSC (przenośny)
 *  drift.*          - kompensacja dryfu zegarów (przenośny)
 *  ten plik         - I2S/PCM5102, task audio, diagnostyka
 ***************************************************************************/
#include <Arduino.h>
#include <math.h>
#include "driver/i2s_std.h"

#include "audio_config.h"
#include "fifo.h"
#include "drift.h"
#include "usb_soundcard.h"
#include "input.h"
#include "ui_oled.h"

/***************************************************************************
 *                              KONFIGURACJA
 ***************************************************************************/
#define PIN_BCLK            GPIO_NUM_12
#define PIN_WS              GPIO_NUM_14
#define PIN_DOUT            GPIO_NUM_13

#define I2S_DMA_DESC_NUM    4
#define I2S_DMA_FRAME_NUM   256
#define I2S_BLOCK_FRAMES    256
#define I2S_BLOCK_BYTES     (I2S_BLOCK_FRAMES * AUDIO_BYTES_PER_FRAME)

#define FIFO_SIZE_BYTES     16384                   /* potęga 2, ok. 93 ms */
#define FIFO_TARGET_BYTES   (FIFO_SIZE_BYTES / 2)   /* punkt pracy i próg startu */

#define AUDIO_DIAG_SERIAL   0        /* 1 = co sekundę statystyki audio na Serial (USB/FIFO fps, FIFO %) */
#define VOL_DIAG_SERIAL     1        /* 1 = log "VOL host=... -> ...%" przy zmianie głośności */
#define DIAG_PERIOD_MS      1000
#define OLED_PERIOD_MS      100
#define VOL_PERIOD_MS       50

/* Głośność po starcie w dB (zakres VOL_HOST_MIN_DB..0, 0 dB = pełna).
 * Ustawiana w bibliotece USB przed podłączeniem do hosta. Jeśli biblioteka jej
 * nie przyjmie, ta sama wartość działa jako awaryjny limit do pierwszej zmiany
 * głośności z Windows lub dwuklika. */
#define STARTUP_VOL_DB      (-20)

/***************************************************************************
 *                              STAN GLOBALNY
 ***************************************************************************/
static uint8_t g_fifoStorage[ FIFO_SIZE_BYTES ];
static FIFO_t g_fifo;
static DRIFT_t g_drift;
static i2s_chan_handle_t g_txHandle = NULL;

/* Liczniki dla diagnostyki (zapisuje task audio, czyta loop). */
static volatile uint32_t g_i2sBlocks = 0;
static volatile uint32_t g_underruns = 0;
static volatile uint32_t g_i2sErrors = 0;
static volatile uint32_t g_fifoMin = 0xFFFFFFFFUL;
static volatile uint32_t g_fifoMax = 0;
static volatile bool g_playing = false;
static volatile bool g_guardActive = true;
static volatile float g_gainTarget = 0.0f; /* mnożnik głośności (cisza do pierwszego odczytu) */
static volatile float g_guardGain = 1.0f;   /* mnożnik limitu startowego */

/***************************************************************************
 *                    I2S -> PCM5102
 ***************************************************************************/
static bool i2sInit( void ) {
	i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG( I2S_NUM_AUTO, I2S_ROLE_MASTER );
	chan_cfg.dma_desc_num   = I2S_DMA_DESC_NUM;
	chan_cfg.dma_frame_num  = I2S_DMA_FRAME_NUM;
	chan_cfg.auto_clear     = true; /* przy braku danych DMA wysyła ciszę */

	if ( i2s_new_channel( &chan_cfg, &g_txHandle, NULL ) != ESP_OK ) {
		Serial.println( "I2S channel error" );
		return false;
	}
	i2s_std_config_t std_cfg = { 
    .clk_cfg = { 
      .sample_rate_hz = AUDIO_SAMPLE_RATE,
      .clk_src        = I2S_CLK_SRC_PLL_160M,
			.mclk_multiple  = I2S_MCLK_MULTIPLE_256
    },
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG( I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO ), 
    .gpio_cfg = {
      .mclk     = I2S_GPIO_UNUSED,
      .bclk         = PIN_BCLK,
			.ws           = PIN_WS,
      .dout         = PIN_DOUT,
      .din          = I2S_GPIO_UNUSED,
      .invert_flags = {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false 
                      } 
    }
  };
	if ( i2s_channel_init_std_mode( g_txHandle, &std_cfg ) != ESP_OK ) {
		Serial.println( "I2S init error" );
		return false;
	}
	if ( i2s_channel_enable( g_txHandle ) != ESP_OK ) {
		Serial.println( "I2S enable error" );
		return false;
	}
	return true;
}
/***************************************************************************
 *                         TASK AUDIO (FIFO -> I2S)
 *  Bez Serial.print i innych operacji blokujących!
 ***************************************************************************/
static void i2sTask( void * arg ) {
	static int16_t out[ I2S_BLOCK_FRAMES * 2 ];
	bool playing = false;

	(void)arg;

	for ( ;; ) {
		uint32_t level = FIFO_Available( &g_fifo );
		size_t written = 0;

		if ( level < g_fifoMin ) g_fifoMin = level;
		if ( level > g_fifoMax ) g_fifoMax = level;

		/* start dopiero po napełnieniu FIFO do połowy (prefill) */
		if ( !playing && level >= FIFO_TARGET_BYTES ) {
			playing = true;
			DRIFT_Start( &g_drift );
		}

		if ( playing ) {
			uint32_t n = DRIFT_Process( &g_drift, out, I2S_BLOCK_FRAMES );
			if ( n < I2S_BLOCK_FRAMES ) { /* underrun: wygaś ogon i zrób ponowny prefill */
				DRIFT_FadeOutTail( out, n, I2S_BLOCK_FRAMES );
				playing = false;
				g_underruns++;
			}
		} else {
			memset( out, 0, sizeof( out ) ); /* strumień do DMA nigdy się nie urywa */
		}
		g_playing = playing;

		{ /* głośność (host + limit startowy): jeden mnożnik z rampą w obrębie bloku (bez trzasków) */
			static float gPrev = 0.0f;
			float gNew = g_gainTarget * ( g_guardActive ? g_guardGain : 1.0f );

			if ( !( gPrev == 1.0f && gNew == 1.0f ) ) {
				float step = ( gNew - gPrev ) / (float)I2S_BLOCK_FRAMES;
				float g = gPrev;
				for ( uint32_t k = 0; k < I2S_BLOCK_FRAMES; k++ ) {
					g += step;
					out[ k * 2 ] = (int16_t)lrintf( (float)out[ k * 2 ] * g );
					out[ k * 2 + 1 ] = (int16_t)lrintf( (float)out[ k * 2 + 1 ] * g );
				}
			}
			gPrev = gNew;
		}

		if ( i2s_channel_write( g_txHandle, out, I2S_BLOCK_BYTES, &written, portMAX_DELAY ) != ESP_OK ) {
			g_i2sErrors++;
		}
		g_i2sBlocks++;
	}
}

/***************************************************************************
 *                              DIAGNOSTYKA
 ***************************************************************************/
#if AUDIO_DIAG_SERIAL
static void diagPrint(uint32_t now) {
  static uint32_t tLast = 0, prevUsbBytes = 0, prevConsumed = 0;
  USB_Stats_t u;
  uint32_t dt = now - tLast;
  uint32_t fMin, fMax, usbFps, outFps, consumed;
  bool playing = g_playing;

  if (dt < DIAG_PERIOD_MS) return;
  tLast = now;

  USB_GetStats(&u);

  fMin      = g_fifoMin;
  fMax      = g_fifoMax;
  g_fifoMin = 0xFFFFFFFFUL;
  g_fifoMax = 0;
  if (fMin == 0xFFFFFFFFUL) fMin = 0;

  consumed = DRIFT_GetConsumedFrames(&g_drift);
  usbFps = (u.bytes - prevUsbBytes) / AUDIO_BYTES_PER_FRAME * 1000UL / dt;
  outFps = (consumed - prevConsumed) * 1000UL / dt;
  prevUsbBytes = u.bytes;
  prevConsumed = consumed;

  Serial.printf(
    "%s | USB in %lu fps, FIFO out %lu fps | FIFO %lu%% (min %lu%% max %lu%%) | "
    "korekta %+ld ppm | OV %lu UF %lu short %lu | USB age %lu ms | I2S err %lu\n",
    playing ? "PLAY" : "IDLE",
    (unsigned long)usbFps, (unsigned long)outFps,
    (unsigned long)(FIFO_Available(&g_fifo) * 100UL / FIFO_SIZE_BYTES),
    (unsigned long)(fMin * 100UL / FIFO_SIZE_BYTES),
    (unsigned long)(fMax * 100UL / FIFO_SIZE_BYTES),
    playing ? (long)((DRIFT_GetRatio(&g_drift) - 1.0f) * 1000000.0f) : 0L,
    (unsigned long)FIFO_GetOverruns(&g_fifo),
    (unsigned long)g_underruns,
    (unsigned long)u.short_writes,
    (unsigned long)USB_GetPacketAgeMs(),
    (unsigned long)g_i2sErrors);
}
#endif

/***************************************************************************
 *                    CALLBACKI WEJŚĆ (enkoder / przycisk)
 ***************************************************************************/
static void onEncRotate( int steps ) { USB_VolStep( steps ); }                           /* tick = jeden poziom głośności */
static void onBtnClick( void ) { USB_HidMuteToggle(); }
static void onBtnLongPress( void ) { UI_ToggleDebug(); }                                  /* ekran diagnostyczny */
static void onBtnDoubleClick( void ) { g_guardActive = false; UI_SetGuard( 0, 0.0f ); }   /* zwolnij limit */

// ========================================================================================
void setup() {
	Serial.begin( 115200 );
	delay( 1000 );
	Serial.println( "ESP32-S3 USB AUDIO DAC (PCM5102)" );

	UI_Init();
	INPUT_init();
	INPUT_attach_rotate( onEncRotate );
	INPUT_attach_action( onBtnClick );
	INPUT_attach_long_press( onBtnLongPress );

	if ( !FIFO_Init( &g_fifo, g_fifoStorage, FIFO_SIZE_BYTES, AUDIO_BYTES_PER_FRAME ) || 
                  !DRIFT_Init(&g_drift, &g_fifo, FIFO_TARGET_BYTES ) ) {
		Serial.println( "FIFO/DRIFT init failed" );
		for ( ;; )
			delay( 1000 );
	}
	if ( !i2sInit() ) {
		Serial.println( "I2S FAILED" );
		for ( ;; )
			delay( 1000 );
	}

	/* głośność startowa: biblioteka USB, a przy niepowodzeniu awaryjny limit w torze audio */
	g_guardGain = powf( 10.0f, (float)STARTUP_VOL_DB / 20.0f );
	g_guardActive = !USB_Init( &g_fifo, (int8_t)STARTUP_VOL_DB );
	UI_SetGuard( g_guardActive ? 1 : 0, (float)STARTUP_VOL_DB );

	if ( g_guardActive ) {
    INPUT_attach_double_click( onBtnDoubleClick ); /* dwuklik zwalnia awaryjny limit */
  }
	xTaskCreatePinnedToCore( i2sTask, "I2S", 4096, NULL, configMAX_PRIORITIES - 1, NULL, 1 );
	Serial.println( "System ready" );
}
// -----------------------------------------------------------------------------------------
void loop() {
	static uint32_t tOled = 0, tVol = 0;
	uint32_t now = millis();

	INPUT_proc();         /* enkoder + przyciski (callbacki) */
	USB_VolPoll();        /* dochodzi do poziomu docelowego klawiszami HID */
	USB_DebugPoll();      /* log zdarzeń volume/mute od hosta (diagnostyka) */

	if ( now - tVol >= VOL_PERIOD_MS ) {
		tVol = now;
		if ( g_guardActive && USB_GetHostCtrlCount() > 0 ) { /* host odezwał się: zwolnij limit */
			g_guardActive = false;
			UI_SetGuard( 0, 0.0f );
		}
		float host = USB_GetVolumeDb();
		int muted = USB_IsMuted();
		int level = USB_VolLevel();         /* w trakcie ruchu: poziom docelowy (od razu widać tick) */
		if ( USB_VolBusy() ) {
			muted = ( level == 0 );
		}
		g_gainTarget = VOL_HostToGain( host );

#if VOL_DIAG_SERIAL
        static int lastHost = 12345, lastMute = -1;
        if ((int)host != lastHost || muted != lastMute) {   /* log przy każdej zmianie głośności */
          lastHost = (int)host;
          lastMute = muted;
          Serial.printf("VOL host=%d dB -> lvl %d (win %d %%) gain=%.4f mute=%d\n", (int)host, 
												 VOL_LevelFromHost(host), VOL_LevelPercent(VOL_LevelFromHost(host)),
												 (double)VOL_HostToGain(host), muted);
        }
#endif

		UI_SetVolume( level, VOL_LEVELS - 1, muted ); /* liniowo 0..31, jeden tick = jeden krok */
		// UI_SetCalib(VCAL_Progress());
		UI_SetHostDb( (int)host );
	}
	if ( now - tOled >= OLED_PERIOD_MS ) {
		USB_Stats_t u;
		tOled = now;
		USB_GetStats( &u );
		UI_SetDebug( g_i2sBlocks, FIFO_Available( &g_fifo ), u.packets );
	}
	UI_Loop( now );

#if AUDIO_DIAG_SERIAL
  diagPrint(now);
#endif
	delay( 1 );
}
// ========================================================================================