/*
 * dsp_manage.cpp - zarządca modułów DSP (kod niezależny od platformy).
 *
 * Łączy tor audio (audio_manage.h) z analizatorem widma (DSP/spectrum.h):
 *  - tap (wątek wyjścia audio) kopiuje ramki stereo do kolejki, nic więcej,
 *  - DSP_Poll() (loop) przenosi ramki z kolejki do analizatora w porcjach ograniczonych w czasie,
 *    śledzi częstotliwość próbkowania, liczy ramki widma i mierzy ich koszt (us na hop),
 *  - polityka zaległości: najstarsze ramki są nadpisywane, a gdy zaległość przekroczy próg (DSP_QUEUE_*),
 *    wykrywana jest luka: zaległość jest porzucana i analizator resetowany (SPECTRUM_Resync).
 * Analizator (DSP/) jest jednowątkowy i nie zna wątku audio. Nic z USB, I2S ani Arduino.
 */
#include <stdint.h>

#include "dsp_manage.h"
#include "audio_manage.h"
#include "AUDIO/audio_config.h"
#include "DSP/dsp_config.h"
#include "DSP/spectrum.h"

static_assert( DSP_SPECTRUM_MAX_BANDS == SPECTRUM_MAX_BANDS, "DSP_SPECTRUM_MAX_BANDS musi byc rowne SPECTRUM_MAX_BANDS" );

/* Widmo: liczone w loop (DSP_Poll), tylko gdy włączone. Gdy wyłączone, tap nic nie kopiuje. */
static volatile bool s_spectrumOn = false;
static uint32_t s_spectrumSeq = 0, s_spectrumReadSeq = 0;
static uint32_t s_spectrumRate = 0;          /* częstotliwość, dla której skonfigurowano widmo */
static uint32_t s_fftUs = 0;                 /* czas silnika widma na jeden hop próbek (us), średnia z okna */
static uint32_t s_fftAccUs = 0;              /* okno uśredniania: suma czasów SPECTRUM_Process ... */
static uint32_t s_fftAccN = 0;               /* ... i suma próbek audio, które objęły te wywołania */

/* Kolejka tap -> loop: bufor kołowy ramek stereo.
 * Zapisuje tylko wątek audio (s_qWr), czyta tylko loop (s_qRd). Zapis nigdy nie czeka: gdy loop nie nadąża,
 * najstarsze ramki są nadpisywane. Loop kopiuje porcję, a potem sprawdza, czy zapis jej nie dogonił
 * (jak seqlock); jeśli tak, porzuca całą zaległość (luka) i resetuje analizator. */
typedef struct {
	int16_t l, r;
} QFrame_t;

#define QUEUE_MASK          ( DSP_QUEUE_FRAMES - 1 )
#define QUEUE_SAFE          ( DSP_QUEUE_FRAMES - DSP_QUEUE_GUARD_FRAMES )   /* zaległość powyżej progu = luka */
#define QUEUE_MAX_PER_POLL  ( DSP_POLL_MAX_HOPS * SPECTRUM_HOP )
#define PIECE_FRAMES        AUDIO_BLOCK_FRAMES

static_assert( ( DSP_QUEUE_FRAMES & ( DSP_QUEUE_FRAMES - 1 ) ) == 0, "DSP_QUEUE_FRAMES musi byc potega dwojki" );
static_assert( DSP_QUEUE_GUARD_FRAMES >= AUDIO_BLOCK_FRAMES, "DSP_QUEUE_GUARD_FRAMES musi byc >= AUDIO_BLOCK_FRAMES" );
static_assert( DSP_POLL_MAX_HOPS >= 1, "DSP_POLL_MAX_HOPS musi byc >= 1" );
static_assert( QUEUE_SAFE >= QUEUE_MAX_PER_POLL, "kolejka za mala wobec DSP_POLL_MAX_HOPS" );
static_assert( SPECTRUM_FFT_N >= QUEUE_MAX_PER_POLL, "porcja na jedno DSP_Poll nie miesci sie w buforze analizatora (2 * SPECTRUM_FFT_N)" );

static QFrame_t          s_q[ DSP_QUEUE_FRAMES ];
static volatile uint32_t s_qWr = 0;          /* licznik zapisanych ramek (zapis: wątek audio) */
static uint32_t          s_qRd = 0;          /* licznik przeniesionych ramek (tylko loop) */
static uint32_t          s_backlogMax = 0;   /* największa zaległość (ramki) od ostatniego odczytu statystyk */
static uint32_t          s_gaps = 0;         /* liczba luk (porzuconych zaległości) od DSP_Init */

