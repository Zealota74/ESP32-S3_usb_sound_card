/*
 * ui_oled.h - interfejs użytkownika na OLED SH1122 256x64 (U8g2)
 *
 * Ekran główny: duży napis "Volume", wartość w dB i pasek postępu.
 * Ekran diagnostyczny (przełączany długim naciśnięciem): liczniki audio.
 * Rysowanie tylko przy zmianie wartości (lub co 200 ms na ekranie diagnostyki).
 */
#ifndef UI_OLED_H
#define UI_OLED_H

#include <stdint.h>
#include "volume.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Dolna granica paska = użyteczny zakres głośności po skalowaniu (volume.h).
 * Pasek jest wtedy liniowo zgodny z pozycją suwaka w Windows. */
#ifndef UI_DB_MIN
#define UI_DB_MIN   (-(float)VOL_EFF_RANGE_DB)
#endif

void UI_Init(void);

/* db: poziom efektywny w dB (<= UI_DB_MIN lub -INFINITY = cisza), mute: wyciszenie. */
void UI_SetVolume(float db, int mute);

void UI_SetDebug(uint32_t audioBlock, uint32_t fifo, uint32_t usbPackets);
void UI_ToggleDebug(void);

/* Surowa głośność z hosta (dB) - tylko do ekranu diagnostycznego. */
void UI_SetHostDb(int host_db);

/* Pokazuje na ekranie, że działa startowe ograniczenie głośności (limit). */
void UI_SetGuard(int active, float limit_db);

/* Wołaj z loop(); sama decyduje, czy trzeba przerysować. */
void UI_Loop(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* UI_OLED_H */
