/*
 * audio_out.h - interfejs wyjścia audio (warstwa sprzętowa, HAL)
 *
 * Rdzeń audio nie zna I2S ani żadnego peryferium. Wyjście samo decyduje,
 * jak pompuje dane (zadanie RTOS z blokującym zapisem, przerwanie DMA...),
 * a o kolejne ramki prosi callbackiem fill().
 *
 * Implementacje: audio_out_i2s_esp32.cpp (ESP32-S3, I2S + PCM5102).
 * Port na inny MCU = nowy plik z tymi samymi funkcjami.
 */
#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t sample_rate;
  uint8_t  channels;
  uint8_t  bits;
  uint32_t block_frames;   /* ile ramek żąda fill() za jednym razem */
} AUDIO_OutConfig_t;

/* Wypełnia buf (frames ramek, interleaved, little-endian). Musi zawsze
 * wypełnić cały blok (w razie braku danych ciszą) i nie może blokować. */
typedef void (*AUDIO_OutFillFn)(int16_t *buf, uint32_t frames, void *ctx);

/* Konfiguruje peryferium. Zwraca 1 przy sukcesie. */
int      AUDIO_OUT_Init(const AUDIO_OutConfig_t *cfg);

/* Uruchamia pompowanie danych (wywołuje fill() cyklicznie). Zwraca 1 przy sukcesie. */
int      AUDIO_OUT_Start(AUDIO_OutFillFn fill, void *ctx);

/* Zmienia częstotliwość próbkowania wyjścia w czasie pracy. Zwraca 1 przy sukcesie.
 * Wołać wyłącznie z fill() (wątek wyjścia): wtedy nie ma równoległego zapisu do peryferium.
 * To jedyny wyjątek od zasady "fill() nie blokuje": zmiana zegara trwa krótko i zdarza się
 * tylko przy zmianie formatu. Po niepowodzeniu implementacja wraca do poprzedniej częstotliwości. */
int      AUDIO_OUT_SetSampleRate(uint32_t sample_rate);

/* Liczba błędów zapisu do peryferium od startu. */
uint32_t AUDIO_OUT_GetErrors(void);

/* Monotoniczny czas w mikrosekundach (tylko do pomiarów diagnostycznych, np. czasu FFT). */
uint32_t AUDIO_OUT_TimeUs(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_OUT_H */
