/*
 * usb_soundcard.cpp - źródło audio USB (ESP32-S3 + USBAudioCard).
 * Reszta projektu widzi tylko obiekt AUDIO_SRC_USB (na końcu pliku).
 */
#include <Arduino.h>
#include "USB.h"
#include "USBAudioCard.h"
#include "USBHIDConsumerControl.h"
#include <math.h>
#include <string.h>

#include "usb_soundcard.h"
#include "audio_config.h"

// ============================================================================================
/* Zdarzenia hosta (głośność / mute). Działa w tasku pętli zdarzeń ESP-IDF. */
#ifndef USB_DEBUG_EVENTS
#define USB_DEBUG_EVENTS 1     /* 1 = wypisuj zdarzenia głośności/mute od hosta na Serial */
#endif

static USBAudioCard s_uac( AUDIO_SAMPLE_RATE, UAC_BPS_16, UAC_SPK_STEREO, UAC_MIC_NONE );
static USBHIDConsumerControl s_consumer;
static AUDIO_PushFn s_push = NULL;

static volatile uint32_t s_packets = 0;
static volatile uint32_t s_bytes = 0;
static volatile uint32_t s_shortWrites = 0;
static volatile uint32_t s_lastPacketMs = 0;
static volatile uint32_t s_hostCtrl = 0;
static volatile uint32_t s_hostRate = AUDIO_SAMPLE_RATE_DEFAULT;   /* częstotliwość wybrana przez hosta */

/* Lista częstotliwości ogłaszanych hostowi jest w USBAudioCard.cpp (USB_UAC1_RATES): ta kopia biblioteki
 * w projekcie dodaje wiele częstotliwości do deskryptora UAC1. Lista musi zgadzać się z AUDIO_SAMPLE_RATE_LIST. */

#if USB_DEBUG_EVENTS
/* Kolejka zdarzeń do wypisania: callback tylko zapisuje wpis (bez printf!),
 * a USB_DebugPoll() wypisuje je z loop(). Producent: task zdarzeń, konsument: loop. */
#define DBG_RING_SIZE  64      /* potęga dwójki */
typedef struct {
	int8_t kind;
	int8_t chan;
	int8_t val;
} DbgEvt_t;
static DbgEvt_t s_dbgRing[ DBG_RING_SIZE ];
static volatile uint32_t s_dbgHead = 0, s_dbgTail = 0;
static volatile uint32_t s_dbgDropped = 0;
#endif

#ifndef USB_DEBUG_PRINT
#define USB_DEBUG_PRINT 0      /* 1 = wypisuj każde zdarzenie osobną linią; 0 = tylko zbieraj do tabeli (komenda 'd') */
#endif

#if USB_DEBUG_EVENTS
/* Tabela głośności mastera: kolejne wartości dB od hosta oraz zdarzenia mute.
 * Zrzut jedną linią komendą 'd' z monitora szeregowego (łatwo skopiować). */
#define DBG_TAB_SIZE   200
#define DBG_TAB_MUTE   1000
#define DBG_TAB_UNMUTE 1001
static int16_t s_dbgTab[ DBG_TAB_SIZE ];
static int s_dbgTabN = 0;
#endif
// ============================================================================================

// ============================================================================================
/* Wywoływane w kontekście TinyUSB. Bez printów i bez blokowania! */
static void usbRxCallback( void * data, uint16_t len ) {
	uint32_t written;

	if ( s_push == NULL ) return;

	written = s_push( (const uint8_t*)data, len );

	s_packets++;
	s_bytes += written;
	if ( written != len ) s_shortWrites++;
	s_lastPacketMs = millis();
}
// ---------------------------------------------------------------------------------------------
/* Callback zdarzeń hosta. Działa w tasku pętli zdarzeń ESP-IDF, więc musi być
 * bardzo szybki: żadnych printf ani blokowania (wolny callback zapycha kolejkę
 * zdarzeń, a przepełnienie potrafi zresetować urządzenie). */
