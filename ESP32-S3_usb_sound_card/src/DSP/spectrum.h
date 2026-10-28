/*
 * spectrum.h - analizator widma (FFT) dla toru audio. Kod niezależny od platformy.
 *
 * SPECTRUM_Feed() zapisuje próbki do buforów kołowych (historia okna analizy).
 * SPECTRUM_Process() co SPECTRUM_HOP nowych próbek liczy moc pasm logarytmicznych (7/16/32/64 na kanał) wybranym
 * silnikiem, a SPECTRUM_GetPower() oddaje ją w skali liniowej (sinus pełnej skali = 1.0). Dalsza obróbka na wygląd
 * słupków (dB, opadanie, kreska szczytu) należy do UI (ui_bars.*). Cały moduł jest jednowątkowy: wszystkie funkcje woła
 * ten sam wątek (loop). Przekazanie próbek z wątku audio i politykę zaległości prowadzi dsp_manage.
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

#define SPECTRUM_MAX_BANDS  64      /* łączny limit pasm: kanały x pasma na kanał */

#define SPECTRUM_ENGINE_FFT    0
#define SPECTRUM_ENGINE_IIR    1
#define SPECTRUM_ENGINE_IIR_MR 2
#define SPECTRUM_ENGINE_COUNT  3

void SPECTRUM_Init(uint32_t sample_rate);

/* Dopisuje ramki stereo int16 (L,R,L,R...): w trybie 1 kanału jako miks mono (L+R)/2, w trybie 2 kanałów osobno L i R.
 * Wołać z tego samego wątku co SPECTRUM_Process(). */
void SPECTRUM_Feed(const int16_t *stereo, uint32_t frames);

/* Zmiana częstotliwości próbkowania w czasie pracy: przelicza pasma i zeruje stan analizy.
 * Wołać z tego samego wątku co SPECTRUM_Process() (loop). */
void SPECTRUM_SetRate(uint32_t sample_rate);

/* Wybór silnika analizy pasm (SPECTRUM_ENGINE_*). Zmiana zeruje stan analizy (filtry).
 * Wołać z tego samego wątku co SPECTRUM_Process() (loop). Indeks spoza zakresu jest ograniczany. */
void        SPECTRUM_SetEngine(int engine);
int         SPECTRUM_GetEngine(void);
const char *SPECTRUM_EngineName(int engine);   /* "FFT" / "IIR" / "IIR-MR", do wyświetlenia na ekranie */

/* Dobroć pasm w silniku FFT. q = 0: pasma "cegiełkowe" (suma binów, jak dotąd); q > 0: jądro trójkątne o szerokości
 * połówkowej f0/q, wspólne dla wszystkich pasm (nie węższe niż rozdzielczość FFT), pasma nachodzą na siebie;
 * q < 0: automatycznie dobroć najniższego pasma (wynika z rozdzielczości FFT). Domyślnie SPECTRUM_FFT_Q.
 * Wołać z tego samego wątku co SPECTRUM_Process() (loop). Nie zeruje stanu analizy. */
void  SPECTRUM_SetFftQ(float q);
float SPECTRUM_GetFftQ(void);

/* Kanały i pasma jednym wywołaniem (jedna przebudowa pasm i silnika).
 * channels: 1 = miks mono (L+R)/2, 2 = L i R osobno (ograniczone do SPECTRUM_MAX_CHANNELS).
 * bands = pasma NA KANAŁ, 1..SPECTRUM_MAX_BANDS / channels (używane: 7, 16, 32, 64); wartości spoza zakresu są ograniczane,
 * a odczyt (GetChannels/GetBands) zwraca wartości faktycznie ustawione. Zmiana zeruje stan analizy; zmiana liczby kanałów
 * czyści też bufory próbek. */
void SPECTRUM_Configure(int channels, int bands);

/* Liczba pasm na kanał (jak wyżej, przy bieżącej liczbie kanałów). Zmiana zeruje stan analizy. */
void SPECTRUM_SetBands(int bands);
int  SPECTRUM_GetBands(void);

/* Liczba kanałów (1 lub 2). Gdy bieżąca liczba pasm się nie mieści (kanały x pasma > SPECTRUM_MAX_BANDS), jest zmniejszana;
 * powrót do 1 kanału jej nie przywraca. */
void SPECTRUM_SetChannels(int channels);
int  SPECTRUM_GetChannels(void);

/* Zeruje stan silnika i zaczyna od świeżych próbek (np. po włączeniu widma). */
void SPECTRUM_Reset(void);

/* Nieciągłość strumienia (utracone próbki): zeruje historię okna i stan silnika. Analiza rusza od próbek,
 * które przyjdą po wywołaniu. */
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

/* Moc pasm z ostatniej ramki: układ [kanał * bands + pasmo], razem kanały x bands wartości (tablica min. SPECTRUM_MAX_BANDS
 * floatów). Skala liniowa: sinus pełnej skali w środku pasma = 1.0 (0 dB). Wołać po SPECTRUM_Process() zwracającym 1. */
void SPECTRUM_GetPower(float *power);

#ifdef SPECTRUM_TEST
const float *SPECTRUM_DebugPower(void);        /* moc binów FFT (tylko po silniku FFT) */
uint32_t     SPECTRUM_DebugLastStart(void);
const float *SPECTRUM_DebugBandPower(void);    /* moc pasm z ostatniej ramki, skala: sinus pełnej skali = 1.0 */
#endif

#ifdef __cplusplus
}
#endif

#endif /* SPECTRUM_H */
