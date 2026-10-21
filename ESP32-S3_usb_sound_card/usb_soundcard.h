/*
 * usb_soundcard.h - odbiór audio z USB (UAC 1.0, ESP32-S3 TinyUSB)
 *
 * Moduł odbiera pakiety PCM od hosta i wpisuje je do podanego FIFO.
 * Nic nie wie o I2S ani o DAC-u.
 */
#ifndef USB_SOUNDCARD_H
#define USB_SOUNDCARD_H

#include <stdint.h>
#include "fifo.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t packets;         /* liczba odebranych pakietów */
  uint32_t bytes;           /* bajty faktycznie zapisane do FIFO */
  uint32_t short_writes;    /* pakiety zapisane niepełno (FIFO pełne) */
  uint32_t last_packet_ms;  /* millis() ostatniego pakietu */
} USB_Stats_t;

/* Uruchamia urządzenie USB Audio. Odebrane dane trafiają do rx_fifo. */
void     USB_Init(FIFO_t *rx_fifo);

/* Stosuje głośność ustawioną przez hosta do bufora PCM 16 bit stereo. */
void     USB_ApplyVolume(uint8_t *data, uint32_t bytes);

void     USB_GetStats(USB_Stats_t *out);

/* Czas w ms od ostatniego pakietu (duża wartość = host nie wysyła dźwięku). */
uint32_t USB_GetPacketAgeMs(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_SOUNDCARD_H */
