/*
 * setup.h - konfiguracja sprzętowa projektu "karta dźwiękowa USB" dla modułu input
 * (ten sam input.cpp/input.h co w innych projektach, różni się tylko ten plik).
 */
#ifndef SETUP_H
#define SETUP_H

#include <Arduino.h>

/* Parametry konstruktora OneButton: (pin, activeLow, pullupActive) */
#ifndef ACTIVE_LOW
#define ACTIVE_LOW        true
#endif
#ifndef ACTIVE_HIGH
#define ACTIVE_HIGH       false
#endif
#ifndef PULLUP_ENABLE
#define PULLUP_ENABLE     true
#endif
#ifndef PULLUP_DISABLE
#define PULLUP_DISABLE    false
#endif

/* ---- Enkoder (piny A i B zamienione względem opisu CLK/DT) ---- */
#define ENCODERA_PIN      5      /* DT  */
#define ENCODERB_PIN      4      /* CLK */
#define ENCODER_PIN       6      /* przycisk enkodera (środek) */
#define ENCODER_TYPE      HALF_STEP
#define ENCODER_ACCEL     ACCEL_OFF

/* ---- Przyciski kierunkowe enkodera (keyA..D) ----
 * TODO: wpisz numery GPIO. -1 = przycisk nieobsługiwany (kod się nie kompiluje).
 * KEY_ACTIVE / KEY_PULLUP: dla przycisków zwierających do GND zostaw
 * ACTIVE_LOW + PULLUP_ENABLE. */
#define KEY_A_PIN       -1
#define KEY_B_PIN       -1
#define KEY_C_PIN       -1
#define KEY_D_PIN       -1
#define KEY_ACTIVE     ACTIVE_LOW
#define KEY_PULLUP     PULLUP_ENABLE

/* ---- Dodatkowe przyciski BACK / ENTER: brak w tym projekcie ---- */
#define BACK_PIN          -1
#define ENTER_PIN         -1

/* ---- Bufor kroków silnika (motor.h): niepotrzebny w tym projekcie ---- */
#define INPUT_USE_MOTOR_BUFFER  0

#endif /* SETUP_H */
