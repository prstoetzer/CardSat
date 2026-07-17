#pragma once
// ===========================================================================
//  net.h  -  WiFi + HTTPS downloads (AMSAT GP, SatNOGS transponders)
// ===========================================================================
#include <Arduino.h>
#include <WiFiClient.h>
// 0.9.43: HTTPS now uses ESP_SSLClient (BearSSL) layered on a plain WiFiClient,
// instead of the core's WiFiClientSecure (mbedTLS). mbedTLS's handshake buffers are
// a fixed ~32 KB contiguous block that this no-PSRAM part can't reliably provide
// (the display sprite pins the largest block just under it), which made HTTPS to
// larger-cert hosts fail with "SSL - Memory allocation failed". ESP_SSLClient lets us
// set the RX/TX buffer sizes explicitly (setBufferSizes), so the handshake fits the
// contiguous block that IS available -- no sprite-freeing hacks required.
#include <ESP_SSLClient.h>

// TLS buffer sizes for ESP_SSLClient (bytes). CRITICAL: BearSSL's RX buffer must hold
// a FULL TLS record (up to 16 KB) for any server that doesn't support MFLN -- which is
// nearly all public servers. A smaller RX truncates or fails the read: 4096 made NOAA
// return an empty response and hams.at deliver only a partial body. So RX is sized for a
// full record. TX does NOT limit upload size (BearSSL fragments a large write() across
// multiple TLS records automatically) -- it only sets the per-record payload. 512 is
// proven on device: LoTW/Cloudlog uploads streamed with zeroWrites=0 (no send-window
// stalls) and an effective write size of ~1024. This ~16 KB RX + 512 TX + ~6 KB BearSSL
// stack (~23 KB) is still under mbedTLS's ~32 KB contiguous demand -- and the single
// largest allocation (16 KB RX) fits the block that mbedTLS's 32 KB couldn't. Full-duplex
// (separate RX/TX), matching our request-then-response HTTP pattern.
#define SSL_RX_BUF 16384
#define SSL_TX_BUF 512

// One access point returned by a WiFi scan.
struct WifiAp {
  char    ssid[33];
  int8_t  rssi;     // signal strength, dBm
  bool    enc;      // true = secured (needs a password)
};

class Net {
public:
  bool connect(const String& ssid, const String& pass, uint32_t timeoutMs = 15000);
  String lastSsid_, lastPass_;   // remembered by connect() so hardResetWifi() can re-begin explicitly
  int  scanWifi(WifiAp* out, int maxAps);   // scan nearby APs (blocking); count or -1
  bool connected();
  void syncTimeNtp();                       // sets system clock via NTP (UTC)

  // GET a URL over HTTPS into `out`. Returns false on HTTP/transport error.
  bool httpsGet(const String& url, String& out, size_t maxBytes = 200000);
  void logConnectProbe(const String& url);   // diagnostic: raw TLS connect + errno on GET failure
  void pcbCounts(int& active, int& timeWait); // diagnostic: LWIP TCP PCB list sizes

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

  // POST a file as multipart/form-data (used for the LoTW .tq8 upload). Reads
  // the server's text response into 'response' (capped). Mirrors the GET TLS
  // pattern (insecure CA, busy-guard, fd cleanup). Returns true on HTTP 200.
  bool httpsPostMultipart(const String& url, const char* fieldName,
                          const char* filePath, const char* fileName,
                          const char* contentType, String& response,
                          size_t maxResp = 8192);

  // POST a JSON (or other text) body to 'url' and capture the server's response.
  // Accepts http:// or https:// (self-hosted Cloudlog/Wavelog instances may be on a
  // LAN over plain HTTP). 'redactBody' (default true) keeps secrets out of the serial
  // log; pass the request body in 'body'. Returns true on a 2xx HTTP status.
  bool httpsPostJson(const String& url, const String& body, String& response,
                     const char* contentType = "application/json",
                     size_t maxResp = 8192, bool redactBody = true);

  // POST a JSON body that is too large to hold in RAM all at once: the body is read
  // from a file on Store::fs() (SD or internal LittleFS) and STREAMED to the socket in
  // small chunks, so only one ~1 KB buffer is live regardless of body size. Crucially
  // this also keeps the upload to a SINGLE TLS handshake (one body, one connect),
  // unlike batching, which on this no-PSRAM heap multiplies the handshake count and the
  // post-TLS fragmentation that degrades each subsequent handshake. The handshake is
  // performed before the request is streamed. Returns true on a 2xx HTTP status.
  bool httpsPostJsonFile(const String& url, const char* bodyFilePath, String& response,
                         const char* contentType = "application/json",
                         size_t maxResp = 8192);

