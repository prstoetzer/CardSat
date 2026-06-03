// ===========================================================================
//  rig.cpp  -  Rig factory + shared helpers
// ===========================================================================
#include "rig.h"
#include "civ.h"
#include "yaesu.h"
#include "kenwood.h"

RigMode Rig::modeFromString(const String& s) {
  String u = s; u.toUpperCase();
  if (u.indexOf("FM")  >= 0) return RM_FM;
  if (u.indexOf("USB") >= 0) return RM_USB;
  if (u.indexOf("LSB") >= 0) return RM_LSB;
  if (u.indexOf("CW")  >= 0) return RM_CW;
  if (u.indexOf("AM")  >= 0) return RM_AM;
  if (u.indexOf("FSK") >= 0 || u.indexOf("RTTY") >= 0 ||
      u.indexOf("DATA") >= 0 || u.indexOf("DIG") >= 0) return RM_DATA;
  // Linear transponders are most often operated USB up / USB down.
  return RM_USB;
}

Rig* makeRig(RadioModel model) {
  switch (RADIOS[model].proto) {
    case PROTO_YAESU:   return new YaesuRig(model);
    case PROTO_KENWOOD: return new KenwoodRig(model);
    case PROTO_CIV:
    default:            return new CivRig(model);
  }
}

// Standard 39 EIA CTCSS tones in tenths of Hz, ascending. This exact order is
// shared with Hamlib's ft847_ctcss_list[] and the Kenwood tone list, so the
// index doubles as the Kenwood tone number (index+1) and the row into the
// FT-847 CAT code table. Icom encodes the frequency in BCD instead.
static const uint16_t CTCSS_TENTHS[39] = {
  670, 693, 719, 744, 770, 797, 825, 854, 885, 915,
  948, 974, 1000,1035,1072,1109,1148,1188,1230,1273,
  1318,1365,1413,1462,1514,1567,1622,1679,1738,1799,
  1862,1928,2035,2107,2181,2257,2336,2418,2503
};

int ctcssToneIndex(float hz) {
  if (hz <= 0) return -1;
  int target = (int)lroundf(hz * 10.0f);   // tenths of Hz
  int best = -1, bestErr = 9999;
  for (int i = 0; i < 39; ++i) {
    int e = abs((int)CTCSS_TENTHS[i] - target);
    if (e < bestErr) { bestErr = e; best = i; }
  }
  // Reject if the nearest standard tone is more than ~1 Hz away (bad input).
  return (bestErr <= 10) ? best : -1;
}

float ctcssToneHz(int index) {
  if (index < 0 || index >= 39) return 0.0f;
  return CTCSS_TENTHS[index] / 10.0f;
}
