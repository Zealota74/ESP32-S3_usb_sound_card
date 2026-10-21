#include <OneButton.h>
#include "input.h"
#include "SimonEncoder.h"

/* ---- Konfiguracja domyślna (zachowuje dotychczasowe zachowanie w innych projektach).
 * Projekt może ją nadpisać w setup.h. ---- */
#ifndef INPUT_USE_MOTOR_BUFFER
#define INPUT_USE_MOTOR_BUFFER 1        /* 1 = bufor kroków i MOTOR_move_steps() */
#endif
#ifndef ENCODER_TYPE
#define ENCODER_TYPE FULL_STEP
#endif
#ifndef ENCODER_ACCEL
#define ENCODER_ACCEL ACCEL_MEDIUM
#endif
#ifndef KEY_ACTIVE
#define KEY_ACTIVE ACTIVE_HIGH
#endif
#ifndef KEY_PULLUP
#define KEY_PULLUP PULLUP_DISABLE
#endif

#if INPUT_USE_MOTOR_BUFFER
#include "motor.h"
#endif

/* Zgodność ze starymi nazwami pinów (RX480_x_PIN) z setup.h innych projektów;
 * brak definicji pinu = przycisk wyłączony. */
#ifndef KEY_A_PIN
  #ifdef RX480_A_PIN
    #define KEY_A_PIN RX480_A_PIN
  #else
    #define KEY_A_PIN (-1)
  #endif
#endif
#ifndef KEY_B_PIN
  #ifdef RX480_B_PIN
    #define KEY_B_PIN RX480_B_PIN
  #else
    #define KEY_B_PIN (-1)
  #endif
#endif
#ifndef KEY_C_PIN
  #ifdef RX480_C_PIN
    #define KEY_C_PIN RX480_C_PIN
  #else
    #define KEY_C_PIN (-1)
  #endif
#endif
#ifndef KEY_D_PIN
  #ifdef RX480_D_PIN
    #define KEY_D_PIN RX480_D_PIN
  #else
    #define KEY_D_PIN (-1)
  #endif
#endif
#ifndef BACK_PIN
#define BACK_PIN (-1)
#endif
#ifndef ENTER_PIN
#define ENTER_PIN (-1)
#endif

/* Przycisk jest obsługiwany tylko wtedy, gdy jego pin w setup.h jest >= 0 */
#define BTN_BACK_USED  ((BACK_PIN)  >= 0)
#define BTN_ENTER_USED ((ENTER_PIN) >= 0)
#define BTN_KEY_A_USED ((KEY_A_PIN) >= 0)
#define BTN_KEY_B_USED ((KEY_B_PIN) >= 0)
#define BTN_KEY_C_USED ((KEY_C_PIN) >= 0)
#define BTN_KEY_D_USED ((KEY_D_PIN) >= 0)


// JAWNA INICJALIZACJA NA NULL - Krytyczne w C
static EncoderCallback _cbRotate = NULL;

static InputCallback _cbAction = NULL;
static InputCallback _cbLong   = NULL;
static InputCallback _cbDouble = NULL;
static InputCallback _cbEnter  = NULL;
static InputCallback _cbBack   = NULL;

static InputCallback _cbKeyA = NULL;
static InputCallback _cbKeyB = NULL;
static InputCallback _cbKeyC = NULL;
static InputCallback _cbKeyD = NULL;

static int _stepsBuffer = 0;
static unsigned long _lastMoveTime = 0;
static int _timeout = 500;
static bool _bufferEnabled = true;

// Obiekty bibliotek (statyczne)
static SimonEncoder encoder(ENCODERA_PIN, ENCODERB_PIN, ENCODER_TYPE, ENCODER_ACCEL);
static OneButton btnEnc  (ENCODER_PIN, ACTIVE_LOW, PULLUP_ENABLE);
#if BTN_BACK_USED
static OneButton btnBack (BACK_PIN, ACTIVE_LOW, PULLUP_ENABLE);
#endif
#if BTN_ENTER_USED
static OneButton btnEnter(ENTER_PIN, ACTIVE_LOW, PULLUP_ENABLE);
#endif

#if BTN_KEY_A_USED
static OneButton keyA(KEY_A_PIN, KEY_ACTIVE, KEY_PULLUP);
#endif
#if BTN_KEY_B_USED
static OneButton keyB(KEY_B_PIN, KEY_ACTIVE, KEY_PULLUP);
#endif
#if BTN_KEY_C_USED
static OneButton keyC(KEY_C_PIN, KEY_ACTIVE, KEY_PULLUP);
#endif
#if BTN_KEY_D_USED
static OneButton keyD(KEY_D_PIN, KEY_ACTIVE, KEY_PULLUP);
#endif


