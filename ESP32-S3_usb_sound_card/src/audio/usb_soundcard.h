/*
 * usb_soundcard.h - źródło audio: USB (UAC 1.0, ESP32-S3 TinyUSB)
 *
 * Moduł odbiera pakiety PCM od hosta i przekazuje je funkcją push (AUDIO_Push)
 * podaną w USB_Init(). Nic nie wie o FIFO, I2S ani o DAC-u.
 * Dla reszty projektu jest widoczny jako obiekt AUDIO_SRC_USB (audio_source.h),
 * a funkcje USB_* i VOL_* są jego szczegółem implementacyjnym.
 */
#ifndef USB_SOUNDCARD_H
#define USB_SOUNDCARD_H

#include <stdint.h>
#include "audio_types.h"
#include "audio_source.h"

#ifdef __cplusplus
extern "C" {
#endif
// =========================== Volueme calib =============================
/* Zakres zgłaszany hostowi przez USBAudioCard (GET_MIN / GET_MAX, krok 1 dB). */
#ifndef VOL_HOST_MIN_DB
#define VOL_HOST_MIN_DB   (-50)
#endif
#ifndef VOL_HOST_MAX_DB
#define VOL_HOST_MAX_DB   (0)
#endif

/* Krok klawisza głośności w Windows (2 %) i liczba punktów krzywej (0..100 %). */
#define VOL_CAL_STEP_PCT  2
#define VOL_CAL_POINTS    (100 / VOL_CAL_STEP_PCT + 1)


/* dB od hosta -> pozycja suwaka 0..100 % (najbliższa możliwa przy kroku 1 dB). */
int   VOL_HostToPercent(float host_db);

/* dB od hosta -> mnożnik amplitudy 0..1; poziom minimalny (suwak 0 %) daje 0. */
float VOL_HostToGain(float host_db);
// =======================================================================

typedef struct {
  uint32_t packets;         /* liczba odebranych pakietów */
  uint32_t bytes;           /* bajty faktycznie zapisane do FIFO */
  uint32_t short_writes;    /* pakiety zapisane niepełno (FIFO pełne) */
  uint32_t last_packet_ms;  /* millis() ostatniego pakietu */
} USB_Stats_t;

/*
 * Uruchamia urządzenie USB Audio. Odebrane dane są przekazywane funkcją push.
 * Przed podłączeniem do hosta ustawia początkową głośność (dB, master),
 * żeby po starcie nie grało na 0 dB. Zwraca 1, gdy biblioteka przyjęła tę
 * wartość (odczyt zwrotny się zgadza), inaczej 0.
 */
int      USB_Init(AUDIO_PushFn push, int8_t initial_vol_db);

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
/* Następny / poprzedni utwór w Windows (HID Consumer: Scan Next/Previous Track). */
void     USB_HidMediaKey(AUDIO_MediaKey_t key);

/* ---------------------------------------------------------------------------
 * Poziomy głośności: jeden tick enkodera = jeden poziom.
 * Poziomy to wszystkie wartości dB, które Windows faktycznie wysyła do karty
 * (zmierzone klawiszami +-2 %). Poziom 0 = suwak 0 % (MUTE), poziom
 * VOL_LEVELS-1 = 0 dB (100 %). Pozostałe pozycje suwaka dają to samo dB co
 * sąsiedni poziom, więc na urządzeniu są nierozróżnialne i pomijane.
 * ------------------------------------------------------------------------- */
#define VOL_LEVELS          32

/* Czas oczekiwania na reakcję hosta po wysłanym klawiszu (ms). Gdy Windows nie
 * zmienia dB (klawisz w obrębie jednego poziomu), tyle trwa taki "pusty" krok. */
#ifndef VOL_ACK_TIMEOUT_MS
#define VOL_ACK_TIMEOUT_MS  50
#endif

/* Zabezpieczenie: tyle klawiszy pod rząd bez zmiany dB naszej karty i regulator
 * się poddaje (np. host nie odpowiada). Najdłuższy "pusty" odcinek krzywej to
 * kilka klawiszy, więc wartość ma zapas. */
#ifndef VOL_MAX_NOCHANGE
#define VOL_MAX_NOCHANGE    12
#endif

/* dB od hosta -> numer poziomu 0..VOL_LEVELS-1 (najbliższy z tabeli). */
int      VOL_LevelFromHost(float host_db);
/* Numer poziomu -> procent do wyświetlenia (zgodny z suwakiem Windows). */
int      VOL_LevelPercent(int level);

/* Przesuwa głośność o 'ticks' poziomów (+ głośniej, - ciszej), z ograniczeniem
 * do 0..VOL_LEVELS-1. Tylko zapisuje cel; klawisze wysyła USB_VolPoll(). */
void     USB_VolStep(int32_t ticks);
/* Wołaj z loop() w każdym przebiegu: wysyła klawisze HID po jednym i sprawdza
 * po każdym, jakie dB ustawił Windows, aż osiągnie poziom docelowy. */
void     USB_VolPoll(void);
/* Poziom do pokazania na OLED: docelowy w trakcie ruchu, inaczej bieżący. */
int      USB_VolLevel(void);
/* 1 = trwa dochodzenie do poziomu docelowego. */
int      USB_VolBusy(void);

/* Aktualna głośność (master) w dB, całkowite dB z biblioteki, i stan mute. */
float    USB_GetVolumeDb(void);
int      USB_IsMuted(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_SOUNDCARD_H */
