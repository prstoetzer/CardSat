// ===========================================================================
//  satdb.cpp
// ===========================================================================
#include "satdb.h"
#include <LittleFS.h>
#include "storage.h"
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include <cstdio>

bool SatDb::begin() {
  return Store::begin();
}

int SatDb::indexOfNorad(uint32_t norad) const {
  for (int i = 0; i < _n; ++i) if (_sats[i].norad == norad) return i;
  return -1;
}

static void rstrip(char* s) {
  int n = strlen(s);
  while (n > 0 && (s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\n')) s[--n] = 0;
}

// ---- EPOCH "YYYY-MM-DD HH:MM:SS.ffffff" -> Unix UTC seconds (fractional) ----
// Civil-from-days (Howard Hinnant) so it never depends on the process TZ.
double SatDb::gpEpochToUnix(const char* s) {
  if (!s) return 0.0;
  // OMM/GP JSON gives the epoch in ISO 8601 with a 'T' between date and time
  // (e.g. "2024-01-15T12:34:56.789012"); some sources use a space instead.
  // Normalize 'T'/'t' to a space so the time-of-day is always parsed -- missing
  // it silently zeroed HH:MM:SS and shifted pass times by up to ~12 hours.
  char b[40]; strncpy(b, s, sizeof(b) - 1); b[sizeof(b) - 1] = 0;
  for (char* p = b; *p; ++p) if (*p == 'T' || *p == 't') *p = ' ';
  int Y = 0, Mo = 1, D = 1, h = 0, mi = 0; double se = 0.0;
  if (sscanf(b, "%d-%d-%d %d:%d:%lf", &Y, &Mo, &D, &h, &mi, &se) < 3) return 0.0;
  int y = Y - (Mo <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (Mo + (Mo > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097 + (long)doe - 719468;
  return (double)days * 86400.0 + h * 3600 + mi * 60 + se;
}

// ---- Unix UTC seconds -> EPOCH string (for persisting manual entries) ------
static String unixToGpEpoch(double u) {
  time_t ip = (time_t)floor(u);
  double frac = u - (double)ip;
  struct tm tmv; gmtime_r(&ip, &tmv);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%09.6f",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, (double)tmv.tm_sec + frac);
  return String(buf);
}

// ===========================================================================
//  GP/OMM parsing
// ===========================================================================
// AMSAT sends the element values as JSON *strings* (e.g. "101.9903"); a few
// fields (ELEMENT_SET_NO) are numbers. Read either form without relying on the
// JSON library's string->number coercion.
static double jnum(JsonObjectConst o, const char* key) {
  JsonVariantConst v = o[key];
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    return s ? strtod(s, nullptr) : 0.0;
  }
  return v.as<double>();
}

static bool parseGpObject(JsonObjectConst o, SatEntry& s) {
  const char* nm = o["AMSAT_NAME"] | (const char*)(o["OBJECT_NAME"] | "");
  if (!nm || !nm[0]) return false;
  strncpy(s.name, nm, sizeof(s.name) - 1); s.name[sizeof(s.name)-1] = 0; rstrip(s.name);
  const char* idd = o["OBJECT_ID"] | "";
  strncpy(s.intlDes, idd, sizeof(s.intlDes) - 1); s.intlDes[sizeof(s.intlDes)-1] = 0;

  s.norad       = (uint32_t)jnum(o, "NORAD_CAT_ID");
  const char* ep = o["EPOCH"] | "";
  s.epochUnix   = SatDb::gpEpochToUnix(ep);
  s.incl        = jnum(o, "INCLINATION");
  s.ecc         = jnum(o, "ECCENTRICITY");
  s.raan        = jnum(o, "RA_OF_ASC_NODE");
  s.argp        = jnum(o, "ARG_OF_PERICENTER");
  s.ma          = jnum(o, "MEAN_ANOMALY");
  s.meanMotion  = jnum(o, "MEAN_MOTION");
  s.bstar       = jnum(o, "BSTAR");
  s.ndot        = jnum(o, "MEAN_MOTION_DOT");
  s.nddot       = jnum(o, "MEAN_MOTION_DDOT");
  s.revAtEpoch  = (uint32_t)jnum(o, "REV_AT_EPOCH");
  s.elsetNum    = (uint16_t)jnum(o, "ELEMENT_SET_NO");
  s.txLoaded    = false;
  // A valid element set needs a non-zero epoch and mean motion.
  return s.norad != 0 && s.epochUnix > 0 && s.meanMotion > 0;
}

int SatDb::loadGpFromJson(const String& json) {
  _n = 0;
  return appendGpFromJson(json);
}

// Extract the raw value of "key" from a flat JSON object in [o, o+len). Copies
// the unquoted string value (or a bare token like 999.0 / null) into out. The
// trailing-quote check means "MEAN_MOTION" won't match "MEAN_MOTION_DOT". This
// uses no heap, unlike a per-object ArduinoJson document -- which matters
// because the GP array is parsed while a ~75 KB download buffer is resident and
// repeated document alloc/free fragments the no-PSRAM heap (it would quietly
// fail partway and drop the rest of the satellites).
static bool gpFindValue(const char* o, size_t len, const char* key,
                        char* out, size_t outsz) {
  out[0] = 0;
  size_t klen = strlen(key);
  const char* end = o + len;
  const char* hit = nullptr;
  for (const char* p = o; p + klen + 2 <= end; ++p) {
    if (*p == '"' && memcmp(p + 1, key, klen) == 0 && p[1 + klen] == '"') {
      hit = p + klen + 2; break;            // just past the key's closing quote
    }
  }
  if (!hit) return false;
  while (hit < end && (*hit == ' ' || *hit == '\t' || *hit == ':')) ++hit;
  if (hit >= end) return false;
  size_t n = 0;
  if (*hit == '"') {                        // quoted string value
    ++hit;
    while (hit < end && *hit != '"' && n + 1 < outsz) {
      if (*hit == '\\' && hit + 1 < end) ++hit;
      out[n++] = *hit++;
    }
  } else {                                  // bare token (number / null / true)
    while (hit < end && *hit != ',' && *hit != '}' &&
           *hit != ' ' && *hit != '\n' && *hit != '\r' && *hit != '\t' &&
           n + 1 < outsz)
      out[n++] = *hit++;
  }
  out[n] = 0;
  return true;
}

// Parse one flat OMM object (raw text, bounded) into a SatEntry. Same validity
// rule as parseGpObject but allocation-free, for the bulk GP-array path.
static bool parseGpObjectRaw(const char* o, size_t len, SatEntry& s) {
  char v[48];
  if (!gpFindValue(o, len, "AMSAT_NAME", v, sizeof(v)) || !v[0]) {
    if (!gpFindValue(o, len, "OBJECT_NAME", v, sizeof(v)) || !v[0]) return false;
  }
  strncpy(s.name, v, sizeof(s.name) - 1); s.name[sizeof(s.name)-1] = 0; rstrip(s.name);
  gpFindValue(o, len, "OBJECT_ID", v, sizeof(v));
  strncpy(s.intlDes, v, sizeof(s.intlDes) - 1); s.intlDes[sizeof(s.intlDes)-1] = 0;

  gpFindValue(o, len, "NORAD_CAT_ID",      v, sizeof(v)); s.norad      = (uint32_t)strtoul(v, nullptr, 10);
  gpFindValue(o, len, "EPOCH",             v, sizeof(v)); s.epochUnix  = SatDb::gpEpochToUnix(v);
  gpFindValue(o, len, "INCLINATION",       v, sizeof(v)); s.incl       = strtod(v, nullptr);
  gpFindValue(o, len, "ECCENTRICITY",      v, sizeof(v)); s.ecc        = strtod(v, nullptr);
  gpFindValue(o, len, "RA_OF_ASC_NODE",    v, sizeof(v)); s.raan       = strtod(v, nullptr);
  gpFindValue(o, len, "ARG_OF_PERICENTER", v, sizeof(v)); s.argp       = strtod(v, nullptr);
  gpFindValue(o, len, "MEAN_ANOMALY",      v, sizeof(v)); s.ma         = strtod(v, nullptr);
  gpFindValue(o, len, "MEAN_MOTION",       v, sizeof(v)); s.meanMotion = strtod(v, nullptr);
  gpFindValue(o, len, "BSTAR",             v, sizeof(v)); s.bstar      = strtod(v, nullptr);
  gpFindValue(o, len, "MEAN_MOTION_DOT",   v, sizeof(v)); s.ndot       = strtod(v, nullptr);
  gpFindValue(o, len, "MEAN_MOTION_DDOT",  v, sizeof(v)); s.nddot      = strtod(v, nullptr);
  gpFindValue(o, len, "REV_AT_EPOCH",      v, sizeof(v)); s.revAtEpoch = (uint32_t)strtoul(v, nullptr, 10);
  gpFindValue(o, len, "ELEMENT_SET_NO",    v, sizeof(v)); s.elsetNum   = (uint16_t)strtoul(v, nullptr, 10);
  s.txLoaded = false;
  return s.norad != 0 && s.epochUnix > 0 && s.meanMotion > 0;
}

int SatDb::appendGpFromJson(const String& json) {
  // Parse one OMM object at a time, allocation-free (see parseGpObjectRaw).
  // Walking object-by-object also tolerates a truncated download tail.
  const char* arr = strchr(json.c_str(), '[');
  if (!arr) return _n;
  const char* s = arr + 1;
  while (*s && _n < MAX_SATS) {
    while (*s && *s != '{' && *s != ']') ++s;     // skip whitespace/commas
    if (*s != '{') break;                          // ']' or end of input
    const char* objStart = s;
    int depth = 0; bool inStr = false, esc = false;
    const char* q = s;
    for (; *q; ++q) {                              // find the matching '}'
      char c = *q;
      if (inStr) {
        if (esc) esc = false;
        else if (c == '\\') esc = true;
        else if (c == '"')  inStr = false;
      } else if (c == '"') inStr = true;
      else if (c == '{')   ++depth;
      else if (c == '}') { if (--depth == 0) { ++q; break; } }
    }
    if (depth != 0) break;                         // truncated / malformed tail
    size_t len = (size_t)(q - objStart);

    SatEntry tmp;                                  // zero-allocation field parse
    if (parseGpObjectRaw(objStart, len, tmp)) {
      int idx = indexOfNorad(tmp.norad);           // replace if it already exists
      if (idx < 0) { idx = _n; _n++; }
      _sats[idx] = tmp;
    }
    s = q;                                         // continue after this object
  }
  return _n;
}

bool SatDb::addGp(const SatEntry& s) {
  if (s.norad == 0 || s.meanMotion <= 0) return false;
  int idx = indexOfNorad(s.norad);
  if (idx < 0) { if (_n >= MAX_SATS) return false; idx = _n++; }
  _sats[idx] = s;

  // Persist as one compact OMM object per line (NDJSON), kept separate so an
  // AMSAT refresh that rewrites FILE_GP doesn't wipe hand-entered satellites.
  JsonDocument d;
  d["AMSAT_NAME"]        = s.name;
  d["OBJECT_ID"]         = s.intlDes;
  d["NORAD_CAT_ID"]      = s.norad;
  d["EPOCH"]             = unixToGpEpoch(s.epochUnix);
  d["INCLINATION"]       = s.incl;
  d["ECCENTRICITY"]      = s.ecc;
  d["RA_OF_ASC_NODE"]    = s.raan;
  d["ARG_OF_PERICENTER"] = s.argp;
  d["MEAN_ANOMALY"]      = s.ma;
  d["MEAN_MOTION"]       = s.meanMotion;
  d["BSTAR"]             = s.bstar;
  d["MEAN_MOTION_DOT"]   = s.ndot;
  d["MEAN_MOTION_DDOT"]  = s.nddot;
  d["REV_AT_EPOCH"]      = s.revAtEpoch;
  d["ELEMENT_SET_NO"]    = s.elsetNum;
  File f = Store::fs().open(FILE_MGP, "a");
  if (f) { serializeJson(d, f); f.print("\n"); f.close(); }
  return true;
}

bool SatDb::loadManualGpFile() {
  File f = Store::fs().open(FILE_MGP, "r");
  if (!f) return false;
  bool any = false;
  while (f.available() && _n < MAX_SATS) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    JsonDocument d;
    if (deserializeJson(d, line)) continue;
    SatEntry tmp;
    if (!parseGpObject(d.as<JsonObjectConst>(), tmp)) continue;
    int idx = indexOfNorad(tmp.norad);
    if (idx < 0) { idx = _n; _n++; }
    _sats[idx] = tmp; any = true;
  }
  f.close();
  return any;
}

// True if this norad has a hand-entered line in FILE_MGP (i.e. it's a manual sat
// the user can delete), regardless of whether a cached GP entry also exists.
bool SatDb::isManualGp(uint32_t norad) {
  File f = Store::fs().open(FILE_MGP, "r");
  if (!f) return false;
  bool found = false;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length() == 0) continue;
    JsonDocument d;
    if (deserializeJson(d, line)) continue;
    if ((uint32_t)(d["NORAD_CAT_ID"] | 0) == norad) { found = true; break; }
  }
  f.close();
  return found;
}

// Delete a hand-entered sat from FILE_MGP: rewrite the file without its line.
// Lines that aren't this norad (including any the user annotated) are preserved.
bool SatDb::removeManualGp(uint32_t norad) {
  File f = Store::fs().open(FILE_MGP, "r");
  if (!f) return false;
  String out; bool removed = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    String t = line; t.trim();
    if (t.length() == 0) continue;
    JsonDocument d;
    if (!deserializeJson(d, t) && (uint32_t)(d["NORAD_CAT_ID"] | 0) == norad) {
      removed = true; continue;                 // drop this line
    }
    out += t; out += '\n';
  }
  f.close();
  if (!removed) return false;
  if (out.length() == 0) { Store::fs().remove(FILE_MGP); return true; }
  File w = Store::fs().open(FILE_MGP, "w");
  if (!w) return false;
  w.print(out); w.close();
  return true;
}

