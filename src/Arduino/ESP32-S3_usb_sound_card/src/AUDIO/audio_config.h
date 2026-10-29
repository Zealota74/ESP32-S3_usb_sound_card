/*
 * audio_config.h - parametry toru audio (źródło -> FIFO -> drift -> wyjście).
 * Parametry analizatora widma: DSP/dsp_config.h.
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
/* Cel dla częstotliwości startowej; dla bieżącej liczy go rdzeń (audio_manage.cpp) z AUDIO_FIFO_TARGET_MS. */
#define AUDIO_FIFO_TARGET_BYTES ((AUDIO_SAMPLE_RATE_DEFAULT * AUDIO_BYTES_PER_FRAME / 1000) * AUDIO_FIFO_TARGET_MS)

/* ---- Wybór źródła audio (kompilowany jeden) ---- */
#define AUDIO_SOURCE_USB        1
#define AUDIO_SOURCE_WIFI       2     /* miejsce na przyszłe źródło (audio_src_wifi.cpp) */
#define AUDIO_SOURCE            AUDIO_SOURCE_USB

/* ---- Okres odświeżania głośności ze źródła (ms) ---- */
#ifndef AUDIO_VOL_POLL_MS
#define AUDIO_VOL_POLL_MS       50
#endif

#endif /* AUDIO_CONFIG_H */
