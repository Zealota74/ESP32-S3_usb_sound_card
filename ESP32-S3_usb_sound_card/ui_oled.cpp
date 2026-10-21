/*
 * ui_oled.cpp - rysowanie na SH1122 (U8g2, pełny bufor, HW SPI)
 */
#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <math.h>
#include <stdio.h>

#include "simon_defines.h"
#include "ui_oled.h"

static U8G2_SH1122_256X64_F_4W_HW_SPI u8g2(U8G2_R2, CS_OLED, DC_OLED, RESET_OLED);

static volatile int      s_pct = 0;
static volatile int      s_mute = 0;
static volatile uint32_t s_dbgBlock = 0, s_dbgFifo = 0, s_dbgUsb = 0;
static volatile int      s_debugScreen = 0;
static volatile int      s_dirty = 1;
static volatile int      s_hostDb = 0;
static volatile int      s_guard = 0;
static volatile float    s_guardDb = 0.0f;
static volatile int      s_calib = -1;

void UI_Init(void)
{
  SPI.begin(SPI_SCK_OLED, SPI_MISO_OLED, SPI_MOSI_OLED);
  u8g2.begin();
#ifdef OLED_SPI_HZ
  u8g2.setBusClock(OLED_SPI_HZ);
#endif
  s_dirty = 1;
}

void UI_SetVolume(int percent, int mute)
{
  if (percent < 0)   percent = 0;
  if (percent > 100) percent = 100;
  if (percent != s_pct || mute != s_mute) {     /* przerysuj tylko przy zmianie */
    s_pct = percent;
    s_mute = mute;
    s_dirty = 1;
  }
}

void UI_SetCalib(int progress)
{
  if (progress != s_calib) {
    s_calib = progress;
    s_dirty = 1;
  }
}

void UI_SetDebug(uint32_t audioBlock, uint32_t fifo, uint32_t usbPackets)
{
  s_dbgBlock = audioBlock;
  s_dbgFifo = fifo;
  s_dbgUsb = usbPackets;
}

void UI_SetGuard(int active, float limit_db)
{
  if (active != s_guard || limit_db != s_guardDb) {
    s_guard = active;
    s_guardDb = limit_db;
    s_dirty = 1;
  }
}

void UI_SetHostDb(int host_db)
{
  s_hostDb = host_db;
}

void UI_ToggleDebug(void)
{
  s_debugScreen = !s_debugScreen;
  s_dirty = 1;
}

static void drawVolume(void)
{
  int pct = s_pct;
  int calib = s_calib;
  char txt[24];
  float frac;
  int fill;

  if (calib >= 0) {                            /* kalibracja: pasek = postęp pomiaru */
    frac = (float)calib / 100.0f;
    snprintf(txt, sizeof(txt), "%d %%", calib);
  } else if (s_mute) {
    frac = 0.0f;
    snprintf(txt, sizeof(txt), "MUTE");
  } else {
    frac = (float)pct / 100.0f;
    snprintf(txt, sizeof(txt), "%d", pct);
  }

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_logisoso28_tf);
  u8g2.drawStr(0, 30, (calib >= 0) ? "CAL" : "Volume");

  u8g2.setFont(u8g2_font_logisoso20_tf);
  u8g2.drawStr(256 - u8g2.getStrWidth(txt), 28, txt);

  if (s_guard) {
    char g[16];
    snprintf(g, sizeof(g), "LIMIT %.0f", (double)s_guardDb);
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawStr(112, 12, g);
  }

  u8g2.drawFrame(0, 40, 256, 24);
  fill = (int)(frac * 252.0f + 0.5f);
  if (fill > 0) u8g2.drawBox(2, 42, fill, 20);

  u8g2.sendBuffer();
}

static void drawDebug(void)
{
  char txt[32];

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);

  snprintf(txt, sizeof(txt), "BLK %lu", (unsigned long)s_dbgBlock);
  u8g2.drawStr(0, 12, txt);
  snprintf(txt, sizeof(txt), "FIFO %lu", (unsigned long)s_dbgFifo);
  u8g2.drawStr(0, 26, txt);
  snprintf(txt, sizeof(txt), "USB %lu", (unsigned long)s_dbgUsb);
  u8g2.drawStr(0, 40, txt);
  snprintf(txt, sizeof(txt), "VOL %d (host %d dB)%s", (int)s_pct, (int)s_hostDb, s_mute ? " MUTE" : "");
  u8g2.drawStr(0, 54, txt);

  u8g2.sendBuffer();
}

void UI_Loop(uint32_t now_ms)
{
  static uint32_t tLast = 0;

  if (s_debugScreen) {
    if (now_ms - tLast < 200) return;      /* liczniki odświeżamy co 200 ms */
    tLast = now_ms;
    drawDebug();
    s_dirty = 0;
    return;
  }

  if (!s_dirty) return;
  s_dirty = 0;
  drawVolume();
}
