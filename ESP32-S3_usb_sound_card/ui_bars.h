/*
 * ui_bars.h - słupki widma: z mocy pasm (DSP) robi wysokości słupków i kreski szczytu. Warstwa UI.
 *
 * DSP oddaje tylko moc pasm (skala liniowa, sinus pełnej skali = 1.0), a tutaj powstaje wygląd: skala dB -> 0..255,
 * natychmiastowe narastanie, opadanie słupka i kreska szczytu. Opadanie i podtrzymanie szczytu liczone są z RZECZYWISTEGO
 * czasu między ramkami (a nie z liczby ramek), więc wygląd nie zależy od hopa analizy, częstotliwości próbkowania ani od
 * tego, jak często pętla główna zdąży policzyć ramkę. Moduł nie zna DSP, wyświetlacza ani Arduino.
 *
 * Użycie (loop): po każdej nowej ramce widma BARS_Update( moc, ile, millis() ), potem BARS_Get().
 * Wszystko z jednego wątku. Po zmianie układu pasm (liczba pasm, silnik) wołaj BARS_Reset().
 */
#ifndef UI_BARS_H
#define UI_BARS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BARS_MAX  64   /* musi być równe DSP_SPECTRUM_MAX_BANDS (sprawdza audio_ui.cpp) */

/* Skala dB: moc pasma 0 dB = sinus pełnej skali. */
#ifndef BARS_DB_TOP
#define BARS_DB_TOP        (-15)   /* dB = pełny słupek */
#endif
#ifndef BARS_DB_FLOOR
#define BARS_DB_FLOOR      (-75)   /* dB = pusty słupek */
#endif

/* Czas opadania słupka z pełnej skali do zera (ms), od najwolniejszego do najszybszego.
 * Regulowany w czasie pracy (BARS_FallStep, menu SPECTRUM FALL). */
#ifndef BARS_FALL_MS_LIST
#define BARS_FALL_MS_LIST  { 1500, 1000, 700, 500, 350, 250, 150, 100 }
#endif
#ifndef BARS_FALL_DEFAULT
#define BARS_FALL_DEFAULT  3       /* indeks na liście: 500 ms */
#endif

/* Kreska szczytu: podtrzymanie (ms), potem opadanie z pełnej skali do zera w czasie z listy BARS_PEAK_FALL_MS_LIST.
 * Lista rośnie (większa wartość = wolniejsze opadanie); wartość 0 = brak kresek szczytu (nie są liczone ani rysowane).
 * Regulowane w czasie pracy (BARS_PeakStep, menu PEAK FALL). */
#ifndef BARS_PEAK_HOLD_MS
#define BARS_PEAK_HOLD_MS  460
#endif
#ifndef BARS_PEAK_FALL_MS_LIST
#define BARS_PEAK_FALL_MS_LIST  { 0, 300, 500, 700, 1000, 1500, 2000, 3000 }
#endif
#ifndef BARS_PEAK_FALL_DEFAULT
#define BARS_PEAK_FALL_DEFAULT  5  /* indeks na liście: 1500 ms (tak było przed wprowadzeniem regulacji) */
#endif

/* Najdłuższy odstęp między ramkami brany do opadania (ms). Dłuższa przerwa (np. powrót z ekranu STAT) opuszcza słupki
 * najwyżej o tyle, a nie "w jednym skoku do zera". */
#ifndef BARS_MAX_DT_MS
#define BARS_MAX_DT_MS     500
#endif

/* Zeruje słupki i szczyty. */
void BARS_Reset(void);

/* Nowa ramka: power[0..count-1] to moc kolejnych pasm (liniowo), now_ms = czas ramki (np. millis()).
 * count do BARS_MAX; zmiana count względem poprzedniego wywołania zeruje słupki. */
void BARS_Update(const float *power, int count, uint32_t now_ms);

/* Poziomy 0..255 i szczyty 0..255 z ostatniego BARS_Update (count wartości; tablice min. BARS_MAX). */
void BARS_Get(uint8_t *level, uint8_t *peak);

/* Szybkość opadania: indeks 0 = najwolniej ... BARS_FallSteps()-1 = najszybciej (czasy z BARS_FALL_MS_LIST).
 * BARS_FallStep(dir): +1 szybciej, -1 wolniej, z ograniczeniem do zakresu (bez zawijania). */
void BARS_FallStep(int dir);
int  BARS_FallIndex(void);
int  BARS_FallSteps(void);
int  BARS_GetFallMs(void);

/* Opadanie kresek szczytu: indeks 0 = BRAK szczytów (0 ms) ... BARS_PeakSteps()-1 = najwolniej (czasy z BARS_PEAK_FALL_MS_LIST).
 * BARS_PeakStep(dir): +1 większa wartość (wolniej), -1 mniejsza (szybciej, a na końcu brak szczytów), z ograniczeniem do zakresu.
 * Przy braku szczytów BARS_Get() oddaje szczyt 0 dla każdego słupka. */
void BARS_PeakStep(int dir);
int  BARS_PeakIndex(void);
int  BARS_PeakSteps(void);
int  BARS_GetPeakMs(void);                 /* 0 = brak szczytów */

#ifdef __cplusplus
}
#endif

#endif /* UI_BARS_H */