// ---------------------------------------------------------------------------------------------
/* Zaczyna nowe okno pomiaru czasu widma (po zmianie liczby pasm, częstotliwości, włączeniu widma). */
static void fftStatReset( void ) {
	s_fftAccUs = 0;
	s_fftAccN = 0;
}
// ---------------------------------------------------------------------------------------------
/* Wołane w wątku wyjścia audio (tap). Tylko kopiuje ramki do kolejki: bez printów, blokowania i liczenia. */
static void dspTap( const int16_t * stereo, uint32_t frames ) {
	uint32_t wr;

	if ( !s_spectrumOn ) return;
	wr = s_qWr;   /* jedyny zapisujący */
	for ( uint32_t i = 0; i < frames; i++ ) {
		QFrame_t * f = &s_q[ ( wr + i ) & QUEUE_MASK ];

		f->l = stereo[ i * 2 ];
		f->r = stereo[ i * 2 + 1 ];
	}
	__atomic_store_n( &s_qWr, wr + frames, __ATOMIC_RELEASE );   /* dane muszą być widoczne przed przesunięciem s_qWr */
}
// ---------------------------------------------------------------------------------------------
/* Luka w strumieniu: porzuca zaległość i resetuje analizator (kolejne ramki są liczone od nowa). */
static void queueGap( uint32_t wr ) {
	s_qRd = wr;
	s_gaps++;
	SPECTRUM_Resync();
	fftStatReset();
}
// ---------------------------------------------------------------------------------------------
/* Przenosi ramki z kolejki do analizatora, najwyżej DSP_POLL_MAX_HOPS hopów na wywołanie (ogranicza czas jednego
 * DSP_Poll; reszta zostaje w kolejce). Do analizatora trafiają tylko porcje sprawdzone po skopiowaniu. */
