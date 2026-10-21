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

#endif /* AUDIO_CONFIG_H */