static void uacEventCb( void * arg, esp_event_base_t base, int32_t id, void * data ) {
	(void)arg;
	(void)base;

	if ( id == ARDUINO_USB_AUDIO_CARD_SAMPLE_RATE_EVENT ) { /* nie zalicza się do licznika głośności/mute */
		arduino_usb_audio_card_event_data_t * r = (arduino_usb_audio_card_event_data_t*)data;
		if ( r != NULL ) s_hostRate = (uint32_t)r->sample_rate.rate;
		return;
	}
	s_hostCtrl++;

#if USB_DEBUG_EVENTS
	{
		arduino_usb_audio_card_event_data_t * d = (arduino_usb_audio_card_event_data_t*)data;
		uint32_t head = s_dbgHead;

		if ( d != NULL ) {
			if ( head - s_dbgTail >= DBG_RING_SIZE ) {
				s_dbgDropped++;
			} else {
				DbgEvt_t * e = &s_dbgRing[ head & ( DBG_RING_SIZE - 1 ) ];
				if ( id == ARDUINO_USB_AUDIO_CARD_VOLUME_EVENT ) {
					e->kind = 0;
					e->chan = (int8_t)d->volume.channel;
					e->val = d->volume.db;
				} else {
					e->kind = 1;
					e->chan = (int8_t)d->mute.channel;
					e->val = d->mute.muted ? 1 : 0;
				}
				s_dbgHead = head + 1;
			}
		}
	}
#endif
}
// ---------------------------------------------------------------------------------------------
#if USB_DEBUG_EVENTS
static void dbgTabAdd( int16_t v ) {
	if ( s_dbgTabN >= DBG_TAB_SIZE ) { /* pełna: wyrzuć najstarszy wpis */
		memmove( &s_dbgTab[ 0 ], &s_dbgTab[ 1 ], ( DBG_TAB_SIZE - 1 ) * sizeof( s_dbgTab[ 0 ] ) );
		s_dbgTabN = DBG_TAB_SIZE - 1;
	}
	s_dbgTab[ s_dbgTabN++ ] = v;
}
#endif
// ---------------------------------------------------------------------------------------------
void USB_DebugPoll( void ) {
#if USB_DEBUG_EVENTS
	int n;
	for ( n = 0; n < 8 && s_dbgTail != s_dbgHead; n++ ) { /* maks. 8 wpisów na wywołanie */
		DbgEvt_t e = s_dbgRing[ s_dbgTail & ( DBG_RING_SIZE - 1 ) ];
		s_dbgTail = s_dbgTail + 1;

		if ( e.chan == 0 ) { /* tabela tylko dla kanału master */
			if ( e.kind == 0 )
				dbgTabAdd( (int16_t)e.val );
			else
				dbgTabAdd( e.val ? DBG_TAB_MUTE : DBG_TAB_UNMUTE );
		}
#if USB_DEBUG_PRINT
    {
      static int lastMasterVol = 1000, lastMasterMute = -1;
      if (e.kind == 0) {
        if (e.chan == 0) lastMasterVol = e.val;
        if (e.chan == 0 || e.val != lastMasterVol)          /* L/R pokazuj tylko, gdy różnią się od master */
          Serial.printf("UAC EVT volume: chan=%d db=%d\n", (int)e.chan, (int)e.val);
      } else {
        if (e.chan == 0) lastMasterMute = e.val;
        if (e.chan == 0 || e.val != lastMasterMute)
          Serial.printf("UAC EVT mute: chan=%d muted=%d\n", (int)e.chan, (int)e.val);
      }
    }
#endif
	}
	if ( s_dbgDropped != 0 ) {
		Serial.printf( "UAC EVT dropped: %lu\n", (unsigned long)s_dbgDropped );
		s_dbgDropped = 0;
	}
#endif
}
// ---------------------------------------------------------------------------------------------
void USB_DebugDump( void ) {
#if USB_DEBUG_EVENTS
	int i;
	Serial.printf( "VOLTAB (%d):", s_dbgTabN );
	for ( i = 0; i < s_dbgTabN; i++ ) {
		if ( s_dbgTab[ i ] == DBG_TAB_MUTE )
			Serial.print( " MUTE" );
		else if ( s_dbgTab[ i ] == DBG_TAB_UNMUTE )
			Serial.print( " UNMUTE" );
		else
			Serial.printf( " %d", (int)s_dbgTab[ i ] );
	}
	Serial.println();
#endif
}
// ---------------------------------------------------------------------------------------------
void USB_DebugClear( void ) {
#if USB_DEBUG_EVENTS
	s_dbgTabN = 0;
	Serial.println( "VOLTAB cleared" );
#endif
}
// ---------------------------------------------------------------------------------------------
int USB_Init( AUDIO_PushFn push, int8_t initial_vol_db ) {
	int ok;

	s_push = push;

	USB.VID( 0x303A );
	USB.PID( 0x4001 );

	s_uac.onData( usbRxCallback );
	s_uac.begin();
	s_consumer.begin(); /* drugi interfejs: HID Consumer Control (klawisze głośności) */

	/* Początkowa głośność ustawiana lokalnie, zanim host zobaczy urządzenie. */
	s_uac.volume( UAC_CHAN_MASTER, initial_vol_db );
	ok = ( s_uac.volume( UAC_CHAN_MASTER ) == initial_vol_db ) ? 1 : 0;

	s_uac.onEvent( ARDUINO_USB_AUDIO_CARD_VOLUME_EVENT, uacEventCb );
	s_uac.onEvent( ARDUINO_USB_AUDIO_CARD_MUTE_EVENT, uacEventCb );
	s_uac.onEvent( ARDUINO_USB_AUDIO_CARD_SAMPLE_RATE_EVENT, uacEventCb );
	delay( 30 ); /* ewentualne zdarzenia z lokalnego ustawienia zdążą się wyczyścić */
	s_hostCtrl = 0; /* od teraz liczymy tylko zmiany od hosta */

	USB.begin();
	return ok;
}
// ---------------------------------------------------------------------------------------------
uint32_t USB_GetHostCtrlCount( void ) {
	return s_hostCtrl;
}
// ---------------------------------------------------------------------------------------------
uint32_t USB_GetSampleRate( void ) {
	return s_hostRate;
}
// ---------------------------------------------------------------------------------------------
void USB_ApplyVolume( uint8_t * data, uint32_t bytes ) {
	s_uac.applyVolume( data, (uint16_t)bytes );
}
// ---------------------------------------------------------------------------------------------
void USB_GetStats( USB_Stats_t * out ) {
	out->packets = s_packets;
	out->bytes = s_bytes;
	out->short_writes = s_shortWrites;
	out->last_packet_ms = s_lastPacketMs;
}
// ---------------------------------------------------------------------------------------------
uint32_t USB_GetPacketAgeMs( void ) {
	return millis() - s_lastPacketMs;
}
/* ---------- HID: klawisze multimedialne ----------
 * USB_HidVolumeStep() tylko dopisuje kroki do licznika (nic nie blokuje),
 * a USB_HidPoll() wysyła je do hosta po kilka na przebieg loop(), więc
 * nadwyżka przy szybkim kręceniu nie przepada, tylko jest wysyłana później.
 * Oba wywołania pochodzą z tego samego wątku (loop), więc licznik nie wymaga atomowości.
 */
