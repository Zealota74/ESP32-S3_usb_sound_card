/*
 * simon_defines.h - tylko definicje pinów i ustawienia sprzętowe.
 * Żadnych obiektów ani zmiennych globalnych (żeby można go było dołączać
 * z kilku plików .cpp bez błędów linkera).
 */
#ifndef SIMON_DEFINES_H
#define SIMON_DEFINES_H

/* OLED SH1122 256x64, HW SPI */
#define SPI_MOSI_OLED   39
#define SPI_MISO_OLED   0
#define SPI_SCK_OLED    38
#define CS_OLED         42
#define DC_OLED         2
#define RESET_OLED      40

/* Opcjonalnie: zegar SPI OLED w Hz (odkomentuj po sprawdzeniu stabilności) */
/* #define OLED_SPI_HZ  10000000UL */

/* Piny enkodera i przycisków: patrz setup.h */

#endif /* SIMON_DEFINES_H */
