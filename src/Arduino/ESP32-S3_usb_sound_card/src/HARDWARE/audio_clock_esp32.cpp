/*
 * audio_clock_esp32.cpp - zegar platformy (ESP32-S3, Arduino): implementacja AUDIO/audio_clock.h
 */
#include <Arduino.h>

#include "../AUDIO/audio_clock.h"

uint32_t AUDIO_CLOCK_TimeUs( void ) {
	return (uint32_t)micros();
}
