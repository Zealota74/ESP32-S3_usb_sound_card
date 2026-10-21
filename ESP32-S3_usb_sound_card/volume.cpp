/*
 * volume.cpp - mapowanie głośności (kod w stylu C).
 */
#include <math.h>
#include <string.h>
#include "volume.h"

static int8_t s_curve[VOL_CAL_POINTS];
static int    s_curveInit = 0;

static void curveDefault(int8_t *c)
{
  int k;
  for (k = 0; k < VOL_CAL_POINTS; k++) {
    c[k] = (int8_t)(VOL_HOST_MIN_DB +
                    ((VOL_HOST_MAX_DB - VOL_HOST_MIN_DB) * k + (VOL_CAL_POINTS - 1) / 2) / (VOL_CAL_POINTS - 1));
  }
}

static void curveEnsure(void)
{
  if (!s_curveInit) {
    curveDefault(s_curve);
    s_curveInit = 1;
  }
}

int VOL_CurveIsValid(const int8_t *curve)
{
  int k;
  if (curve == NULL) return 0;
  for (k = 0; k < VOL_CAL_POINTS; k++) {
    if (curve[k] < VOL_HOST_MIN_DB || curve[k] > VOL_HOST_MAX_DB) return 0;
    if (k > 0 && curve[k] < curve[k - 1]) return 0;          /* musi być niemalejąca */
  }
  return curve[0] < curve[VOL_CAL_POINTS - 1];
}

int VOL_SetCurve(const int8_t *curve)
{
  if (curve == NULL) {
    curveDefault(s_curve);
    s_curveInit = 1;
    return 1;
  }
  if (!VOL_CurveIsValid(curve)) return 0;
  memcpy(s_curve, curve, sizeof(s_curve));
  s_curveInit = 1;
  return 1;
}

const int8_t *VOL_GetCurve(void)
{
  curveEnsure();
  return s_curve;
}

int VOL_HostToPercent(float host_db)
{
  const int last = VOL_CAL_POINTS - 1;
  int db, lo, hi, k;

  curveEnsure();
  db = (int)lrintf(host_db);
  if (db <= s_curve[0])    return 0;
  if (db >= s_curve[last]) return 100;

  /* Kilka pozycji suwaka może dawać to samo dB (krok 1 dB): bierzemy środek. */
  for (lo = 0; lo <= last && s_curve[lo] < db; lo++) { }
  for (hi = lo; hi < last && s_curve[hi + 1] == db; hi++) { }
  if (s_curve[lo] == db) {
    return (lo + hi) * VOL_CAL_STEP_PCT / 2;
  }

  /* dB pomiędzy punktami krzywej (np. suwak przesunięty myszą): interpolacja. */
  k = lo - 1;
  return (int)lrintf(((float)k + (float)(db - s_curve[k]) / (float)(s_curve[lo] - s_curve[k]))
                     * (float)VOL_CAL_STEP_PCT);
}

float VOL_HostToGain(float host_db)
{
  curveEnsure();
  if (isnan(host_db) || host_db <= (float)s_curve[0] || host_db <= (float)VOL_HOST_MIN_DB) return 0.0f;
  if (host_db >= 0.0f) return 1.0f;
  return powf(10.0f, host_db / 20.0f);
}
