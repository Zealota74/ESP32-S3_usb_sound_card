#ifndef FONTS_H
#define FONTS_H

#include "SPI.h"  // Biblioteka do obsługi komunikacji SPI
#include <U8g2lib.h>


// Testy
volatile int16_t diag_lastSample = 0;
volatile int32_t diag_maxJump = 0;
// testy


// OLED
#define SPI_MOSI_OLED 39
#define SPI_MISO_OLED 0
#define SPI_SCK_OLED 38
#define CS_OLED 42
#define DC_OLED 2
#define RESET_OLED 40


U8G2_SH1122_256X64_F_4W_HW_SPI u8g2(
  U8G2_R2,
  CS_OLED,
  DC_OLED,
  RESET_OLED);


// Encoder
#define CLK_PIN2 4
#define DT_PIN2 5
#define SW_PIN2 6

struct OLEDStatus {
  volatile uint32_t audioBlock;
  volatile uint32_t fifo;
  volatile uint32_t usbPackets;
  volatile uint32_t i2sBlocks;
};


int encoderPos = 0;
int lastCLK = HIGH;
bool buttonState = false;

volatile uint32_t oled_audioBlock = 0;
volatile uint32_t oled_fifo = 0;
volatile uint32_t oled_usbPackets = 0;
volatile uint32_t oled_i2sBlocks = 0;

static void oled_encoder_init() {
  SPI.begin(
    SPI_SCK_OLED,
    SPI_MISO_OLED,
    SPI_MOSI_OLED);
  u8g2.begin();

  pinMode(CLK_PIN2, INPUT_PULLUP);
  pinMode(DT_PIN2, INPUT_PULLUP);
  pinMode(SW_PIN2, INPUT_PULLUP);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);

  u8g2.drawStr(0, 15, "ESP32-S3 AUDIO TEST");
  u8g2.drawStr(0, 32, "OLED OK");

  u8g2.sendBuffer();

  lastCLK = digitalRead(CLK_PIN2);

  Serial.println("INIT OK");
}
static void oled_encoder_test_loop() {
  int clk = digitalRead(CLK_PIN2);

  if (clk != lastCLK) {
    if (digitalRead(DT_PIN2) != clk)
      encoderPos++;
    else
      encoderPos--;

    Serial.printf(
      "ENCODER: %d\n",
      encoderPos);

    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_6x12_tf);

    char txt[32];

    sprintf(txt, "ENC: %d", encoderPos);

    u8g2.drawStr(0, 15, txt);
    u8g2.drawStr(0, 35, "Turn OK");

    u8g2.sendBuffer();
  }

  lastCLK = clk;

  if (digitalRead(SW_PIN2) == LOW) {
    if (!buttonState) {
      buttonState = true;

      Serial.println("BUTTON PRESS");


      u8g2.clearBuffer();

      u8g2.setFont(u8g2_font_6x12_tf);

      u8g2.drawStr(0, 15, "BUTTON OK");

      char txt[32];

      sprintf(txt, "ENC:%d", encoderPos);

      u8g2.drawStr(0, 35, txt);

      u8g2.sendBuffer();
    }
  } else {
    buttonState = false;
  }


  delay(5);
}

static void oled_encoder_loop() {
  int clk = digitalRead(CLK_PIN2);

  static uint32_t lastRefresh = 0;
  if (millis() - lastRefresh < 200) return;
  lastRefresh = millis();

  char txt[32];

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x12_tf);

  sprintf(txt, "BLK %lu", oled_audioBlock);
  u8g2.drawStr(0, 12, txt);

  sprintf(txt, "FIFO %lu", oled_fifo);
  u8g2.drawStr(0, 26, txt);

  sprintf(txt, "USB %lu", oled_usbPackets);
  u8g2.drawStr(0, 40, txt);

  sprintf(txt, "I2S %lu", oled_i2sBlocks);
  u8g2.drawStr(0, 54, txt);

  u8g2.sendBuffer();
}



#endif