static void queueDrain( void ) {
	uint32_t wr = __atomic_load_n( &s_qWr, __ATOMIC_ACQUIRE );
	uint32_t avail = wr - s_qRd;
	uint32_t left;
	int16_t  piece[ PIECE_FRAMES * 2 ];

	if ( avail > s_backlogMax ) s_backlogMax = avail;
	if ( avail > QUEUE_SAFE ) {
		queueGap( wr );
		return;
	}

	left = ( avail > QUEUE_MAX_PER_POLL ) ? QUEUE_MAX_PER_POLL : avail;
	while ( left > 0 ) {
		uint32_t n = ( left > PIECE_FRAMES ) ? PIECE_FRAMES : left;
		uint32_t wr2;

		for ( uint32_t i = 0; i < n; i++ ) {
			const QFrame_t * f = &s_q[ ( s_qRd + i ) & QUEUE_MASK ];

			piece[ i * 2 ] = f->l;
			piece[ i * 2 + 1 ] = f->r;
		}
		__atomic_thread_fence( __ATOMIC_ACQUIRE );   /* kopia musi być zakończona przed ponownym odczytem s_qWr */
		wr2 = __atomic_load_n( &s_qWr, __ATOMIC_RELAXED );
		if ( wr2 - s_qRd > QUEUE_SAFE ) {            /* zapis mógł nadpisać skopiowane ramki */
			queueGap( wr2 );
			return;
		}
		SPECTRUM_Feed( piece, n );
		s_qRd += n;
		left -= n;
	}
}
// ---------------------------------------------------------------------------------------------
void DSP_Init( void ) {
	s_spectrumRate = AUDIO_GetSampleRate();

	s_spectrumOn = false;
	s_spectrumSeq = s_spectrumReadSeq = 0;
	s_fftUs = 0;
	s_qWr = 0;
	s_qRd = 0;
	s_backlogMax = 0;
	s_gaps = 0;
	fftStatReset();

	SPECTRUM_Init( s_spectrumRate );
	AUDIO_SetTap( dspTap );   /* na końcu: kolejka i analizator muszą być gotowe, zanim wątek wyjścia zacznie je karmić */
}
// ---------------------------------------------------------------------------------------------
void DSP_Poll( void ) {
	uint32_t rate = AUDIO_GetSampleRate();

	/* widmo podąża za rzeczywistą częstotliwością wyjścia (SPECTRUM_* wołamy tylko z loop) */
	if ( rate != s_spectrumRate ) {
		s_spectrumRate = rate;
		SPECTRUM_SetRate( s_spectrumRate );
		s_qRd = __atomic_load_n( &s_qWr, __ATOMIC_ACQUIRE );   /* ramki z poprzedniej częstotliwości są bezużyteczne */
		fftStatReset();
	}

	if ( s_spectrumOn ) {
		uint32_t t0;

		queueDrain();
		t0 = AUDIO_TimeUs();
		if ( SPECTRUM_Process() ) {
			/* Czas jednego wywołania zależy od liczby przekazanych próbek (silnik IIR liczy wszystkie próbki od ostatniego
			 * wywołania, a okres loop() zmienia się z zapełnieniem ekranu), więc uśredniamy w oknie ok. 0,5 s
			 * i normalizujemy do jednego hopa: us na SPECTRUM_HOP próbek. Budżet = hop / fs (np. 21,3 ms @48 kHz). */
			s_fftAccUs += AUDIO_TimeUs() - t0;
			s_fftAccN  += SPECTRUM_GetLastBlock();
			s_spectrumSeq++;
			if ( s_fftAccN >= s_spectrumRate / 2 ) {
				s_fftUs = (uint32_t)( (uint64_t)s_fftAccUs * SPECTRUM_HOP / s_fftAccN );
				fftStatReset();
			}
		}
	}
}
// ---------------------------------------------------------------------------------------------
void DSP_SpectrumEnable( int on ) {
	if ( on && !s_spectrumOn ) {
		SPECTRUM_Reset();
		fftStatReset();
		s_spectrumReadSeq = s_spectrumSeq;
		s_qRd = __atomic_load_n( &s_qWr, __ATOMIC_ACQUIRE );   /* analiza zaczyna od ramek, które przyjdą po włączeniu */
	}
	s_spectrumOn = ( on != 0 );
}
// ---------------------------------------------------------------------------------------------
void DSP_SpectrumSetBands( int bands ) {
	SPECTRUM_SetBands( bands );
	fftStatReset();
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumGetBands( void ) {
	return SPECTRUM_GetBands();
}
// ---------------------------------------------------------------------------------------------
void DSP_SpectrumSetChannels( int channels ) {
	SPECTRUM_SetChannels( channels );
	fftStatReset();
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumGetChannels( void ) {
	return SPECTRUM_GetChannels();
}
// ---------------------------------------------------------------------------------------------
void DSP_SpectrumConfigure( int channels, int bands ) {
	SPECTRUM_Configure( channels, bands );
	fftStatReset();
}
// ---------------------------------------------------------------------------------------------
int DSP_GetBandPower( DSP_Bands_t * out ) {
	if ( s_spectrumSeq == s_spectrumReadSeq ) return 0;
	s_spectrumReadSeq = s_spectrumSeq;
	out->channels = (uint8_t)SPECTRUM_GetChannels();
	out->bands = (uint8_t)SPECTRUM_GetBands();
	SPECTRUM_GetPower( out->power );
	return 1;
}
// ---------------------------------------------------------------------------------------------
void DSP_SpectrumSetFftWindow( int n ) {
	SPECTRUM_SetFftWindow( n );
	fftStatReset();
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumGetFftWindow( void ) {
	return SPECTRUM_GetFftWindow();
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumFftWindowMin( void ) {
	return ( SPECTRUM_FFT_WIN_MIN < SPECTRUM_FFT_N ) ? SPECTRUM_FFT_WIN_MIN : SPECTRUM_FFT_N;
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumFftWindowMax( void ) {
	return SPECTRUM_FFT_N;
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumGetEngine( void ) {
	return SPECTRUM_GetEngine();
}
// ---------------------------------------------------------------------------------------------
void DSP_SpectrumSetEngine( int e ) {
	SPECTRUM_SetEngine( e );
}
// ---------------------------------------------------------------------------------------------
int DSP_SpectrumEngineCount( void ) {
	return SPECTRUM_ENGINE_COUNT;
}
// ---------------------------------------------------------------------------------------------
const char * DSP_SpectrumEngineName( int e ) {
	return SPECTRUM_EngineName( e );
}
// ---------------------------------------------------------------------------------------------
uint32_t DSP_SpectrumGetUs( void ) {
	return s_fftUs;
}
// ---------------------------------------------------------------------------------------------
void DSP_GetStats( DSP_Stats_t * out, int reset_minmax ) {
	out->backlog_max = s_backlogMax;
	out->backlog_limit = QUEUE_SAFE;
	out->gaps = s_gaps;
	if ( reset_minmax ) s_backlogMax = 0;
}