#ifndef HID_STEPS_PER_POLL
#define HID_STEPS_PER_POLL  2      /* maks. klawiszy wysłanych na jedno USB_HidPoll() */
#endif
#ifndef HID_PENDING_MAX
#define HID_PENDING_MAX     50     /* limit zaległych kroków (zabezpieczenie) */
#endif

static int32_t s_hidPending = 0;
// ---------------------------------------------------------------------------------------------
void USB_HidVolumeStep( int32_t steps ) {
	s_hidPending += steps;
	if ( s_hidPending > HID_PENDING_MAX ) s_hidPending = HID_PENDING_MAX;
	if ( s_hidPending < -HID_PENDING_MAX ) s_hidPending = -HID_PENDING_MAX;
}
// ---------------------------------------------------------------------------------------------
void USB_HidPoll( void ) {
	int n;

	for ( n = 0; n < HID_STEPS_PER_POLL && s_hidPending != 0; n++ ) {
		uint16_t key = ( s_hidPending > 0 ) ? CONSUMER_CONTROL_VOLUME_INCREMENT : CONSUMER_CONTROL_VOLUME_DECREMENT;
		s_hidPending += ( s_hidPending > 0 ) ? -1 : 1;
		s_consumer.press( key );
		s_consumer.release();
	}
}
// ---------------------------------------------------------------------------------------------
int32_t USB_HidPending( void ) {
	return s_hidPending;
}
// ---------------------------------------------------------------------------------------------
void USB_HidMuteToggle( void ) {
	s_consumer.press( CONSUMER_CONTROL_MUTE );
	s_consumer.release();
}
// ---------------------------------------------------------------------------------------------
/* Kody z HID Usage Tables, strona Consumer (0x0C). Własne stałe, żeby nie zależeć od nazw w bibliotece. */
#define HID_USAGE_SCAN_NEXT_TRACK   0x00B5
#define HID_USAGE_SCAN_PREV_TRACK   0x00B6

void USB_HidMediaKey( AUDIO_MediaKey_t key ) {
	uint16_t usage;

	switch ( key ) {
		case AUDIO_MEDIA_NEXT: usage = HID_USAGE_SCAN_NEXT_TRACK; break;
		case AUDIO_MEDIA_PREV: usage = HID_USAGE_SCAN_PREV_TRACK; break;
		default: return;
	}
	s_consumer.press( usage );
	s_consumer.release();
}

