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
  // `firstByteMs` (optional): for slow-first-response hosts (e.g. NOAA SWPC),
  // extends the connect/handshake/first-byte allowance so a slow-but-healthy
  // negotiation isn't aborted. 0 (default) keeps the standard 10-15s timeouts.
  bool httpsGetToFile(const String& url, const char* path,
                      size_t maxBytes = 400000, size_t* written = nullptr,
                      uint32_t firstByteMs = 0);

  // Same, but retries a few times with backoff. Transient TLS/Wi-Fi hiccups are
  // the common cause of a failed or short download on this hardware; a couple of
  // retries turns most of those into successes. `attempts` total tries.
  bool httpsGetToFileRetry(const String& url, const char* path,
                           size_t maxBytes = 400000, size_t* written = nullptr,
                           int attempts = 3, uint32_t firstByteMs = 0);

  // Convenience wrappers.
  bool fetchGp(const String& url, String& out);    // AMSAT GP/OMM JSON array
  bool fetchGpToFile(const String& url, const char* path);  // GP -> cache file
  bool fetchSatnogsTransmitters(uint32_t norad, String& out);
  bool fetchSatnogsTransmittersToFile(uint32_t norad, const char* path);  // tx -> cache file

  // Diagnostics from the most recent httpsGet (for on-screen / serial errors).
  int    lastCode = 0;     // HTTP status (>0) or HTTPClient error (<0)
  String lastErr  = "";    // short human-readable reason

  // --- Socket-failure recovery -------------------------------------------
  // Connect-level failures (code <= 0, e.g. the -1 returned once the LWIP socket
  // pool is wedged) tend to cascade: once a few fds leak, every subsequent
  // connect() also returns -1. We count consecutive connect-level failures and,
  // after a threshold, hard-reset the WiFi stack (disconnect(true) flushes the
  // whole socket pool) -- the only reliable way out. A successful transfer clears
  // the counter. noteConnResult() is called by the fetch paths; hardResetWifi()
  // is also callable directly.
  int  connFails = 0;                          // consecutive connect-level failures
  int  failedResets = 0;                        // consecutive hard resets that didn't recover
  void noteConnResult(int code);               // update counter; auto-reset at threshold
  bool hardResetWifi();                         // disconnect(true) + reconnect; flush pool
  bool recoverExhausted = false;                // set when hard resets keep failing; the
                                                // app prompts the user for a reboot
  static int  RECOVER_AFTER;                    // failures before a hard reset (default 3)
  static int  REBOOT_AFTER;                     // failed hard resets before prompting reboot
  static uint32_t INTER_FETCH_MS;               // settle delay before each TLS session

  // Optional hook invoked around EVERY outbound TLS session: busy(true) just
  // before a connection is opened, busy(false) after it closes. The app uses it
  // to release its LAN listener sockets (rigctld/rotctld/web) for the duration of
  // the fetch -- on the socket-limited, no-PSRAM ESP32-S3 those listeners (plus a
  // kept-alive browser tab) can otherwise starve the outbound HTTPS connect and
  // it gets refused. Set once at startup; leaving it null disables the behaviour.
  // Guarding here (the single choke point) covers every fetch -- GP, weather,
  // space weather, AMSAT, transponders, QRZ -- without per-call-site discipline.
  static void (*onTlsBusy)(bool busy);
};
