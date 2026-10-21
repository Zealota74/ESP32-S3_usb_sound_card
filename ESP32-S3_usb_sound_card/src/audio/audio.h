/*
 * audio.h - podsystem audio: źródło -> FIFO -> drift -> głośność -> wyjście
 *
 * Jedyny moduł audio, który dołącza program główny. Ukrywa FIFO, drift,
 * źródło (USB/Wi-Fi) i wyjście (I2S). Źródło wybiera się w audio_config.h,
 * wyjście wyznacza plik audio_out_*.cpp w projekcie.
 *
 * Użycie:
 *   setup(): AUDIO_Init(&cfg)
 *   loop():  AUDIO_Poll(millis()) w każdym przebiegu
 *            AUDIO_GetVolume()/AUDIO_GetStats() dla UI i diagnostyki
 *            AUDIO_VolumeStep()/AUDIO_MuteToggle() z obsługi wejść
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include "audio_config.h"
#include "audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  AUDIO_OK = 0,
  AUDIO_ERR_PARAM,    /* błędna konfiguracja */
  AUDIO_ERR_FIFO,     /* FIFO/drift nie wystartowały */
  AUDIO_ERR_OUT,      /* wyjście (I2S) nie wystartowało */
} AUDIO_Err_t;

typedef struct {
  int8_t startup_vol_db;   /* głośność po starcie w dB (0 = pełna); limit awaryjny, gdy źródło jej nie przyjmie */
} AUDIO_Config_t;

typedef struct {
  int      playing;            /* 1 = trwa odtwarzanie (po prefillu) */
  uint32_t blocks;             /* bloki przekazane na wyjście */
  uint32_t underruns;          /* niedobory FIFO */
  uint32_t overruns;           /* przepełnienia FIFO (skrócone zapisy) */
  uint32_t out_errors;         /* błędy zapisu na wyjście */
  uint32_t fifo_bytes;         /* bieżący poziom FIFO */
  uint32_t fifo_size;          /* rozmiar FIFO */
  uint32_t fifo_min;           /* minimum/maksimum poziomu od ostatniego skasowania */
  uint32_t fifo_max;
  uint32_t consumed_frames;    /* ramki zdjęte z FIFO od startu */
  int32_t  drift_ppm;          /* bieżąca korekta tempa (0 poza odtwarzaniem) */
  AUDIO_SrcStats_t src;        /* statystyki źródła */
} AUDIO_Stats_t;

/* Inicjalizuje cały tor i uruchamia wyjście. Zwraca AUDIO_OK lub kod błędu. */
int          AUDIO_Init(const AUDIO_Config_t *cfg);
const char * AUDIO_ErrorText(int err);

/* Wołaj z loop() w każdym przebiegu (nieblokująco). now_ms = millis(). */
void AUDIO_Poll(uint32_t now_ms);

/* Wejście danych dla źródeł (producent FIFO, dokładnie jeden). Źródła dostają
 * ten wskaźnik w init(), program główny go nie używa. */
uint32_t AUDIO_Push(const uint8_t *data, uint32_t bytes);

/* Głośność: stan z ostatniego AUDIO_Poll(), zmiana o 'ticks' poziomów (+/-), mute. */
void AUDIO_GetVolume(AUDIO_Volume_t *v);
void AUDIO_VolumeStep(int32_t ticks);
void AUDIO_MuteToggle(void);

/* Klawisz multimedialny do strony sterującej (USB: HID Consumer Control).
 * Wołać z wątku loop(). Źródło bez tej funkcji ignoruje wywołanie. */
void AUDIO_MediaKey(AUDIO_MediaKey_t key);

/* Limit startowy: 1 = aktywny (źródło nie przyjęło głośności startowej i host
 * jeszcze się nie odezwał). Zdejmuje go odezwanie się hosta albo AUDIO_GuardRelease(). */
int  AUDIO_GuardActive(void);
void AUDIO_GuardRelease(void);

/* Statystyki; reset_minmax = 1 zeruje fifo_min/fifo_max po odczycie. */
void AUDIO_GetStats(AUDIO_Stats_t *out, int reset_minmax);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
