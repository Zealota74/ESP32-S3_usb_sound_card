/*
 * input.cpp - wejścia użytkownika: enkoder, przycisk enkodera, klawisze A-D, pilot IR (IRMP/NEC).
 *
 * Jeden wspólny timer sprzętowy (ISR, tempo próbkowania IRMP = F_INTERRUPTS):
 *   - każdy tick: irmp_ISR() (próbkowanie odbiornika IR),
 *   - co INPUT_ENC_DIV ticków: SimonEncoder::tick() (ok. 2 kHz),
 *   - co INPUT_KEY_DIV ticków: odczyt poziomów przycisków (ok. 1 kHz).
 * ISR tylko próbkuje. Całą logikę (OneButton, callbacki, dekodowanie IR) wykonuje
 * INPUT_proc() z loop(), więc callbacki mogą bezpiecznie wołać USB, OLED i audio.
 */
#include <Arduino.h>
#include <OneButton.h>

#include "input.h"
#include "SimonEncoder.h"

/* ---- IRMP: tylko NEC, bez diody zwrotnej (LED_BUILTIN koliduje z pinami OLED) ----
 * Makra muszą być przed #include <irmp.hpp>, a irmp.hpp dołączamy tylko w tym pliku. */
#define IRMP_INPUT_PIN              IR_INPUT_PIN
#define IRMP_SUPPORT_NEC_PROTOCOL   1
#define NO_LED_FEEDBACK_CODE
#if IR_DEBUG_SERIAL
#define IRMP_PROTOCOL_NAMES         1   /* nazwy protokołów w logu IR (kosztuje trochę FLASH) */
#endif
#include <irmp.hpp>

/* ---- Tempo próbkowania enkodera i klawiszy (Hz), dzielniki od F_INTERRUPTS ---- */
#ifndef INPUT_ENC_HZ
#define INPUT_ENC_HZ      2000
#endif
#ifndef INPUT_KEY_HZ
#define INPUT_KEY_HZ      1000
#endif
#define INPUT_ENC_DIV     ((F_INTERRUPTS) / (INPUT_ENC_HZ))
#define INPUT_KEY_DIV     ((F_INTERRUPTS) / (INPUT_KEY_HZ))

// ================== JAWNA INICJALIZACJA CALLBACKÓW ====================
static EncoderCallback _cbRotate = NULL;
static InputCallback _cbAction = NULL;
static InputCallback _cbLong   = NULL;
static InputCallback _cbDouble = NULL;

static InputCallback _cbKeyA = NULL;
static InputCallback _cbKeyB = NULL;
static InputCallback _cbKeyC = NULL;
static InputCallback _cbKeyD = NULL;

static InputCallback _cbKeyALong = NULL;
static InputCallback _cbKeyBLong = NULL;
static InputCallback _cbKeyCLong = NULL;
static InputCallback _cbKeyDLong = NULL;

// =========================== OBIEKTY BIBLIOTEK ==========================
static SimonEncoder encoder(ENCODERA_PIN, ENCODERB_PIN, ENCODER_TYPE, ENCODER_ACCEL);
static OneButton btnEnc(ENCODER_PIN, ACTIVE_LOW, PULLUP_ENABLE);

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

// ===================== WSPÓLNY TIMER (ISR) ====================
/* Bity stanu przycisków próbkowane w ISR (1 = wciśnięty). */
#define KB_ENC   0x01
#define KB_A     0x02
#define KB_B     0x04
#define KB_C     0x08
#define KB_D     0x10

static hw_timer_t * s_timer = NULL;
static volatile uint8_t s_keyBits = 0;

