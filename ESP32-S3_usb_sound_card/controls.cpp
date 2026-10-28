/*
 * controls.cpp - callbacki wejść i ich rejestracja. Szczegóły: controls.h.
 * Wszystko wołane z INPUT_proc() (loop), więc można bezpiecznie używać audio, OLED i USB.
 */
#include <Arduino.h>

#include "src/audio_manage.h"

#include "input.h"
#include "ui_oled.h"
#include "menu_oled.h"
#include "controls.h"

#define KEY_DIAG_SERIAL     1        /* 1 = log akcji klawiszy A-D / pilota na Serial */

// ---------------------------------------------------------------------------------------------
static void onEncRotate( int steps ) {
	if ( !MENU_Rotate( steps ) ) AUDIO_VolumeStep( steps );   /* okienko menu przejmuje enkoder, inaczej głośność */
}
// ---------------------------------------------------------------------------------------------
static void onEncClick( void )       { AUDIO_MuteToggle(); }
static void onEncLongPress( void )   { UI_NextScreen(); }                                  /* STAT <-> SPECTRUM */
static void onEncDoubleClick( void ) { AUDIO_GuardRelease(); UI_SetGuard( 0, 0.0f ); }     /* zwolnij limit */
// ---------------------------------------------------------------------------------------------
static void onKeyUp( void )   { MENU_Up(); }       /* B: poprzednie okienko */
static void onKeyDown( void ) { MENU_Down(); }     /* D: następne okienko */
// ---------------------------------------------------------------------------------------------
static void onKeyRight( void ) {                   /* A */
	AUDIO_MediaKey( AUDIO_MEDIA_NEXT );
#if KEY_DIAG_SERIAL
	Serial.println( "KEY_A / IR: następny utwór" );
#endif
}
// ---------------------------------------------------------------------------------------------
static void onKeyLeft( void ) {                    /* C */
	AUDIO_MediaKey( AUDIO_MEDIA_PREV );
#if KEY_DIAG_SERIAL
	Serial.println( "KEY_C / IR: poprzedni utwór" );
#endif
}
// =============================================================================================
void CONTROLS_Init( void ) {
	INPUT_attach_rotate( onEncRotate );
	INPUT_attach_action( onEncClick );
	INPUT_attach_long_press( onEncLongPress );
	INPUT_attach_keyA( onKeyRight );
	INPUT_attach_keyB( onKeyUp );
	INPUT_attach_keyC( onKeyLeft );
	INPUT_attach_keyD( onKeyDown );

	if ( AUDIO_GuardActive() ) {
		INPUT_attach_double_click( onEncDoubleClick );   /* dwuklik zwalnia awaryjny limit */
	}
}
