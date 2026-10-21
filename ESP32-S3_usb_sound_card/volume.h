/*
 * volume.h - przeliczanie głośności hosta (dB) na głośność efektywną (dB, wzmocnienie)
 *
 * Windows wysyła poziom w dB na szerokim zakresie (VOL_HOST_MIN_DB .. 0 dB).
 * Dolna połowa takiego zakresu jest praktycznie niesłyszalna, więc suwak
 * "do połowy" prawie nic nie robi. Ten moduł skaluje dB liniowo do węższego,
 * użytecznego zakresu (VOL_EFF_RANGE_DB .. 0 dB), a sam dół (poziom minimalny
 * hosta) zostaje ciszą. Pozycja suwaka dalej jest liniowa, tylko w użytecznych dB.
 *
 * Moduł nie zależy od platformy.
 */
#ifndef VOL_H
#define VOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Minimalny poziom zgłaszany przez hosta (0 % suwaka) = cisza. */
#ifndef VOL_HOST_MIN_DB
#define VOL_HOST_MIN_DB   (-90)
#endif

/* Użyteczny zakres po skalowaniu: 1 % suwaka ~ -VOL_EFF_RANGE_DB, 100 % = 0 dB. */
#ifndef VOL_EFF_RANGE_DB
#define VOL_EFF_RANGE_DB  (45)
#endif

/* host_db <= VOL_HOST_MIN_DB daje -INFINITY (cisza). */
float VOL_HostToEffDb(float host_db);

/* dB -> mnożnik amplitudy; -INFINITY daje 0. */
float VOL_DbToGain(float db);

#ifdef __cplusplus
}
#endif

#endif /* VOL_H */
