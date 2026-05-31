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
    if (gps.encode(gpsSerial->read())) {
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
  return updated;
}

void Location::setManual(double lat, double lon, double altM) {
  _obs.lat = lat; _obs.lon = lon; _obs.altM = altM;
  _obs.valid = true; _obs.fromGps = false;
}

// Maidenhead grid -> lat/lon (centre of the square). Accepts 4 or 6 chars.
bool Location::setFromGrid(const String& gridIn) {
  String g = gridIn; g.trim(); g.toUpperCase();
  if (g.length() < 4) return false;
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
