#pragma once
// ===========================================================================
//  net.h  -  WiFi + HTTPS downloads (AMSAT keps, SatNOGS transponders)
// ===========================================================================
#include <Arduino.h>

class Net {
public:
  bool connect(const String& ssid, const String& pass, uint32_t timeoutMs = 15000);
  bool connected();
  void syncTimeNtp();                       // sets system clock via NTP (UTC)

  // GET a URL over HTTPS into `out`. Returns false on HTTP/transport error.
  bool httpsGet(const String& url, String& out, size_t maxBytes = 200000);

  // Convenience wrappers.
  bool fetchAmsatTle(String& out);                  // bare 3-line text
  bool fetchSatnogsTransmitters(uint32_t norad, String& out);

  // Diagnostics from the most recent httpsGet (for on-screen / serial errors).
  int    lastCode = 0;     // HTTP status (>0) or HTTPClient error (<0)
  String lastErr  = "";    // short human-readable reason
};
