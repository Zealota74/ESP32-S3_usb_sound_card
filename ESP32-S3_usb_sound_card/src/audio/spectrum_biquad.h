/*
 * spectrum_biquad.h - wspólne klocki DSP dla silników IIR (spectrum_iir.cpp, spectrum_iir_mr.cpp).
 * Wszystko w float, forma transponowana II; współczynniki projektowane w double.
 */
#ifndef SPECTRUM_BIQUAD_H
#define SPECTRUM_BIQUAD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Sekcja pasmowa: b1 = 0, b2 = -b0 (3 współczynniki). */
typedef struct {
	float b0, a1, a2;
	float z1, z2;
} SpecBp_t;

/* Sekcja ogólna (a0 = 1). */
typedef struct {
	float b0, b1, b2, a1, a2;
	float z1, z2;
} SpecBq_t;

/* Pasmowa sekcja o krawędziach -3 dB fl..fh (Hz) przy częstotliwości próbkowania fs, wzmocnienie 1.0 w środku.
 * Transformacja dwuliniowa z pre-warpingiem krawędzi. bwScale mnoży pasmo sekcji (kompensacja kaskady). */
void SpecBp_Design( SpecBp_t * q, double fs, double fl, double fh, double bwScale );

/* Kaskada n identycznych sekcji: bwScale = o ile poszerzyć sekcję, żeby -3 dB całości wypadło na krawędziach,
 * enbwRatio = równoważna szerokość szumowa / pasmo -3 dB całości. n = 1..4. */
void SpecBp_CascadeScale( int n, double * bwScale, double * enbwRatio );

/* Przetwarza 'cnt' próbek: in -> out (te same bufory dozwolone). Stan w sekcji. */
void SpecBp_Run( SpecBp_t * q, const float * in, float * out, int cnt );
void SpecBq_Run( SpecBq_t * q, const float * in, float * out, int cnt );

/* Detektor energii: e = beta*e + alpha*y^2 po 'cnt' próbkach; beta = 1 - alpha. Zwraca nowe e. */
float SpecEnv_Run( float e, float alpha, float beta, const float * y, int cnt );

#ifdef __cplusplus
}
#endif

#endif /* SPECTRUM_BIQUAD_H */
