#pragma once
// ===========================================================================
//  location.h  -  observer location (manual entry, grid square, or GPS)
// ===========================================================================
#include <Arduino.h>

struct Observer {
  double lat = 0.0;     // degrees +N
  double lon = 0.0;     // degrees +E
  double altM = 0.0;    // metres
  bool   valid = false;
  bool   fromGps = false;
};

// One GNSS satellite reported "in view" by an NMEA GSV sentence.
struct GpsSat {
  uint8_t prn = 0;     // satellite id (PRN / slot)
  int16_t el  = -1;    // elevation deg 0-90 (-1 = unknown)
  int16_t az  = -1;    // azimuth deg 0-359 (-1 = unknown)
  uint8_t snr = 0;     // C/No dB-Hz (0 = listed but not tracked)
  char    sys = '?';   // P=GPS L=GLONASS A=Galileo B=BeiDou Q=QZSS N=mixed
};

class Location {
public:
  // Start a NMEA GPS on the given UART/pins (LoRa+GPS cap or external module).
  void beginGps(int uartNum, int rxPin, int txPin, uint32_t baud);
  void endGps();   // H18: release the GPS UART (end+delete); call before restart / on disable
  // Feed bytes from the GPS; call frequently from loop(). Updates obs if a fix
  // is available. Returns true when a new fix was just parsed.
  bool pollGps();

  void setManual(double lat, double lon, double altM);
  bool setFromGrid(const String& grid);    // Maidenhead -> lat/lon (centre)

  const Observer& obs() const { return _obs; }
  bool gpsHasFix() const { return _hasFix; }
  int  gpsSats()  const { return _sats; }
  double gpsSpeedKmh() const { return _speedKmh; }   // ground speed (km/h)
  double gpsCourseDeg() const { return _courseDeg; } // course over ground (deg true)
  double gpsHdop() const { return _hdop; }           // horizontal dilution of precision

  static const int MAX_VIEW = 32;
  int  gpsViewCount() const { return _viewN; }
  const GpsSat& gpsView(int i) const { return _view[i]; }

  static String toGrid(double lat, double lon);  // 6-char Maidenhead
  // Maidenhead -> lat/lon (square centre) without mutating any Location.
  static bool gridToLatLon(const String& grid, double& lat, double& lon);

private:
  Observer _obs;
  bool     _hasFix = false;
  int      _sats   = 0;
  double   _speedKmh = 0.0;
  double   _courseDeg = 0.0;
  double   _hdop = 0.0;
  bool     _gpsOn  = false;
  GpsSat   _view[MAX_VIEW];  int _viewN  = 0;   // committed in-view list
  GpsSat   _build[MAX_VIEW]; int _buildN = 0;   // assembling current NMEA cycle
  char     _nmea[100];       int _nmeaLen = 0;  // raw NMEA line accumulator
  uint32_t _lastView = 0;                       // millis of last commit (stale clear)
  void feedNmeaChar(char c);                    // accumulate NMEA, parse GSV, commit on RMC
  void parseGsv(char* sentence);                // one GSV sentence -> _build upsert
};