bool SatDb::saveGpJson(const String& json) {
  File f = Store::fs().open(FILE_GP, "w");
  if (!f) return false;
  f.print(json); f.close();
  return true;
}

bool SatDb::loadGpFromFs() {
  return loadGpFromFile(FILE_GP) > 0;
}

// ---------------------------------------------------------------------------
//  AMSAT OSCAR status. Reduce a satellite name to a base designator for
//  matching: take the part before a mode tag / space / bracket, upper-case it,
//  and drop a leading zero in the segment after the last hyphen so the bulletin
//  "AO-07" matches the status page "AO-7_[U/v]".
// ---------------------------------------------------------------------------
static void amsatBase(const char* in, char* out, int outsz) {
  char tmp[28]; int j = 0;
  for (int i = 0; in[i] && j < (int)sizeof(tmp) - 1; ++i) {
    char c = in[i];
    if (c == ' ' || c == '[' || c == '(') break;
    if (c == '_' && in[i + 1] == '[') break;
    tmp[j++] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
  }
  tmp[j] = 0;
  int h = -1; for (int i = 0; i < j; ++i) if (tmp[i] == '-') h = i;
  int seg = (h >= 0) ? h + 1 : 0;
  int sgt = seg; while (tmp[sgt] == '0' && tmp[sgt + 1] >= '0' && tmp[sgt + 1] <= '9') ++sgt;
  int k = 0;
  for (int i = 0;   i < seg && k < outsz - 1; ++i) out[k++] = tmp[i];   // prefix + hyphen
  for (int i = sgt; i < j   && k < outsz - 1; ++i) out[k++] = tmp[i];   // de-zeroed segment
  out[k] = 0;
}

