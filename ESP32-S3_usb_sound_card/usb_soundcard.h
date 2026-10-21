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

/*
 * Uruchamia urządzenie USB Audio. Odebrane dane trafiają do rx_fifo.
 * Przed podłączeniem do hosta ustawia początkową głośność (dB, master),
 * żeby po starcie nie grało na 0 dB. Zwraca 1, gdy biblioteka przyjęła tę
 * wartość (odczyt zwrotny się zgadza), inaczej 0.
 */
int      USB_Init(FIFO_t *rx_fifo, int8_t initial_vol_db);

/* Ile razy host zmienił głośność lub mute od startu (zdarzenia biblioteki).
 * Rośnie nawet wtedy, gdy host wyśle tę samą wartość. */
uint32_t USB_GetHostCtrlCount(void);

/* Wypisuje na Serial zdarzenia głośności/mute od hosta zebrane w tle (diagnostyka).
 * Wołaj z loop(); przy USB_DEBUG_EVENTS 0 nie robi nic. */
void     USB_DebugPoll(void);

/* Diagnostyka głośności: tabela zmian głośności mastera zbierana w tle.
 * USB_DebugDump() wypisuje ją jedną linią ("VOLTAB (n): -43 -39 MUTE ..."),
 * USB_DebugClear() ją czyści. W .ino pod komendami 'd' i 'c' z monitora szeregowego. */
void     USB_DebugDump(void);
void     USB_DebugClear(void);

/* Stosuje głośność ustawioną przez hosta do bufora PCM 16 bit stereo. */
void     USB_ApplyVolume(uint8_t *data, uint32_t bytes);

void     USB_GetStats(USB_Stats_t *out);

/* Czas w ms od ostatniego pakietu (duża wartość = host nie wysyła dźwięku). */
uint32_t USB_GetPacketAgeMs(void);


/*
 * Sterowanie głośnością Windows z panelu urządzenia (HID Consumer Control).
 * steps > 0 = głośniej, steps < 0 = ciszej (jeden krok = jedno "naciśnięcie"
 * klawisza multimedialnego, w Windows zwykle 2 % suwaka).
 * USB_HidVolumeStep() tylko zapisuje kroki do licznika; faktyczne wysyłanie
 * robi USB_HidPoll() - wołaj je z loop() w każdym przebiegu.
 */
void     USB_HidVolumeStep(int32_t steps);
void     USB_HidPoll(void);
/* Ile kroków czeka jeszcze na wysłanie (0 = wszystkie wysłane). */
int32_t  USB_HidPending(void);
void     USB_HidMuteToggle(void);

/* Aktualna głośność (master) w dB, całkowite dB z biblioteki, i stan mute. */
float    USB_GetVolumeDb(void);
int      USB_IsMuted(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_SOUNDCARD_H */
