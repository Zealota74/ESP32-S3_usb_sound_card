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

/* Kreska szczytu: podtrzymanie (ms), potem opadanie z pełnej skali do zera w BARS_PEAK_FALL_MS. */
#ifndef BARS_PEAK_HOLD_MS
#define BARS_PEAK_HOLD_MS  460
#endif
#ifndef BARS_PEAK_FALL_MS
#define BARS_PEAK_FALL_MS  1500
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

#ifdef __cplusplus
}
#endif

#endif /* UI_BARS_H */
