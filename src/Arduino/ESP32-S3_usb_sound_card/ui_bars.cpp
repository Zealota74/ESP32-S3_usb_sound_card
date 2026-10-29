/*
 * ui_bars.cpp - słupki widma. Szczegóły: ui_bars.h.
 */
#include <math.h>
#include <string.h>

#include "ui_bars.h"

static const uint16_t kFallMs[] = BARS_FALL_MS_LIST;
#define FALL_STEPS  ( (int)( sizeof( kFallMs ) / sizeof( kFallMs[ 0 ] ) ) )

static const uint16_t kPeakMs[] = BARS_PEAK_FALL_MS_LIST;
#define PEAK_STEPS  ( (int)( sizeof( kPeakMs ) / sizeof( kPeakMs[ 0 ] ) ) )

static int      s_fallIdx = BARS_FALL_DEFAULT;
static int      s_peakIdx = BARS_PEAK_FALL_DEFAULT;
static float    s_level[ BARS_MAX ];    /* wysokość słupka 0..255 */
static float    s_peak[ BARS_MAX ];     /* kreska szczytu 0..255 */
static float    s_hold[ BARS_MAX ];     /* pozostały czas podtrzymania szczytu (ms) */
static int      s_count = 0;            /* liczba słupków z ostatniego BARS_Update */
static uint32_t s_tLast = 0;            /* czas poprzedniej ramki (ms) */
static bool     s_haveLast = false;

// ---------------------------------------------------------------------------------------------
void BARS_Reset( void ) {
	memset( s_level, 0, sizeof( s_level ) );
	memset( s_peak, 0, sizeof( s_peak ) );
	memset( s_hold, 0, sizeof( s_hold ) );
	s_count = 0;
	s_haveLast = false;
}
// ---------------------------------------------------------------------------------------------
void BARS_Update( const float * power, int count, uint32_t now_ms ) {
	const int peakMs = (int)kPeakMs[ s_peakIdx ];               /* 0 = szczyty wyłączone */
	const float peakRate = ( peakMs > 0 ) ? 255.0f / (float)peakMs : 0.0f;   /* opadanie szczytu (skala/ms) */
	float dt = 0.0f, fall;

	if ( count < 1 ) return;
	if ( count > BARS_MAX ) count = BARS_MAX;
	if ( count != s_count ) {                  /* zmienił się układ słupków: zaczynamy od zera */
		BARS_Reset();
		s_count = count;
	}
	if ( s_haveLast ) {
		uint32_t d = now_ms - s_tLast;         /* modulo 2^32: poprawne także po przepełnieniu millis() */

		if ( d > (uint32_t)BARS_MAX_DT_MS ) d = (uint32_t)BARS_MAX_DT_MS;
		dt = (float)d;
	}
	s_tLast = now_ms;
	s_haveLast = true;
	fall = 255.0f * dt / (float)kFallMs[ s_fallIdx ];

	for ( int b = 0; b < count; b++ ) {
		float db = 10.0f * log10f( power[ b ] + 1e-12f );
		float lvl = s_level[ b ];
		int target = (int)( ( db - (float)BARS_DB_FLOOR ) * ( 255.0f / (float)( BARS_DB_TOP - BARS_DB_FLOOR ) ) + 0.5f );

		if ( target < 0 ) target = 0;
		if ( target > 255 ) target = 255;

		/* szybkie narastanie, wolne opadanie */
		if ( (float)target >= lvl ) lvl = (float)target;
		else {
			lvl -= fall;
			if ( lvl < (float)target ) lvl = (float)target;
		}
		s_level[ b ] = lvl;

		/* kreska szczytu: podtrzymanie, potem wolne opadanie; przy peakMs == 0 szczytów nie ma */
		if ( peakMs <= 0 ) {
			s_peak[ b ] = 0.0f;
			s_hold[ b ] = 0.0f;
			continue;
		}
		if ( lvl >= s_peak[ b ] ) {
			s_peak[ b ] = lvl;
			s_hold[ b ] = (float)BARS_PEAK_HOLD_MS;
		} else if ( s_hold[ b ] > 0.0f ) {
			s_hold[ b ] -= dt;
			if ( s_hold[ b ] < 0.0f ) {        /* podtrzymanie skończyło się w trakcie tego odstępu: reszta czasu to opadanie */
				s_peak[ b ] += s_hold[ b ] * peakRate;
				s_hold[ b ] = 0.0f;
			}
		} else {
			s_peak[ b ] -= peakRate * dt;
		}
		if ( s_peak[ b ] < lvl ) s_peak[ b ] = lvl;
	}
}
// ---------------------------------------------------------------------------------------------
void BARS_Get( uint8_t * level, uint8_t * peak ) {
	for ( int b = 0; b < s_count; b++ ) {
		float l = s_level[ b ] + 0.5f, p = s_peak[ b ] + 0.5f;

		level[ b ] = (uint8_t)( ( l > 255.0f ) ? 255.0f : l );
		peak[ b ] = (uint8_t)( ( p > 255.0f ) ? 255.0f : p );
	}
}
// ---------------------------------------------------------------------------------------------
void BARS_FallStep( int dir ) {
	int idx = s_fallIdx + ( dir >= 0 ? 1 : -1 );

	if ( idx < 0 ) idx = 0;
	if ( idx > FALL_STEPS - 1 ) idx = FALL_STEPS - 1;
	s_fallIdx = idx;
}
// ---------------------------------------------------------------------------------------------
int BARS_FallIndex( void ) {
	return s_fallIdx;
}
// ---------------------------------------------------------------------------------------------
int BARS_FallSteps( void ) {
	return FALL_STEPS;
}
// ---------------------------------------------------------------------------------------------
int BARS_GetFallMs( void ) {
	return (int)kFallMs[ s_fallIdx ];
}
// ---------------------------------------------------------------------------------------------
void BARS_PeakStep( int dir ) {
	int idx = s_peakIdx + ( dir >= 0 ? 1 : -1 );

	if ( idx < 0 ) idx = 0;
	if ( idx > PEAK_STEPS - 1 ) idx = PEAK_STEPS - 1;
	s_peakIdx = idx;
}
// ---------------------------------------------------------------------------------------------
int BARS_PeakIndex( void ) {
	return s_peakIdx;
}
// ---------------------------------------------------------------------------------------------
int BARS_PeakSteps( void ) {
	return PEAK_STEPS;
}
// ---------------------------------------------------------------------------------------------
int BARS_GetPeakMs( void ) {
	return (int)kPeakMs[ s_peakIdx ];
}