// Report precedence for a satellite seen with several report values:
// heard (1) beats telemetry-only (3) beats not-heard (2) beats nothing (0).
static inline int amsatPrio(uint8_t s) { return s == 1 ? 3 : s == 3 ? 2 : s == 2 ? 1 : 0; }

// Set each catalog entry's amsatStatus from a cached summary.php response:
// 1 = Heard / Crew Active, 3 = Telemetry Only, 2 = Not Heard, 0 = no reports.
// The highest-precedence report value seen for a satellite wins.
// Parse an AMSAT "latest_reported_time" of the fixed form "YYYY-MM-DDTHH:MM:SSZ"
// into a Unix UTC epoch. Returns 0 on any malformed input. Uses a direct days-from-
// civil calculation (no timegm/mktime, which pull in timezone state) so it is cheap
// and self-contained for the per-object parse loop.
static uint32_t amsatIsoToEpoch(const char* s) {
  if (!s || !s[0]) return 0;
  int Y, Mo, D, H, Mi, Se;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &H, &Mi, &Se) != 6) return 0;
  if (Y < 1970 || Mo < 1 || Mo > 12 || D < 1 || D > 31) return 0;
  // days_from_civil (Howard Hinnant's algorithm), epoch = 1970-01-01.
  int y = Y - (Mo <= 2 ? 1 : 0);
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (unsigned)((153 * (Mo + (Mo > 2 ? -3 : 9)) + 2) / 5 + D - 1);
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = (long)era * 146097 + (long)doe - 719468;
  long secs = days * 86400L + H * 3600L + Mi * 60L + Se;
  return (secs > 0) ? (uint32_t)secs : 0;
}

