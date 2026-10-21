/*
 * vol_calib.h - pomiar krzywej głośności Windows (pozycja suwaka -> dB)
 *
 * Karta sama "naciska" klawisze głośności (HID): najpierw schodzi do 0 %,
 * potem co jeden krok (2 %) w górę do 100 % i zapisuje dB, które ustawił
 * Windows. Wynik trafia do volume.* (VOL_SetCurve) i do pamięci NVS, więc
 * wystarczy zrobić to raz na danym komputerze. Na koniec głośność wraca do
 * poprzedniej pozycji (i ewentualnie do stanu MUTE).
 * W trakcie pomiaru wyjście audio jest wyciszone, a enkoder nieaktywny.
 *
 * Komendy z monitora szeregowego (patrz .ino): 'k' = kalibruj,
 * 'p' = wypisz krzywą, 'r' = przywróć domyślną krzywą.
 */
#ifndef VOL_CALIB_H
#define VOL_CALIB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wczytuje zapisaną krzywą z NVS (jeśli jest) - wołaj raz w setup(). */
void VCAL_Init(void);

/* Rozpoczyna pomiar (ignorowane, gdy już trwa). */
void VCAL_Start(void);

/* Maszyna stanów pomiaru - wołaj z loop() w każdym przebiegu. */
void VCAL_Proc(uint32_t now_ms);

/* 1 = pomiar w toku (łącznie z powrotem do poprzedniej głośności). */
int  VCAL_IsActive(void);

/* Postęp pomiaru 0..100 %, -1 = nieaktywny. */
int  VCAL_Progress(void);

/* Wypisuje bieżącą krzywą na Serial. */
void VCAL_Print(void);

/* Przywraca domyślną krzywą i kasuje zapis w NVS. */
void VCAL_Reset(void);

#ifdef __cplusplus
}
#endif

#endif /* VOL_CALIB_H */
