/*
 * dsp_manage.h - zarządca modułów DSP (obecnie: analizator widma)
 *
 * Podłącza się do toru audio przez AUDIO_SetTap() (audio_manage.h), więc tor audio nie wie o DSP,
 * a DSP korzysta tylko z kopii strumienia (po drifcie, przed regulacją głośności - widmo nie zależy od poziomu).
 * Tap tylko kopiuje ramki do kolejki (bez blokowania), a DSP_Poll() przenosi je do analizatora w porcjach ograniczonych
 * w czasie. Gdy loop() spóźni się bardziej niż tolerancja kolejki, zaległość jest porzucana, a analizator resetowany
 * (patrz DSP_GetStats). Analizator jest jednowątkowy i nie zna wątku audio.
 * Silniki widma (FFT, IIR, IIR-MR) i ich parametry: DSP/ (dsp_config.h).
 *
 * Użycie:
 *   setup(): AUDIO_Init(&cfg); DSP_Init();
 *   loop():  AUDIO_Poll(millis()); DSP_Poll();  w każdym przebiegu
 *            DSP_Spectrum*() / DSP_GetSpectrum() dla UI
 */
#ifndef DSP_MANAGE_H
#define DSP_MANAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inicjalizuje DSP i rejestruje tap w torze audio (AUDIO_SetTap). Wołaj w setup() obok AUDIO_Init(). */
void DSP_Init(void);

/* Wołaj z loop() w każdym przebiegu (nieblokująco), po AUDIO_Poll().
 * Śledzi częstotliwość próbkowania toru i liczy kolejne ramki widma, tylko gdy widmo jest włączone. */
void DSP_Poll(void);

/* ---- Analizator widma ---- */
#define DSP_SPECTRUM_MAX_BARS  64

typedef struct {
  uint8_t  bars;                              /* liczba słupków: 7, 16, 32 lub 64 */
  uint8_t  level[DSP_SPECTRUM_MAX_BARS];      /* wysokość słupka 0..255 */
  uint8_t  peak[DSP_SPECTRUM_MAX_BARS];       /* kreska szczytu 0..255 */
} DSP_Spectrum_t;

void DSP_SpectrumEnable(int on);              /* włącz tylko, gdy widmo jest wyświetlane */
void DSP_SpectrumSetBars(int bars);           /* 7, 16, 32 lub 64 */
int  DSP_SpectrumGetBars(void);
void DSP_SpectrumCycleBars(int dir);          /* +1 / -1: 7 -> 16 -> 32 -> 64 -> 7 (cyklicznie) */
int  DSP_GetSpectrum(DSP_Spectrum_t *out);    /* 1 = nowa ramka od ostatniego wywołania */
void DSP_SpectrumFallStep(int dir);           /* +1 szybsze opadanie słupków, -1 wolniejsze (bez zawijania) */
int  DSP_SpectrumGetFallMs(void);             /* czas opadania słupka z pełnej skali do zera (ms) */
int  DSP_SpectrumFallIndex(void);             /* 0 = najwolniej ... DSP_SpectrumFallSteps()-1 = najszybciej */
int  DSP_SpectrumFallSteps(void);

int         DSP_SpectrumGetEngine(void);
void        DSP_SpectrumSetEngine(int e);
int         DSP_SpectrumEngineCount(void);
const char *DSP_SpectrumEngineName(int e);

/* Czas silnika widma (FFT lub IIR) na jeden hop próbek SPECTRUM_HOP, us; średnia z ok. 0,5 s
 * (0 = jeszcze nie policzona). Dawniej AUDIO_Stats_t.fft_us.
 * Budżet = hop / fs; obciążenie % = czas * fs / ( HOP * 1e4 ). */
uint32_t DSP_SpectrumGetUs(void);

/* Diagnostyka kolejki między wątkiem audio a loop(). backlog_max = największa zaległość (ramki stereo) od ostatniego
 * odczytu z reset_minmax = 1; backlog_limit = próg, powyżej którego zaległość jest porzucana (luka); gaps = liczba takich
 * luk od DSP_Init(). Zaległość w ms = ramki * 1000 / częstotliwość próbkowania. */
typedef struct {
  uint32_t backlog_max;
  uint32_t backlog_limit;
  uint32_t gaps;
} DSP_Stats_t;
void DSP_GetStats(DSP_Stats_t *out, int reset_minmax);

#ifdef __cplusplus
}
#endif

#endif /* DSP_MANAGE_H */