// ---------------------------------------------------------------------------
//  AMSAT catalog name map. catalog.php lists every satellite the status system
//  tracks as "BASE_[MODE]" (AO-7_[U/v], ISS_[FM], IO-117 with no tag, ...).
//  Each entry is matched to a catalog satellite with a tolerant, ordered match:
//    1. the base equals a parenthesised designator in the GP name
//       ("OSCAR 7 (AO-7)" <- "AO-7") -- the case the legacy prefix match missed;
//    2. the whole normalized names are equal;
//    3. the base appears as a delimited token inside the GP name
//       ("ISS (ZARYA)" <- "ISS");
//    4. the legacy amsatBase prefix match (kept as the safety net).
//  Normalization uppercases and treats every non-alphanumeric as a space.
// ---------------------------------------------------------------------------
static void amsNorm(const char* in, char* out, int outsz) {
  int k = 0; bool sp = true;
  for (int i = 0; in[i] && k < outsz - 2; ++i) {
    char ch = in[i];
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    bool an = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-';
    if (an) { out[k++] = ch; sp = false; }
    else if (!sp) { out[k++] = ' '; sp = true; }
  }
  while (k > 0 && out[k - 1] == ' ') --k;
  out[k] = 0;
}

// Does padded " haystackNorm " contain " needleNorm " as a delimited token run?
static bool amsTokenIn(const char* hayNorm, const char* needleNorm) {
  if (!needleNorm[0]) return false;
  char hay[72]; snprintf(hay, sizeof(hay), " %s ", hayNorm);
  char nee[40]; snprintf(nee, sizeof(nee), " %s ", needleNorm);
  return strstr(hay, nee) != nullptr;
}

// Known designator aliases with no lexical bridge between the AMSAT status
// catalog and common GP sources (same bird, two unrelated names). Tried as a
// final step when the four lexical steps miss. Left = status-API base name;
// right = the GP-side name to try instead, matched with the same tolerant
// normalization so "LILACSAT-2", "LILACSAT 2", or "LILACSAT-2 (CAS-3H)" all hit.
static const struct { const char* status; const char* gp; } AMS_ALIASES[] = {
  { "CAS-3H", "LILACSAT-2" },   // proven gap vs the AMSAT daily bulletin
  { "IO-117", "GREENCUBE"  },   // GreenCube; bulletin may carry either name
  { "LO-19",  "LUSAT"      },   // historic name in most GP sources
};

