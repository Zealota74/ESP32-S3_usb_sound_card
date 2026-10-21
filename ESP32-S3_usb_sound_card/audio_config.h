/*
 * audio_config.h - wspólne parametry strumienia audio (USB <-> FIFO <-> I2S)
 */
#ifndef AUDIO_CONFIG_H
#define AUDIO_CONFIG_H

#define AUDIO_SAMPLE_RATE       44100
#define AUDIO_CHANNELS          2
#define AUDIO_BITS              16
#define AUDIO_BYTES_PER_FRAME   (AUDIO_CHANNELS * (AUDIO_BITS / 8))

#endif /* AUDIO_CONFIG_H */
