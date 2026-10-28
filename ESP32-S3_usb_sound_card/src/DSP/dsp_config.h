/*
 * dsp_config.h - parametry modułów DSP (analizator widma).
 * Parametry toru audio (format, FIFO, źródło): AUDIO/audio_config.h.
 */
#ifndef DSP_CONFIG_H
#define DSP_CONFIG_H

/* ---- Analizator widma ---- */
#define SPECTRUM_FFT_N       4096    /* potęga 2; okno analizy (93 ms przy 44,1 kHz) */
#define SPECTRUM_HOP         1024    /* co tyle nowych próbek liczymy FFT (512 = gładziej, 2x CPU) */
#define SPECTRUM_F_LOW       30      /* Hz, dolna krawędź pierwszego pasma */
#define SPECTRUM_F_HIGH      20000   /* Hz, górna krawędź ostatniego pasma */
#define SPECTRUM_DB_TOP      (-15)   /* dB (energia pasma, 0 dB = sinus pełnej skali) = pełny słupek */
#define SPECTRUM_DB_FLOOR    (-75)   /* dB = pusty słupek */
/* Czas opadania słupka z pełnej skali do zera (ms), od najwolniejszego do najszybszego.
 * Regulowany w czasie pracy (ekran SPECTRUM, długie naciśnięcie B/D); przeliczany na krok na ramkę z hopa i częstotliwości
 * próbkowania, więc zmiana SPECTRUM_HOP nie zmienia wrażenia. */
#define SPECTRUM_FALL_MS_LIST  { 1500, 1000, 700, 500, 350, 250, 150, 100 }
#define SPECTRUM_FALL_DEFAULT  3     /* indeks na liście: 500 ms */
#define SPECTRUM_PEAK_HOLD   20      /* ramki podtrzymania kreski szczytu */
#define SPECTRUM_PEAK_FALL   4       /* opadanie kreski szczytu na ramkę */

// Simon test
// #define SPECTRUM_FFT_Q           -1.0f   /* 0 = jak dotąd, > 0 = Q, < 0 = auto */
/* Silnik analizy pasm (przełączalny w czasie pracy: SPECTRUM_SetEngine):
 *   0 = FFT (jedna FFT, pasma = sumy binów; słaba rozdzielczość w basie),
 *   1 = bank filtrów IIR (pasmowe biquady o stałym Q, rozdzielczość bez zmian w całym paśmie),
 *   2 = bank IIR z decymacją oktawową (to samo co 1, ale tańszy przy dużej liczbie pasm). */
#define SPECTRUM_ENGINE_DEFAULT  1
/* Bank IIR: liczba kaskadowanych sekcji biquad na pasmo, 1..4 (1 = 2. rząd, -12 dB/okt; 2 = 4. rząd, -24 dB/okt).
 * Krawędzie pasma (-3 dB) zostają w tym samym miejscu, zmienia się tylko stromość zboczy i koszt CPU. */
#define SPECTRUM_IIR_SECTIONS    2
/* Stała czasowa detektora energii: tau = max( TAU_MIN_MS, TAU_CYCLES / f0 ). Dla basu dłuższa (tłumi tętnienia 2*f0). */
#define SPECTRUM_IIR_TAU_CYCLES  1.0f
#define SPECTRUM_IIR_TAU_MIN_MS  10
/* Korekta poziomu silnika IIR względem FFT (dB, + = wyżej). Bazowo szum biały daje ten sam poziom co FFT. */
#define SPECTRUM_IIR_TRIM_DB     0.0f

#endif /* DSP_CONFIG_H */