/* --------------------------- Odczyt głośności ustawionej przez hosta ------------------------- */
float USB_GetVolumeDb( void ) {
	return (float)s_uac.volume( UAC_CHAN_MASTER );
}
// ---------------------------------------------------------------------------------------------
int USB_IsMuted( void ) {
	return s_uac.mute( UAC_CHAN_MASTER ) ? 1 : 0;
}
// ============================================================================================

// ====================================== Volume calibration ==================================
static int8_t s_curve[ VOL_CAL_POINTS ];
static int s_curveInit = 0;

static void curveDefault( int8_t * c ) {
	for ( int k = 0; k < VOL_CAL_POINTS; k++ ) {
		c[ k ] = (int8_t)( VOL_HOST_MIN_DB
				+ ( ( VOL_HOST_MAX_DB - VOL_HOST_MIN_DB ) * k + ( VOL_CAL_POINTS - 1 ) / 2 ) / ( VOL_CAL_POINTS - 1 ) );
	}
}
// ----------------------------------------------------------------------------------------------------
static void curveEnsure( void ) {
	if ( !s_curveInit ) {
		curveDefault( s_curve );
		s_curveInit = 1;
	}
}
// ----------------------------------------------------------------------------------------------------
float VOL_HostToGain( float host_db ) {
	curveEnsure();
	if ( isnan( host_db ) || host_db <= (float)s_curve[ 0 ] || host_db <= (float)VOL_HOST_MIN_DB ) return 0.0f;
	if ( host_db >= 0.0f ) return 1.0f;
	return powf( 10.0f, host_db / 20.0f );
}
// ==============================================================================================================

// ========================================== Volume levels =====================================================
/* Tabela poziomów (poz. 0 = MUTE, ostatnia = 0 dB). Wartości z pomiaru Windows:
 * dB odebrane od hosta i odpowiadający im procent suwaka (środek przedziału). */
static const int8_t s_lvlDb[ VOL_LEVELS ] = {
	-50, -43, -39, -35, -32, -30, -28, -26, -25, -23, -22, -21, -19, -18, -17, -16,
	-15, -14, -13, -12, -11, -10,  -9,  -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,   0
};
static const uint8_t s_lvlPct[ VOL_LEVELS ] = {
	  0,  13,  21,  29,  35,  39,  43,  47,  49,  53,  55,  57,  61,  63,  65,  67,
	 69,  71,  73,  75,  77,  79,  81,  83,  85,  87,  89,  91,  93,  95,  97, 100
};
// ----------------------------------------------------------------------------------------------------
int VOL_LevelFromHost( float host_db ) {
	int db = (int)lrintf( host_db );
	int k, best = 0, bestDist = 1000;

	for ( k = 0; k < VOL_LEVELS; k++ ) {
		int d = db - s_lvlDb[ k ];
		if ( d < 0 ) d = -d;
		if ( d < bestDist ) { /* przy remisie zostaje niższy poziom */
			bestDist = d;
			best = k;
		}
	}
	return best;
}
// ----------------------------------------------------------------------------------------------------
int VOL_LevelPercent( int level ) {
	if ( level < 0 ) level = 0;
	if ( level > VOL_LEVELS - 1 ) level = VOL_LEVELS - 1;
	return s_lvlPct[ level ];
}
// ----------------------------------------------------------------------------------------------------
/* Stan regulatora. Wszystko z jednego wątku (loop), więc bez atomowości.
 * s_volTarget: -1 = bezczynny, inaczej poziom docelowy. */
static int s_volTarget = -1;
static int s_volLastDir = 0;           /* kierunek ostatniego wysłanego klawisza */
static bool s_volWaiting = false;      /* czekamy na reakcję hosta */
static bool s_volUnmuteSent = false;   /* klawisz mute wysłany tylko raz na cel */
static int s_volRefDb = 0;             /* dB przy ostatniej zmianie (do wykrycia braku reakcji) */
static int s_volNoChange = 0;          /* klawisze wysłane od ostatniej zmiany dB */
static uint32_t s_volAckCtrl = 0;
static uint32_t s_volSentMs = 0;

