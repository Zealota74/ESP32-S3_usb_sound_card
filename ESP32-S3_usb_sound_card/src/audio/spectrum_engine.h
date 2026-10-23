/*
 * spectrum_engine.h - wewnętrzny kontrakt między audio_spectrum.cpp a silnikami analizy pasm.
 * Nie dołączać poza modułem widma.
 *
 * Silnik dostaje zakres próbek z bufora kołowego i zwraca moc każdego pasma w skali:
 * sinus pełnej skali w środku pasma = 1.0 (0 dB). Dalsza obróbka (dB, słupki, opadanie,
 * kreska szczytu) jest wspólna i leży w audio_spectrum.cpp, więc nowy silnik (np. CQT)
 * implementuje wyłącznie SpecEngine_t.
 */
#ifndef SPECTRUM_ENGINE_H
#define SPECTRUM_ENGINE_H

#include <stdint.h>
#include "audio_config.h"
#include "audio_spectrum.h"

/* Bufor kołowy mono (wspólny): zapis w SPEC_Feed(), odczyt w silnikach. Rozmiar = 2 * N okna FFT. */
#define SPEC_RING_SIZE   ( 2 * AUDIO_SPECTRUM_FFT_N )
#define SPEC_RING_MASK   ( SPEC_RING_SIZE - 1 )

#if ( ( SPEC_RING_SIZE ) & ( ( SPEC_RING_SIZE ) - 1 ) ) != 0 || ( AUDIO_SPECTRUM_FFT_N ) < 64
  #error "AUDIO_SPECTRUM_FFT_N musi być potęgą dwójki (min. 64)"
#endif

extern int16_t g_specRing[ SPEC_RING_SIZE ];
uint32_t       SpecRing_Wr( void );        /* licznik zapisanych próbek (odczyt atomowy, acquire) */

/* Pasmo analizy w hercach; krawędzie -3 dB, f0 = środek geometryczny. */
typedef struct {
	float f_lo, f_hi, f0;
} SpecBand_t;

typedef struct {
	const char * name;
	/* Jednorazowo przy starcie: ciężkie tablice niezależne od pasm i częstotliwości. */
	void ( * init )( void );
	/* Po każdej zmianie fs / liczby pasm / przełączeniu silnika. Zeruje stan silnika. */
	void ( * configure )( uint32_t fs, const SpecBand_t * band, int bars );
	/* Zeruje stan (filtry) bez zmiany konfiguracji. */
	void ( * reset )( void );
	/* Liczy moc pasm dla próbek o numerach [from, to) (liczniki rosnące, modulo 2^32).
	 * Zwraca 1 i wypełnia power[0..bars-1], albo 0 (dane niepewne: producent nadpisał bufor). */
	int  ( * process )( uint32_t from, uint32_t to, float * power );
} SpecEngine_t;

extern const SpecEngine_t g_specEngineFft;
extern const SpecEngine_t g_specEngineIir;
extern const SpecEngine_t g_specEngineIirMr;

#endif /* SPECTRUM_ENGINE_H */
