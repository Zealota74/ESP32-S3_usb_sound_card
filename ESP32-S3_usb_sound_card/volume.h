/*
 * volume.h - głośność hosta (dB) -> pozycja suwaka Windows (0..100 %) i mnożnik sygnału
 *
 * Karta (USBAudioCard z arduino-esp32) zgłasza hostowi zakres głośności
 * VOL_HOST_MIN_DB .. VOL_HOST_MAX_DB z krokiem 1 dB. Windows przelicza pozycję
 * suwaka 0..100 % na dB własną krzywą ("audio taper") i wysyła wynik w dB.
 *   - Sygnał: mnożnik = 10^(dB/20), czyli dokładnie to, o co prosi Windows;
 *     poziom minimalny (suwak na 0) = cisza.
 *   - Wyświetlacz: dB jest zamieniane z powrotem na % według krzywej
 *     Windows: VOL_CAL_POINTS punktów co VOL_CAL_STEP_PCT % (0, 2, ... 100 %).
 *     Domyślna krzywa jest liniowa w dB; dokładną mierzy kalibracja
 *     (vol_calib.*), która ustawia ją przez VOL_SetCurve().
 *
 * Moduł nie zależy od platformy.
 */
#ifndef VOL_H
#define VOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zakres zgłaszany hostowi przez USBAudioCard (GET_MIN / GET_MAX, krok 1 dB). */
#ifndef VOL_HOST_MIN_DB
#define VOL_HOST_MIN_DB   (-50)
#endif
#ifndef VOL_HOST_MAX_DB
#define VOL_HOST_MAX_DB   (0)
#endif

/* Krok klawisza głośności w Windows (2 %) i liczba punktów krzywej (0..100 %). */
#define VOL_CAL_STEP_PCT  2
#define VOL_CAL_POINTS    (100 / VOL_CAL_STEP_PCT + 1)

/* Krzywa Windows: curve[k] = dB wysyłane przy suwaku k * VOL_CAL_STEP_PCT %.
 * VOL_SetCurve() kopiuje tablicę (NULL = domyślna liniowa); zwraca 0, gdy
 * krzywa jest błędna (wtedy zostaje poprzednia). */
int           VOL_SetCurve(const int8_t *curve);
const int8_t *VOL_GetCurve(void);
int           VOL_CurveIsValid(const int8_t *curve);

/* dB od hosta -> pozycja suwaka 0..100 % (najbliższa możliwa przy kroku 1 dB). */
int   VOL_HostToPercent(float host_db);

/* dB od hosta -> mnożnik amplitudy 0..1; poziom minimalny (suwak 0 %) daje 0. */
float VOL_HostToGain(float host_db);

#ifdef __cplusplus
}
#endif

#endif /* VOL_H */
