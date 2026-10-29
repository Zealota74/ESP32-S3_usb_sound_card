/*
 * audio_clock.h - kontrakt zegara platformy (warstwa sprzętowa, HAL)
 *
 * Rdzeń audio i moduły nad nim nie znają micros() ani żadnego timera. Potrzebują tylko
 * monotonicznego czasu do pomiarów diagnostycznych (np. koszt analizy widma), więc platforma
 * dostarcza jedną funkcję. Implementacje leżą w HARDWARE/: audio_clock_esp32.cpp (Arduino, ESP32-S3).
 * Dla innych modułów dostęp przez AUDIO_TimeUs() z audio_manage.h.
 */
#ifndef AUDIO_CLOCK_H
#define AUDIO_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Monotoniczny czas w mikrosekundach (zawija się po ok. 71 minutach; liczyć różnice uint32_t). */
uint32_t AUDIO_CLOCK_TimeUs(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_CLOCK_H */