void SatDb::applyAmsatCatalogFile(const char* path) {
  _amsMapN = 0;
  File f = Store::fs().open(path, "r");    // Store::fs(): SD-equipped units never mount LittleFS
  if (!f) return;
  // Names are the only field we need; every record carries exactly one
  // "name":"..." key (the links values are plain URL strings). Scan the stream
  // for that pattern -- no DOM, nothing large held in RAM.
  char nb[16]; int st = 0; char nameBuf[AMS_NAME_LEN]; int nk = 0;
  // Whitespace-tolerant scan for the "name" string value. The AMSAT catalog API
  // pretty-prints its JSON as  "name": "AO-7_[V/a]"  -- note the spaces around the
  // colon. An earlier fixed-byte match on "name":" (no spaces) matched NOTHING on
  // the live payload, leaving the map empty (so multi-mode birds like AO-7 offered
  // only a single mode to report). This little state machine accepts optional
  // whitespace after the key and after the colon, then captures the quoted value:
  //   MS_KEY  : matching the literal   "name"
  //   MS_COLON: seen the key; skipping spaces, expecting ':'
  //   MS_PRE  : seen ':';   skipping spaces, expecting the opening '"'
  //   MS_CAP  : inside the value, capturing until the closing '"'
  enum { MS_KEY = 0, MS_COLON, MS_PRE, MS_CAP };
  const char* key = "\"name\"";
  int ks2 = 0;                             // index within key while in MS_KEY
  int ms = MS_KEY;
  // Pre-normalize catalog names once.
  char (*norm)[40] = (char(*)[40])malloc((size_t)_n * 40);
  if (!norm) { f.close(); return; }
  for (int i = 0; i < _n; ++i) amsNorm(_sats[i].name, norm[i], 40);
  char (*base)[20] = (char(*)[20])malloc((size_t)_n * 20);
  if (!base) { free(norm); f.close(); return; }
  for (int i = 0; i < _n; ++i) amsatBase(_sats[i].name, base[i], 20);
  (void)nb; (void)st;
  while (f.available()) {
    char ch = (char)f.read();
    if (ms != MS_CAP) {
      switch (ms) {
        case MS_KEY:                        // matching the literal "name"
          if (ch == key[ks2]) { if (++ks2 == (int)strlen(key)) { ms = MS_COLON; } }
          else ks2 = (ch == key[0]) ? 1 : 0;
          break;
        case MS_COLON:                      // skip spaces, expect ':'
          if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') break;
          if (ch == ':') ms = MS_PRE; else { ms = MS_KEY; ks2 = (ch == key[0]) ? 1 : 0; }
          break;
        case MS_PRE:                        // skip spaces, expect opening '"'
          if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') break;
          if (ch == '"') { ms = MS_CAP; nk = 0; }
          else { ms = MS_KEY; ks2 = (ch == key[0]) ? 1 : 0; }
          break;
      }
      continue;
    }
    if (ch == '"') {                       // end of the name value
      ms = MS_KEY; ks2 = 0;
      nameBuf[nk] = 0;
      if (nk == 0 || _amsMapN >= AMS_MAP_MAX) continue;
      bool dup = false;                    // the live catalog has a duplicate row
      for (int m = 0; m < _amsMapN; ++m)
        if (strcmp(_amsMap[m].name, nameBuf) == 0) { dup = true; break; }
      if (dup) continue;
      // base = everything before the last "_[" (whole name if untagged)
      char bb[AMS_NAME_LEN]; strncpy(bb, nameBuf, sizeof(bb)); bb[sizeof(bb) - 1] = 0;
      for (int q = (int)strlen(bb) - 1; q > 0; --q)
        if (bb[q] == '[' && bb[q - 1] == '_') { bb[q - 1] = 0; break; }
      char bn[40]; amsNorm(bb, bn, sizeof(bn));
      char ab[20]; amsatBase(bb, ab, sizeof(ab));
      int hit = -1;
      // 1. parenthesised designator equality
      for (int i = 0; i < _n && hit < 0; ++i) {
        const char* p = strchr(_sats[i].name, '(');
        while (p && hit < 0) {
          const char* e = strchr(p, ')');
          if (!e) break;
          char tok[24]; int L = (int)(e - p - 1); if (L > 23) L = 23;
          memcpy(tok, p + 1, L); tok[L] = 0;
          char tn[40]; amsNorm(tok, tn, sizeof(tn));
          if (tn[0] && strcmp(tn, bn) == 0) hit = i;
          p = strchr(e, '(');
        }
      }
      // 2. whole-name equality
      for (int i = 0; i < _n && hit < 0; ++i)
        if (strcmp(norm[i], bn) == 0) hit = i;
      // 3. delimited-token containment
      for (int i = 0; i < _n && hit < 0; ++i)
        if (amsTokenIn(norm[i], bn)) hit = i;
      // 4. legacy prefix base
      for (int i = 0; i < _n && hit < 0; ++i)
        if (base[i][0] && strcmp(base[i], ab) == 0) hit = i;
      // 5. known alias table (designators with no lexical bridge)
      for (size_t al = 0; al < sizeof(AMS_ALIASES) / sizeof(AMS_ALIASES[0]) && hit < 0; ++al) {
        if (strcasecmp(bb, AMS_ALIASES[al].status) != 0) continue;
        char an[40]; amsNorm(AMS_ALIASES[al].gp, an, sizeof(an));
        for (int i = 0; i < _n && hit < 0; ++i)
          if (strcmp(norm[i], an) == 0 || amsTokenIn(norm[i], an)) hit = i;
      }
      if (hit >= 0) {
        _amsMap[_amsMapN].sat = (int16_t)hit;
        strncpy(_amsMap[_amsMapN].name, nameBuf, AMS_NAME_LEN - 1);
        _amsMap[_amsMapN].name[AMS_NAME_LEN - 1] = 0;
        _amsMapN++;
      }
      continue;
    }
    if (nk < AMS_NAME_LEN - 1) nameBuf[nk++] = ch;   // overlong names truncate
  }
  free(base); free(norm);
  f.close();
  Serial.printf("[amsat] catalog map: %d entries\n", _amsMapN);
}

int SatDb::amsFindByName(const char* apiName) const {
  for (int m = 0; m < _amsMapN; ++m)
    if (strcmp(_amsMap[m].name, apiName) == 0) return _amsMap[m].sat;
  return -1;
}

int SatDb::amsNamesFor(int satIdx, const char* out[], int maxN) const {
  int k = 0;
  for (int m = 0; m < _amsMapN && k < maxN; ++m)
    if (_amsMap[m].sat == satIdx) out[k++] = _amsMap[m].name;
  return k;
}

