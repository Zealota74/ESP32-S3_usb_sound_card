/*
 * audio_ui.h - most audio -> UI: co cykl loop() przekazuje do ui_oled.* głośność, limit startowy,
 * widmo i liczniki diagnostyczne. Odwrotny kierunek (wejścia -> audio) obsługuje controls.*.
 *
 * Kolejność w loop(): AUDIO_Poll( now ) (źródło, FIFO, widmo), potem AUDIOUI_Sync( now ), potem UI_Loop( now ).
 */
#ifndef AUDIO_UI_H
#define AUDIO_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Po AUDIO_Init(). startup_vol_db: limit startowy w dB (do dopisku LIMIT w OSD głośności). */
void AUDIOUI_Init(int startup_vol_db);

void AUDIOUI_Sync(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_UI_H */
