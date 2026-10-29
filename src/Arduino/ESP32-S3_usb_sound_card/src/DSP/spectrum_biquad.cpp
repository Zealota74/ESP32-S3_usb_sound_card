/*
 * spectrum_biquad.cpp - wspólne klocki DSP silników IIR. Szczegóły: spectrum_biquad.h.
 */
#include <math.h>

#include "spectrum_biquad.h"

/* Arduino buduje z -Os (bez rozwijania pętli, stany w pamięci); pętle filtrów są gorące, więc tu -O3. */
#pragma GCC optimize ( "O3" )

#define PI_D   3.141592653589793
#define TINY   1e-15f        /* poniżej tego stan zerujemy (unikamy liczb zdenormalizowanych) */

// ---------------------------------------------------------------------------------------------
/* Analogowy prototyp H(s) = BW*s / ( s^2 + BW*s + W0^2 ) w osi z pre-warpingiem (W = tan(pi*f/fs)),
 * s = ( 1 - z^-1 ) / ( 1 + z^-1 ). */
void SpectrumBp_Design( SpectrumBp_t * q, double fs, double fl, double fh, double bwScale ) {
	double w1 = tan( PI_D * fl / fs );
	double w2 = tan( PI_D * fh / fs );
	double bw = ( w2 - w1 ) * bwScale;
	double w0sq = w1 * w2;
	double a0 = 1.0 + bw + w0sq;

	q->b0 = (float)( bw / a0 );               /* b1 = 0, b2 = -b0 */
	q->a1 = (float)( 2.0 * ( w0sq - 1.0 ) / a0 );
	q->a2 = (float)( ( 1.0 - bw + w0sq ) / a0 );
	q->z1 = 0.0f;
	q->z2 = 0.0f;
}
// ---------------------------------------------------------------------------------------------
void SpectrumBp_CascadeScale( int n, double * bwScale, double * enbwRatio ) {
	/* Równoważna szerokość szumowa kaskady n sekcji 2. rzędu = K(n) * BW sekcji, gdzie
	 * K(n) = 0.5 * sqrt(pi) * Gamma(n - 1/2) / Gamma(n). */
	static const double kEnbw[ 4 ] = { 1.5707963, 0.7853982, 0.5890486, 0.4908739 };
	/* n identycznych sekcji: pasmo -3 dB kaskady = pasmo sekcji * s, s = sqrt( 2^(1/n) - 1 ),
	 * więc sekcje muszą być szersze o 1/s, żeby krawędzie -3 dB wypadły w f_lo i f_hi. */
	double s;

	if ( n < 1 ) n = 1;
	if ( n > 4 ) n = 4;
	s = sqrt( pow( 2.0, 1.0 / (double)n ) - 1.0 );
	*bwScale = 1.0 / s;
	*enbwRatio = kEnbw[ n - 1 ] / s;
}
// ---------------------------------------------------------------------------------------------
/* Współczynniki i stan siedzą w rejestrach FPU przez cały przebieg. */
void SpectrumBp_Run( SpectrumBp_t * q, const float * in, float * out, int cnt ) {
	const float b0 = q->b0, a1 = q->a1, a2 = q->a2;
	float z1 = q->z1, z2 = q->z2;

	for ( int i = 0; i < cnt; i++ ) {
		float t = b0 * in[ i ];
		float y = t + z1;

		z1 = z2 - a1 * y;
		z2 = -( t + a2 * y );          /* b2 = -b0 */
		out[ i ] = y;
	}
	/* cisza: stany zanikają wykładniczo i trafiłyby w zakres liczb zdenormalizowanych */
	if ( fabsf( z1 ) < TINY ) z1 = 0.0f;
	if ( fabsf( z2 ) < TINY ) z2 = 0.0f;
	q->z1 = z1;
	q->z2 = z2;
}
// ---------------------------------------------------------------------------------------------
void SpectrumBq_Run( SpectrumBq_t * q, const float * in, float * out, int cnt ) {
	const float b0 = q->b0, b1 = q->b1, b2 = q->b2, a1 = q->a1, a2 = q->a2;
	float z1 = q->z1, z2 = q->z2;

	for ( int i = 0; i < cnt; i++ ) {
		float x = in[ i ];
		float y = b0 * x + z1;

		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		out[ i ] = y;
	}
	if ( fabsf( z1 ) < TINY ) z1 = 0.0f;
	if ( fabsf( z2 ) < TINY ) z2 = 0.0f;
	q->z1 = z1;
	q->z2 = z2;
}
// ---------------------------------------------------------------------------------------------
/* Jedna operacja w pętli zależności: e = e*beta + (alpha*y)*y. */
float SpectrumEnv_Run( float e, float alpha, float beta, const float * y, int cnt ) {
	for ( int i = 0; i < cnt; i++ ) e = e * beta + ( alpha * y[ i ] ) * y[ i ];
	if ( e < TINY ) e = 0.0f;
	return e;
}
