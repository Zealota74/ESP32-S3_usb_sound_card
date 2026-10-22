/*
 * audio_spectrum.cpp - FFT rzeczywista N punktów liczona jako zespolona N/2 + post-processing.
 * Pamięć (N = 4096): ring 16 kB, tablice sin/cos 16 kB, bufor FFT 16 kB, moce binów 8 kB.
 */
#include <math.h>
#include <string.h>

#include "audio_spectrum.h"
#include "audio_config.h"

#define N         (AUDIO_SPECTRUM_FFT_N)
#define M         (N / 2)
#define HOP       (AUDIO_SPECTRUM_HOP)
#define RING_SIZE (2 * N)
#define RING_MASK (RING_SIZE - 1)

#if ( ( N ) & ( ( N ) - 1 ) ) != 0 || ( N ) < 64
  #error "AUDIO_SPECTRUM_FFT_N musi być potęgą dwójki (min. 64)"
#endif

#define TWO_PI_D  6.283185307179586

static int16_t           s_ring[ RING_SIZE ];
static volatile uint32_t s_wr = 0;          /* licznik zapisanych próbek (zapis: wątek audio) */
static uint32_t          s_lastWr = 0;      /* wartość s_wr przy ostatniej FFT */

static float s_cos[ M + 1 ], s_sin[ M + 1 ];   /* cos/sin(2*pi*k/N), k = 0..M */
static float s_re[ M ], s_im[ M ];
static float s_pow[ M ];                    /* moc znormalizowana (pełna skala sinus = 1.0 w binie) */

static uint32_t s_fs = 44100;

typedef struct {
	int   i0, i1;      /* zakres binów; i1 < i0 = pasmo węższe niż bin (interpolacja w pos) */
	float pos;         /* środek pasma w jednostkach binów */
} Band_t;

static Band_t s_band[ SPEC_MAX_BARS ];
static int    s_bars = 16;
static const uint16_t kFallMs[] = AUDIO_SPECTRUM_FALL_MS_LIST;
#define FALL_STEPS ( (int)( sizeof( kFallMs ) / sizeof( kFallMs[ 0 ] ) ) )
static int    s_fallIdx = AUDIO_SPECTRUM_FALL_DEFAULT;
static int    s_fallPerFrame = 12;     /* opadanie słupka na ramkę (skala 0..255) */
static int    s_level[ SPEC_MAX_BARS ];
static int    s_peak[ SPEC_MAX_BARS ];
static int    s_hold[ SPEC_MAX_BARS ];

#ifdef AUDIO_SPECTRUM_TEST
static uint32_t s_dbgStart = 0;
const float * SPEC_DebugPower( void ) { return s_pow; }
uint32_t SPEC_DebugLastStart( void ) { return s_dbgStart; }
#endif