static inline bool pressedAt( int pin, bool activeLow ) {
	return ( digitalRead( pin ) == 0 ) == activeLow;
}
// ---------------------------------------------------------------------------------------------
static void IRAM_ATTR inputTimerIsr( void ) {
	static uint8_t encDiv = 0, keyDiv = 0;

	irmp_ISR();

	if ( ++encDiv >= INPUT_ENC_DIV ) {
		encDiv = 0;
		encoder.tick();
	}
	if ( ++keyDiv >= INPUT_KEY_DIV ) {
		uint8_t b = 0;

		keyDiv = 0;
		if ( pressedAt( ENCODER_PIN, ACTIVE_LOW ) ) b |= KB_ENC;
#if BTN_KEY_A_USED
		if ( pressedAt( KEY_A_PIN, KEY_ACTIVE ) ) b |= KB_A;
#endif
#if BTN_KEY_B_USED
		if ( pressedAt( KEY_B_PIN, KEY_ACTIVE ) ) b |= KB_B;
#endif
#if BTN_KEY_C_USED
		if ( pressedAt( KEY_C_PIN, KEY_ACTIVE ) ) b |= KB_C;
#endif
#if BTN_KEY_D_USED
		if ( pressedAt( KEY_D_PIN, KEY_ACTIVE ) ) b |= KB_D;
#endif
		s_keyBits = b;
	}
}
// ---------------------------------------------------------------------------------------------
static bool inputTimerStart( void ) {
	const uint32_t period_us = ( 1000000UL + ( F_INTERRUPTS ) / 2 ) / ( F_INTERRUPTS ); /* 15 kHz -> 67 us */

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL( 3, 0, 0 )
	s_timer = timerBegin( 1000000 ); /* 1 MHz, timer startuje od razu */
	if ( s_timer == NULL ) return false;
	timerAttachInterrupt( s_timer, &inputTimerIsr );
	timerAlarm( s_timer, period_us, true, 0 );
#else
	s_timer = timerBegin( 2, 80, true ); /* timer 3 zajmuje IRMP w core 2.x */
	if ( s_timer == NULL ) return false;
	timerAttachInterrupt( s_timer, &inputTimerIsr, true );
	timerAlarmWrite( s_timer, period_us, true );
	timerAlarmEnable( s_timer );
#endif
	return true;
}

// ===================== CALLBACKI WEWNĘTRZNE ====================
static void _onRotate(int delta) {
    if (_cbRotate != NULL) {
        _cbRotate(delta);
    }
}
static void _cbRight(int d) { _onRotate(d); }
static void _cbLeft(int d)  { _onRotate(-d); }

static void _clickEnc()   { if (_cbAction != NULL) _cbAction(); }
static void _longEnc()    { if (_cbLong != NULL)   _cbLong();   }
static void _doubleEnc()  { if (_cbDouble != NULL) _cbDouble(); }

#if BTN_KEY_A_USED
static void _clickKeyA() { if (_cbKeyA != NULL) _cbKeyA(); }
static void _longKeyA()  { if (_cbKeyALong != NULL) _cbKeyALong(); }
#endif
#if BTN_KEY_B_USED
static void _clickKeyB() { if (_cbKeyB != NULL) _cbKeyB(); }
static void _longKeyB()  { if (_cbKeyBLong != NULL) _cbKeyBLong(); }
#endif
#if BTN_KEY_C_USED
static void _clickKeyC() { if (_cbKeyC != NULL) _cbKeyC(); }
static void _longKeyC()  { if (_cbKeyCLong != NULL) _cbKeyCLong(); }
#endif
#if BTN_KEY_D_USED
static void _clickKeyD() { if (_cbKeyD != NULL) _cbKeyD(); }
static void _longKeyD()  { if (_cbKeyDLong != NULL) _cbKeyDLong(); }
#endif

// ===================== PILOT IR: KOD -> TO SAMO CO KLAWISZ A..D ====================
typedef struct {
	uint16_t cmd;
	uint8_t repeat;          /* 1 = ramki powtórzeń też wywołują akcję */
	InputCallback * cb;
} IrMap_t;

static const IrMap_t kIrMap[] = {
	{ IR_CMD_A, IR_REPEAT_A, &_cbKeyA },
	{ IR_CMD_B, IR_REPEAT_B, &_cbKeyB },
	{ IR_CMD_C, IR_REPEAT_C, &_cbKeyC },
	{ IR_CMD_D, IR_REPEAT_D, &_cbKeyD },
	{ IR_CMD_A_LONG, 0, &_cbKeyALong },
	{ IR_CMD_B_LONG, 0, &_cbKeyBLong },
	{ IR_CMD_C_LONG, 0, &_cbKeyCLong },
	{ IR_CMD_D_LONG, 0, &_cbKeyDLong },
};

