/*
 * menu_oled.h - menu ekranowe (górna warstwa nad ekranem STAT / SPECTRUM)
 *
 * Okienka (przełączane cyklicznie klawiszami Up / Down), wartość zmienia enkoder:
 *   1) liczba słupków widma (zawijanie),
 *   2) kanały widma: MONO / L / R / L+R (zawijanie),
 *   3) jasność wyświetlacza (bez zawijania),
 *   4) rodzaj analizy widmowej (zawijanie),
 *   5) okno FFT 1024 / 2048 / 4096 próbek (bez zawijania; działa tylko dla silnika FFT),
 *   6) szybkość opadania słupków (bez zawijania),
 *   7) szybkość opadania szczytów, 0 = OFF (bez zawijania).
 * Okienko rysuje ui_oled (UI_ShowMenu) i samo gaśnie po UI_MENU_MS bezczynności; każda zmiana odnawia czas.
 * Wołać z tego samego wątku co UI_Loop() i AUDIO_Poll() (loop, callbacki z INPUT_proc).
 */
#ifndef MENU_OLED_H
#define MENU_OLED_H

#ifdef __cplusplus
extern "C" {
#endif

/* Up / Down: gdy okienko jest zamknięte, otwiera ostatnio używane; gdy otwarte, przełącza na poprzednie / następne. */
void MENU_Up(void);
void MENU_Down(void);

/* Obrót enkodera. Gdy okienko jest otwarte: zmienia wartość w okienku i zwraca 1 (zdarzenie zużyte).
 * Gdy zamknięte: nic nie robi i zwraca 0 (obrót ma obsłużyć warstwa dolna, czyli głośność). */
int MENU_Rotate(int steps);

#ifdef __cplusplus
}
#endif

#endif /* MENU_OLED_H */