// ---------------------------------------------------------------------------------------------
static void buildBands( void ) {
	float df = (float)s_fs / (float)N;
	float fLow = (float)AUDIO_SPECTRUM_F_LOW;
	float fHigh = (float)AUDIO_SPECTRUM_F_HIGH;
	float fMax = 0.49f * (float)s_fs;
	float ratio;

	if ( fHigh > fMax ) fHigh = fMax;
	ratio = fHigh / fLow;

	for ( int b = 0; b < s_bars; b++ ) {
		float fl = fLow * powf( ratio, (float)b / (float)s_bars );
		float fh = fLow * powf( ratio, (float)( b + 1 ) / (float)s_bars );
		int i0 = (int)ceilf( fl / df );
		int i1 = (int)ceilf( fh / df ) - 1;
		float pos = sqrtf( fl * fh ) / df;

		if ( i0 < 1 ) i0 = 1;
		if ( i0 > M - 1 ) i0 = M - 1;
		if ( i1 > M - 1 ) i1 = M - 1;
		if ( pos < 1.0f ) pos = 1.0f;
		if ( pos > (float)( M - 2 ) ) pos = (float)( M - 2 );
		s_band[ b ].i0 = i0;
		s_band[ b ].i1 = i1;
		s_band[ b ].pos = pos;
	}
}
// ---------------------------------------------------------------------------------------------
static void computeFall( void ) {
	float frame_s = (float)HOP / (float)s_fs;                       /* czas jednej ramki */
	float full_s = (float)kFallMs[ s_fallIdx ] * 0.001f;            /* czas opadania z pełnej skali */
	int f = (int)( 255.0f * frame_s / full_s + 0.5f );

	s_fallPerFrame = ( f < 1 ) ? 1 : f;
}
// ---------------------------------------------------------------------------------------------
int SPEC_FallSteps( void ) {
	return FALL_STEPS;
}
// ---------------------------------------------------------------------------------------------
void SPEC_SetFall( int idx ) {
	if ( idx < 0 ) idx = 0;
	if ( idx > FALL_STEPS - 1 ) idx = FALL_STEPS - 1;
	s_fallIdx = idx;
	computeFall();
}
// ---------------------------------------------------------------------------------------------
int SPEC_GetFall( void ) {
	return s_fallIdx;
}
// ---------------------------------------------------------------------------------------------
uint32_t SPEC_GetFallMs( void ) {
	return kFallMs[ s_fallIdx ];
}
// ---------------------------------------------------------------------------------------------
void SPEC_Init( uint32_t sample_rate ) {
	for ( int k = 0; k <= M; k++ ) {
		s_cos[ k ] = (float)cos( TWO_PI_D * (double)k / (double)N );
		s_sin[ k ] = (float)sin( TWO_PI_D * (double)k / (double)N );
	}
	memset( s_ring, 0, sizeof( s_ring ) );
	s_wr = 0;
	s_lastWr = 0;
	s_fs = sample_rate ? sample_rate : 44100;
	SPEC_SetFall( s_fallIdx );   /* przelicz krok opadania dla bieżącej częstotliwości */
	buildBands();
	SPEC_Reset();
}
// ---------------------------------------------------------------------------------------------
void SPEC_SetRate( uint32_t sample_rate ) {
	if ( sample_rate == 0 || sample_rate == s_fs ) return;
	s_fs = sample_rate;
	computeFall();
	buildBands();
	SPEC_Reset();
}
// ---------------------------------------------------------------------------------------------
void SPEC_Feed( const int16_t * stereo, uint32_t frames ) {
	uint32_t wr = s_wr;

	for ( uint32_t i = 0; i < frames; i++ ) {
		s_ring[ ( wr + i ) & RING_MASK ] = (int16_t)( ( (int32_t)stereo[ i * 2 ] + (int32_t)stereo[ i * 2 + 1 ] ) >> 1 );
	}
	__atomic_store_n( &s_wr, wr + frames, __ATOMIC_RELEASE ); /* dane muszą być widoczne przed przesunięciem wr */
}
// ---------------------------------------------------------------------------------------------
void SPEC_SetBars( int bars ) {
	if ( bars < 1 ) bars = 1;
	if ( bars > SPEC_MAX_BARS ) bars = SPEC_MAX_BARS;
	if ( bars == s_bars ) return;
	s_bars = bars;
	buildBands();
	SPEC_Reset();
}
// ---------------------------------------------------------------------------------------------
int SPEC_GetBars( void ) {
	return s_bars;
}
// ---------------------------------------------------------------------------------------------
void SPEC_Reset( void ) {
	memset( s_level, 0, sizeof( s_level ) );
	memset( s_peak, 0, sizeof( s_peak ) );
	memset( s_hold, 0, sizeof( s_hold ) );
	s_lastWr = __atomic_load_n( &s_wr, __ATOMIC_ACQUIRE );
}
// ---------------------------------------------------------------------------------------------
/* Okno Hanna z tablicy cos: w(n) = 0.5 - 0.5*cos(2*pi*n/N), n = 0..N-1 */
static inline float win( int n ) {
	return 0.5f - 0.5f * ( ( n <= M ) ? s_cos[ n ] : s_cos[ N - n ] );
}
// ---------------------------------------------------------------------------------------------
/* FFT zespolona M punktów, w miejscu, radix-2 DIT; twiddle z tablicy N-punktowej (krok N/len). */
static void fftComplex( void ) {
	int i, j, bit;

	for ( i = 1, j = 0; i < M; i++ ) {
		bit = M >> 1;
		for ( ; j & bit; bit >>= 1 ) j ^= bit;
		j ^= bit;
		if ( i < j ) {
			float t = s_re[ i ]; s_re[ i ] = s_re[ j ]; s_re[ j ] = t;
			t = s_im[ i ]; s_im[ i ] = s_im[ j ]; s_im[ j ] = t;
		}
	}
	for ( int len = 2; len <= M; len <<= 1 ) {
		int half = len >> 1;
		int step = N / len;

		for ( i = 0; i < M; i += len ) {
			for ( j = 0; j < half; j++ ) {
				int k = j * step;
				float wr = s_cos[ k ];
				float wi = -s_sin[ k ];
				int a = i + j, b = a + half;
				float tr = s_re[ b ] * wr - s_im[ b ] * wi;
				float ti = s_re[ b ] * wi + s_im[ b ] * wr;

				s_re[ b ] = s_re[ a ] - tr;
				s_im[ b ] = s_im[ a ] - ti;
				s_re[ a ] += tr;
				s_im[ a ] += ti;
			}
		}
	}
}
// ---------------------------------------------------------------------------------------------
int SPEC_Process( void ) {
	const float norm = 1.0f / ( (float)N * 0.25f * 32768.0f );   /* sinus pełnej skali -> 1.0 w binie */
	const float norm2 = norm * norm;
	uint32_t wr = __atomic_load_n( &s_wr, __ATOMIC_ACQUIRE );
	uint32_t start, wr2;

	if ( (uint32_t)( wr - s_lastWr ) < (uint32_t)HOP ) return 0;
	start = wr - (uint32_t)N;

	/* okno + upakowanie: z[m] = x[2m] + j*x[2m+1] */
	for ( int m = 0; m < M; m++ ) {
		int n0 = 2 * m, n1 = n0 + 1;
		s_re[ m ] = (float)s_ring[ ( start + (uint32_t)n0 ) & RING_MASK ] * win( n0 );
		s_im[ m ] = (float)s_ring[ ( start + (uint32_t)n1 ) & RING_MASK ] * win( n1 );
	}
	wr2 = __atomic_load_n( &s_wr, __ATOMIC_ACQUIRE );
	if ( (uint32_t)( wr2 - start ) > (uint32_t)RING_SIZE ) { /* producent nadpisał dane w trakcie kopiowania */
		s_lastWr = wr2;
		return 0;
	}
	s_lastWr = wr;
#ifdef AUDIO_SPECTRUM_TEST
	s_dbgStart = start;
#endif

	fftComplex();

	/* rozdzielenie widm: X[k] = Xe[k] + W_N^k * Xo[k], k = 0..M-1 */
	for ( int k = 0; k < M; k++ ) {
		int mk = ( M - k ) & ( M - 1 );
		float xer = 0.5f * ( s_re[ k ] + s_re[ mk ] );
		float xei = 0.5f * ( s_im[ k ] - s_im[ mk ] );
		float xor_ = 0.5f * ( s_im[ k ] + s_im[ mk ] );
		float xoi = -0.5f * ( s_re[ k ] - s_re[ mk ] );
		float c = s_cos[ k ], s = s_sin[ k ];
		float xr = xer + c * xor_ + s * xoi;
		float xi = xei + c * xoi - s * xor_;

		s_pow[ k ] = ( xr * xr + xi * xi ) * norm2;
	}

	/* pasma: suma mocy binów (pasmo węższe niż bin: interpolacja liniowa mocy) */
	for ( int b = 0; b < s_bars; b++ ) {
		float p = 0.0f;
		float db;
		int target, lvl = s_level[ b ];

		if ( s_band[ b ].i1 >= s_band[ b ].i0 ) {
			for ( int k = s_band[ b ].i0; k <= s_band[ b ].i1; k++ ) p += s_pow[ k ];
		} else {
			int ia = (int)s_band[ b ].pos;
			float fr = s_band[ b ].pos - (float)ia;
			p = s_pow[ ia ] * ( 1.0f - fr ) + s_pow[ ia + 1 ] * fr;
		}
		db = 10.0f * log10f( p + 1e-12f );
		target = (int)( ( db - (float)AUDIO_SPECTRUM_DB_FLOOR ) * ( 255.0f / (float)( AUDIO_SPECTRUM_DB_TOP - AUDIO_SPECTRUM_DB_FLOOR ) ) + 0.5f );
		if ( target < 0 ) target = 0;
		if ( target > 255 ) target = 255;

		/* szybkie narastanie, wolne opadanie */
		if ( target >= lvl ) lvl = target;
		else {
			lvl -= s_fallPerFrame;
			if ( lvl < target ) lvl = target;
		}
		s_level[ b ] = lvl;

		/* kreska szczytu: podtrzymanie, potem wolne opadanie */
		if ( lvl >= s_peak[ b ] ) {
			s_peak[ b ] = lvl;
			s_hold[ b ] = AUDIO_SPECTRUM_PEAK_HOLD;
		} else if ( s_hold[ b ] > 0 ) {
			s_hold[ b ]--;
		} else {
			s_peak[ b ] -= AUDIO_SPECTRUM_PEAK_FALL;
			if ( s_peak[ b ] < lvl ) s_peak[ b ] = lvl;
		}
	}
	return 1;
}
// ---------------------------------------------------------------------------------------------
void SPEC_GetFrame( uint8_t * level, uint8_t * peak ) {
	for ( int b = 0; b < s_bars; b++ ) {
		level[ b ] = (uint8_t)s_level[ b ];
		peak[ b ] = (uint8_t)s_peak[ b ];
	}
}
