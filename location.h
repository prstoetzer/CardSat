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

class Location {
public:
  // Start a NMEA GPS on the given UART/pins (LoRa+GPS cap or external module).
  void beginGps(int uartNum, int rxPin, int txPin, uint32_t baud);
  // Feed bytes from the GPS; call frequently from loop(). Updates obs if a fix
  // is available. Returns true when a new fix was just parsed.
  bool pollGps();

  void setManual(double lat, double lon, double altM);
  bool setFromGrid(const String& grid);    // Maidenhead -> lat/lon (centre)

  const Observer& obs() const { return _obs; }
  bool gpsHasFix() const { return _hasFix; }
  int  gpsSats()  const { return _sats; }

  static String toGrid(double lat, double lon);  // 6-char Maidenhead

private:
  Observer _obs;
  bool     _hasFix = false;
  int      _sats   = 0;
  bool     _gpsOn  = false;
};
