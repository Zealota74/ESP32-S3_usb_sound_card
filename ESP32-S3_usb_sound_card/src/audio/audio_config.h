/*
 * audio_config.h - wspólne parametry podsystemu audio (źródło -> FIFO -> drift -> wyjście)
 */
#ifndef AUDIO_CONFIG_H
#define AUDIO_CONFIG_H

/* ---- Format strumienia (stały dla całego toru; źródło musi dostarczać dokładnie taki PCM) ---- */
#define AUDIO_SAMPLE_RATE       44100
#define AUDIO_CHANNELS          2
#define AUDIO_BITS              16
#define AUDIO_BYTES_PER_FRAME   (AUDIO_CHANNELS * (AUDIO_BITS / 8))

/* ---- Blok przetwarzania (tyle ramek żąda wyjście w jednym kroku) ---- */
#define AUDIO_BLOCK_FRAMES      256
#define AUDIO_BLOCK_BYTES       (AUDIO_BLOCK_FRAMES * AUDIO_BYTES_PER_FRAME)

/* ---- FIFO ---- */
#define AUDIO_FIFO_SIZE_BYTES   16384                       /* potęga 2, ok. 93 ms */
#define AUDIO_FIFO_TARGET_BYTES (AUDIO_FIFO_SIZE_BYTES / 2) /* punkt pracy i próg startu */

/* ---- Wybór źródła audio (kompilowany jeden) ---- */
#define AUDIO_SOURCE_USB        1
#define AUDIO_SOURCE_WIFI       2     /* miejsce na przyszłe źródło (audio_src_wifi.cpp) */
#define AUDIO_SOURCE            AUDIO_SOURCE_USB

/* ---- Okres odświeżania głośności ze źródła (ms) ---- */
#ifndef AUDIO_VOL_POLL_MS
#define AUDIO_VOL_POLL_MS       50
#endif

/* ---- Analizator widma ---- */
#define AUDIO_SPECTRUM_FFT_N       4096    /* potęga 2; okno analizy (93 ms przy 44,1 kHz) */
#define AUDIO_SPECTRUM_HOP         1024    /* co tyle nowych próbek liczymy FFT (512 = gładziej, 2x CPU) */
#define AUDIO_SPECTRUM_F_LOW       30      /* Hz, dolna krawędź pierwszego pasma */
#define AUDIO_SPECTRUM_F_HIGH      20000   /* Hz, górna krawędź ostatniego pasma */
#define AUDIO_SPECTRUM_DB_TOP      (-15)   /* dB (energia pasma, 0 dB = sinus pełnej skali) = pełny słupek */
#define AUDIO_SPECTRUM_DB_FLOOR    (-75)   /* dB = pusty słupek */
/* Czas opadania słupka z pełnej skali do zera (ms), od najwolniejszego do najszybszego.
 * Regulowany w czasie pracy (ekran SPECTRUM, długie naciśnięcie B/D); przeliczany na krok na ramkę z hopa i częstotliwości
 * próbkowania, więc zmiana AUDIO_SPECTRUM_HOP nie zmienia wrażenia. */
#define AUDIO_SPECTRUM_FALL_MS_LIST  { 1500, 1000, 700, 500, 350, 250, 150, 100 }
#define AUDIO_SPECTRUM_FALL_DEFAULT  3     /* indeks na liście: 500 ms */
#define AUDIO_SPECTRUM_PEAK_HOLD   20      /* ramki podtrzymania kreski szczytu */
#define AUDIO_SPECTRUM_PEAK_FALL   4       /* opadanie kreski szczytu na ramkę */

#endif /* AUDIO_CONFIG_H */
