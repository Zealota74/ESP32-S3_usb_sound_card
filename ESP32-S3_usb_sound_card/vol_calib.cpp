/*
 * vol_calib.cpp - pomiar krzywej głośności Windows przez klawisze HID.
 */
#include <Arduino.h>
#include <Preferences.h>

#include "vol_calib.h"
#include "volume.h"
#include "usb_soundcard.h"

#ifndef VCAL_STEP_MS
#define VCAL_STEP_MS     250    /* czas na reakcję Windows po jednym klawiszu */
#endif
#ifndef VCAL_SETTLE_MS
#define VCAL_SETTLE_MS   800    /* czas na ustalenie się 0 % po serii klawiszy w dół */
#endif
#define VCAL_DOWN_STEPS  (VOL_CAL_POINTS + 5)   /* z zapasem, żeby na pewno dojść do 0 % */
#define VCAL_NVS_NS      "volume"
#define VCAL_NVS_KEY     "curve"

typedef enum {
  VCAL_IDLE = 0,
  VCAL_DOWN,        /* wysyłanie klawiszy w dół */
  VCAL_SETTLE,      /* czekanie po zejściu do 0 % */
  VCAL_STEP,        /* jeden klawisz w górę i odczyt dB */
  VCAL_RESTORE,     /* powrót do poprzedniej pozycji */
  VCAL_REMUTE       /* przywrócenie MUTE */
} VcalState_t;

static VcalState_t s_state = VCAL_IDLE;
static int8_t      s_meas[VOL_CAL_POINTS];
static int         s_k = 0;
static int         s_downLeft = 0;
static uint32_t    s_t0 = 0;
static float       s_origDb = 0.0f;
static int         s_origMuted = 0;

static void printCurve(const char *title, const int8_t *c)
{
  int k;
  Serial.printf("%s (dB dla 0,%d..100 %%):", title, VOL_CAL_STEP_PCT);
  for (k = 0; k < VOL_CAL_POINTS; k++) Serial.printf(" %d", (int)c[k]);
  Serial.println();
}

void VCAL_Init(void)
{
  Preferences prefs;
  int8_t c[VOL_CAL_POINTS];

  if (!prefs.begin(VCAL_NVS_NS, true)) return;        /* brak zapisu = krzywa domyślna */
  if (prefs.getBytesLength(VCAL_NVS_KEY) == sizeof(c) &&
      prefs.getBytes(VCAL_NVS_KEY, c, sizeof(c)) == sizeof(c) &&
      VOL_SetCurve(c)) {
    Serial.println("VOL: wczytano krzywą Windows z NVS");
  }
  prefs.end();
}

void VCAL_Start(void)
{
  if (s_state != VCAL_IDLE) return;
  s_origDb = USB_GetVolumeDb();
  s_origMuted = USB_IsMuted();
  s_k = 0;
  s_downLeft = VCAL_DOWN_STEPS;
  s_state = VCAL_DOWN;
  Serial.println("VOL CAL: start (ok. 15 s, nie ruszaj głośności)");
}

static void finish(void)
{
  Preferences prefs;
  int pct, down;

  /* 0 % musi dać minimum, a 100 % maksimum zakresu - inaczej część klawiszy przepadła */
  if (s_meas[0] == VOL_HOST_MIN_DB && s_meas[VOL_CAL_POINTS - 1] == VOL_HOST_MAX_DB &&
      VOL_SetCurve(s_meas)) {
    if (prefs.begin(VCAL_NVS_NS, false)) {
      prefs.putBytes(VCAL_NVS_KEY, s_meas, sizeof(s_meas));
      prefs.end();
    }
    printCurve("VOL CAL OK, zapisano", s_meas);
  } else {
    printCurve("VOL CAL BŁĄD (krzywa odrzucona)", s_meas);
  }

  /* powrót do poprzedniej pozycji: teraz suwak jest na 100 % */
  pct = VOL_HostToPercent(s_origDb);
  down = (100 - pct + VOL_CAL_STEP_PCT / 2) / VOL_CAL_STEP_PCT;
  USB_HidVolumeStep(-down);
  s_state = VCAL_RESTORE;
}

void VCAL_Proc(uint32_t now_ms)
{
  switch (s_state) {
    case VCAL_IDLE:
      break;

    case VCAL_DOWN:
      if (USB_HidPending() != 0) break;
      if (s_downLeft > 0) {
        int n = (s_downLeft > 20) ? 20 : s_downLeft;   /* porcjami (limit kolejki HID) */
        USB_HidVolumeStep(-n);
        s_downLeft -= n;
      } else {
        s_t0 = now_ms;
        s_state = VCAL_SETTLE;
      }
      break;

    case VCAL_SETTLE:
      if (now_ms - s_t0 < VCAL_SETTLE_MS) break;
      s_meas[0] = (int8_t)USB_GetVolumeDb();
      s_k = 1;
      USB_HidVolumeStep(1);
      s_t0 = now_ms;
      s_state = VCAL_STEP;
      break;

    case VCAL_STEP:
      if (USB_HidPending() != 0) { s_t0 = now_ms; break; }
      if (now_ms - s_t0 < VCAL_STEP_MS) break;
      s_meas[s_k++] = (int8_t)USB_GetVolumeDb();
      if (s_k < VOL_CAL_POINTS) {
        USB_HidVolumeStep(1);
        s_t0 = now_ms;
      } else {
        finish();
      }
      break;

    case VCAL_RESTORE:
      if (USB_HidPending() != 0) break;
      if (s_origMuted) {
        s_t0 = now_ms;
        s_state = VCAL_REMUTE;
      } else {
        s_state = VCAL_IDLE;
      }
      break;

    case VCAL_REMUTE:
      if (now_ms - s_t0 < VCAL_STEP_MS) break;
      if (!USB_IsMuted()) USB_HidMuteToggle();
      s_state = VCAL_IDLE;
      break;
  }
}

int VCAL_IsActive(void)
{
  return s_state != VCAL_IDLE;
}

int VCAL_Progress(void)
{
  switch (s_state) {
    case VCAL_IDLE:   return -1;
    case VCAL_DOWN:
    case VCAL_SETTLE: return 0;
    case VCAL_STEP:   return s_k * 100 / VOL_CAL_POINTS;
    default:          return 100;
  }
}

void VCAL_Print(void)
{
  printCurve("VOL krzywa", VOL_GetCurve());
}

void VCAL_Reset(void)
{
  Preferences prefs;

  VOL_SetCurve(NULL);
  if (prefs.begin(VCAL_NVS_NS, false)) {
    prefs.remove(VCAL_NVS_KEY);
    prefs.end();
  }
  printCurve("VOL krzywa domyślna", VOL_GetCurve());
}
