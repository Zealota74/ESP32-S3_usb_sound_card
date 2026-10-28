/*
 * spectrum_fft.cpp - silnik FFT: rzeczywista FFT N punktów liczona jako zespolona N/2 + post-processing.
 * Pasmo = suma mocy binów (pasmo węższe niż bin: interpolacja liniowa mocy między sąsiednimi binami).
 * Opcjonalnie (SPECTRUM_SetFftQ / SPECTRUM_FFT_Q): pasmo = średnia ważona mocy binów z jądrem trójkątnym
 * o szerokości połówkowej f0/Q. Wszystkie pasma dostają wtedy tę samą dobroć (nie węższą niż rozdzielczość
 * FFT), więc pasma nachodzą na siebie jak w analizatorze o niskim Q (podobny wygląd jak bank IIR).
 * Pamięć (N = 4096): tablice sin/cos 16 kB, bufor FFT 16 kB, moce binów 8 kB (ring jest wspólny).
 * Kanały (stereo) liczone są kolejno w tych samych buforach roboczych: drugi kanał nie zwiększa pamięci ani nie zmienia
 * wyniku pierwszego, a koszt rośnie proporcjonalnie do liczby kanałów (jedna FFT na kanał, niezależnie od liczby pasm).
 */
#include <math.h>
#include <string.h>

#include "spectrum_engine.h"

#define N         (SPECTRUM_FFT_N)
#define M         (N / 2)

#define TWO_PI_D  6.283185307179586

static float s_cos[ M + 1 ], s_sin[ M + 1 ];   /* cos/sin(2*pi*k/N), k = 0..M */
static float s_re[ M ], s_im[ M ];
static float s_pow[ M ];                    /* moc znormalizowana (pełna skala sinus = 1.0 w binie) */

#ifndef SPECTRUM_FFT_Q
#define SPECTRUM_FFT_Q   0.0f       /* 0 = pasma "cegiełkowe" (jak dotąd), > 0 = dobroć jądra, < 0 = automatycznie */
#endif
#define FFT_MIN_BINS  2.0f                /* najwęższe jądro: szerokość połówkowa w binach (limit rozdzielczości) */

typedef struct {
	int   i0, i1;      /* zakres binów; i1 < i0 = pasmo węższe niż bin (interpolacja w pos) */
	float pos;         /* środek pasma w jednostkach binów */
	float c, invH;     /* tryb jądra: środek (w binach) i 1 / połowa podstawy trójkąta (FWHM = H) */
	float norm;        /* tryb jądra: Nb / suma wag, Nb = szerokość pasma nominalnego w binach */
} FftBand_t;

static FftBand_t s_fb[ SPECTRUM_MAX_BANDS ];
static int       s_bands = 0;
static int       s_channels = 1;
static uint32_t  s_fs = 44100;
static const SpectrumBand_t * s_bandSrc = NULL;   /* pasma wspólne (statyczne w spectrum.cpp) */
static float     s_q = SPECTRUM_FFT_Q;

#ifdef SPECTRUM_TEST
static uint32_t s_dbgStart = 0;
const float * SPECTRUM_DebugPower( void ) { return s_pow; }
uint32_t SPECTRUM_DebugLastStart( void ) { return s_dbgStart; }
#endif