void SatDb::applyAmsatStatusFile(const char* path) {
  for (int i = 0; i < _n; ++i) { _sats[i].amsatStatus = 0; _sats[i].amsatHeardEpoch = 0; _sats[i].amsatReports = 0; _sats[i].amsatName[0] = 0; }
  if (_n <= 0) return;
  File f = Store::fs().open(path, "r");
  if (!f) return;

  // Pre-compute the base callsign of each catalog sat once (cb[i]), so the per-report
  // matching below is a cheap strcmp.
  char (*cb)[16] = (char (*)[16])malloc((size_t)_n * 16);
  if (!cb) { f.close(); return; }
  for (int i = 0; i < _n; ++i) amsatBase(_sats[i].name, cb[i], 16);

  // Parse the "data" array ONE OBJECT AT A TIME instead of loading the whole filtered
  // document. The summary.php payload is large (hundreds of reports); deserializing it
  // whole spiked free heap to a few KB, which churned the no-PSRAM free-list and (per a
  // long debugging trail) destabilised later TLS connects. Here we scan the file for each
  // top-level {...} inside the array, deserialize just that object, apply it, and discard
  // it -- so peak heap is one small object (~200 B), not the entire array.
  // Bounded object buffer: AMSAT records are short; anything longer than this is skipped
  // safely (we only need name/report/report_count, which sit well within it).
  static const size_t OBJ_MAX = 512;
  char obj[OBJ_MAX];
  size_t objLen = 0;
  int depth = 0;          // brace depth; we capture while depth>=1 inside an object
  bool inObj = false;     // currently accumulating an object
  bool inStr = false;     // inside a JSON string (so braces/quotes are literal)
  bool esc = false;       // previous char was a backslash inside a string
  bool sawData = false;   // only start capturing objects after the "data" key

  // We don't need a full tokenizer: just find the "data" marker, then capture each
  // brace-balanced object that follows until the array closes.
  int c;
  // Cheap scan to the "data" array opener so we don't capture any objects before it.
  // (The top-level wrapper {"data":[ ... ]} -- skip until the '[' after "data".)
  {
    const char* key = "\"data\"";
    int ki = 0;
    while ((c = f.read()) >= 0) {
      char ch = (char)c;
      if (ch == key[ki]) { if (++ki == 6) { sawData = true; break; } }
      else ki = (ch == '"') ? 1 : 0;
    }
    if (sawData) { while ((c = f.read()) >= 0 && (char)c != '['); }  // advance to '['
  }
  if (!sawData) { free(cb); f.close(); return; }

  while ((c = f.read()) >= 0) {
    char ch = (char)c;
    if (inObj) {
      if (objLen < OBJ_MAX - 1) obj[objLen++] = ch;   // capture (truncate over-long safely)
    }
    if (inStr) {
      if (esc)            esc = false;
      else if (ch == '\\') esc = true;
      else if (ch == '"')  inStr = false;
      continue;
    }
    if (ch == '"') { inStr = true; continue; }
    if (ch == '{') {
      if (depth == 0) { inObj = true; objLen = 0; if (objLen < OBJ_MAX-1) obj[objLen++] = ch; }
      depth++;
    } else if (ch == '}') {
      depth--;
      if (depth == 0 && inObj) {
        obj[objLen] = 0;
        // Deserialize just this one object and apply it.
        JsonDocument one;
        if (!deserializeJson(one, obj)) {
          const char* nm  = one["name"]  | "";
          const char* rep = one["report"] | "";
          long cnt = one["report_count"] | 0;
          const char* lrt = one["latest_reported_time"] | "";
          if (nm[0] && cnt > 0) {
            uint8_t st = !strcmp(rep, "Not Heard")      ? 2
                       : !strcmp(rep, "Telemetry Only") ? 3
                                                        : 1;
            uint32_t ep = amsatIsoToEpoch(lrt);
            char sb[16]; amsatBase(nm, sb, 16);
            int mapped = amsFindByName(nm);   // exact API-name hit via the catalog map
            for (int i = 0; i < _n; ++i) {
              if (mapped >= 0 ? (i != mapped) : (strcmp(sb, cb[i]) != 0)) continue;
              int pNew = amsatPrio(st), pCur = amsatPrio(_sats[i].amsatStatus);
              // A row wins if it has higher status precedence, or ties precedence but is
              // more recent -- so the shown status, age and count all come from that row.
              bool win = (pNew > pCur) ||
                         (pNew == pCur && ep && ep > _sats[i].amsatHeardEpoch);
              if (win) {
                _sats[i].amsatStatus = st;
                _sats[i].amsatHeardEpoch = ep;
                _sats[i].amsatReports = (cnt > 255) ? 255 : (uint8_t)cnt;
                strncpy(_sats[i].amsatName, nm, sizeof(_sats[i].amsatName) - 1);
                _sats[i].amsatName[sizeof(_sats[i].amsatName) - 1] = 0;
              }
            }
          }
        }
        inObj = false;
      }
    } else if (ch == ']' && depth == 0) {
      break;   // end of the data array
    }
  }
  free(cb);
  f.close();
}

// Stream-parse a GP/OMM JSON array from a file, one object at a time, using a
// small fixed buffer. Never loads the whole file into RAM, so it works for the
// full ~75 KB amateur list on the no-PSRAM heap (where a single contiguous
// String would fail). Object state carries across read-buffer boundaries.
int SatDb::loadGpFromFile(const char* path) {
  _n = 0;
  File f = Store::fs().open(path, "r");
  if (!f) return 0;

  static const size_t OBJ_MAX = 1200;     // largest OMM object is ~800 bytes
  static char obj[OBJ_MAX];               // static: keep it off the stack
  uint8_t rd[256];
  size_t oi = 0;
  int  depth = 0;
  bool inStr = false, esc = false, collecting = false, started = false;

  int avail;
  while ((avail = f.read(rd, sizeof(rd))) > 0 && _n < MAX_SATS) {
    for (int i = 0; i < avail && _n < MAX_SATS; ++i) {
      char c = (char)rd[i];
      if (!started) { if (c == '[') started = true; continue; }
      if (!collecting) {                  // between objects: wait for '{'
        if (c == '{') { collecting = true; depth = 1; inStr = false; esc = false;
                        oi = 0; obj[oi++] = c; }
        continue;
      }
      bool overflow = (oi >= OBJ_MAX - 1);
      if (!overflow) obj[oi++] = c;
      if (inStr) {
        if (esc) esc = false;
        else if (c == '\\') esc = true;
        else if (c == '"')  inStr = false;
      } else if (c == '"') inStr = true;
      else if (c == '{')   ++depth;
      else if (c == '}') {
        if (--depth == 0) {               // object complete
          collecting = false;
          if (!overflow) {                // only parse if captured whole
            obj[oi] = 0;
            SatEntry tmp;
            if (parseGpObjectRaw(obj, oi, tmp)) {
              int idx = indexOfNorad(tmp.norad);
              if (idx < 0) { idx = _n; _n++; }
              _sats[idx] = tmp;
            }
          }
          oi = 0;
        }
      }
    }
  }
  f.close();
  return _n;
}

