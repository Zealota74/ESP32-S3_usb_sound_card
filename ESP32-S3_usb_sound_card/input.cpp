#include <OneButton.h>
#include "input.h"
#include "SimonEncoder.h"


// ================== JAWNA INICJALIZACJA NA NULL - Krytyczne w C ====================
static EncoderCallback _cbRotate = NULL;
static InputCallback _cbAction = NULL;
static InputCallback _cbLong   = NULL;
static InputCallback _cbDouble = NULL;

static InputCallback _cbKeyA = NULL;
static InputCallback _cbKeyB = NULL;
static InputCallback _cbKeyC = NULL;
static InputCallback _cbKeyD = NULL;

static int _stepsBuffer = 0;
static unsigned long _lastMoveTime = 0;
static int _timeout = 500;
static bool _bufferEnabled = true;

// =========================== Obiekty bibliotek (statyczne) ==========================
static SimonEncoder encoder(ENCODERA_PIN, ENCODERB_PIN, ENCODER_TYPE, ENCODER_ACCEL);
static OneButton btnEnc  (ENCODER_PIN, ACTIVE_LOW, PULLUP_ENABLE);

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
// ====================================================================================

// ===================== FUNKCJE POMOCNICZE (CALLBACKI WEWNĘTRZNE) ====================
static void _onRotate(int delta) {
    // 1. Zabezpieczone wywołanie callbacku do UI
    if (_cbRotate != NULL) {
        _cbRotate(delta);
    }
}
// Funkcje pośredniczące dla enkodera (zgodne z Twoim wymogiem czystego C)
static void _cbRight(int d) { _onRotate(d); }
static void _cbLeft(int d)  { _onRotate(-d); }
// -----------------------------------------------------------------------

// Wrappery dla przycisków z kontrolą NULL
static void _clickEnc()   { if (_cbAction != NULL) _cbAction(); }
static void _longEnc()    { if (_cbLong != NULL)   _cbLong();   }
static void _doubleEnc()  { if (_cbDouble != NULL) _cbDouble(); }

// -----------------------------------------------------------------------
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
// ====================================================================================

// ================================ FUNKCJE PUBLICZNE =================================
void INPUT_init() {
    btnEnc.attachClick(_clickEnc);
    btnEnc.attachLongPressStart(_longEnc);

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
// ------------------------------ Rejestracja callbacków ----------------------------
void INPUT_attach_rotate(EncoderCallback cb)   { _cbRotate = cb; }
void INPUT_attach_action(InputCallback cb)     { _cbAction = cb; }
void INPUT_attach_long_press(InputCallback cb) { _cbLong = cb; }
void INPUT_attach_double_click(InputCallback cb) {
    _cbDouble = cb;
    if (cb != NULL) btnEnc.attachDoubleClick(_doubleEnc);   // dopiero tu, bo dwuklik opóźnia pojedynczy klik
}
void INPUT_attach_keyA(InputCallback cb)    { _cbKeyA = cb; }
void INPUT_attach_keyB(InputCallback cb)    { _cbKeyB = cb; }
void INPUT_attach_keyC(InputCallback cb)    { _cbKeyC = cb; }
void INPUT_attach_keyD(InputCallback cb)    { _cbKeyD = cb; }
// -----------------------------------------------------------------------------------
void INPUT_proc() {
    encoder.eventAnalyze();
    btnEnc.tick();

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
// ====================================================================================
