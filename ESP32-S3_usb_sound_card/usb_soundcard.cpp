/*
 * usb_soundcard.cpp - część zależna od platformy (ESP32-S3 + USBAudioCard).
 * Cała reszta projektu widzi tylko funkcje USB_*.
 */
#include <Arduino.h>
#include "USB.h"
#include "USBAudioCard.h"
#include "USBHIDConsumerControl.h"
#include <math.h>
#include <string.h>

#include "usb_soundcard.h"
#include "audio_config.h"

static USBAudioCard s_uac(AUDIO_SAMPLE_RATE, UAC_BPS_16, UAC_SPK_STEREO, UAC_MIC_NONE);
static USBHIDConsumerControl s_consumer;
static FIFO_t *s_rxFifo = NULL;

static volatile uint32_t s_packets = 0;
static volatile uint32_t s_bytes = 0;
static volatile uint32_t s_shortWrites = 0;
static volatile uint32_t s_lastPacketMs = 0;
static volatile uint32_t s_hostCtrl = 0;

/* Wywoływane w kontekście TinyUSB. Bez printów i bez blokowania! */
static void usbRxCallback(void *data, uint16_t len)
{
  uint32_t written;

  if (s_rxFifo == NULL) return;

  written = FIFO_Write(s_rxFifo, (const uint8_t *)data, len);

  s_packets++;
  s_bytes += written;
  if (written != len) s_shortWrites++;
  s_lastPacketMs = millis();
}

/* Zdarzenia hosta (głośność / mute). Działa w tasku pętli zdarzeń ESP-IDF. */
#ifndef USB_DEBUG_EVENTS
#define USB_DEBUG_EVENTS 1     /* 1 = wypisuj zdarzenia głośności/mute od hosta na Serial */
#endif

#if USB_DEBUG_EVENTS
/* Kolejka zdarzeń do wypisania: callback tylko zapisuje wpis (bez printf!),
 * a USB_DebugPoll() wypisuje je z loop(). Producent: task zdarzeń, konsument: loop. */
#define DBG_RING_SIZE  64      /* potęga dwójki */
typedef struct { int8_t kind; int8_t chan; int8_t val; } DbgEvt_t;
static DbgEvt_t s_dbgRing[DBG_RING_SIZE];
static volatile uint32_t s_dbgHead = 0, s_dbgTail = 0;
static volatile uint32_t s_dbgDropped = 0;
#endif

/* Callback zdarzeń hosta. Działa w tasku pętli zdarzeń ESP-IDF, więc musi być
 * bardzo szybki: żadnych printf ani blokowania (wolny callback zapycha kolejkę
 * zdarzeń, a przepełnienie potrafi zresetować urządzenie). */
static void uacEventCb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
  (void)arg; (void)base;
  s_hostCtrl++;

#if USB_DEBUG_EVENTS
  {
    arduino_usb_audio_card_event_data_t *d = (arduino_usb_audio_card_event_data_t *)data;
    uint32_t head = s_dbgHead;

    if (d != NULL) {
      if (head - s_dbgTail >= DBG_RING_SIZE) {
        s_dbgDropped++;
      } else {
        DbgEvt_t *e = &s_dbgRing[head & (DBG_RING_SIZE - 1)];
        if (id == ARDUINO_USB_AUDIO_CARD_VOLUME_EVENT) {
          e->kind = 0; e->chan = (int8_t)d->volume.channel; e->val = d->volume.db;
        } else {
          e->kind = 1; e->chan = (int8_t)d->mute.channel;   e->val = d->mute.muted ? 1 : 0;
        }
        s_dbgHead = head + 1;
      }
    }
  }
#endif
}

#ifndef USB_DEBUG_PRINT
#define USB_DEBUG_PRINT 0      /* 1 = wypisuj każde zdarzenie osobną linią; 0 = tylko zbieraj do tabeli (komenda 'd') */
#endif

#if USB_DEBUG_EVENTS
/* Tabela głośności mastera: kolejne wartości dB od hosta oraz zdarzenia mute.
 * Zrzut jedną linią komendą 'd' z monitora szeregowego (łatwo skopiować). */
#define DBG_TAB_SIZE   200
#define DBG_TAB_MUTE   1000
#define DBG_TAB_UNMUTE 1001
static int16_t s_dbgTab[DBG_TAB_SIZE];
static int     s_dbgTabN = 0;

static void dbgTabAdd(int16_t v)
{
  if (s_dbgTabN >= DBG_TAB_SIZE) {                       /* pełna: wyrzuć najstarszy wpis */
    memmove(&s_dbgTab[0], &s_dbgTab[1], (DBG_TAB_SIZE - 1) * sizeof(s_dbgTab[0]));
    s_dbgTabN = DBG_TAB_SIZE - 1;
  }
  s_dbgTab[s_dbgTabN++] = v;
}
#endif