// ===========================================================================
//  GP elements -> TLE line-pair (only to initialise the SGP4 propagator)
// ===========================================================================
//  Field layout follows the canonical NORAD two-line spec. This is host-tested
//  by round-tripping the elements back through spec column offsets and by
//  checksum verification; SGP4 results are identical to the original element
//  set because TLE is just an alternate encoding of the same mean elements.

// Assumed-decimal exponential field (8 chars), e.g. " 71831-4", " 00000-0".
static void encExp(double v, char out[10]) {
  char s = (v < 0) ? '-' : ' ';
  double a = fabs(v);
  int e = 0;
  if (a != 0.0) {
    while (a >= 1.0) { a /= 10.0; e++; }
    while (a < 0.1)  { a *= 10.0; e--; }
  }
  long mant = llround(a * 1e5);
  if (mant >= 100000) { mant = 10000; e++; }
  if (e > 9)  e = 9;  if (e < -9) e = -9;
  snprintf(out, 10, "%c%05ld%c%01d", s, mant, (e < 0 ? '-' : '+'), (int)labs(e));
}

// First-derivative field (10 chars): sign + ".XXXXXXXX".
static void encNdot(double v, char out[12]) {
  char s = (v < 0) ? '-' : ' ';
  long m = llround(fabs(v) * 1e8);
  if (m > 99999999L) m = 99999999L;
  snprintf(out, 12, "%c.%08ld", s, m);
}

// Catalog number for the *synthesized TLE line* only. The TLE format's field is
// 5 columns, so it physically cannot hold a >5-digit catalog number. We use
// Alpha-5 (CelesTrak's documented stopgap) for 100000-339999, and for anything
// larger (6-9 digit Space-Fence / analyst IDs, e.g. 799xxxxxx) we emit the low 5
// digits purely so the TLE stays well-formed for twoline2rv. This is SAFE because
// the satrec's catalog number is never read back as identity: CardSat keeps the
// full NORAD id in SatEntry.norad (uint32_t, good to 4.29e9) for all identity,
// storage, dedup, file paths, and display. SGP4 propagation depends only on the
// orbital elements, not this field, so 9-digit objects still propagate correctly.
static void encCatalog(uint32_t n, char out[6]) {
  if (n <= 99999u) { snprintf(out, 6, "%05lu", (unsigned long)n); return; }
  static const char* A = "ABCDEFGHJKLMNPQRSTUVWXYZ";   // skips I and O
  int hi = (int)(n / 10000), lo = (int)(n % 10000);
  if (hi >= 10 && hi <= 33) snprintf(out, 6, "%c%04d", A[hi - 10], lo);
  else snprintf(out, 6, "%05lu", (unsigned long)(n % 100000u));  // >339999: low 5 (TLE cosmetic only)
}

static int tleChecksum(const char* line) {
  int s = 0;
  for (int i = 0; i < 68 && line[i]; i++) {
    char c = line[i];
    if (c >= '0' && c <= '9') s += c - '0';
    else if (c == '-')        s += 1;
  }
  return s % 10;
}

static void putAt(char* line, int col1, const char* s) {   // col1 is 1-indexed
  int i = col1 - 1;
  for (int k = 0; s[k]; k++) line[i + k] = s[k];
}

bool SatDb::gpToTle(const SatEntry& s, char l1[72], char l2[72]) {
  if (s.meanMotion <= 0 || s.epochUnix <= 0) return false;
  memset(l1, ' ', 69); l1[69] = 0;
  memset(l2, ' ', 69); l2[69] = 0;

  char cat[6]; encCatalog(s.norad, cat);

  // International designator OBJECT_ID "YYYY-NNNP[PP]" -> "YYNNNPPP".
  char intl[9] = "        ";
  if (s.intlDes[0] && strlen(s.intlDes) >= 8 && s.intlDes[4] == '-') {
    intl[0] = s.intlDes[2]; intl[1] = s.intlDes[3];
    intl[2] = s.intlDes[5]; intl[3] = s.intlDes[6]; intl[4] = s.intlDes[7];
    int k = 5;
    for (size_t j = 8; j < strlen(s.intlDes) && k < 8; ++j) intl[k++] = s.intlDes[j];
  }

  // Epoch -> YYDDD.DDDDDDDD.
  time_t ip = (time_t)floor(s.epochUnix);
  double frac = s.epochUnix - (double)ip;
  struct tm tmv; gmtime_r(&ip, &tmv);
  double day = (tmv.tm_yday + 1)
             + (tmv.tm_hour * 3600 + tmv.tm_min * 60 + tmv.tm_sec + frac) / 86400.0;
  char epoch[16];
  snprintf(epoch, sizeof(epoch), "%02d%012.8f", tmv.tm_year % 100, day);

  char nd[12];  encNdot(s.ndot, nd);
  char ndd[10]; encExp(s.nddot, ndd);
  char bs[10];  encExp(s.bstar, bs);

  // --- line 1 ---
  l1[0] = '1'; putAt(l1, 3, cat); l1[7] = 'U';
  putAt(l1, 10, intl);
  putAt(l1, 19, epoch);
  putAt(l1, 34, nd);
  putAt(l1, 45, ndd);
  putAt(l1, 54, bs);
  l1[62] = '0';                                   // ephemeris type
  char es[6]; snprintf(es, sizeof(es), "%4u", (unsigned)(s.elsetNum % 10000));
  putAt(l1, 65, es);
  l1[68] = '0' + tleChecksum(l1);

  // --- line 2 ---
  // Guard every fixed-width field so a transient out-of-range element (e.g. a wild
  // Gauss-Newton iterate in the state-vector fitter) can never overflow its TLE column and
  // corrupt the line -- a malformed TLE can put SGP4's init/propagator into a spin. Angles
  // are wrapped into [0,360); mean-motion is clamped to the 2-digit field; non-finite -> 0.
  auto fin  = [](double x){ return (x == x && x - x == 0.0) ? x : 0.0; };   // NaN/Inf -> 0
  auto wrap = [&](double a){ a = fin(a); a = fmod(a, 360.0); if (a < 0) a += 360.0; return a; };
  double inclW = wrap((double)s.incl), raanW = wrap((double)s.raan);
  double argpW = wrap((double)s.argp), maW = wrap((double)s.ma);
  double mmW   = fin(s.meanMotion); if (mmW < 0.0) mmW = 0.0; if (mmW > 99.99999999) mmW = 99.99999999;
  char buf[16];
  l2[0] = '2'; putAt(l2, 3, cat);
  snprintf(buf, sizeof(buf), "%8.4f", inclW); putAt(l2, 9,  buf);
  snprintf(buf, sizeof(buf), "%8.4f", raanW); putAt(l2, 18, buf);
  long e7 = llround((double)s.ecc * 1e7); if (e7 < 0) e7 = 0; if (e7 > 9999999L) e7 = 9999999L;
  snprintf(buf, sizeof(buf), "%07ld", e7);     putAt(l2, 27, buf);
  snprintf(buf, sizeof(buf), "%8.4f", argpW); putAt(l2, 35, buf);
  snprintf(buf, sizeof(buf), "%8.4f", maW);   putAt(l2, 44, buf);
  snprintf(buf, sizeof(buf), "%11.8f", mmW);  putAt(l2, 53, buf);
  snprintf(buf, sizeof(buf), "%5lu", (unsigned long)(s.revAtEpoch % 100000u));
  putAt(l2, 64, buf);
  l2[68] = '0' + tleChecksum(l2);
  return true;
}

