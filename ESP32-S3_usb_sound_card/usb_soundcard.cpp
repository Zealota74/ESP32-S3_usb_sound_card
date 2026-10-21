/*
 * usb_soundcard.cpp - część zależna od platformy (ESP32-S3 + USBAudioCard).
 * Cała reszta projektu widzi tylko funkcje USB_*.
 */
#include <Arduino.h>
#include "USB.h"
#include "USBAudioCard.h"

#include "usb_soundcard.h"
#include "audio_config.h"

static USBAudioCard s_uac(AUDIO_SAMPLE_RATE, UAC_BPS_16, UAC_SPK_STEREO, UAC_MIC_NONE);
static FIFO_t *s_rxFifo = NULL;

static volatile uint32_t s_packets = 0;
static volatile uint32_t s_bytes = 0;
static volatile uint32_t s_shortWrites = 0;
static volatile uint32_t s_lastPacketMs = 0;

/* Wywoływane w kontekście TinyUSB. Bez printów i bez blokowania! */
static void usbRxCallback(void *data, uint16_t len)
{
  uint32_t written;

  if (s_rxFifo == NULL) return;

  written = FIFO_Write(s_rxFifo, (const uint8_t *)data, len);

  s_packets++;
  s_bytes += written;
  if (written != len) s_shortWrites++;
  s_lastPacketMs = millis();
}

void USB_Init(FIFO_t *rx_fifo)
{
  s_rxFifo = rx_fifo;

  USB.VID(0x303A);
  USB.PID(0x4001);

  s_uac.onData(usbRxCallback);
  s_uac.begin();
  USB.begin();
}

void USB_ApplyVolume(uint8_t *data, uint32_t bytes)
{
  s_uac.applyVolume(data, (uint16_t)bytes);
}

void USB_GetStats(USB_Stats_t *out)
{
  out->packets = s_packets;
  out->bytes = s_bytes;
  out->short_writes = s_shortWrites;
  out->last_packet_ms = s_lastPacketMs;
}

uint32_t USB_GetPacketAgeMs(void)
{
  return millis() - s_lastPacketMs;
}
