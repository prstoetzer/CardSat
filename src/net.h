#pragma once
// ===========================================================================
//  net.h  -  WiFi + HTTPS downloads (AMSAT GP, SatNOGS transponders)
// ===========================================================================
#include <Arduino.h>

// One access point returned by a WiFi scan.
struct WifiAp {
  char    ssid[33];
  int8_t  rssi;     // signal strength, dBm
  bool    enc;      // true = secured (needs a password)
};

class Net {
public:
  bool connect(const String& ssid, const String& pass, uint32_t timeoutMs = 15000);
  int  scanWifi(WifiAp* out, int maxAps);   // scan nearby APs (blocking); count or -1
  bool connected();
  void syncTimeNtp();                       // sets system clock via NTP (UTC)

  // GET a URL over HTTPS into `out`. Returns false on HTTP/transport error.
  bool httpsGet(const String& url, String& out, size_t maxBytes = 200000);

  // GET a URL over HTTPS straight into a LittleFS file (no large RAM buffer).
  // Essential for the GP file: a ~75 KB body can't be held as one contiguous
  // String on the fragmented no-PSRAM heap (String growth silently truncates).
  bool httpsGetToFile(const String& url, const char* path,
                      size_t maxBytes = 400000, size_t* written = nullptr);

  // Convenience wrappers.
  bool fetchGp(const String& url, String& out);    // AMSAT GP/OMM JSON array
  bool fetchGpToFile(const String& url, const char* path);  // GP -> cache file
  bool fetchSatnogsTransmitters(uint32_t norad, String& out);

  // Diagnostics from the most recent httpsGet (for on-screen / serial errors).
  int    lastCode = 0;     // HTTP status (>0) or HTTPClient error (<0)
  String lastErr  = "";    // short human-readable reason
};