void USB_VolStep( int32_t ticks ) {
	int base = ( s_volTarget >= 0 ) ? s_volTarget : VOL_LevelFromHost( USB_GetVolumeDb() );
	int t = base + (int)ticks;

	if ( t < 0 ) t = 0;
	if ( t > VOL_LEVELS - 1 ) t = VOL_LEVELS - 1;
	s_volTarget = t;
	s_volLastDir = 0;
	s_volUnmuteSent = false;
	s_volRefDb = 1000;                 /* wymusza reset licznika przy pierwszym klawiszu */
	s_volNoChange = 0;
}
// ----------------------------------------------------------------------------------------------------
void USB_VolPoll( void ) {
	uint32_t now = millis();
	int hostDb, diff, dir;

	if ( s_volTarget < 0 ) return;

	if ( s_volWaiting ) { /* czekamy, aż host zareaguje (zdarzenie) albo minie limit czasu */
		if ( s_hostCtrl == s_volAckCtrl && ( now - s_volSentMs ) < VOL_ACK_TIMEOUT_MS ) return;
		s_volWaiting = false;
	}

	hostDb = (int)lrintf( USB_GetVolumeDb() );
	diff = (int)s_lvlDb[ s_volTarget ] - hostDb;
	dir = ( diff > 0 ) - ( diff < 0 );

	/* koniec: cel osiągnięty, albo host przeskoczył cel (nie cofamy się, żeby nie oscylować) */
	if ( dir == 0 || ( s_volLastDir != 0 && dir != s_volLastDir ) ) {
		s_volTarget = -1;
		s_volLastDir = 0;
		return;
	}

	/* wyciszone przyciskiem (suwak nie na dole): najpierw jednokrotnie wyłącz mute */
	if ( !s_volUnmuteSent && USB_IsMuted() && hostDb > VOL_HOST_MIN_DB ) {
		s_volUnmuteSent = true;
		s_volAckCtrl = s_hostCtrl;
		s_volSentMs = now;
		s_volWaiting = true;
		s_consumer.press( CONSUMER_CONTROL_MUTE );
		s_consumer.release();
		return;
	}

	/* host nie reaguje (np. strumień wstrzymany): przerwij, zamiast wysyłać klawisze w nieskończoność */
	if ( hostDb != s_volRefDb ) {
		s_volRefDb = hostDb;
		s_volNoChange = 0;
	} else if ( ++s_volNoChange > VOL_MAX_NOCHANGE ) {
		s_volTarget = -1;
		s_volLastDir = 0;
		return;
	}

	s_volAckCtrl = s_hostCtrl;
	s_volSentMs = now;
	s_volWaiting = true;
	s_volLastDir = dir;
	s_consumer.press( dir > 0 ? CONSUMER_CONTROL_VOLUME_INCREMENT : CONSUMER_CONTROL_VOLUME_DECREMENT );
	s_consumer.release();
}
// ----------------------------------------------------------------------------------------------------
int USB_VolLevel( void ) {
	return ( s_volTarget >= 0 ) ? s_volTarget : VOL_LevelFromHost( USB_GetVolumeDb() );
}
// ----------------------------------------------------------------------------------------------------
int USB_VolBusy( void ) {
	return ( s_volTarget >= 0 ) ? 1 : 0;
}
// ==============================================================================================================

// ==============================================================================================================
/* ====================== Adapter źródła audio (interfejs z audio_source.h) ====================== */
static int srcInit( AUDIO_PushFn push, int8_t initial_vol_db ) {
	return USB_Init( push, initial_vol_db );
}
// ----------------------------------------------------------------------------------------------------
static void srcPoll( void ) {
	USB_VolPoll();     /* dochodzi do poziomu docelowego klawiszami HID */
	USB_DebugPoll();   /* log zdarzeń volume/mute od hosta (diagnostyka) */
}
// ----------------------------------------------------------------------------------------------------
static void srcGetVolume( AUDIO_Volume_t * v ) {
	float host = USB_GetVolumeDb();
	int hostLevel = VOL_LevelFromHost( host );

	v->host_db = host;
	v->gain = VOL_HostToGain( host );
	v->level = USB_VolLevel();             /* w trakcie ruchu: poziom docelowy (od razu widać tick) */
	v->level_max = VOL_LEVELS - 1;
	v->host_level = hostLevel;
	v->host_percent = VOL_LevelPercent( hostLevel );
	v->busy = USB_VolBusy();
	v->mute = v->busy ? ( v->level == 0 ) : USB_IsMuted();
	v->ctrl_count = USB_GetHostCtrlCount();
}
// ----------------------------------------------------------------------------------------------------
static void srcGetStats( AUDIO_SrcStats_t * s ) {
	USB_Stats_t u;

	USB_GetStats( &u );
	s->packets = u.packets;
	s->bytes = u.bytes;
	s->short_writes = u.short_writes;
	s->age_ms = USB_GetPacketAgeMs();
}
// ----------------------------------------------------------------------------------------------------
const AUDIO_Source_t AUDIO_SRC_USB = {
	"USB",
	srcInit,
	srcPoll,
	srcGetVolume,
	USB_VolStep,
	USB_HidMuteToggle,
	USB_HidMediaKey,
	srcGetStats,
	USB_GetSampleRate
};
// ==============================================================================================================
