/***************************************************************************
 *  ESP32-S3 USB Audio DAC  (USB Audio Class 1.0 -> FIFO -> I2S -> PCM5102)
 *
 *  usb_soundcard.*  - odbiór PCM z USB do FIFO
 *  fifo.*           - bufor kołowy SPSC (przenośny)
 *  drift.*          - kompensacja dryfu zegarów (przenośny)
 *  ten plik         - I2S/PCM5102, task audio, diagnostyka
 ***************************************************************************/
#include <Arduino.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"

#include "simon_defines.h"   /* oled_encoder_init/loop, oled_audioBlock, oled_fifo, oled_usbPackets */
#include "audio_config.h"
#include "fifo.h"
#include "drift.h"
#include "usb_soundcard.h"

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

#define AUDIO_DIAG_SERIAL   1        /* 0 = wyłącz diagnostykę na Serial */
#define DIAG_PERIOD_MS      1000
#define OLED_PERIOD_MS      100

/***************************************************************************
 *                              STAN GLOBALNY
 ***************************************************************************/
static uint8_t  g_fifoStorage[FIFO_SIZE_BYTES];
static FIFO_t   g_fifo;
static DRIFT_t  g_drift;
static i2s_chan_handle_t g_txHandle = NULL;

/* Liczniki dla diagnostyki (zapisuje task audio, czyta loop). */
static volatile uint32_t g_i2sBlocks = 0;
static volatile uint32_t g_underruns = 0;
static volatile uint32_t g_i2sErrors = 0;
static volatile uint32_t g_fifoMin = 0xFFFFFFFFUL;
static volatile uint32_t g_fifoMax = 0;
static volatile bool     g_playing = false;

/***************************************************************************
 *                    I2S -> PCM5102
 ***************************************************************************/
static bool i2sInit(void)
{
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = I2S_DMA_DESC_NUM;
  chan_cfg.dma_frame_num = I2S_DMA_FRAME_NUM;
  chan_cfg.auto_clear = true;   /* przy braku danych DMA wysyła ciszę */

  if (i2s_new_channel(&chan_cfg, &g_txHandle, NULL) != ESP_OK) {
    Serial.println("I2S channel error");
    return false;
  }

  i2s_std_config_t std_cfg = {
    .clk_cfg = {
      .sample_rate_hz = AUDIO_SAMPLE_RATE,
      .clk_src = I2S_CLK_SRC_PLL_160M,
      .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    },
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = PIN_BCLK, .ws = PIN_WS, .dout = PIN_DOUT, .din = I2S_GPIO_UNUSED, .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } }
  };

  if (i2s_channel_init_std_mode(g_txHandle, &std_cfg) != ESP_OK) {
    Serial.println("I2S init error");
    return false;
  }
  if (i2s_channel_enable(g_txHandle) != ESP_OK) {
    Serial.println("I2S enable error");
    return false;
  }
  return true;
}

/***************************************************************************
 *                         TASK AUDIO (FIFO -> I2S)
 *  Bez Serial.print i innych operacji blokujących!
 ***************************************************************************/
static void i2sTask(void *arg)
{
  static int16_t out[I2S_BLOCK_FRAMES * 2];
  bool playing = false;

  (void)arg;

  for (;;) {
    uint32_t level = FIFO_Available(&g_fifo);
    size_t written = 0;

    if (level < g_fifoMin) g_fifoMin = level;
    if (level > g_fifoMax) g_fifoMax = level;

    /* start dopiero po napełnieniu FIFO do połowy (prefill) */
    if (!playing && level >= FIFO_TARGET_BYTES) {
      playing = true;
      DRIFT_Start(&g_drift);
    }

    if (playing) {
      uint32_t n = DRIFT_Process(&g_drift, out, I2S_BLOCK_FRAMES);
      if (n < I2S_BLOCK_FRAMES) {          /* underrun: wygaś ogon i zrób ponowny prefill */
        DRIFT_FadeOutTail(out, n, I2S_BLOCK_FRAMES);
        playing = false;
        g_underruns++;
      }
    } else {
      memset(out, 0, sizeof(out));         /* strumień do DMA nigdy się nie urywa */
    }
    g_playing = playing;

    USB_ApplyVolume((uint8_t *)out, I2S_BLOCK_BYTES);

    if (i2s_channel_write(g_txHandle, out, I2S_BLOCK_BYTES, &written, portMAX_DELAY) != ESP_OK) {
      g_i2sErrors++;
    }
    g_i2sBlocks++;
  }
}

/***************************************************************************
 *                              DIAGNOSTYKA
 ***************************************************************************/
#if AUDIO_DIAG_SERIAL
static void diagPrint(uint32_t now)
{
  static uint32_t tLast = 0, prevUsbBytes = 0, prevConsumed = 0;
  USB_Stats_t u;
  uint32_t dt = now - tLast;
  uint32_t fMin, fMax, usbFps, outFps, consumed;
  bool playing = g_playing;

  if (dt < DIAG_PERIOD_MS) return;
  tLast = now;

  USB_GetStats(&u);

  fMin = g_fifoMin;  fMax = g_fifoMax;
  g_fifoMin = 0xFFFFFFFFUL;  g_fifoMax = 0;
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
 *                              SETUP / LOOP
 ***************************************************************************/
void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-S3 USB AUDIO DAC (PCM5102)");

  oled_encoder_init();

  if (!FIFO_Init(&g_fifo, g_fifoStorage, FIFO_SIZE_BYTES, AUDIO_BYTES_PER_FRAME) ||
      !DRIFT_Init(&g_drift, &g_fifo, FIFO_TARGET_BYTES)) {
    Serial.println("FIFO/DRIFT init failed");
    for (;;) delay(1000);
  }
  if (!i2sInit()) {
    Serial.println("I2S FAILED");
    for (;;) delay(1000);
  }

  USB_Init(&g_fifo);

  xTaskCreatePinnedToCore(i2sTask, "I2S", 4096, NULL, configMAX_PRIORITIES - 1, NULL, 1);
  Serial.println("System ready");
}

void loop()
{
  static uint32_t tOled = 0;
  uint32_t now = millis();

  oled_encoder_loop();

  if (now - tOled >= OLED_PERIOD_MS) {
    USB_Stats_t u;
    tOled = now;
    USB_GetStats(&u);
    oled_audioBlock = g_i2sBlocks;
    oled_fifo = FIFO_Available(&g_fifo);
    oled_usbPackets = u.packets;
  }

#if AUDIO_DIAG_SERIAL
  diagPrint(now);
#endif
}
