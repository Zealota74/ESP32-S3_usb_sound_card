/*
 * ui_oled.h - interfejs użytkownika na OLED SH1122 256x64 (U8g2)
 *
 * Ekran główny: duży napis "Volume", poziom głośności (0..max / MUTE)
 * i pasek postępu.
 * Ekran diagnostyczny (przełączany długim naciśnięciem): liczniki audio.
 * Rysowanie tylko przy zmianie wartości (lub co 200 ms na ekranie diagnostyki).
 */
#ifndef UI_OLED_H
#define UI_OLED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OLED SH1122 256x64, HW SPI */
#define SPI_MOSI_OLED   39
#define SPI_MISO_OLED   0
#define SPI_SCK_OLED    38
#define CS_OLED         42
#define DC_OLED         2
#define RESET_OLED      40
#define OLED_SPI_HZ     8000000UL

void UI_Init(void);

/* value: poziom głośności 0..max (liniowo, jeden tick enkodera = jeden krok),
 * max: wartość maksymalna (np. 31), mute: wyciszenie. Napis pokazuje value,
 * pasek ma długość value/max. */
void UI_SetVolume(int value, int max, int mute);

/* Postęp kalibracji krzywej głośności 0..100, -1 = brak kalibracji. */
void UI_SetCalib(int progress);

void UI_SetDebug(uint32_t audioBlock, uint32_t fifo, uint32_t usbPackets);
void UI_ToggleDebug(void);

/* Surowa głośność z hosta (dB) - tylko do ekranu diagnostycznego. */
void UI_SetHostDb(int host_db);

/* Pokazuje na ekranie, że działa startowe ograniczenie głośności (limit). */
void UI_SetGuard(int active, float limit_db);

/* Jasność wyświetlacza: poziomy 0..UI_BRIGHT_LEVELS-1 (krzywa kwadratowa,
 * poziom 0 jest przyciemniony, ale ekran nie gaśnie całkiem).
 * Wołać z tego samego wątku co UI_Loop() (loop). */
#ifndef UI_BRIGHT_LEVELS
#define UI_BRIGHT_LEVELS   16
#endif
#ifndef UI_BRIGHT_DEFAULT
#define UI_BRIGHT_DEFAULT  8
#endif
void UI_SetBrightness(int level);
void UI_BrightnessStep(int steps);   /* + jaśniej, - ciemniej, z ograniczeniem do zakresu */
int  UI_GetBrightness(void);

/* Wołaj z loop(); sama decyduje, czy trzeba przerysować. */
void UI_Loop(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* UI_OLED_H */