// --- FUNKCJE POMOCNICZE (CALLBACKI WEWNĘTRZNE) ---

static void _onRotate(int delta) {
    // 1. Zabezpieczone wywołanie callbacku do UI
    if (_cbRotate != NULL) {
        _cbRotate(delta);
    }
}

// Wrappery dla przycisków z kontrolą NULL
static void _clickEnc()   { if (_cbAction != NULL) _cbAction(); }
static void _longEnc()    { if (_cbLong != NULL)   _cbLong();   }
static void _doubleEnc()  { if (_cbDouble != NULL) _cbDouble(); }
#if BTN_BACK_USED
static void _clickBack()  { if (_cbBack != NULL)   _cbBack();   }
#endif
#if BTN_ENTER_USED
static void _clickEnter() { if (_cbEnter != NULL)  _cbEnter();  }
#endif

#if BTN_KEY_A_USED
static void _clickKeyA() { if (_cbKeyA != NULL)  _cbKeyA();  }
#endif
#if BTN_KEY_B_USED
static void _clickKeyB() { if (_cbKeyB != NULL)  _cbKeyB();  }
#endif
#if BTN_KEY_C_USED
static void _clickKeyC() { if (_cbKeyC != NULL)  _cbKeyC();  }
#endif
#if BTN_KEY_D_USED
static void _clickKeyD() { if (_cbKeyD != NULL)  _cbKeyD();  }
#endif


// Funkcje pośredniczące dla enkodera (zgodne z Twoim wymogiem czystego C)
static void _cbRight(int d) { _onRotate(d); }
static void _cbLeft(int d)  { _onRotate(-d); }

// --- FUNKCJE PUBLICZNE ---

void INPUT_init() {
    btnEnc.attachClick(_clickEnc);
    btnEnc.attachLongPressStart(_longEnc);
#if BTN_BACK_USED
    btnBack.attachClick(_clickBack);
#endif
#if BTN_ENTER_USED
    btnEnter.attachClick(_clickEnter);   // było: _clickKeyD (wygląda na pomyłkę kopiuj-wklej)
#endif

#if BTN_KEY_A_USED
    keyA.attachClick(_clickKeyA);
#endif
#if BTN_KEY_B_USED
    keyB.attachClick(_clickKeyB);
#endif
#if BTN_KEY_C_USED
    keyC.attachClick(_clickKeyC);
#endif
#if BTN_KEY_D_USED
    keyD.attachClick(_clickKeyD);
#endif
    encoder.attachOnRight(_cbRight);
    encoder.attachOnLeft(_cbLeft);

    encoder.begin();
}

void INPUT_set_buffer_enabled(bool enabled) {
    _bufferEnabled = enabled;
    if (!enabled) _stepsBuffer = 0;
}

void INPUT_update_settings(int timeout) {
    _timeout = timeout;
}

// Rejestracja callbacków
void INPUT_attach_action(InputCallback cb)     { _cbAction = cb; }
void INPUT_attach_long_press(InputCallback cb) { _cbLong = cb; }
void INPUT_attach_double_click(InputCallback cb) {
    _cbDouble = cb;
    if (cb != NULL) btnEnc.attachDoubleClick(_doubleEnc);   // dopiero tu, bo dwuklik opóźnia pojedynczy klik
}
void INPUT_attach_enter(InputCallback cb)      { _cbEnter = cb; }
void INPUT_attach_back(InputCallback cb)       { _cbBack = cb; }
void INPUT_attach_rotate(EncoderCallback cb)   { _cbRotate = cb; }

void INPUT_attach_keyA(InputCallback cb)    { _cbKeyA = cb; }
void INPUT_attach_keyB(InputCallback cb)    { _cbKeyB = cb; }
void INPUT_attach_keyC(InputCallback cb)    { _cbKeyC = cb; }
void INPUT_attach_keyD(InputCallback cb)    { _cbKeyD = cb; }


void INPUT_proc() {
    encoder.eventAnalyze();
    btnEnc.tick();
#if BTN_BACK_USED
    btnBack.tick();
#endif
#if BTN_ENTER_USED
    btnEnter.tick();
#endif

#if BTN_KEY_A_USED
    keyA.tick();
#endif
#if BTN_KEY_B_USED
    keyB.tick();
#endif
#if BTN_KEY_C_USED
    keyC.tick();
#endif
#if BTN_KEY_D_USED
    keyD.tick();
#endif
}
