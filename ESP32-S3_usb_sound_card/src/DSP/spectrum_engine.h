/*
 * spectrum_engine.h - wewnętrzny kontrakt między spectrum.cpp a silnikami analizy pasm.
 * Nie dołączać poza modułem widma.
 *
 * Silnik dostaje zakres próbek z bufora kołowego i zwraca moc każdego pasma w skali:
 * sinus pełnej skali w środku pasma = 1.0 (0 dB). Dalsza obróbka (dB, słupki, opadanie,
 * kreska szczytu) jest wspólna i leży w spectrum.cpp, więc nowy silnik (np. CQT)
 * implementuje wyłącznie SpectrumEngine_t.
 */
#ifndef SPECTRUM_ENGINE_H
#define SPECTRUM_ENGINE_H

#include <stdint.h>
#include "dsp_config.h"
#include "spectrum.h"

/* Bufor kołowy mono (wspólny): zapis w SPECTRUM_Feed(), odczyt w silnikach. Rozmiar = 2 * N okna FFT. */
#define SPECTRUM_RING_SIZE   ( 2 * SPECTRUM_FFT_N )
#define SPECTRUM_RING_MASK   ( SPECTRUM_RING_SIZE - 1 )

#if ( ( SPECTRUM_RING_SIZE ) & ( ( SPECTRUM_RING_SIZE ) - 1 ) ) != 0 || ( SPECTRUM_FFT_N ) < 64
  #error "SPECTRUM_FFT_N musi być potęgą dwójki (min. 64)"
#endif

extern int16_t g_spectrumRing[ SPECTRUM_RING_SIZE ];
uint32_t       SpectrumRing_Wr( void );        /* licznik zapisanych próbek (odczyt atomowy, acquire) */

/* Pasmo analizy w hercach; krawędzie -3 dB, f0 = środek geometryczny. */
typedef struct {
	float f_lo, f_hi, f0;
} SpectrumBand_t;

typedef struct {
	const char * name;
	/* Jednorazowo przy starcie: ciężkie tablice niezależne od pasm i częstotliwości. */
	void ( * init )( void );
	/* Po każdej zmianie fs / liczby pasm / przełączeniu silnika. Zeruje stan silnika. */
	void ( * configure )( uint32_t fs, const SpectrumBand_t * band, int bars );
	/* Zeruje stan (filtry) bez zmiany konfiguracji. */
	void ( * reset )( void );
	/* Liczy moc pasm dla próbek o numerach [from, to) (liczniki rosnące, modulo 2^32).
	 * Zwraca 1 i wypełnia power[0..bars-1], albo 0 (dane niepewne: producent nadpisał bufor). */
	int  ( * process )( uint32_t from, uint32_t to, float * power );
} SpectrumEngine_t;

extern const SpectrumEngine_t g_spectrumEngineFft;
extern const SpectrumEngine_t g_spectrumEngineIir;
extern const SpectrumEngine_t g_spectrumEngineIirMr;

#endif /* SPECTRUM_ENGINE_H */
