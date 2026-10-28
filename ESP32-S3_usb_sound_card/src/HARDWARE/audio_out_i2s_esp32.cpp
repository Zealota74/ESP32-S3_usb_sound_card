/*
 * audio_out_i2s_esp32.cpp - wyjście audio: ESP32-S3, I2S (driver i2s_std) -> PCM5102.
 * Cały kod zależny od I2S i FreeRTOS jest tylko tutaj.
 */
#include <Arduino.h>
#include "driver/i2s_std.h"

#include "../AUDIO/audio_config.h"
#include "../AUDIO/audio_out.h"

/* ---- Konfiguracja sprzętowa (można nadpisać przed kompilacją) ---- */
#ifndef AUDIO_I2S_PIN_BCLK
#define AUDIO_I2S_PIN_BCLK     GPIO_NUM_12
#endif
#ifndef AUDIO_I2S_PIN_WS
#define AUDIO_I2S_PIN_WS       GPIO_NUM_14
#endif
#ifndef AUDIO_I2S_PIN_DOUT
#define AUDIO_I2S_PIN_DOUT     GPIO_NUM_13
#endif
#ifndef AUDIO_I2S_DMA_DESC_NUM
#define AUDIO_I2S_DMA_DESC_NUM   4
#endif
#ifndef AUDIO_I2S_DMA_FRAME_NUM
#define AUDIO_I2S_DMA_FRAME_NUM  256
#endif
#ifndef AUDIO_I2S_TASK_STACK
#define AUDIO_I2S_TASK_STACK     4096
#endif
#ifndef AUDIO_I2S_TASK_CORE
#define AUDIO_I2S_TASK_CORE      1
#endif

static i2s_chan_handle_t s_tx = NULL;
static AUDIO_OutFillFn   s_fill = NULL;
static void *            s_ctx = NULL;
static uint32_t          s_blockFrames = 0;
static uint32_t          s_sampleRate = 0;
static volatile uint32_t s_errors = 0;
static int16_t           s_buf[ AUDIO_BLOCK_FRAMES * 2 ];

// ---------------------------------------------------------------------------------------------
int AUDIO_OUT_Init( const AUDIO_OutConfig_t * cfg ) {
	if ( cfg == NULL || cfg->channels != 2 || cfg->bits != 16 ) return 0;
	if ( cfg->block_frames == 0 || cfg->block_frames > AUDIO_BLOCK_FRAMES ) return 0;
	s_blockFrames = cfg->block_frames;
	s_sampleRate = cfg->sample_rate;

	i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG( I2S_NUM_AUTO, I2S_ROLE_MASTER );
	chan_cfg.dma_desc_num  = AUDIO_I2S_DMA_DESC_NUM;
	chan_cfg.dma_frame_num = AUDIO_I2S_DMA_FRAME_NUM;
	chan_cfg.auto_clear    = true; /* przy braku danych DMA wysyła ciszę */

	if ( i2s_new_channel( &chan_cfg, &s_tx, NULL ) != ESP_OK ) {
		Serial.println( "I2S channel error" );
		return 0;
	}
	i2s_std_config_t std_cfg = {
		.clk_cfg = {
			.sample_rate_hz = cfg->sample_rate,
			.clk_src        = I2S_CLK_SRC_PLL_160M,
			.mclk_multiple  = I2S_MCLK_MULTIPLE_256
		},
		.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG( I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO ),
		.gpio_cfg = {
			.mclk         = I2S_GPIO_UNUSED,
			.bclk         = AUDIO_I2S_PIN_BCLK,
			.ws           = AUDIO_I2S_PIN_WS,
			.dout         = AUDIO_I2S_PIN_DOUT,
			.din          = I2S_GPIO_UNUSED,
			.invert_flags = {
				.mclk_inv = false,
				.bclk_inv = false,
				.ws_inv   = false
			}
		}
	};
	if ( i2s_channel_init_std_mode( s_tx, &std_cfg ) != ESP_OK ) {
		Serial.println( "I2S init error" );
		return 0;
	}
	if ( i2s_channel_enable( s_tx ) != ESP_OK ) {
		Serial.println( "I2S enable error" );
		return 0;
	}
	return 1;
}
// ---------------------------------------------------------------------------------------------
/* Kanał musi być wyłączony (stan INIT). Zegar jak w AUDIO_OUT_Init (PLL 160 MHz, MCLK = 256 * fs). */
static int setClock( uint32_t rate ) {
	i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG( rate );

	clk.clk_src       = I2S_CLK_SRC_PLL_160M;
	clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;
	return ( i2s_channel_reconfig_std_clock( s_tx, &clk ) == ESP_OK ) ? 1 : 0;
}
// ---------------------------------------------------------------------------------------------
int AUDIO_OUT_SetSampleRate( uint32_t rate ) {
	int ok;

	if ( s_tx == NULL || rate == 0 ) return 0;
	if ( rate == s_sampleRate ) return 1;

	if ( i2s_channel_disable( s_tx ) != ESP_OK ) return 0;
	ok = setClock( rate );
	if ( ok ) {
		s_sampleRate = rate;
	} else {
		setClock( s_sampleRate ); /* wróć do poprzedniej, żeby wyjście nie zostało bez zegara */
	}
	if ( i2s_channel_enable( s_tx ) != ESP_OK ) {
		s_errors++;
		return 0;
	}
	return ok;
}
// ---------------------------------------------------------------------------------------------
/* Bez Serial.print i innych operacji blokujących! */
static void outTask( void * arg ) {
	(void)arg;

	for ( ;; ) {
		size_t written = 0;

		s_fill( s_buf, s_blockFrames, s_ctx );
		if ( i2s_channel_write( s_tx, s_buf, s_blockFrames * AUDIO_BYTES_PER_FRAME, &written, portMAX_DELAY ) != ESP_OK ) {
			s_errors++;
		}
	}
}
// ---------------------------------------------------------------------------------------------
int AUDIO_OUT_Start( AUDIO_OutFillFn fill, void * ctx ) {
	if ( fill == NULL || s_tx == NULL ) return 0;
	s_fill = fill;
	s_ctx = ctx;
	return ( xTaskCreatePinnedToCore( outTask, "I2S", AUDIO_I2S_TASK_STACK, NULL,
	                                  configMAX_PRIORITIES - 1, NULL, AUDIO_I2S_TASK_CORE ) == pdPASS ) ? 1 : 0;
}
// ---------------------------------------------------------------------------------------------
uint32_t AUDIO_OUT_GetErrors( void ) {
	return s_errors;
}
