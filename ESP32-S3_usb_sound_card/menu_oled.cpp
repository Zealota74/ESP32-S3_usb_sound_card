/*
 * menu_oled.cpp - logika menu ekranowego. Szczegóły: menu_oled.h.
 * Rysowaniem zajmuje się ui_oled (UI_ShowMenu), zmiany ustawień idą do audio.* i ui_oled.*.
 */
#include <Arduino.h>
#include <stdio.h>

#include "src/audio_manage.h"
#include "src/dsp_manage.h"

#include "ui_oled.h"
#include "ui_bars.h"
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
			snprintf( v, sizeof( v ), "%d", DSP_SpectrumGetBands() );
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
			snprintf( v, sizeof( v ), "%lu ms", (unsigned long)BARS_GetFallMs() );
			pos = BARS_FallIndex();
			max = BARS_FallSteps() - 1;     /* pasek: dalej w prawo = szybsze opadanie */
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
/* Liczba słupków: 7 -> 16 -> 32 -> 64 -> 7 (cyklicznie), tylko wartości mieszczące się w limicie DSP (kanały x pasma). */
static void cycleBars( int dir ) {
	static const int opts[] = { 7, 16, 32, 64 };
	const int n = (int)( sizeof( opts ) / sizeof( opts[ 0 ] ) );
	int limit = DSP_SPECTRUM_MAX_BANDS / DSP_SpectrumGetChannels();
	int cur = DSP_SpectrumGetBands();
	int idx = 1;

	for ( int i = 0; i < n; i++ ) {
		if ( opts[ i ] == cur ) idx = i;
	}
	for ( int step = 0; step < n; step++ ) {
		idx = ( idx + ( dir >= 0 ? 1 : n - 1 ) ) % n;
		if ( opts[ idx ] <= limit ) break;
	}
	DSP_SpectrumSetBands( opts[ idx ] );
	BARS_Reset();                            /* nowy układ słupków zaczyna od zera */
}
// ---------------------------------------------------------------------------------------------
static void menuChange( int steps ) {
	int dir = ( steps > 0 ) ? 1 : -1;

	switch ( s_menuItem ) {
		case MENU_BARS_CNT:
			cycleBars( dir );
			break;
		case MENU_BRIGHT:
			UI_BrightnessStep( steps );      /* bez zawijania: skok z max na min byłby nieprzyjemny */
			break;
		case MENU_ENGINE: {
			int n = DSP_SpectrumEngineCount();
			DSP_SpectrumSetEngine( ( DSP_SpectrumGetEngine() + dir + n ) % n );
			BARS_Reset();                    /* nowy silnik zaczyna słupki od zera */
			break;
		}
		case MENU_BARS_FALL:
			BARS_FallStep( dir );          /* z ograniczeniem do zakresu, bez zawijania */
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
