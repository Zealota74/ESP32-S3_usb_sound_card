/*
 * spectrum.h - analizator widma (FFT) dla toru audio. Kod niezależny od platformy.
 *
 * SPECTRUM_Feed() zapisuje próbki mono do bufora kołowego (historia okna analizy).
 * SPECTRUM_Process() co SPECTRUM_HOP nowych próbek liczy moc pasm
 * logarytmicznych (7/16/32/64) wybranym silnikiem i wygładza słupki (szybkie narastanie,
 * wolne opadanie, kreska szczytu z podtrzymaniem). Cały moduł jest jednowątkowy: wszystkie funkcje woła ten sam
 * wątek (loop). Przekazanie próbek z wątku audio i politykę zaległości prowadzi dsp_manage.
 *
 * Silniki analizy pasm (przełączane w czasie pracy):
 *   SPECTRUM_ENGINE_FFT - FFT N punktów (okno Hanna, przesuwane), pasma = sumy mocy binów,
 *   SPECTRUM_ENGINE_IIR - bank pasmowych filtrów IIR o stałym Q (dobra rozdzielczość w basie),
 *   SPECTRUM_ENGINE_IIR_MR - ten sam bank z decymacją oktawową (kilka razy mniejszy koszt przy 32/64 pasmach).
 * Kod silników: spectrum_fft.cpp, spectrum_iir.cpp; kontrakt: spectrum_engine.h.
 */
#ifndef SPECTRUM_H
#define SPECTRUM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPECTRUM_MAX_BARS   64

#define SPECTRUM_ENGINE_FFT    0
#define SPECTRUM_ENGINE_IIR    1
#define SPECTRUM_ENGINE_IIR_MR 2
#define SPECTRUM_ENGINE_COUNT  3

void SPECTRUM_Init(uint32_t sample_rate);

/* Dopisuje ramki stereo int16 (L,R,L,R...) jako mono (L+R)/2. Wołać z tego samego wątku co SPECTRUM_Process(). */
void SPECTRUM_Feed(const int16_t *stereo, uint32_t frames);

/* Zmiana częstotliwości próbkowania w czasie pracy: przelicza pasma i czas opadania, zeruje słupki.
 * Wołać z tego samego wątku co SPECTRUM_Process() (loop). */
void SPECTRUM_SetRate(uint32_t sample_rate);

/* Wybór silnika analizy pasm (SPECTRUM_ENGINE_*). Zmiana zeruje słupki i stan filtrów.
 * Wołać z tego samego wątku co SPECTRUM_Process() (loop). Indeks spoza zakresu jest ograniczany. */
void        SPECTRUM_SetEngine(int engine);
int         SPECTRUM_GetEngine(void);
const char *SPECTRUM_EngineName(int engine);   /* "FFT" / "IIR" / "IIR-MR", do wyświetlenia na ekranie */

/* Dobroć pasm w silniku FFT. q = 0: pasma "cegiełkowe" (suma binów, jak dotąd); q > 0: jądro trójkątne o szerokości
 * połówkowej f0/q, wspólne dla wszystkich pasm (nie węższe niż rozdzielczość FFT), pasma nachodzą na siebie;
 * q < 0: automatycznie dobroć najniższego pasma (wynika z rozdzielczości FFT). Domyślnie SPECTRUM_FFT_Q.
 * Wołać z tego samego wątku co SPECTRUM_Process() (loop). Nie zeruje słupków. */
void  SPECTRUM_SetFftQ(float q);
float SPECTRUM_GetFftQ(void);

/* Liczba słupków 1..SPECTRUM_MAX_BARS (używane: 7, 16, 32). Zmiana zeruje słupki. */
void SPECTRUM_SetBars(int bars);
int  SPECTRUM_GetBars(void);

/* Szybkość opadania słupków: indeks 0 = najwolniej ... SPECTRUM_FallSteps()-1 = najszybciej
 * (czasy z SPECTRUM_FALL_MS_LIST). SPECTRUM_SetFall ogranicza indeks do zakresu. */
int      SPECTRUM_FallSteps(void);
void     SPECTRUM_SetFall(int idx);
int      SPECTRUM_GetFall(void);
uint32_t SPECTRUM_GetFallMs(void);

/* Zeruje słupki i szczyty (np. po włączeniu ekranu). */
void SPECTRUM_Reset(void);

/* Nieciągłość strumienia (utracone próbki): zeruje historię okna i stan silnika, ale zostawia słupki
 * (opadają same). Analiza rusza od próbek, które przyjdą po wywołaniu. */
void SPECTRUM_Resync(void);

/* Liczy kolejną ramkę, jeśli przybyło dość próbek. Zwraca 1, gdy powstała nowa ramka. */
int  SPECTRUM_Process(void);

/* Liczba próbek objętych ostatnim wywołaniem silnika (>= SPECTRUM_HOP; rośnie, gdy loop wola SPECTRUM_Process rzadziej
 * niż co hop). Obciążenie silnika = czas SPECTRUM_Process / ( SPECTRUM_GetLastBlock() / fs ). */
uint32_t SPECTRUM_GetLastBlock(void);

/* Pomiar kosztu silnika niezależny od pętli głównej: liczy blok SPECTRUM_HOP próbek z bufora (jak jedna ramka
 * przy idealnym wołaniu co hop). Mierz czas wokół tego wywołania (np. micros()). Tylko diagnostyka: po serii wywołań
 * zawołaj SPECTRUM_Reset(), bo stan filtrów IIR dostaje powtórnie te same próbki. Zwraca 1/0 jak SPECTRUM_Process. */
int SPECTRUM_Bench(void);

/* Poziomy 0..255 i szczyty 0..255 dla bieżącej liczby słupków (tablice min. SPECTRUM_MAX_BARS). */
void SPECTRUM_GetFrame(uint8_t *level, uint8_t *peak);

#ifdef SPECTRUM_TEST
const float *SPECTRUM_DebugPower(void);        /* moc binów FFT (tylko po silniku FFT) */
uint32_t     SPECTRUM_DebugLastStart(void);
const float *SPECTRUM_DebugBandPower(void);    /* moc pasm z ostatniej ramki, skala: sinus pełnej skali = 1.0 */
#endif

#ifdef __cplusplus
}
#endif

#endif /* SPECTRUM_H */