static void irDispatch( const IRMP_DATA * d ) {
	bool repeat = ( d->flags & IRMP_FLAG_REPETITION ) != 0;

#if IR_DEBUG_SERIAL
	Serial.print( "IR: " );
	irmp_result_print( &Serial, (IRMP_DATA *)d );   /* "P=<nr> (<nazwa>) A=0x.. C=0x.. F=0x.." + nowa linia */
#endif
	if ( d->protocol != IRMP_NEC_PROTOCOL ) return;
#if ( IR_ADDRESS ) != ( IR_ADDRESS_ANY )
	if ( (long)d->address != (long)( IR_ADDRESS ) ) return;
#endif
	for ( unsigned i = 0; i < sizeof( kIrMap ) / sizeof( kIrMap[ 0 ] ); i++ ) {
		if ( d->command != kIrMap[ i ].cmd ) continue;
		if ( repeat && !kIrMap[ i ].repeat ) return;
		if ( *kIrMap[ i ].cb != NULL ) ( *kIrMap[ i ].cb )();
		return;
	}
}

// ================================ FUNKCJE PUBLICZNE =================================
void INPUT_init() {
    btnEnc.attachClick(_clickEnc);
    btnEnc.attachLongPressStart(_longEnc);

#if BTN_KEY_A_USED
    keyA.attachClick(_clickKeyA);
    keyA.attachLongPressStart(_longKeyA);
#endif
#if BTN_KEY_B_USED
    keyB.attachClick(_clickKeyB);
    keyB.attachLongPressStart(_longKeyB);
#endif
#if BTN_KEY_C_USED
    keyC.attachClick(_clickKeyC);
    keyC.attachLongPressStart(_longKeyC);
#endif
#if BTN_KEY_D_USED
    keyD.attachClick(_clickKeyD);
    keyD.attachLongPressStart(_longKeyD);
#endif

    encoder.attachOnRight(_cbRight);
    encoder.attachOnLeft(_cbLeft);
    encoder.begin();

    /* IRMP uruchamia własny timer w irmp_init(): zatrzymujemy go,
     * bo irmp_ISR() jest wołane z naszego wspólnego timera. */
    irmp_init();
    disableIRTimerInterrupt();

    if (!inputTimerStart()) {
        Serial.println("INPUT: timer start failed");
    }
}

// Rejestracja callbacków
void INPUT_attach_rotate(EncoderCallback cb)     { _cbRotate = cb; }
void INPUT_attach_action(InputCallback cb)       { _cbAction = cb; }
void INPUT_attach_long_press(InputCallback cb)   { _cbLong = cb; }
void INPUT_attach_double_click(InputCallback cb) {
    _cbDouble = cb;
    if (cb != NULL) btnEnc.attachDoubleClick(_doubleEnc);   // dopiero tu, bo dwuklik opóźnia pojedynczy klik
}
void INPUT_attach_keyA(InputCallback cb)         { _cbKeyA = cb; }
void INPUT_attach_keyA_long(InputCallback cb)    { _cbKeyALong = cb; }
void INPUT_attach_keyB(InputCallback cb)         { _cbKeyB = cb; }
void INPUT_attach_keyB_long(InputCallback cb)    { _cbKeyBLong = cb; }
void INPUT_attach_keyC(InputCallback cb)         { _cbKeyC = cb; }
void INPUT_attach_keyC_long(InputCallback cb)    { _cbKeyCLong = cb; }
void INPUT_attach_keyD(InputCallback cb)         { _cbKeyD = cb; }
void INPUT_attach_keyD_long(InputCallback cb)    { _cbKeyDLong = cb; }

void INPUT_proc() {
    uint8_t b = s_keyBits;   // poziomy przycisków spróbkowane w timerze
    IRMP_DATA ir;

    encoder.eventAnalyze();

    btnEnc.tick((b & KB_ENC) != 0);
#if BTN_KEY_A_USED
    keyA.tick((b & KB_A) != 0);
#endif
#if BTN_KEY_B_USED
    keyB.tick((b & KB_B) != 0);
#endif
#if BTN_KEY_C_USED
    keyC.tick((b & KB_C) != 0);
#endif
#if BTN_KEY_D_USED
    keyD.tick((b & KB_D) != 0);
#endif

    if (irmp_get_data(&ir)) {
        irDispatch(&ir);
    }
}
