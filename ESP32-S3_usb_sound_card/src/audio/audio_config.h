/*
 * audio_config.h - wspólne parametry podsystemu audio (źródło -> FIFO -> drift -> wyjście)
 */
#ifndef AUDIO_CONFIG_H
#define AUDIO_CONFIG_H

/* ---- Format strumienia (stały dla całego toru; źródło musi dostarczać dokładnie taki PCM) ---- */
/* Częstotliwość próbkowania nie jest już stała: zmienia ją źródło w czasie pracy (host USB wybiera
 * format w Windows: Właściwości -> Zaawansowane). Tu: wartość startowa, lista obsługiwanych i maksimum.
 * Format: zawsze 16 bit stereo. Dla USB (UAC1, rdzeń arduino-esp32 3.3.x) sufit to 48 kHz:
 * USBAudioCard odrzuca więcej, a bufory TinyUSB są zbudowane w prekompilowanych bibliotekach rdzenia.
 * Wyższe częstotliwości (88,2 / 96 kHz) wymagają przebudowy rdzenia albo źródła innego niż USB UAC1. */
#define AUDIO_SAMPLE_RATE_DEFAULT  44100
#define AUDIO_SAMPLE_RATE_LIST     { 44100, 48000 }
#define AUDIO_SAMPLE_RATE_MAX      48000
#define AUDIO_SAMPLE_RATE       AUDIO_SAMPLE_RATE_DEFAULT   /* zgodność wsteczna: częstotliwość startowa */
#define AUDIO_CHANNELS          2
#define AUDIO_BITS              16
#define AUDIO_BYTES_PER_FRAME   (AUDIO_CHANNELS * (AUDIO_BITS / 8))

/* ---- Blok przetwarzania (tyle ramek żąda wyjście w jednym kroku) ---- */
#define AUDIO_BLOCK_FRAMES      256
#define AUDIO_BLOCK_BYTES       (AUDIO_BLOCK_FRAMES * AUDIO_BYTES_PER_FRAME)

/* ---- FIFO ---- */
#define AUDIO_FIFO_SIZE_BYTES   32768                       /* potęga 2; ok. 186 ms @44,1k, 85 ms @96k */
#define AUDIO_FIFO_TARGET_MS    46                          /* punkt pracy i próg startu w ms (stały czas, nie bajty) */
/* Cel dla częstotliwości startowej; dla bieżącej liczy go rdzeń (audio.cpp) z AUDIO_FIFO_TARGET_MS. */
#define AUDIO_FIFO_TARGET_BYTES ((AUDIO_SAMPLE_RATE_DEFAULT * AUDIO_BYTES_PER_FRAME / 1000) * AUDIO_FIFO_TARGET_MS)

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

// Simon test
// #define AUDIO_SPECTRUM_FFT_Q           -1.0f   /* 0 = jak dotąd, > 0 = Q, < 0 = auto */
/* Silnik analizy pasm (przełączalny w czasie pracy: SPEC_SetEngine):
 *   0 = FFT (jedna FFT, pasma = sumy binów; słaba rozdzielczość w basie),
 *   1 = bank filtrów IIR (pasmowe biquady o stałym Q, rozdzielczość bez zmian w całym paśmie),
 *   2 = bank IIR z decymacją oktawową (to samo co 1, ale tańszy przy dużej liczbie pasm). */
#define AUDIO_SPECTRUM_ENGINE_DEFAULT  1
/* Bank IIR: liczba kaskadowanych sekcji biquad na pasmo, 1..4 (1 = 2. rząd, -12 dB/okt; 2 = 4. rząd, -24 dB/okt).
 * Krawędzie pasma (-3 dB) zostają w tym samym miejscu, zmienia się tylko stromość zboczy i koszt CPU. */
#define AUDIO_SPECTRUM_IIR_SECTIONS    2
/* Stała czasowa detektora energii: tau = max( TAU_MIN_MS, TAU_CYCLES / f0 ). Dla basu dłuższa (tłumi tętnienia 2*f0). */
#define AUDIO_SPECTRUM_IIR_TAU_CYCLES  1.0f
#define AUDIO_SPECTRUM_IIR_TAU_MIN_MS  10
/* Korekta poziomu silnika IIR względem FFT (dB, + = wyżej). Bazowo szum biały daje ten sam poziom co FFT. */
#define AUDIO_SPECTRUM_IIR_TRIM_DB     0.0f

#endif /* AUDIO_CONFIG_H */