// ---------------------------------------------------------------------------------------------
static void fftInit( void ) {
	for ( int k = 0; k <= M; k++ ) {
		s_cos[ k ] = (float)cos( TWO_PI_D * (double)k / (double)N );
		s_sin[ k ] = (float)sin( TWO_PI_D * (double)k / (double)N );
	}
}
// ---------------------------------------------------------------------------------------------
static void fftBuildBands( void ) {
	const SpectrumBand_t * band = s_bandSrc;
	float df = (float)s_fs / (float)N;
	float q = s_q;

	if ( band == NULL ) return;
	if ( q < 0.0f ) q = band[ 0 ].f0 / ( FFT_MIN_BINS * df );     /* auto: dobroć najniższego pasma przy limicie rozdzielczości */

	for ( int b = 0; b < s_bands; b++ ) {
		int i0 = (int)ceilf( band[ b ].f_lo / df );
		int i1 = (int)ceilf( band[ b ].f_hi / df ) - 1;
		float pos = band[ b ].f0 / df;

		if ( i0 < 1 ) i0 = 1;
		if ( i0 > M - 1 ) i0 = M - 1;
		if ( i1 > M - 1 ) i1 = M - 1;
		if ( pos < 1.0f ) pos = 1.0f;
		if ( pos > (float)( M - 2 ) ) pos = (float)( M - 2 );
		s_fb[ b ].i0 = i0;
		s_fb[ b ].i1 = i1;
		s_fb[ b ].pos = pos;

		if ( q > 0.0f ) {
			/* jądro trójkątne: FWHM = H = max( pasmo nominalne, f0 / Q, FFT_MIN_BINS ) w binach */
			float nb = ( band[ b ].f_hi - band[ b ].f_lo ) / df;
			float h = band[ b ].f0 / q / df;
			float c = band[ b ].f0 / df;
			double sw = 0.0;
			int k0, k1;

			if ( h < nb ) h = nb;
			if ( h < FFT_MIN_BINS ) h = FFT_MIN_BINS;
			k0 = (int)ceilf( c - h );
			k1 = (int)floorf( c + h );
			if ( k0 < 1 ) k0 = 1;
			if ( k1 > M - 1 ) k1 = M - 1;
			for ( int k = k0; k <= k1; k++ ) sw += 1.0 - fabs( (double)k - (double)c ) / (double)h;
			s_fb[ b ].i0 = k0;
			s_fb[ b ].i1 = k1;
			s_fb[ b ].c = c;
			s_fb[ b ].invH = 1.0f / h;
			s_fb[ b ].norm = ( sw > 1e-9 ) ? (float)( (double)nb / sw ) : 0.0f;
		}
	}
}
// ---------------------------------------------------------------------------------------------
static void fftConfigure( uint32_t fs, const SpectrumBand_t * band, int bands, int channels ) {
	s_fs = fs;
	s_bandSrc = band;
	s_bands = bands;
	s_channels = channels;
	fftBuildBands();
}
// ---------------------------------------------------------------------------------------------
void SPECTRUM_SetFftQ( float q ) {
	s_q = q;
	fftBuildBands();
}
// ---------------------------------------------------------------------------------------------
float SPECTRUM_GetFftQ( void ) {
	return s_q;
}
// ---------------------------------------------------------------------------------------------
static void fftReset( void ) {
	/* FFT nie ma stanu między ramkami (okno bierze się wprost z bufora kołowego) */
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
/* Analizuje jeden kanał: N próbek bufora 'ring' od numeru 'start' (okno przesuwane). Wynik: power[0..s_bands-1]. */
static int fftChannel( const int16_t * ring, uint32_t start, float * power ) {
	const float norm = 1.0f / ( (float)N * 0.25f * 32768.0f );   /* sinus pełnej skali -> 1.0 w binie */
	const float norm2 = norm * norm;

	/* okno + upakowanie: z[m] = x[2m] + j*x[2m+1] */
	for ( int m = 0; m < M; m++ ) {
		int n0 = 2 * m, n1 = n0 + 1;
		s_re[ m ] = (float)ring[ ( start + (uint32_t)n0 ) & SPECTRUM_RING_MASK ] * win( n0 );
		s_im[ m ] = (float)ring[ ( start + (uint32_t)n1 ) & SPECTRUM_RING_MASK ] * win( n1 );
	}
	if ( (uint32_t)( SpectrumRing_Wr() - start ) > (uint32_t)SPECTRUM_RING_SIZE ) {   /* producent nadpisał dane w trakcie kopiowania */
		return 0;
	}
#ifdef SPECTRUM_TEST
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

	/* pasma: suma mocy binów (pasmo węższe niż bin: interpolacja liniowa mocy) albo jądro trójkątne */
	for ( int b = 0; b < s_bands; b++ ) {
		float p = 0.0f;

		if ( s_q != 0.0f ) {
			float x = (float)s_fb[ b ].i0 - s_fb[ b ].c;          /* odległość binu od środka, rośnie o 1 */
			const float invH = s_fb[ b ].invH;

			for ( int k = s_fb[ b ].i0; k <= s_fb[ b ].i1; k++, x += 1.0f ) {
				p += ( 1.0f - fabsf( x ) * invH ) * s_pow[ k ];
			}
			p *= s_fb[ b ].norm;
		} else if ( s_fb[ b ].i1 >= s_fb[ b ].i0 ) {
			for ( int k = s_fb[ b ].i0; k <= s_fb[ b ].i1; k++ ) p += s_pow[ k ];
		} else {
			int ia = (int)s_fb[ b ].pos;
			float fr = s_fb[ b ].pos - (float)ia;
			p = s_pow[ ia ] * ( 1.0f - fr ) + s_pow[ ia + 1 ] * fr;
		}
		power[ b ] = p;
	}
	return 1;
}
// ---------------------------------------------------------------------------------------------
/* Analizuje ostatnie N próbek przed 'to' w każdym kanale; 'from' nie jest potrzebne (okno przesuwane). */
static int fftProcess( uint32_t from, uint32_t to, float * power ) {
	uint32_t start = to - (uint32_t)N;

	(void)from;
	for ( int ch = 0; ch < s_channels; ch++ ) {
		if ( !fftChannel( g_spectrumRing[ ch ], start, power + ch * s_bands ) ) return 0;
	}
	return 1;
}
// ---------------------------------------------------------------------------------------------
const SpectrumEngine_t g_spectrumEngineFft = { "FFT", fftInit, fftConfigure, fftReset, fftProcess };
