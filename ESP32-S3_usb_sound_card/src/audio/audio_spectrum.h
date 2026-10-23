/*
 * audio_spectrum.h - analizator widma (FFT) dla toru audio. Kod niezależny od platformy.
 *
 * Producent (wątek audio): SPEC_Feed() zapisuje próbki mono do bufora kołowego.
 * Konsument (loop): SPEC_Process() co AUDIO_SPECTRUM_HOP nowych próbek liczy moc pasm
 * logarytmicznych (7/16/32/64) wybranym silnikiem i wygładza słupki (szybkie narastanie,
 * wolne opadanie, kreska szczytu z podtrzymaniem). Wszystko poza SPEC_Feed() wołać z jednego wątku.
 *
 * Silniki analizy pasm (przełączane w czasie pracy):
 *   SPEC_ENGINE_FFT - FFT N punktów (okno Hanna, przesuwane), pasma = sumy mocy binów,
 *   SPEC_ENGINE_IIR - bank pasmowych filtrów IIR o stałym Q (dobra rozdzielczość w basie),
 *   SPEC_ENGINE_IIR_MR - ten sam bank z decymacją oktawową (kilka razy mniejszy koszt przy 32/64 pasmach).
 * Kod silników: spectrum_fft.cpp, spectrum_iir.cpp; kontrakt: spectrum_engine.h.
 */
#ifndef AUDIO_SPECTRUM_H
#define AUDIO_SPECTRUM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPEC_MAX_BARS   64

#define SPEC_ENGINE_FFT    0
#define SPEC_ENGINE_IIR    1
#define SPEC_ENGINE_IIR_MR 2
#define SPEC_ENGINE_COUNT  3

void SPEC_Init(uint32_t sample_rate);

/* Producent: ramki stereo int16 (L,R,L,R...), zapisywane jako mono (L+R)/2. */
void SPEC_Feed(const int16_t *stereo, uint32_t frames);

/* Zmiana częstotliwości próbkowania w czasie pracy: przelicza pasma i czas opadania, zeruje słupki.
 * Wołać z tego samego wątku co SPEC_Process() (loop). */
void SPEC_SetRate(uint32_t sample_rate);

/* Wybór silnika analizy pasm (SPEC_ENGINE_*). Zmiana zeruje słupki i stan filtrów.
 * Wołać z tego samego wątku co SPEC_Process() (loop). Indeks spoza zakresu jest ograniczany. */
void        SPEC_SetEngine(int engine);
int         SPEC_GetEngine(void);
const char *SPEC_EngineName(int engine);   /* "FFT" / "IIR" / "IIR-MR", do wyświetlenia na ekranie */

/* Dobroć pasm w silniku FFT. q = 0: pasma "cegiełkowe" (suma binów, jak dotąd); q > 0: jądro trójkątne o szerokości
 * połówkowej f0/q, wspólne dla wszystkich pasm (nie węższe niż rozdzielczość FFT), pasma nachodzą na siebie;
 * q < 0: automatycznie dobroć najniższego pasma (wynika z rozdzielczości FFT). Domyślnie AUDIO_SPECTRUM_FFT_Q.
 * Wołać z tego samego wątku co SPEC_Process() (loop). Nie zeruje słupków. */
void  SPEC_SetFftQ(float q);
float SPEC_GetFftQ(void);

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

/* Liczba próbek objętych ostatnim wywołaniem silnika (>= AUDIO_SPECTRUM_HOP; rośnie, gdy loop wola SPEC_Process rzadziej
 * niż co hop). Obciążenie silnika = czas SPEC_Process / ( SPEC_GetLastBlock() / fs ). */
uint32_t SPEC_GetLastBlock(void);

/* Pomiar kosztu silnika niezależny od pętli głównej: liczy blok AUDIO_SPECTRUM_HOP próbek z bufora (jak jedna ramka
 * przy idealnym wołaniu co hop). Mierz czas wokół tego wywołania (np. micros()). Tylko diagnostyka: po serii wywołań
 * zawołaj SPEC_Reset(), bo stan filtrów IIR dostaje powtórnie te same próbki. Zwraca 1/0 jak SPEC_Process. */
int SPEC_Bench(void);

/* Poziomy 0..255 i szczyty 0..255 dla bieżącej liczby słupków (tablice min. SPEC_MAX_BARS). */
void SPEC_GetFrame(uint8_t *level, uint8_t *peak);

#ifdef AUDIO_SPECTRUM_TEST
const float *SPEC_DebugPower(void);        /* moc binów FFT (tylko po silniku FFT) */
uint32_t     SPEC_DebugLastStart(void);
const float *SPEC_DebugBandPower(void);    /* moc pasm z ostatniej ramki, skala: sinus pełnej skali = 1.0 */
#endif

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SPECTRUM_H */
