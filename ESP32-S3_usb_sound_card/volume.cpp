/*
 * volume.cpp - mapowanie głośności (kod w stylu C).
 */
#include <math.h>
#include "volume.h"

float VOL_HostToEffDb(float host_db)
{
  if (host_db <= (float)VOL_HOST_MIN_DB) return -INFINITY;
  if (host_db > 0.0f) host_db = 0.0f;
  return host_db * ((float)VOL_EFF_RANGE_DB / (float)(-VOL_HOST_MIN_DB));
}

float VOL_DbToGain(float db)
{
  if (isinf(db) || db <= -200.0f) return 0.0f;
  if (db > 0.0f) db = 0.0f;
  return powf(10.0f, db / 20.0f);
}
