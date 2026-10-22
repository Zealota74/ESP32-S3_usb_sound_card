/*
 * audio_spectrum.h - analizator widma (FFT) dla toru audio. Kod niezależny od platformy.
 *
 * Producent (wątek audio): SPEC_Feed() zapisuje próbki mono do bufora kołowego.
 * Konsument (loop): SPEC_Process() co AUDIO_SPECTRUM_HOP nowych próbek liczy FFT
 * z ostatnich AUDIO_SPECTRUM_FFT_N próbek (okno Hanna, przesuwane), grupuje biny
 * w 7/16/32 pasm logarytmicznych i wygładza słupki (szybkie narastanie, wolne opadanie,
 * kreska szczytu z podtrzymaniem). Wszystko poza SPEC_Feed() wołać z jednego wątku.
 */
#ifndef AUDIO_SPECTRUM_H
#define AUDIO_SPECTRUM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPEC_MAX_BARS   64

void SPEC_Init(uint32_t sample_rate);

/* Producent: ramki stereo int16 (L,R,L,R...), zapisywane jako mono (L+R)/2. */
void SPEC_Feed(const int16_t *stereo, uint32_t frames);

/* Zmiana częstotliwości próbkowania w czasie pracy: przelicza pasma i czas opadania, zeruje słupki.
 * Wołać z tego samego wątku co SPEC_Process() (loop). */
void SPEC_SetRate(uint32_t sample_rate);

/* Liczba słupków 1..SPEC_MAX_BARS (używane: 7, 16, 32). Zmiana zeruje słupki. */
void SPEC_SetBars(int bars);
int  SPEC_GetBars(void);

/* Szybkość opadania słupków: indeks 0 = najwolniej ... SPEC_FallSteps()-1 = najszybciej
 * (czasy z AUDIO_SPECTRUM_FALL_MS_LIST). SPEC_SetFall ogranicza indeks do zakresu. */
int      SPEC_FallSteps(void);
void     SPEC_SetFall(int idx);
int      SPEC_GetFall(void);
uint32_t SPEC_GetFallMs(void);

/* Zeruje słupki i szczyty (np. po włączeniu ekranu). */
void SPEC_Reset(void);

/* Liczy kolejną ramkę, jeśli przybyło dość próbek. Zwraca 1, gdy powstała nowa ramka. */
int  SPEC_Process(void);

/* Poziomy 0..255 i szczyty 0..255 dla bieżącej liczby słupków (tablice min. SPEC_MAX_BARS). */
void SPEC_GetFrame(uint8_t *level, uint8_t *peak);

#ifdef AUDIO_SPECTRUM_TEST
const float *SPEC_DebugPower(void);
uint32_t     SPEC_DebugLastStart(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SPECTRUM_H */
