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

typedef enum { MENU_BARS_CNT = 0, MENU_CHANNELS, MENU_BRIGHT, MENU_ENGINE, MENU_BARS_FALL, MENU_PEAK_FALL, MENU_COUNT } Menu_t;

static int s_menuItem = MENU_BARS_CNT;       /* ostatnio używane okienko */

// ---------------------------------------------------------------------------------------------
/* Liczba słupków: 7 -> 16 -> 32 -> 64 -> 7 (cyklicznie), 
 * tylko wartości mieszczące się w limicie DSP (kanały x pasma).
 */
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
/* Tryb kanałów widma: MONO -> L -> R -> L+R -> MONO (cyklicznie). MONO liczy 1 kanał (miks), pozostałe 2 kanały
 * (L i R liczą się zawsze oba, tryb decyduje tylko o tym, co jest rysowane). Dwa kanały mają wspólny limit słupków,
 * więc DSP może zmniejszyć liczbę słupków (np. 64 -> 32). */
static void cycleChannels( int dir ) {
	int m = ( (int)UI_GetSpecMode() + dir + (int)UI_SPEC_MODE_COUNT ) % (int)UI_SPEC_MODE_COUNT;

	UI_SetSpecMode( (UI_SpecMode_t)m );
	DSP_SpectrumSetChannels( ( m == (int)UI_SPEC_MONO ) ? 1 : 2 );
	BARS_Reset();
}
// ---------------------------------------------------------------------------------------------
static const char * channelsName( void ) {
	switch ( UI_GetSpecMode() ) {
		case UI_SPEC_L:  return "L";
		case UI_SPEC_R:  return "R";
		case UI_SPEC_LR: return "L+R";
		default:         return "MONO";
	}
}
// ---------------------------------------------------------------------------------------------
static void menuShow( void ) {
	char t[ 20 ], v[ 16 ];
	int pos = 0, max = 0;

	switch ( s_menuItem ) {
		case MENU_BARS_CNT:
			snprintf( t, sizeof( t ), "%d/%d SPECTRUM BARS", MENU_BARS_CNT + 1, MENU_COUNT );
			snprintf( v, sizeof( v ), "%d", DSP_SpectrumGetBands() );
			break;
		case MENU_CHANNELS:
			snprintf( t, sizeof( t ), "%d/%d CHANNELS", MENU_CHANNELS + 1, MENU_COUNT );
			snprintf( v, sizeof( v ), "%s", channelsName() );
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
			snprintf( t, sizeof( t ), "%d/%d SPECTRUM FALL", MENU_BARS_FALL + 1, MENU_COUNT );
			snprintf( v, sizeof( v ), "%lu ms", (unsigned long)BARS_GetFallMs() );
			pos = BARS_FallSteps() - 1 - BARS_FallIndex();
			max = BARS_FallSteps() - 1;     /* pasek rośnie razem z wartością: dalej w prawo = dłuższy czas opadania (wolniej) */
			break;
		case MENU_PEAK_FALL:
		default:
			snprintf( t, sizeof( t ), "%d/%d PEAK FALL", MENU_PEAK_FALL + 1, MENU_COUNT );
			if ( BARS_GetPeakMs() > 0 ) snprintf( v, sizeof( v ), "%d ms", BARS_GetPeakMs() );
			else snprintf( v, sizeof( v ), "OFF" );   /* wartość 0 = brak szczytów */
			pos = BARS_PeakIndex();
			max = BARS_PeakSteps() - 1;     /* lista rośnie razem z wartością: w prawo = dłużej (wolniej), w lewo = krócej, na końcu OFF */
			break;
	}
	UI_ShowMenu( t, v, pos, max );
}
// ---------------------------------------------------------------------------------------------
static void menuChange( int steps ) {
	int dir = ( steps > 0 ) ? 1 : -1;

	switch ( s_menuItem ) {
		case MENU_BARS_CNT:
			cycleBars( dir );
			break;
		case MENU_CHANNELS:
			cycleChannels( dir );
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
			BARS_FallStep( -dir );         /* obrót w prawo = większa wartość (ms) = wolniejsze opadanie; bez zawijania */
			break;
		case MENU_PEAK_FALL:
			BARS_PeakStep( dir );          /* obrót w prawo = większa wartość (ms) = wolniejsze opadanie szczytu; 0 = brak szczytów; bez zawijania */
			break;
		default:
			break;
	}
	menuShow();                              /* nadpisuje OSD jasności i odnawia czas okienka */
}
// ---------------------------------------------------------------------------------------------
static void menuMove( int dir ) { /* pierwsze naciśnięcie tylko otwiera, kolejne przełączają cyklicznie */
	if ( UI_MenuOpen() ) s_menuItem = ( s_menuItem + dir + MENU_COUNT ) % MENU_COUNT;
	menuShow();
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
// =============================================================================================