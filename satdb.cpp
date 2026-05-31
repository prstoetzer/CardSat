// ===========================================================================
//  satdb.cpp
// ===========================================================================
#include "satdb.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

bool SatDb::begin() {
  return LittleFS.begin(true);
}

int SatDb::indexOfNorad(uint32_t norad) const {
  for (int i = 0; i < _n; ++i) if (_sats[i].norad == norad) return i;
  return -1;
}

static void rstrip(char* s) {
  int n = strlen(s);
  while (n > 0 && (s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\n')) s[--n] = 0;
}

// Parse bare 3-line stanzas:  NAME \n  1 ... \n  2 ... \n
int SatDb::loadTleFromText(const String& blob) {
  _n = 0;
  return appendTleFromText(blob);
}

int SatDb::appendTleFromText(const String& blob) {
  int i = 0, len = blob.length();
  String name, l1, l2;
  auto nextLine = [&](String& out) -> bool {
    if (i >= len) return false;
    int nl = blob.indexOf('\n', i);
    if (nl < 0) nl = len;
    out = blob.substring(i, nl);
    out.trim();
    i = nl + 1;
    return true;
  };
  while (_n < MAX_SATS) {
    if (!nextLine(name)) break;
    if (name.length() == 0) continue;
    if (name.startsWith("1 ") || name.startsWith("2 ")) continue; // resync
    if (!nextLine(l1)) break;
    if (!nextLine(l2)) break;
    if (!l1.startsWith("1 ") || !l2.startsWith("2 ")) continue;

    uint32_t norad = (uint32_t) atol(l1.substring(2, 7).c_str());
    int idx = indexOfNorad(norad);          // replace if this sat already exists
    if (idx < 0) { idx = _n; if (_n < MAX_SATS) _n++; else break; }

    SatEntry& s = _sats[idx];
    strncpy(s.name, name.c_str(), sizeof(s.name) - 1); s.name[sizeof(s.name)-1]=0;
    rstrip(s.name);
    strncpy(s.line1, l1.c_str(), sizeof(s.line1) - 1); s.line1[sizeof(s.line1)-1]=0;
    strncpy(s.line2, l2.c_str(), sizeof(s.line2) - 1); s.line2[sizeof(s.line2)-1]=0;
    s.norad = norad;
    s.txLoaded = false;
  }
  return _n;
}

bool SatDb::addTle(const char* name, const char* l1, const char* l2) {
  String L1(l1), L2(l2); L1.trim(); L2.trim();
  if (!L1.startsWith("1 ") || !L2.startsWith("2 ")) return false;
  String blob = String(name) + "\n" + L1 + "\n" + L2 + "\n";
  int before = _n;
  appendTleFromText(blob);
  // Persist to the manual-TLE file (kept separate so an AMSAT refresh which
  // rewrites FILE_TLE doesn't wipe hand-entered satellites).
  File f = LittleFS.open(FILE_MTLE, "a");
  if (f) { f.print(blob); f.close(); }
  return _n >= before;   // true whether appended or replaced an existing entry
}

bool SatDb::loadManualTleFile() {
  File f = LittleFS.open(FILE_MTLE, "r");
  if (!f) return false;
  String blob = f.readString();
  f.close();
  if (blob.length() == 0) return false;
  appendTleFromText(blob);
  return true;
}

bool SatDb::saveTleText(const String& blob) {
  File f = LittleFS.open(FILE_TLE, "w");
  if (!f) return false;
  f.print(blob); f.close();
  return true;
}

bool SatDb::loadTleFromFs() {
  File f = LittleFS.open(FILE_TLE, "r");
  if (!f) return false;
  String blob = f.readString(); f.close();
  return loadTleFromText(blob) > 0;
}

// --- SatNOGS transmitters JSON -------------------------------------------
int SatDb::parseTransmittersJson(const String& json, Transponder* out, int maxN) {
  JsonDocument filter;
  JsonObject fe = filter.add<JsonObject>();
  fe["description"]   = true;
  fe["uplink_low"]    = true;
  fe["uplink_high"]   = true;
  fe["downlink_low"]  = true;
  fe["downlink_high"] = true;
  fe["mode"]          = true;
  fe["invert"]        = true;
  fe["type"]          = true;
  fe["status"]        = true;
  fe["alive"]         = true;

  JsonDocument doc;
  if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) return 0;

  int n = 0;
  for (JsonObject o : doc.as<JsonArray>()) {
    if (n >= maxN) break;
    const char* st = o["status"] | "";
    bool alive = o["alive"] | true;
    if (!alive || (st[0] && strcmp(st, "active") != 0)) continue; // active only

    Transponder& t = out[n];
    const char* d = o["description"] | "";
    strncpy(t.desc, d, sizeof(t.desc)-1); t.desc[sizeof(t.desc)-1]=0;
    const char* m = o["mode"] | "";
    strncpy(t.mode, m, sizeof(t.mode)-1); t.mode[sizeof(t.mode)-1]=0;
    t.downlink     = o["downlink_low"]   | 0u;
    t.downlinkHigh = o["downlink_high"]  | 0u;
    t.uplink       = o["uplink_low"]      | 0u;
    t.uplinkHigh   = o["uplink_high"]     | 0u;
    t.invert       = o["invert"]          | false;

    // Linear (tunable-passband) transponder: a real downlink passband plus an
    // uplink. SatNOGS marks these type=="Transponder", but we also require a
    // positive downlink width so single-channel "Transponder" rows don't count.
    const char* ty = o["type"] | "";
    bool typeLinear = (strcmp(ty, "Transponder") == 0);
    t.isLinear = (t.uplink != 0) && (t.downlinkHigh > t.downlink) &&
                 (typeLinear || (t.downlinkHigh - t.downlink) >= 5000u);
    n++;
  }
  return n;
}

static String txPath(uint32_t norad) {
  char buf[32]; snprintf(buf, sizeof(buf), FILE_TXCACHE, (unsigned long)norad);
  return String(buf);
}

bool SatDb::saveTxCache(uint32_t norad, const String& json) {
  File f = LittleFS.open(txPath(norad), "w");
  if (!f) return false;
  f.print(json); f.close();
  return true;
}

int SatDb::loadTxCache(uint32_t norad, Transponder* out, int maxN) {
  File f = LittleFS.open(txPath(norad), "r");
  if (!f) return 0;
  String j = f.readString(); f.close();
  return parseTransmittersJson(j, out, maxN);
}