void USB_DebugPoll(void)
{
#if USB_DEBUG_EVENTS
  int n;
  for (n = 0; n < 8 && s_dbgTail != s_dbgHead; n++) {      /* maks. 8 wpisów na wywołanie */
    DbgEvt_t e = s_dbgRing[s_dbgTail & (DBG_RING_SIZE - 1)];
    s_dbgTail = s_dbgTail + 1;

    if (e.chan == 0) {                                      /* tabela tylko dla kanału master */
      if (e.kind == 0) dbgTabAdd((int16_t)e.val);
      else             dbgTabAdd(e.val ? DBG_TAB_MUTE : DBG_TAB_UNMUTE);
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
  if (s_dbgDropped != 0) {
    Serial.printf("UAC EVT dropped: %lu\n", (unsigned long)s_dbgDropped);
    s_dbgDropped = 0;
  }
#endif
}

void USB_DebugDump(void)
{
#if USB_DEBUG_EVENTS
  int i;
  Serial.printf("VOLTAB (%d):", s_dbgTabN);
  for (i = 0; i < s_dbgTabN; i++) {
    if (s_dbgTab[i] == DBG_TAB_MUTE)        Serial.print(" MUTE");
    else if (s_dbgTab[i] == DBG_TAB_UNMUTE) Serial.print(" UNMUTE");
    else                                    Serial.printf(" %d", (int)s_dbgTab[i]);
  }
  Serial.println();
#endif
}

void USB_DebugClear(void)
{
#if USB_DEBUG_EVENTS
  s_dbgTabN = 0;
  Serial.println("VOLTAB cleared");
#endif
}

int USB_Init(FIFO_t *rx_fifo, int8_t initial_vol_db)
{
  int ok;

  s_rxFifo = rx_fifo;

  USB.VID(0x303A);
  USB.PID(0x4001);

  s_uac.onData(usbRxCallback);
  s_uac.begin();
  s_consumer.begin();      /* drugi interfejs: HID Consumer Control (klawisze głośności) */

  /* Początkowa głośność ustawiana lokalnie, zanim host zobaczy urządzenie. */
  s_uac.volume(UAC_CHAN_MASTER, initial_vol_db);
  ok = (s_uac.volume(UAC_CHAN_MASTER) == initial_vol_db) ? 1 : 0;

  s_uac.onEvent(ARDUINO_USB_AUDIO_CARD_VOLUME_EVENT, uacEventCb);
  s_uac.onEvent(ARDUINO_USB_AUDIO_CARD_MUTE_EVENT, uacEventCb);
  delay(30);               /* ewentualne zdarzenia z lokalnego ustawienia zdążą się wyczyścić */
  s_hostCtrl = 0;          /* od teraz liczymy tylko zmiany od hosta */

  USB.begin();
  return ok;
}

uint32_t USB_GetHostCtrlCount(void)
{
  return s_hostCtrl;
}

void USB_ApplyVolume(uint8_t *data, uint32_t bytes)
{
  s_uac.applyVolume(data, (uint16_t)bytes);
}

void USB_GetStats(USB_Stats_t *out)
{
  out->packets = s_packets;
  out->bytes = s_bytes;
  out->short_writes = s_shortWrites;
  out->last_packet_ms = s_lastPacketMs;
}

uint32_t USB_GetPacketAgeMs(void)
{
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

void USB_HidVolumeStep(int32_t steps)
{
  s_hidPending += steps;
  if (s_hidPending >  HID_PENDING_MAX) s_hidPending =  HID_PENDING_MAX;
  if (s_hidPending < -HID_PENDING_MAX) s_hidPending = -HID_PENDING_MAX;
}

void USB_HidPoll(void)
{
  int n;

  for (n = 0; n < HID_STEPS_PER_POLL && s_hidPending != 0; n++) {
    uint16_t key = (s_hidPending > 0) ? CONSUMER_CONTROL_VOLUME_INCREMENT
                                      : CONSUMER_CONTROL_VOLUME_DECREMENT;
    s_hidPending += (s_hidPending > 0) ? -1 : 1;
    s_consumer.press(key);
    s_consumer.release();
  }
}

int32_t USB_HidPending(void)
{
  return s_hidPending;
}

void USB_HidMuteToggle(void)
{
  s_consumer.press(CONSUMER_CONTROL_MUTE);
  s_consumer.release();
}

/* ---------- Odczyt głośności ustawionej przez hosta ---------- */
float USB_GetVolumeDb(void)
{
  return (float)s_uac.volume(UAC_CHAN_MASTER);
}

int USB_IsMuted(void)
{
  return s_uac.mute(UAC_CHAN_MASTER) ? 1 : 0;
}
