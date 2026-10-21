#ifndef INPUT_MODULE_H
#define INPUT_MODULE_H


// =================================================================================
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

/* Przycisk jest obsługiwany tylko wtedy, gdy jego pin w setup.h jest >= 0 */
#define BTN_KEY_A_USED ((KEY_A_PIN) >= 0)
#define BTN_KEY_B_USED ((KEY_B_PIN) >= 0)
#define BTN_KEY_C_USED ((KEY_C_PIN) >= 0)
#define BTN_KEY_D_USED ((KEY_D_PIN) >= 0)
// ==========================================================================

// Funkcje do rejestracji akcji (callbacki)
typedef void (*InputCallback)();
typedef void (*EncoderCallback)(int delta); // Callback przyjmujący inta

// Inicjalizacja modułu (piny i biblioteki)
void INPUT_init();

// Główna pętla modułu (obsługa timerów, przycisków i bufora)
void INPUT_proc();

void INPUT_attach_action(InputCallback cb);
void INPUT_attach_long_press(InputCallback cb);
void INPUT_attach_double_click(InputCallback cb);   // opcjonalny; dwuklik opóźnia pojedynczy klik, więc podpinaj tylko gdy potrzebny
void INPUT_attach_enter(InputCallback cb);
void INPUT_attach_back(InputCallback cb);
void INPUT_attach_rotate(EncoderCallback cb);

void INPUT_attach_keyA(InputCallback cb);
void INPUT_attach_keyB(InputCallback cb);
void INPUT_attach_keyC(InputCallback cb);
void INPUT_attach_keyD(InputCallback cb);

#endif
