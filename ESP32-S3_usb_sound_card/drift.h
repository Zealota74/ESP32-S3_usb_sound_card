/*
 * drift.h - kompensacja dryfu zegarów między źródłem (USB) a wyjściem (I2S)
 *
 * Moduł zdejmuje ramki z FIFO i generuje blok wyjściowy z lekko zmienionym
 * tempem (resampler z interpolacją liniową). Regulator utrzymuje poziom
 * FIFO w okolicy punktu docelowego: gdy FIFO się przepełnia, ramki są
 * zdejmowane odrobinę szybciej, gdy się opróżnia - odrobinę wolniej.
 * Korekta jest ograniczona do +-DRIFT_MAX_CORR (domyślnie +-0,2 %),
 * co jest niesłyszalne.
 *
 * Zależności: tylko fifo.h. Moduł jest niezależny od USB, I2S i platformy.
 * Format danych: 16 bit, 2 kanały (ramka = 4 bajty), little-endian,
 * a FIFO musi mieć frame_bytes == 4.
 *
 * Wątki: DRIFT_Process() wywołuje wyłącznie konsument FIFO (task audio).
 * DRIFT_GetRatio() i DRIFT_GetConsumedFrames() można czytać z innego wątku.
 */
#ifndef DRIFT_H
#define DRIFT_H

#include <stdint.h>
#include "fifo.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DRIFT_AVG_ALPHA
#define DRIFT_AVG_ALPHA   0.02f    /* wygładzanie poziomu FIFO (na blok) */
#endif
#ifndef DRIFT_GAIN
#define DRIFT_GAIN        0.002f   /* wzmocnienie regulatora */
#endif
#ifndef DRIFT_MAX_CORR
#define DRIFT_MAX_CORR    0.002f   /* maksymalna korekta tempa */
#endif
#ifndef DRIFT_IN_FRAMES
#define DRIFT_IN_FRAMES   64       /* lokalny bufor wejściowy (ramki) */
#endif

typedef struct {
  FIFO_t  *fifo;
  uint32_t target_bytes;     /* docelowy poziom FIFO */
  float    avg;              /* wygładzony poziom FIFO */
  float    phase;
  int16_t  prev[2];
  int16_t  cur[2];
  int16_t  in[DRIFT_IN_FRAMES * 2];
  uint32_t inPos;
  uint32_t inCnt;
  volatile float    ratio;            /* ostatnie tempo (1.0 = bez korekty) */
  volatile uint32_t consumed_frames;  /* ramki zdjęte z FIFO od Init */
} DRIFT_t;

/* Zwraca 1 przy sukcesie, 0 gdy fifo->frame_bytes != 4 lub błędne parametry. */
int      DRIFT_Init(DRIFT_t *d, FIFO_t *fifo, uint32_t target_bytes);

/* Wywołaj przed rozpoczęciem grania (po prefillu i po każdym underrunie). */
void     DRIFT_Start(DRIFT_t *d);

/*
 * Wypełnia out[] ramkami (frames * 2 próbki int16).
 * Zwraca liczbę wygenerowanych ramek. Mniej niż 'frames' = underrun
 * (FIFO się wyczerpało); wtedy wywołaj DRIFT_FadeOutTail().
 */
uint32_t DRIFT_Process(DRIFT_t *d, int16_t *out, uint32_t frames);

/* Wygasza ogon bloku od ramki 'valid' do 'frames' (unika kliknięcia). */
void     DRIFT_FadeOutTail(int16_t *out, uint32_t valid, uint32_t frames);

float    DRIFT_GetRatio(const DRIFT_t *d);
uint32_t DRIFT_GetConsumedFrames(const DRIFT_t *d);

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_H */
