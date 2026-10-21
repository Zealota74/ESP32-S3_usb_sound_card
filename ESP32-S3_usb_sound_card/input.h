#ifndef INPUT_MODULE_H
#define INPUT_MODULE_H

#include <stdint.h>

/* =================================================================================
 * PARAMETRY KONFIGURACYJNE
 * ================================================================================= */
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

/* ---- Enkoder oraz przycisk centralny ---- */
#define ENCODERA_PIN      5      /* DT  */
#define ENCODERB_PIN      4      /* CLK */
#define ENCODER_PIN       6      /* przycisk enkodera (środek) */
#define ENCODER_TYPE      HALF_STEP
#define ENCODER_ACCEL     ACCEL_OFF

/* ---- Przyciski KEY_A do KEY_D (-1 = nieużywany) ---- */
#define KEY_A_PIN         7
#define KEY_B_PIN         15
#define KEY_C_PIN         16
#define KEY_D_PIN         17
#define KEY_ACTIVE        ACTIVE_LOW
#define KEY_PULLUP        PULLUP_ENABLE

#define BTN_KEY_A_USED    ((KEY_A_PIN) >= 0)
#define BTN_KEY_B_USED    ((KEY_B_PIN) >= 0)
#define BTN_KEY_C_USED    ((KEY_C_PIN) >= 0)
#define BTN_KEY_D_USED    ((KEY_D_PIN) >= 0)

/* ---- Pilot na podczerwień (IRMP, protokół NEC) ----
 * Każdy kod pilota wywołuje to samo callback co odpowiedni przycisk KEY_x.
 * Kody poniżej są przykładowe: podejrzyj prawdziwe w logu (IR_DEBUG_SERIAL)
 * i wpisz własne. IR_ADDRESS: adres NEC pilota albo IR_ADDRESS_ANY (każdy).
 * IR_REPEAT_x = 1: ramki powtórzeń (trzymany klawisz pilota) też wywołują
 * akcję, np. do ciągłej zmiany jasności. */
#define IR_INPUT_PIN      41
#define IR_ADDRESS_ANY    (-1L)
#define IR_ADDRESS        IR_ADDRESS_ANY
#define IR_CMD_A          0x10
#define IR_CMD_B          0x11
#define IR_CMD_C          0x12
#define IR_CMD_D          0x13
#define IR_REPEAT_A       0
#define IR_REPEAT_B       1
#define IR_REPEAT_C       0
#define IR_REPEAT_D       1
#define IR_DEBUG_SERIAL   1      /* 1 = każda odebrana ramka IR na Serial (do poznawania kodów) */

/* =================================================================================
 * TYPY I CALLBACKI
 * ================================================================================= */
typedef void (*InputCallback)();
typedef void (*EncoderCallback)(int delta);

/* Publiczne API modułu */
void INPUT_init();    /* piny, enkoder, IRMP i wspólny timer */
void INPUT_proc();    /* wołać z loop(): tu wywołują się wszystkie callbacki */

void INPUT_attach_action(InputCallback cb);
void INPUT_attach_long_press(InputCallback cb);
void INPUT_attach_double_click(InputCallback cb);
void INPUT_attach_rotate(EncoderCallback cb);

void INPUT_attach_keyA(InputCallback cb);
void INPUT_attach_keyB(InputCallback cb);
void INPUT_attach_keyC(InputCallback cb);
void INPUT_attach_keyD(InputCallback cb);

#endif
