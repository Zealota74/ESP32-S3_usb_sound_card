/*
 * audio_types.h - typy wspólne dla audio.h, audio_source.h i źródeł audio.
 */
#ifndef AUDIO_TYPES_H
#define AUDIO_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Funkcja, którą źródło wpisuje dane PCM do toru audio (AUDIO_Push).
 * Zwraca liczbę faktycznie przyjętych bajtów (może być mniej niż bytes). */
typedef uint32_t (*AUDIO_PushFn)(const uint8_t *data, uint32_t bytes);

/* Klawisze multimedialne wysyłane do strony sterującej (np. hosta USB). */
typedef enum {
  AUDIO_MEDIA_NEXT = 1,   /* następny utwór */
  AUDIO_MEDIA_PREV        /* poprzedni utwór */
} AUDIO_MediaKey_t;

/* Stan głośności widziany z zewnątrz (UI, diagnostyka). */
typedef struct {
  float    host_db;       /* głośność ustawiona przez stronę sterującą (dB) */
  float    gain;          /* mnożnik amplitudy 0..1 stosowany w torze audio */
  int      level;         /* poziom do wyświetlenia 0..level_max (w ruchu: docelowy) */
  int      level_max;     /* maksymalny poziom */
  int      host_level;    /* poziom odpowiadający host_db */
  int      host_percent;  /* procent suwaka odpowiadający host_level */
  int      mute;          /* 1 = wyciszone (tor audio zeruje wtedy sygnał) */
  int      busy;          /* 1 = trwa dochodzenie do poziomu docelowego */
  uint32_t ctrl_count;    /* ile razy strona sterująca zmieniła głośność/mute od startu */
} AUDIO_Volume_t;

/* Statystyki źródła. */
typedef struct {
  uint32_t packets;       /* odebrane pakiety */
  uint32_t bytes;         /* bajty faktycznie przyjęte przez FIFO */
  uint32_t short_writes;  /* pakiety przyjęte niepełno (FIFO pełne) */
  uint32_t age_ms;        /* czas od ostatniego pakietu (duży = brak strumienia) */
} AUDIO_SrcStats_t;

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_TYPES_H */
