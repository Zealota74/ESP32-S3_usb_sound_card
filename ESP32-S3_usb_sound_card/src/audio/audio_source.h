/*
 * audio_source.h - interfejs źródła audio (USB, Wi-Fi, ...)
 *
 * Źródło dostarcza PCM w formacie z audio_config.h wywołując funkcję push
 * przekazaną w init(). Dokładnie jedno źródło jest producentem FIFO (SPSC).
 * Źródło nie zna FIFO, drift ani wyjścia. Wszystkie wskaźniki poza
 * name i init są opcjonalne (NULL = źródło tego nie obsługuje).
 */
#ifndef AUDIO_SOURCE_H
#define AUDIO_SOURCE_H

#include <stdint.h>
#include "audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *name;

  /* Uruchamia źródło. Zwraca 1, gdy startowa głośność jest zagwarantowana
   * (ustawiona w źródle albo źródło nie ma własnej głośności), 0 gdy rdzeń
   * ma sam ograniczyć głośność (limit startowy). */
  int  (*init)(AUDIO_PushFn push, int8_t initial_vol_db);

  /* Wołane z AUDIO_Poll() (pętla główna), bez blokowania. */
  void (*poll)(void);

  /* Głośność/mute ze strony sterującej (np. host USB). */
  void (*get_volume)(AUDIO_Volume_t *v);
  void (*volume_step)(int32_t ticks);
  void (*mute_toggle)(void);

  /* Klawisz multimedialny (następny/poprzedni utwór) do strony sterującej. */
  void (*media_key)(AUDIO_MediaKey_t key);

  void (*get_stats)(AUDIO_SrcStats_t *s);
} AUDIO_Source_t;

/* Dostępne źródła (definiowane w ich plikach, wybór w audio_config.h). */
extern const AUDIO_Source_t AUDIO_SRC_USB;    /* usb_soundcard.cpp */
/* extern const AUDIO_Source_t AUDIO_SRC_WIFI;   audio_src_wifi.cpp (przyszłość) */

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SOURCE_H */