// --- SatNOGS transmitters JSON -------------------------------------------
static void txBuildFilter(JsonDocument& filter) {
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
}

static int txFillFromDoc(JsonDocument& doc, Transponder* out, int maxN) {
  // Keep the full SatNOGS list (active and inactive); skip only entries with no
  // tunable frequency at all (e.g. invalid records with null up/downlinks).
  int n = 0;
  for (JsonObject o : doc.as<JsonArray>()) {
    if (n >= maxN) break;
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
    if (t.downlink == 0 && t.uplink == 0) continue;   // nothing to tune -> skip

    const char* ty = o["type"] | "";
    bool typeLinear = (strcmp(ty, "Transponder") == 0);
    t.isLinear = (t.uplink != 0) && (t.downlinkHigh > t.downlink) &&
                 (typeLinear || (t.downlinkHigh - t.downlink) >= 5000u);
    // Activity: SatNOGS marks a transmitter status=="active" and alive==true when it
    // is believed operational. Either signal counts as active; default active when the
    // fields are absent (older caches) so we never hide a usable transponder.
    const char* st = o["status"] | "active";
    bool aliveField = o["alive"] | true;
    t.active = aliveField && (strcmp(st, "inactive") != 0);
    n++;
  }
  // NOTE: ordering by usefulness is done once in the app layer (prioritizeTransponders),
  // after manual transponders are appended, so the whole list is ranked together.
  return n;
}

int SatDb::parseTransmittersJson(const String& json, Transponder* out, int maxN) {
  JsonDocument filter; txBuildFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) return 0;
  return txFillFromDoc(doc, out, maxN);
}

int SatDb::parseTransmittersStream(Stream& src, Transponder* out, int maxN) {
  JsonDocument filter; txBuildFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, src, DeserializationOption::Filter(filter))) return 0;
  return txFillFromDoc(doc, out, maxN);
}

static String txPath(uint32_t norad) {
  char buf[32]; snprintf(buf, sizeof(buf), FILE_TXCACHE, (unsigned long)norad);
  return String(buf);
}

String SatDb::txCachePath(uint32_t norad) { return txPath(norad); }

bool SatDb::saveTxCache(uint32_t norad, const String& json) {
  File f = Store::fs().open(txPath(norad), "w");
  if (!f) return false;
  f.print(json); f.close();
  return true;
}

int SatDb::loadTxCache(uint32_t norad, Transponder* out, int maxN) {
  File f = Store::fs().open(txPath(norad), "r");
  if (!f) return 0;
  int n = parseTransmittersStream(f, out, maxN);   // stream-parse: never builds a large RAM String
  f.close();
  return n;
}


// Required FM-uplink CTCSS (PL) tones for the common FM birds. SatNOGS has no
// structured tone field, so these are built in by NORAD id. Operating tones
// only (e.g. SO-50's 74.4 Hz arming burst is a separate manual action; its
// working uplink tone is 67.0 Hz). Extend as new FM satellites appear.
float SatDb::knownCtcssHz(uint32_t norad) {
  switch (norad) {
    case 25544: return 67.0f;   // ISS (FM cross-band repeater)
    case 27607: return 67.0f;   // SO-50  (SaudiSat-1C)
    case 43017: return 67.0f;   // AO-91  (RadFxSat / Fox-1B)
    case 43137: return 67.0f;   // AO-92  (Fox-1D)
    case 43678: return 141.3f;  // PO-101 (Diwata-2)
    default:    return 0.0f;
  }
}
