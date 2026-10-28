/*
 * ui_oled.h - interfejs użytkownika na OLED SH1122 256x64 (U8g2)
 *
 * Dwie warstwy:
 *  - warstwa dolna (ekran): STAT albo SPECTRUM, przełączane UI_NextScreen()
 *    (długie naciśnięcie enkodera); w przyszłości kolejne tryby,
 *  - warstwa OSD: panel z nazwą, wartością i paskiem (głośność, jasność, parametry widma).
 *    Pojawia się po zmianie wartości i znika po UI_OSD_MS. W tym czasie warstwa dolna jest
 *    przyciemniona: prawdziwą szarością (SH1122 ma 16 poziomów, 4 bity na piksel) albo rastrem.
 * Rysowanie tylko przy zmianie (STAT: co 200 ms, SPECTRUM: przy każdej nowej ramce widma).
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

/* OSD: czas widoczności (ms) i sposób przyciemniania warstwy dolnej:
 *   0 = bez przyciemniania,
 *   1 = raster szachownicy (co drugi piksel, działa zawsze),
 *   2 = prawdziwa szarość: klatka składana z dwóch warstw i wysyłana do SH1122 w 4 bitach/piksel
 *       (poziom UI_OSD_DIM_GRAY, 1..15; 15 = pełna jasność). Gdyby obraz był zniekształcony,
 *       ustaw 1. */
#ifndef UI_OSD_MS
#define UI_OSD_MS       2000
#endif
#ifndef UI_OSD_DIM_MODE
#define UI_OSD_DIM_MODE 2
#endif
#ifndef UI_OSD_DIM_GRAY
#define UI_OSD_DIM_GRAY 4
#endif

void UI_Init(void);

/* Ekrany (warstwa dolna). */
typedef enum {
  UI_SCREEN_STAT = 0,
  UI_SCREEN_SPECTRUM,
  UI_SCREEN_COUNT
} UI_Screen_t;

#define UI_SCREEN_DEFAULT  UI_SCREEN_SPECTRUM
#ifndef UI_MENU_MS
#define UI_MENU_MS      5000
#endif

void UI_ShowMenu(const char *title, const char *value, int pos, int max);
int  UI_MenuOpen(void);   /* 1, gdy na wierzchu jest okienko menu (a nie OSD głośności/jasności) */

void        UI_NextScreen(void);   /* STAT -> SPECTRUM -> STAT ... */
UI_Screen_t UI_GetScreen(void);

/* Głośność: value 0..max (liniowo, jeden tick enkodera = jeden krok), mute: wyciszenie.
 * Wołać cyklicznie; każda zmiana (też z hosta) pokazuje OSD głośności. */
void UI_SetVolume(int value, int max, int mute);

/* Startowe ograniczenie głośności: dopisek "LIMIT" w OSD głośności. */
void UI_SetGuard(int active, float limit_db);

/* Ekran STAT: liczniki audio i czasy; fftUs = czas ostatniej FFT widma w mikrosekundach. */
void UI_SetDebug(uint32_t audioBlock, uint32_t fifo, uint32_t usbPackets, uint32_t fftUs);

/* Surowa głośność z hosta (dB) - tylko do ekranu STAT. */
void UI_SetHostDb(int host_db);

/* Analizator widma: poziomy i szczyty 0..255 dla 'bars' słupków (1..UI_SPEC_MAX_BARS).
 * Wołać po każdej nowej ramce widma. */
#define UI_SPEC_MAX_BARS   64   /* musi być równe DSP_SPECTRUM_MAX_BANDS (sprawdza audio_ui.cpp) */
void UI_SetSpectrum(const uint8_t *level, const uint8_t *peak, int bars);

/* Ogólne OSD: title (maks. 19 znaków), value (maks. 15 znaków) wyświetlana dużą czcionką,
 * pasek postępu pos/max (max = 0: bez paska). */
void UI_ShowOsd(const char *title, const char *value, int pos, int max);

/* Jasność wyświetlacza: poziomy 0..UI_BRIGHT_LEVELS-1 (krzywa kwadratowa, poziom 0 jest
 * przyciemniony, ale ekran nie gaśnie całkiem). Każda zmiana pokazuje OSD jasności.
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
