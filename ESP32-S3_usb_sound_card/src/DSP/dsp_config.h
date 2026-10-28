/*
 * dsp_config.h - parametry modułów DSP (analizator widma).
 * Parametry toru audio (format, FIFO, źródło): AUDIO/audio_config.h.
 * Wygląd słupków (skala dB, opadanie, kreska szczytu): ui_bars.h (warstwa UI).
 */
#ifndef DSP_CONFIG_H
#define DSP_CONFIG_H

/* ---- Analizator widma ---- */
/* Maksymalna liczba kanałów analizy: 1 = tylko miks mono (oszczędza 16 KB RAM), 2 = także L i R osobno.
 * Łączny limit pasm (kanały x pasma) to SPECTRUM_MAX_BANDS w spectrum.h. */
#ifndef SPECTRUM_MAX_CHANNELS
#define SPECTRUM_MAX_CHANNELS  2
#endif
#define SPECTRUM_FFT_N       4096    /* potęga 2; MAKSYMALNE okno FFT (93 ms przy 44,1 kHz); okno robocze 1024..N wybiera się w menu */
#define SPECTRUM_HOP         1024    /* co tyle nowych próbek liczymy FFT (512 = gładziej, 2x CPU); dla krótszych okien min( HOP, okno / 2 ) */
#define SPECTRUM_F_LOW       30      /* Hz, dolna krawędź pierwszego pasma */
#define SPECTRUM_F_HIGH      20000   /* Hz, górna krawędź ostatniego pasma */

// Simon test
// #define SPECTRUM_FFT_Q           -1.0f   /* 0 = jak dotąd, > 0 = Q, < 0 = auto */
/* Silnik analizy pasm (przełączalny w czasie pracy: SPECTRUM_SetEngine):
 *   0 = FFT (jedna FFT, pasma = sumy binów; słaba rozdzielczość w basie),
 *   1 = bank filtrów IIR (pasmowe biquady o stałym Q, rozdzielczość bez zmian w całym paśmie),
 *   2 = bank IIR z decymacją oktawową (to samo co 1, ale tańszy przy dużej liczbie pasm). */
#define SPECTRUM_ENGINE_DEFAULT  2
/* Bank IIR: liczba kaskadowanych sekcji biquad na pasmo, 1..4 (1 = 2. rząd, -12 dB/okt; 2 = 4. rząd, -24 dB/okt).
 * Krawędzie pasma (-3 dB) zostają w tym samym miejscu, zmienia się tylko stromość zboczy i koszt CPU. */
#define SPECTRUM_IIR_SECTIONS    2
/* Stała czasowa detektora energii: tau = max( TAU_MIN_MS, TAU_CYCLES / f0 ). Dla basu dłuższa (tłumi tętnienia 2*f0). */
#define SPECTRUM_IIR_TAU_CYCLES  1.0f
#define SPECTRUM_IIR_TAU_MIN_MS  10
/* Korekta poziomu silnika IIR względem FFT (dB, + = wyżej). Bazowo szum biały daje ten sam poziom co FFT. */
#define SPECTRUM_IIR_TRIM_DB     0.0f

/* ---- Kolejka tap -> loop (dsp_manage) ----
 * Wątek audio kopiuje ramki stereo do kolejki, a loop() (DSP_Poll) przenosi je do analizatora. Gdy loop() nie nadąża,
 * kolejka nadpisuje najstarsze ramki, a dsp_manage wykrywa lukę, porzuca zaległość i resetuje analizator.
 * Tolerancja spóźnienia loop() = ( FRAMES - GUARD ) / fs, czyli ok. 81 ms przy 44,1 kHz i wartościach domyślnych. */
#ifndef DSP_QUEUE_FRAMES
#define DSP_QUEUE_FRAMES        4096   /* potęga 2; ramki stereo int16 (4 B każda), czyli 16 KB RAM */
#endif
#define DSP_QUEUE_GUARD_FRAMES  512    /* zapas na blok zapisywany w trakcie odczytu; musi być >= AUDIO_BLOCK_FRAMES */
/* Ile hopów analizy loop() przekazuje silnikowi w jednym DSP_Poll(). Ogranicza czas jednego wywołania (silnik IIR liczy
 * wszystkie przekazane próbki); reszta zaległości czeka w kolejce. Musi być <= SPECTRUM_FFT_N / SPECTRUM_HOP. */
#ifndef DSP_POLL_MAX_HOPS
#define DSP_POLL_MAX_HOPS       2
#endif

#endif /* DSP_CONFIG_H */
