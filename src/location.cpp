// ===========================================================================
//  location.cpp
// ===========================================================================
#include "location.h"
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <time.h>
#include <math.h>

static TinyGPSPlus    gps;
static HardwareSerial* gpsSerial = nullptr;

void Location::beginGps(int uartNum, int rxPin, int txPin, uint32_t baud) {
  gpsSerial = new HardwareSerial(uartNum);
  gpsSerial->begin(baud, SERIAL_8N1, rxPin, txPin);
  _gpsOn = true;
}

bool Location::pollGps() {
  if (!_gpsOn || !gpsSerial) return false;
  bool updated = false;
  while (gpsSerial->available()) {
    char _gc = gpsSerial->read();
    feedNmeaChar(_gc);                 // capture per-satellite GSV (az/el/SNR)
    if (gps.encode(_gc)) {
      if (gps.location.isValid()) {
        _obs.lat = gps.location.lat();
        _obs.lon = gps.location.lng();
        if (gps.altitude.isValid()) _obs.altM = gps.altitude.meters();
        _obs.valid = true;
        _obs.fromGps = true;
        _hasFix = true;
        updated = true;
      }
      if (gps.satellites.isValid()) _sats = gps.satellites.value();
      // Opportunistically set the clock from GPS time if NTP wasn't available.
      if (gps.date.isValid() && gps.time.isValid() && gps.date.year() > 2020) {
        struct tm t = {};
        t.tm_year = gps.date.year() - 1900;
        t.tm_mon  = gps.date.month() - 1;
        t.tm_mday = gps.date.day();
        t.tm_hour = gps.time.hour();
        t.tm_min  = gps.time.minute();
        t.tm_sec  = gps.time.second();
        time_t epoch = mktime(&t);     // GPS gives UTC; TZ is UTC (set in main)
        struct timeval tv = { epoch, 0 };
        settimeofday(&tv, nullptr);
      }
    }
  }
  // Fix is "held" only while the last position is fresh; if the receiver goes
  // quiet or drops lock, age() climbs past the timeout and we report fix lost.
  static const uint32_t GPS_FIX_TIMEOUT_MS = 5000;
  _hasFix = gps.location.isValid() && gps.location.age() < GPS_FIX_TIMEOUT_MS;
  if (millis() - _lastView > 8000) _viewN = 0;   // drop the sky plot if GSV/RMC stops
  return updated;
}

// Accumulate raw NMEA characters into a line buffer; parse GSV sentences for
// per-satellite az/el/SNR, and commit the assembled in-view list on the once-
// per-cycle RMC sentence (TinyGPS++ does not expose GSV data).
void Location::feedNmeaChar(char c) {
  if (c == '\r') return;
  if (c == '\n') {
    if (_nmeaLen > 6 && _nmea[0] == '$') {
      _nmea[_nmeaLen] = 0;
      if (_nmea[3] == 'G' && _nmea[4] == 'S' && _nmea[5] == 'V') {
        parseGsv(_nmea);
      } else if (_nmea[3] == 'R' && _nmea[4] == 'M' && _nmea[5] == 'C') {
        for (int i = 0; i < _buildN; i++) _view[i] = _build[i];
        _viewN = _buildN; _buildN = 0; _lastView = millis();
      }
    }
    _nmeaLen = 0;
    return;
  }
  if (_nmeaLen < (int)sizeof(_nmea) - 1) _nmea[_nmeaLen++] = c;
  else _nmeaLen = 0;                 // overlong line: resynchronise
}

// Parse one $xxGSV sentence (in place; commas/'*' turned into NUL) and upsert
// each reported satellite into the assembling list, keyed by (system, prn).
void Location::parseGsv(char* s) {
  char t1 = s[1], t2 = s[2], sys = 'N';
  if      (t1 == 'G' && t2 == 'P') sys = 'P';
  else if (t1 == 'G' && t2 == 'L') sys = 'L';
  else if (t1 == 'G' && t2 == 'A') sys = 'A';
  else if ((t1 == 'G' && t2 == 'B') || (t1 == 'B' && t2 == 'D')) sys = 'B';
  else if (t1 == 'G' && t2 == 'Q') sys = 'Q';

  char* tok[24]; int nt = 0;
  for (char* q = s; *q && nt < 24; ) {
    tok[nt++] = q;
    while (*q && *q != ',' && *q != '*') q++;
    if (*q) { *q = 0; q++; }
  }
  if (nt < 4) return;                // header: $xxGSV,msgs,msg#,sats,...

  for (int g = 4; g + 3 < nt; g += 4) {       // groups of prn,elev,azim,snr
    if (!tok[g][0]) continue;
    int prn = atoi(tok[g]);
    if (prn <= 0) continue;
    int el  = tok[g + 1][0] ? atoi(tok[g + 1]) : -1;
    int az  = tok[g + 2][0] ? atoi(tok[g + 2]) : -1;
    int snr = tok[g + 3][0] ? atoi(tok[g + 3]) : 0;
    int idx = -1;
    for (int i = 0; i < _buildN; i++)
      if (_build[i].prn == (uint8_t)prn && _build[i].sys == sys) { idx = i; break; }
    if (idx < 0) { if (_buildN >= MAX_VIEW) continue; idx = _buildN++; }
    _build[idx].prn = (uint8_t)prn;
    _build[idx].el  = (int16_t)el;
    _build[idx].az  = (int16_t)az;
    _build[idx].snr = (uint8_t)constrain(snr, 0, 99);
    _build[idx].sys = sys;
  }
}

void Location::setManual(double lat, double lon, double altM) {
  _obs.lat = lat; _obs.lon = lon; _obs.altM = altM;
  _obs.valid = true; _obs.fromGps = false;
}

// Maidenhead grid -> lat/lon (centre of the square). Accepts 4 or 6 chars.
bool Location::gridToLatLon(const String& gridIn, double& latOut, double& lonOut) {
  String g = gridIn; g.trim(); g.toUpperCase();
  if (g.length() < 4) return false;
  if (g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R') return false;
  if (g[2] < '0' || g[2] > '9' || g[3] < '0' || g[3] > '9') return false;
  double lon = (g[0] - 'A') * 20.0 - 180.0;
  double lat = (g[1] - 'A') * 10.0 - 90.0;
  lon += (g[2] - '0') * 2.0;
  lat += (g[3] - '0') * 1.0;
  if (g.length() >= 6) {
    lon += (g[4] - 'A') * (2.0 / 24.0) + (1.0 / 24.0);
    lat += (g[5] - 'A') * (1.0 / 24.0) + (0.5 / 24.0);
  } else {
    lon += 1.0; lat += 0.5;   // centre of the 2x1 deg square
  }
  latOut = lat; lonOut = lon;
  return true;
}
bool Location::setFromGrid(const String& gridIn) {
  double lat, lon;
  if (!gridToLatLon(gridIn, lat, lon)) return false;
  setManual(lat, lon, 0.0);
  return true;
}

String Location::toGrid(double lat, double lon) {
  lon += 180.0; lat += 90.0;
  char g[7];
  g[0] = 'A' + (int)(lon / 20.0);
  g[1] = 'A' + (int)(lat / 10.0);
  g[2] = '0' + (int)(fmod(lon, 20.0) / 2.0);
  g[3] = '0' + (int)(fmod(lat, 10.0) / 1.0);
  g[4] = 'A' + (int)(fmod(lon, 2.0) / (2.0 / 24.0));
  g[5] = 'A' + (int)(fmod(lat, 1.0) / (1.0 / 24.0));
  g[6] = 0;
  return String(g);
}