  // Best-effort heap defragment before a TLS handshake. A failed prior fetch can leave
  // the heap fragmented so the largest contiguous block falls below what mbedTLS needs
  // (TLS_MIN_BLOCK), which then makes the next connect() fail with -1 -- the cascade the
  // user hit after a mistyped upload URL. This gives async socket/TLS buffers from the
  // previous attempt time to actually return to the heap and the allocator a chance to
  // coalesce adjacent free blocks, retrying a few times. Returns the largest free block
  // (bytes) afterwards, so the caller can decline gracefully if it's still too small
  // rather than entering a handshake that fails messily and fragments further.
  size_t reclaimHeapForTls();   // 0.9.41: now instance (uses the WiFi-cycle guard + hardResetWifi)

  // Diagnostics from the most recent httpsGet (for on-screen / serial errors).
  int    lastCode = 0;     // HTTP status (>0) or HTTPClient error (<0)
  // Sentinel for "declined the handshake before connecting because the largest
  // contiguous heap block was below TLS_MIN_BLOCK". Distinct, well outside the
  // HTTPClient error range, and NEGATIVE so callers that treat lastCode<0 as a
  // transport failure (e.g. the Cloudlog reboot-to-clean-heap prompt) trigger on
  // it -- a fresh boot is exactly the cure when the heap is fragmented this way.
  static const int HEAP_ABORT_CODE = -100;
  String lastErr  = "";    // short human-readable reason (UI text)

  // Typed download outcome, so retry/abort policy branches on a code rather than on
  // display text (which drifts and silently breaks string comparisons). Set by the
  // file-download path; translated to lastErr text at the point it is raised.
  enum class DownloadError : uint8_t {
    None = 0,        // success
    ConnectFailed,   // socket/TLS never established (lastCode <= 0)
    HttpError,       // server returned a non-200 status
    StorageFull,     // declared size won't fit the filesystem (do NOT retry)
    WriteFailed,     // filesystem write error mid-stream
    ShortRead,       // fewer bytes than the declared Content-Length
    EmptyBody,       // 200 with no body
    TooManyRedirects
  };
  DownloadError lastDlErr = DownloadError::None;

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
  static uint32_t TLS_MIN_BLOCK;                // min contiguous heap to attempt a TLS POST (see net.cpp preamble)

  // --- 0.9.41 proactive WiFi-cycle defrag (REVERSIBLE via TLS_WIFI_CYCLE) -------
  // When the passive coalesce-wait in reclaimHeapForTls() still leaves the largest
  // free block below TLS_MIN_BLOCK, optionally do ONE WiFi disconnect(true)+reconnect
  // (via hardResetWifi) to forcibly return LWIP/mbedTLS async buffers to the heap so
  // adjacent free blocks merge -- the last resort before declining the handshake.
  // Gated so it only fires when really needed and never twice in quick succession.
  //
  // TO DISABLE: set TLS_WIFI_CYCLE = false (reverts to passive-wait-only behaviour).
  // See docs/design/HEAP_WIFI_CYCLE.md.
  static bool     TLS_WIFI_CYCLE;               // master enable for the proactive cycle
  static uint32_t WIFI_CYCLE_MIN_GAP_MS;        // min gap between proactive cycles
  uint32_t        lastWifiCycleMs = 0;          // millis() of the last proactive cycle (0 = never)
  bool            wifiCycleIneffective = false; // set once a cycle fails to grow the largest
                                                // block: the fragmentation is mid-heap persistent
                                                // allocations (not socket buffers), so further
                                                // cycles are pure cost -- skip them this session.
  // ----------------------------------------------------------------------------

  // Optional hook invoked around EVERY outbound TLS session: busy(true) just
  // before a connection is opened, busy(false) after it closes. The app uses it
  // to release its LAN listener sockets (rigctld/rotctld/web) for the duration of
  // the fetch -- on the socket-limited, no-PSRAM ESP32-S3 those listeners (plus a
  // kept-alive browser tab) can otherwise starve the outbound HTTPS connect and
  // it gets refused. Set once at startup; leaving it null disables the behaviour.
  // Guarding here (the single choke point) covers every fetch -- GP, weather,
  // space weather, AMSAT, transponders, QRZ -- without per-call-site discipline.
  static void (*onTlsBusy)(bool busy);
  static bool tlsHeapTooLow();   // contiguous heap too small for a TLS handshake
                                 // (USB CAT engaged leaves ~7 KB largest block)
};
