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
  uint32_t fft_us;             /* czas ostatniej FFT widma (0 = jeszcze nie liczona) */
  int32_t  drift_ppm;          /* bieżąca korekta tempa (0 poza odtwarzaniem) */
  uint32_t sample_rate;        /* bieżąca częstotliwość próbkowania wyjścia (Hz) */
  AUDIO_SrcStats_t src;        /* statystyki źródła */
} AUDIO_Stats_t;

/* Inicjalizuje cały tor i uruchamia wyjście. Zwraca AUDIO_OK lub kod błędu. */
int          AUDIO_Init(const AUDIO_Config_t *cfg);
const char * AUDIO_ErrorText(int err);

/* Wołaj z loop() w każdym przebiegu (nieblokująco). now_ms = millis(). */
void AUDIO_Poll(uint32_t now_ms);

/* Bieżąca częstotliwość próbkowania toru (Hz). Zmienia się, gdy źródło (host USB) wybierze inny format;
 * lista obsługiwanych częstotliwości: AUDIO_SAMPLE_RATE_LIST w audio_config.h. */
uint32_t AUDIO_GetSampleRate(void);

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

/* Analizator widma. Liczony w AUDIO_Poll() (czyli w loop()), tylko gdy włączony.
 * Próbki pochodzą z toru audio przed regulacją głośności, więc widmo nie zależy od poziomu. */
#define AUDIO_SPECTRUM_MAX_BARS  64

typedef struct {
  uint8_t  bars;                              /* liczba słupków: 7, 16 lub 32 */
  uint8_t  level[AUDIO_SPECTRUM_MAX_BARS];    /* wysokość słupka 0..255 */
  uint8_t  peak[AUDIO_SPECTRUM_MAX_BARS];     /* kreska szczytu 0..255 */
} AUDIO_Spectrum_t;

void AUDIO_SpectrumEnable(int on);            /* włącz tylko, gdy widmo jest wyświetlane */
void AUDIO_SpectrumSetBars(int bars);         /* 7, 16 lub 32 */
int  AUDIO_SpectrumGetBars(void);
void AUDIO_SpectrumCycleBars(int dir);        /* +1 / -1: 7 -> 16 -> 32 -> 7 (cyklicznie) */
int  AUDIO_GetSpectrum(AUDIO_Spectrum_t *out);/* 1 = nowa ramka od ostatniego wywołania */
void AUDIO_SpectrumFallStep(int dir);         /* +1 szybsze opadanie słupków, -1 wolniejsze (bez zawijania) */
int  AUDIO_SpectrumGetFallMs(void);           /* czas opadania słupka z pełnej skali do zera (ms) */
int  AUDIO_SpectrumFallIndex(void);           /* 0 = najwolniej ... AUDIO_SpectrumFallSteps()-1 = najszybciej */
int  AUDIO_SpectrumFallSteps(void);

/* Statystyki; reset_minmax = 1 zeruje fifo_min/fifo_max po odczycie. */
void AUDIO_GetStats(AUDIO_Stats_t *out, int reset_minmax);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
