// ===========================================================================
//  rig.cpp  -  Rig factory + shared helpers
// ===========================================================================
#include "rig.h"
#include "civ.h"
#include "icomnet.h"
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

Rig* makeRig(RadioModel model, uint8_t catType, const char* host,
             uint16_t port, const char* user, const char* pass) {
  if (catType == 2) {                     // rigctld client: model-agnostic
    (void)user; (void)pass;
    return new RigctlRig(host, port);
  }
  // Icom LAN (RS-BA1 UDP) network CAT: only for CI-V models; catType 1 = CAT_NET.
  if (catType == 1 && RADIOS[model].proto == PROTO_CIV)
    return new IcomNetRig(model, host, port, user, pass);
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

// ---------------------------------------------------------------------------
//  RigctlRig - rigctld (Hamlib NET rigctl) TCP client backend
// ---------------------------------------------------------------------------
bool RigctlRig::ensure() {
  if (_c.connected()) { _ok = true; return true; }
  uint32_t now = millis();
  if (_lastTry && (now - _lastTry) < 3000) { _ok = false; return false; }   // throttle retries
  _lastTry = now; _c.stop();
  if (WiFi.status() != WL_CONNECTED || _host.length() == 0) { _ok = false; return false; }
  _ok = _c.connect(_host.c_str(), _port, 1500);
  if (_ok) {                              // enable split so the uplink leg (I/X) works
    _c.print("S 1 VFOB\n");
    uint32_t t = millis();
    while (_c.available() && (millis() - t) < 50) _c.read();    // drain the RPRT ack
  }
  return _ok;
}

const char* RigctlRig::modeName(RigMode m) {
  switch (m) {
    case RM_LSB: return "LSB";  case RM_USB:  return "USB";
    case RM_CW:  return "CW";   case RM_FM:   return "FM";
    case RM_AM:  return "AM";   case RM_DATA: return "PKTUSB";
  }
  return "USB";
}

// Send one command line; return the first non-empty reply line ("" on failure).
String RigctlRig::xchg(const String& tx) {
  if (!ensure()) return "";
  uint32_t t0 = millis();
  while (_c.available() && (millis() - t0) < 20) _c.read();      // drain stale reply
  if (_c.write((const uint8_t*)tx.c_str(), tx.length()) != tx.length()) {
    _ok = false; _c.stop(); return "";
  }
  String line; t0 = millis();
  while ((millis() - t0) < 400) {
    int ch = _c.read();
    if (ch < 0) { delay(2); continue; }
    if (ch == '\n' || ch == '\r') { if (line.length()) break; else continue; }
    line += (char)ch;
  }
  return line;
}

bool RigctlRig::setSubFreq (uint32_t hz) { return xchg("F " + String(hz) + "\n") == "RPRT 0"; }
bool RigctlRig::setMainFreq(uint32_t hz) { return xchg("I " + String(hz) + "\n") == "RPRT 0"; }
bool RigctlRig::setSubMode (RigMode m)   { return xchg(String("M ") + modeName(m) + " 0\n") == "RPRT 0"; }
bool RigctlRig::setMainMode(RigMode m)   { return xchg(String("X ") + modeName(m) + " 0\n") == "RPRT 0"; }

bool RigctlRig::readSubFreq(uint32_t& hzOut) {
  String r = xchg("f\n");
  if (r.length() == 0 || r.startsWith("RPRT")) return false;
  hzOut = (uint32_t)strtoul(r.c_str(), nullptr, 10);
  return hzOut > 0;
}
bool RigctlRig::readMainFreq(uint32_t& hzOut) {
  String r = xchg("i\n");
  if (r.length() == 0 || r.startsWith("RPRT")) return false;
  hzOut = (uint32_t)strtoul(r.c_str(), nullptr, 10);
  return hzOut > 0;
}
bool RigctlRig::readPtt(bool& tx) {
  String r = xchg("t\n");
  if (r.length() == 0 || r.startsWith("RPRT")) return false;
  tx = (r.toInt() != 0);
  return true;
}
