/*
 * menu_oled.cpp - logika menu ekranowego. Szczegóły: menu_oled.h.
 * Rysowaniem zajmuje się ui_oled (UI_ShowMenu), zmiany ustawień idą do audio.* i ui_oled.*.
 */
#include <Arduino.h>
#include <stdio.h>

#include "src/audio_manage.h"
#include "src/dsp_manage.h"
#include "ui_oled.h"
#include "menu_oled.h"

typedef enum { MENU_BARS_CNT = 0, MENU_BRIGHT, MENU_ENGINE, MENU_BARS_FALL, MENU_COUNT } Menu_t;

static int s_menuItem = MENU_BARS_CNT;       /* ostatnio używane okienko */

// ---------------------------------------------------------------------------------------------
static void menuShow( void ) {
	char t[ 20 ], v[ 16 ];
	int pos = 0, max = 0;

	switch ( s_menuItem ) {
		case MENU_BARS_CNT:
			snprintf( t, sizeof( t ), "%d/%d SPECTRUM BARS", MENU_BARS_CNT + 1, MENU_COUNT );
			snprintf( v, sizeof( v ), "%d", DSP_SpectrumGetBars() );
			break;
		case MENU_BRIGHT:
			snprintf( t, sizeof( t ), "%d/%d BRIGHTNESS", MENU_BRIGHT + 1, MENU_COUNT );
			snprintf( v, sizeof( v ), "%d/%d", UI_GetBrightness() + 1, UI_BRIGHT_LEVELS );
			pos = UI_GetBrightness();
			max = UI_BRIGHT_LEVELS - 1;
			break;
		case MENU_ENGINE:
			snprintf( t, sizeof( t ), "%d/%d ANALYSIS", MENU_ENGINE + 1, MENU_COUNT );
			snprintf( v, sizeof( v ), "%s", DSP_SpectrumEngineName( DSP_SpectrumGetEngine() ) );
			break;
		case MENU_BARS_FALL:
		default:
			snprintf( t, sizeof( t ), "%d/%d SPECTRUM FALL", MENU_BARS_FALL + 1, MENU_COUNT );
			snprintf( v, sizeof( v ), "%lu ms", (unsigned long)DSP_SpectrumGetFallMs() );
			pos = DSP_SpectrumFallIndex();
			max = DSP_SpectrumFallSteps() - 1;     /* pasek: dalej w prawo = szybsze opadanie */
			break;
	}
	UI_ShowMenu( t, v, pos, max );
}
// ---------------------------------------------------------------------------------------------
static void menuMove( int dir ) {        /* pierwsze naciśnięcie tylko otwiera, kolejne przełączają cyklicznie */
	if ( UI_MenuOpen() ) s_menuItem = ( s_menuItem + dir + MENU_COUNT ) % MENU_COUNT;
	menuShow();
}
// ---------------------------------------------------------------------------------------------
static void menuChange( int steps ) {
	int dir = ( steps > 0 ) ? 1 : -1;

	switch ( s_menuItem ) {
		case MENU_BARS_CNT:
			DSP_SpectrumCycleBars( dir );
			break;
		case MENU_BRIGHT:
			UI_BrightnessStep( steps );      /* bez zawijania: skok z max na min byłby nieprzyjemny */
			break;
		case MENU_ENGINE: {
			int n = DSP_SpectrumEngineCount();
			DSP_SpectrumSetEngine( ( DSP_SpectrumGetEngine() + dir + n ) % n );
			break;
		}
		case MENU_BARS_FALL:
			DSP_SpectrumFallStep( dir );   /* z ograniczeniem do zakresu, bez zawijania */
			break;
		default:
			break;
	}
	menuShow();                              /* nadpisuje OSD jasności i odnawia czas okienka */
}
// =============================================================================================
void MENU_Up( void ) {
	menuMove( -1 );
}
// ---------------------------------------------------------------------------------------------
void MENU_Down( void ) {
	menuMove( +1 );
}
// ---------------------------------------------------------------------------------------------
int MENU_Rotate( int steps ) {
	if ( !UI_MenuOpen() ) return 0;
	menuChange( steps );
	return 1;
}
