/*
 * drift.cpp - resampler z regulatorem poziomu FIFO (kod w stylu C).
 * Plik można bez zmian przemianować na drift.c.
 */
#include "drift.h"
#include <string.h>

#define DRIFT_FRAME_BYTES 4

int DRIFT_Init(DRIFT_t *d, FIFO_t *fifo, uint32_t target_bytes)
{
  if (d == NULL || fifo == NULL || target_bytes == 0) return 0;
  if (fifo->frame_bytes != DRIFT_FRAME_BYTES) return 0;

  memset(d, 0, sizeof(*d));
  d->fifo = fifo;
  d->target_bytes = target_bytes;
  d->ratio = 1.0f;
  return 1;
}

void DRIFT_Start(DRIFT_t *d)
{
  d->prev[0] = d->prev[1] = 0;
  d->cur[0] = d->cur[1] = 0;
  d->phase = 0.0f;
  d->inPos = 0;
  d->inCnt = 0;
  d->avg = (float)FIFO_Available(d->fifo);
  d->ratio = 1.0f;
}

static int DRIFT_NextFrame(DRIFT_t *d, int16_t *frame)
{
  if (d->inPos >= d->inCnt) {
    uint32_t bytes = FIFO_Read(d->fifo, (uint8_t *)d->in, sizeof(d->in));
    d->inCnt = bytes / DRIFT_FRAME_BYTES;
    d->inPos = 0;
    if (d->inCnt == 0) return 0;
  }
  frame[0] = d->in[d->inPos * 2];
  frame[1] = d->in[d->inPos * 2 + 1];
  d->inPos++;
  d->consumed_frames = d->consumed_frames + 1;   /* licznik per ramka (bez kwantyzacji co 64) */
  return 1;
}

uint32_t DRIFT_Process(DRIFT_t *d, int16_t *out, uint32_t frames)
{
  float corr, ratio;
  uint32_t i;

  /* regulator: wygładzony poziom FIFO -> korekta tempa */
  d->avg += DRIFT_AVG_ALPHA * ((float)FIFO_Available(d->fifo) - d->avg);
  corr = ((d->avg - (float)d->target_bytes) / (float)d->target_bytes) * DRIFT_GAIN;
  if (corr > DRIFT_MAX_CORR) corr = DRIFT_MAX_CORR;
  if (corr < -DRIFT_MAX_CORR) corr = -DRIFT_MAX_CORR;
  ratio = 1.0f + corr;
  d->ratio = ratio;

  /* ratio > 1: ramki z FIFO zdejmowane szybciej, ratio < 1: wolniej */
  for (i = 0; i < frames; i++) {
    d->phase += ratio;
    while (d->phase >= 1.0f) {
      d->phase -= 1.0f;
      d->prev[0] = d->cur[0];
      d->prev[1] = d->cur[1];
      if (!DRIFT_NextFrame(d, d->cur)) return i;
    }
    out[i * 2]     = (int16_t)(d->prev[0] + (int32_t)((d->cur[0] - d->prev[0]) * d->phase));
    out[i * 2 + 1] = (int16_t)(d->prev[1] + (int32_t)((d->cur[1] - d->prev[1]) * d->phase));
  }
  return frames;
}

void DRIFT_FadeOutTail(int16_t *out, uint32_t valid, uint32_t frames)
{
  int16_t lastL, lastR;
  uint32_t i;

  if (valid >= frames) return;

  lastL = valid ? out[(valid - 1) * 2]     : 0;
  lastR = valid ? out[(valid - 1) * 2 + 1] : 0;

  for (i = valid; i < frames; i++) {
    float gain = 1.0f - (float)(i - valid) / (float)(frames - valid);
    out[i * 2]     = (int16_t)(lastL * gain);
    out[i * 2 + 1] = (int16_t)(lastR * gain);
  }
}

float DRIFT_GetRatio(const DRIFT_t *d)
{
  return d->ratio;
}

uint32_t DRIFT_GetConsumedFrames(const DRIFT_t *d)
{
  return d->consumed_frames;
}
