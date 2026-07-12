// ===========================================================================
//  net.cpp
// ===========================================================================
#include "net.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <limits.h>   // LONG_MAX (chunked-transfer sentinel in httpsGetToFile)
#include "storage.h"
#include <time.h>
#include <esp_heap_caps.h>   // heap_caps_get_largest_free_block(): contiguous block,
#include <fcntl.h>           // fcntl(F_GETFD) for the socket-FD-count diagnostic
#include <lwip/sockets.h>    // LWIP_SOCKET_OFFSET / CONFIG_LWIP_MAX_SOCKETS
// TCP PCB-pool diagnostic (socket exhaustion shows here, not in the fd table). The
// PCB-list globals live in LWIP's private header, which the Arduino core may or may
// not put on the include path -- gate on its presence so the build never breaks.
#if defined(__has_include)
#  if __has_include(<lwip/priv/tcp_priv.h>)
#    include <lwip/tcp.h>
#    include <lwip/priv/tcp_priv.h>
#    define CARDSAT_HAVE_TCP_PCBS 1
#  endif
#endif
                             // which is what the TLS handshake actually needs

// TLS-session hook (see net.h). Null by default; the app installs it at startup.
void (*Net::onTlsBusy)(bool) = nullptr;

// Log the full heap picture (not just the single largest block) at a labeled point.
// Retained as a diagnostic on the upload path: a fragmented heap can show a healthy
// largest block while a TLS handshake -- which needs several allocations at once --
// still fails on a later, smaller one. free_blocks (the count of separate free chunks)
// and total_free vs largest expose that. This was the key instrument in tracking down
// the Cloudlog upload failure (see docs/design/CLOUDLOG_UPLOAD_POSTMORTEM.md).
static void logHeapDetail(const char* where) {
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_8BIT);
  Serial.printf("[heap] %s: free=%u largest=%u min_free_ever=%u free_blocks=%u alloc_blocks=%u\n",
                where,
                (unsigned)info.total_free_bytes,
                (unsigned)info.largest_free_block,
                (unsigned)info.minimum_free_bytes,
                (unsigned)info.free_blocks,
                (unsigned)info.allocated_blocks);
}

// Redact secrets from a URL before logging it. QRZ (and any future credentialed
// endpoint) carry username/password/session keys in the query string; logging the
// raw URL to serial would leak them. This masks the VALUE of any sensitive parameter
// (password, passwd, pwd, username, user, s, key, apikey, api_key, token) while
// keeping the rest of the URL useful for debugging. Parameters may be separated by
// '&' or ';' (QRZ uses ';'). Case-insensitive on the parameter name.
static String redactUrl(const String& url) {
  static const char* kSecret[] = { "password", "passwd", "pwd", "username", "user",
                                   "s", "key", "apikey", "api_key", "token" };
  int q = url.indexOf('?');
  if (q < 0) return url;                          // no query string, nothing to mask
  String out = url.substring(0, q + 1);
  String qs = url.substring(q + 1);
  int i = 0, len = qs.length();
  while (i < len) {
    int sep = len;
    for (int j = i; j < len; ++j) { char ch = qs[j]; if (ch == '&' || ch == ';') { sep = j; break; } }
    String pair = qs.substring(i, sep);
    int eq = pair.indexOf('=');
    if (eq > 0) {
      String name = pair.substring(0, eq);
      String lname = name; lname.toLowerCase();
      bool secret = false;
      for (size_t k = 0; k < sizeof(kSecret) / sizeof(kSecret[0]); ++k)
        if (lname == kSecret[k]) { secret = true; break; }
      out += secret ? (name + "=***") : pair;
    } else {
      out += pair;
    }
    if (sep < len) out += qs[sep];                // preserve the original separator
    i = sep + 1;
  }
  return out;
}

// Socket-failure recovery tunables (see net.h).
int      Net::RECOVER_AFTER  = 3;       // consecutive connect failures before reset
int      Net::REBOOT_AFTER   = 3;       // failed hard resets in a row -> prompt reboot
uint32_t Net::INTER_FETCH_MS = 200;     // settle delay before each TLS session so a
                                        // just-closed socket leaves the LWIP pool
// --- TLS_MIN_BLOCK: pre-handshake contiguous-heap gate -----------------------
// Written for the mbedTLS era (~32 KB contiguous per handshake); since 0.9.43 the
// transport is BearSSL, whose largest single allocation is the 16 KB RX buffer plus
// LWIP send-path room -- comfortably under the resident largest block (~31.7 KB), and
// 0.9.53's heap reclaim restored the coexistence margin that made multi-batch uploads
// reliable. The floor below still stands as an OOM tripwire; it only gates the POST paths
// (GETs don't call reclaim). 28000 sits below the resident block so real attempts
// proceed, and above a genuinely-exhausted heap so the upload declines + offers a
// reboot rather than failing mid-handshake with a bare -1.
uint32_t Net::TLS_MIN_BLOCK  = 28000;   // below the ~31.7 KB resident block; catches real OOM

// --- 0.9.41 proactive WiFi-cycle defrag (see net.h / docs/design/HEAP_WIFI_CYCLE.md) ---
// Set TLS_WIFI_CYCLE=false to revert to the passive-wait-only reclaim behaviour.
bool     Net::TLS_WIFI_CYCLE      = true;   // enable the last-resort WiFi cycle
uint32_t Net::WIFI_CYCLE_MIN_GAP_MS = 30000; // don't cycle more than once per 30 s

// Flush the LWIP socket pool by tearing the STA association down hard, then
// reconnect with the credentials WiFi already holds. This is the reliable cure
// once fds wedge and connect() starts returning -1.
bool Net::hardResetWifi() {
  Serial.println("[net] hard WiFi reset (full radio re-init)");
  // Fully re-initialise the radio rather than reconnect() the old association. On
  // this part, after the PHY has been powered down (here, or by the charge-screen
  // WIFI_OFF), a bare reconnect() can reassociate (IP/RSSI look fine) yet leave the
  // stack in a degraded state where every outbound connect() returns -1 -- the
  // ~10 KB WiFi working buffer isn't re-claimed (visible as the largest free block
  // sitting ~10 KB too HIGH, e.g. 31.7 KB instead of ~21.5 KB, with connects then
  // failing). A clean OFF -> STA -> begin() cycle forces that buffer to be
  // re-allocated. See docs/design/HEAP_WIFI_CYCLE.md.
  size_t blkBefore = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  WiFi.disconnect(true);                 // disconnect + radio off (keep stored config)
  WiFi.mode(WIFI_OFF);
  delay(250);
  WiFi.mode(WIFI_STA);
  if (lastSsid_.length()) WiFi.begin(lastSsid_.c_str(), lastPass_.c_str());
  else                    WiFi.begin();   // fall back to NVS-stored credentials
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) delay(150);
  bool ok = (WiFi.status() == WL_CONNECTED);
  size_t blkAfter = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  Serial.printf("[net] hard reset %s (largest block %u -> %u, mode %d)\n",
                ok ? "recovered" : "still down",
                (unsigned)blkBefore, (unsigned)blkAfter, (int)WiFi.getMode());
  if (ok) connFails = 0;
  return ok;
}

// Update the consecutive-failure counter from a fetch result. A success (any
// HTTP code > 0, i.e. we actually reached the server) clears it; a connect-level
// failure (code <= 0) increments it, and once we cross the threshold we hard-reset
// the WiFi stack to clear a wedged socket pool before the next attempt.
void Net::noteConnResult(int code) {
  if (code > 0) { connFails = 0; recoverExhausted = false; failedResets = 0; return; }
  if (++connFails >= RECOVER_AFTER) {
    Serial.printf("[net] %d consecutive connect failures -> recovering\n", connFails);
    if (hardResetWifi()) {
      failedResets = 0;                         // recovered: clear the reboot counter
    } else if (++failedResets >= REBOOT_AFTER) {
      Serial.println("[net] hard resets exhausted -> requesting reboot prompt");
      recoverExhausted = true;                  // app surfaces a reboot prompt
    }
    connFails = 0;                              // give the next attempt a clean count
  }
}

// RAII guard: fires onTlsBusy(true) when the FIRST guard on the stack is entered
// and onTlsBusy(false) when the LAST one leaves, on every exit path. The depth
// count makes it reentrant: httpsGetToFileRetry can hold a guard across its whole
// retry loop while each inner httpsGetToFile also guards, without the inner scope
// resuming (rebuilding the listeners) between attempts.
namespace {
int g_tlsDepth = 0;
struct TlsBusyGuard {
  TlsBusyGuard()  { if (g_tlsDepth++ == 0 && Net::onTlsBusy) Net::onTlsBusy(true);  }
  ~TlsBusyGuard() { if (--g_tlsDepth == 0 && Net::onTlsBusy) Net::onTlsBusy(false); }
};
}

bool Net::connect(const String& ssid, const String& pass, uint32_t timeoutMs) {
  if (ssid.length() == 0) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(150);
  bool ok = (WiFi.status() == WL_CONNECTED);
  if (ok) { lastSsid_ = ssid; lastPass_ = pass; }   // remember for hardResetWifi()
  return ok;
}

bool Net::connected() { return WiFi.status() == WL_CONNECTED; }

int Net::scanWifi(WifiAp* out, int maxAps) {
  if (!out || maxAps <= 0) return 0;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();                        // drop any association for a clean scan
  delay(50);
  int n = WiFi.scanNetworks();              // blocking, a few seconds
  if (n < 0) { WiFi.scanDelete(); return -1; }
  int count = 0;
  for (int i = 0; i < n; ++i) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;          // skip hidden / blank SSIDs
    int8_t r = (int8_t)WiFi.RSSI(i);
    bool   e = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    int found = -1;                         // de-dup by SSID, keep the strongest
    for (int j = 0; j < count; ++j) if (s == out[j].ssid) { found = j; break; }
    if (found >= 0) {
      if (r > out[found].rssi) { out[found].rssi = r; out[found].enc = e; }
      continue;
    }
    if (count >= maxAps) continue;
    strncpy(out[count].ssid, s.c_str(), sizeof(out[count].ssid) - 1);
    out[count].ssid[sizeof(out[count].ssid) - 1] = 0;
    out[count].rssi = r;
    out[count].enc  = e;
    count++;
  }
  WiFi.scanDelete();                        // free the scan result buffer
  for (int i = 1; i < count; ++i) {         // insertion sort, strongest first
    WifiAp key = out[i]; int j = i - 1;
    while (j >= 0 && out[j].rssi < key.rssi) { out[j + 1] = out[j]; --j; }
    out[j + 1] = key;
  }
  return count;
}

void Net::syncTimeNtp() {
  // UTC (no offset, no DST). Pool servers.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm ti;
  for (int i = 0; i < 40 && !getLocalTime(&ti, 250); ++i) { /* wait */ }
}

// Diagnostic for a connect-level GET failure. HTTPClient reports only a generic code;
// this probes a raw TLS connect to the same host and logs the underlying result +
// mbedTLS/socket error string. A socket/allocation errno here (connects failing while
// heap is fine) points at LWIP socket-pool / TIME_WAIT exhaustion; a TLS/memory error
// points at the handshake. Cheap, only runs on failure.
void Net::logConnectProbe(const String& url) {
  // Lightweight connect-failure diagnostic. mbedTLS reports "SSL - Memory allocation
  // failed" both on true OOM and when the LWIP socket layer is exhausted (no FD/PCB to
  // bind), which look identical in the error string. Count live socket FDs so a field
  // log distinguishes the two. (An earlier version also ran a second raw TLS connect to
  // surface the errno; that was removed for release -- it doubled the connect cost on
  // every failure and the FD count plus lastErr is enough to triage.)
  #ifndef LWIP_SOCKET_OFFSET
  #define LWIP_SOCKET_OFFSET 54
  #endif
  #ifndef CONFIG_LWIP_MAX_SOCKETS
  #define CONFIG_LWIP_MAX_SOCKETS 16
  #endif
  int openFds = 0;
  for (int fd = LWIP_SOCKET_OFFSET; fd < LWIP_SOCKET_OFFSET + CONFIG_LWIP_MAX_SOCKETS; ++fd) {
    if (fcntl(fd, F_GETFD) != -1) openFds++;
  }
  // The fd table can look clean (0/16) while the LWIP TCP PCB pool underneath is
  // exhausted -- sockets in TIME_WAIT hold a tcp_pcb without holding an fd. Count
  // the active + TIME_WAIT PCBs so a field log distinguishes true socket-pool
  // exhaustion (connects refused while heap is fine) from a heap/handshake problem.
#if CARDSAT_HAVE_TCP_PCBS
  int nActive = 0, nTw = 0;
  pcbCounts(nActive, nTw);
  Serial.printf("[net] connect failed; socket FDs open: %d / %d; TCP PCBs active=%d time_wait=%d\n",
                openFds, (int)CONFIG_LWIP_MAX_SOCKETS, nActive, nTw);
#else
  Serial.printf("[net] connect failed; socket FDs open: %d / %d\n",
                openFds, (int)CONFIG_LWIP_MAX_SOCKETS);
#endif
}

// Count LWIP TCP PCBs in the active and TIME_WAIT lists (0 written when the private
// header isn't available). Used both by the connect-failure probe and the
// before/after-stop leak probe. Investigation instrumentation (0.9.43).
void Net::pcbCounts(int& active, int& timeWait) {
  active = 0; timeWait = 0;
#if CARDSAT_HAVE_TCP_PCBS
  for (struct tcp_pcb* p = tcp_active_pcbs; p != nullptr; p = p->next) active++;
  for (struct tcp_pcb* p = tcp_tw_pcbs;     p != nullptr; p = p->next) timeWait++;
#endif
}

// GET the URL into a String (single attempt). NOTE: deliberately NOT wrapped in a
// connect-retry loop. A retry was tried (0.9.42) on the theory that QRZ hit the same
// handshake knife-edge the file fetches survive via httpsGetToFileRetry -- but on-device
// logs showed QRZ failing all attempts every time, so retrying never recovered it and
// only churned the LWIP socket pool faster (rapid connect/close leaves PCBs in TIME_WAIT
// that a WiFi radio reset doesn't flush), which contributed to a whole-stack connect()
// wedge after a session. Kept single-shot; the caller handles a failed GET.
bool Net::httpsGet(const String& url, String& out, size_t maxBytes) {
  lastCode = 0; lastErr = "";
  if (!connected()) { lastErr = "no WiFi"; return false; }
  TlsBusyGuard _tls;   // free the app's LAN listener sockets for this session
  if (INTER_FETCH_MS) delay(INTER_FETCH_MS);   // let a just-closed socket leave the pool

  Serial.printf("[net] GET %s\n", redactUrl(url).c_str());
  Serial.printf("[net] heap before TLS: %u (largest block %u), IP %s, RSSI %d\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());

  // ESP_SSLClient is a generic Client, not the NetworkClient HTTPClient needs, so we
  // hand-roll the GET over the raw TLS client (see httpsGetToFile). Single hop: this
  // path serves QRZ (no redirects) and the deprecated fetchGp; the real GP catalog uses
  // httpsGetToFile (which follows one redirect).
  bool https = url.startsWith("https:");
  int sspos = url.indexOf("://"); int st = (sspos >= 0) ? sspos + 3 : 0;
  int slash = url.indexOf('/', st);
  String hostPort = (slash >= 0) ? url.substring(st, slash) : url.substring(st);
  String reqPath  = (slash >= 0) ? url.substring(slash) : "/";
  int port = https ? 443 : 80;
  int colon = hostPort.indexOf(':');
  String host;
  if (colon >= 0) { host = hostPort.substring(0, colon); port = hostPort.substring(colon + 1).toInt(); }
  else host = hostPort;

  WiFiClient transport;          // plain TCP transport under the TLS layer
  ESP_SSLClient client;          // BearSSL TLS with app-sized buffers (see SSL_RX_BUF)
  struct ClientStop { ESP_SSLClient& c; ~ClientStop() { c.stop(); } } _cstop{client};
  client.setClient(&transport);  // MUST precede connect; transport must outlive client
  client.setInsecure();          // no cert verification (public GP/QRZ data)
  client.setBufferSizes(SSL_RX_BUF, SSL_TX_BUF);
  client.setTimeout(15000);

  uint32_t t0 = millis();
  if (!client.connect(host.c_str(), port)) {
    lastCode = -1; lastErr = "connect failed"; noteConnResult(-1);
    Serial.printf("[net] GET connect failed in %ums\n", (unsigned)(millis() - t0));
    return false;
  }

  String req;
  req.reserve(reqPath.length() + host.length() + 96);
  req  = "GET " + reqPath + " HTTP/1.1\r\n";
  req += "Host: " + host + "\r\n";
  req += "User-Agent: CardSat-Cardputer/1.0\r\n";
  req += "Accept: */*\r\n";
  req += "Connection: close\r\n\r\n";
  client.print(req);
  client.flush();

  // Status line.
  uint32_t rdStart = millis();
  while (!client.available() && millis() - rdStart < 15000) delay(10);
  String statusLine = client.available() ? client.readStringUntil('\n') : String("");
  int code = 0;
  { int sp = statusLine.indexOf(' ');
    if (sp >= 0) code = statusLine.substring(sp + 1, sp + 4).toInt(); }
  if (statusLine.length() == 0) {
    lastCode = 0; lastErr = "no response from server"; noteConnResult(0);
    return false;
  }
  lastCode = code;
  noteConnResult(code);
  if (code != 200) {
    lastErr = "HTTP " + String(code);
    Serial.printf("[net] GET failed: code=%d\n", code);
    return false;
  }

  // Headers: capture Content-Length and chunked flag.
  int len = -1; bool chunked = false;
  uint32_t hdrStart = millis();
  while (millis() - hdrStart < 15000) {
    if (!client.available()) { if (!client.connected()) break; delay(5); continue; }
    String h = client.readStringUntil('\n');
    if (h == "\r" || h.length() == 0) break;
    String hl = h; hl.toLowerCase();
    if (hl.startsWith("content-length:")) len = h.substring(15).toInt();
    else if (hl.startsWith("transfer-encoding:") && hl.indexOf("chunked") >= 0) chunked = true;
  }

  out = "";
  if (len > 0) out.reserve(min((size_t)len + 16, maxBytes));

  uint8_t buf[512];
  size_t total = 0;
  uint32_t lastRx = millis();
  long chunkRemain = chunked ? -1 : (len > 0 ? len : LONG_MAX);
  bool done = false;
  while (total < maxBytes && !done) {
    if (chunked && chunkRemain <= 0) {
      if (!client.available()) {
        if (!client.connected()) break;
        if (millis() - lastRx > 10000) break;
        delay(5); continue;
      }
      String szLine = client.readStringUntil('\n'); szLine.trim();
      if (szLine.length() == 0) continue;
      long csz = strtol(szLine.c_str(), nullptr, 16);
      if (csz <= 0) { done = true; break; }
      chunkRemain = csz; lastRx = millis();
    }
    size_t avail = client.available();
    if (avail) {
      size_t want = sizeof(buf);
      if ((long)want > chunkRemain) want = (size_t)chunkRemain;
      int r = client.readBytes(buf, min(avail, want));
      if (r <= 0) break;
      out.concat((const char*)buf, r);
      total += r;
      if (chunked || len > 0) chunkRemain -= r;
      lastRx = millis();
      if (!chunked && len > 0 && total >= (size_t)len) { done = true; break; }
    } else {
      if (!chunked && len > 0 && total >= (size_t)len) { done = true; break; }
      if (len <= 0 && !chunked && !client.connected() && !client.available() &&
          millis() - lastRx > 500) break;
      if (millis() - lastRx > 10000) break;
      delay(5);
    }
  }

  Serial.printf("[net] received %u bytes (declared %d), heap now %u (largest %u)\n",
                (unsigned)total, len, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  if (out.length() == 0) { lastErr = "empty body"; return false; }
  if (!chunked && len > 0 && total < (size_t)len) {
    lastErr = "short read " + String((unsigned)total) + "/" + String(len);
    return false;
  }
  return true;
}

bool Net::httpsGetToFile(const String& url, const char* path,
                         size_t maxBytes, size_t* written, uint32_t firstByteMs) {
  lastCode = 0; lastErr = "";
  if (written) *written = 0;
  if (!connected()) { lastErr = "no WiFi"; return false; }
  TlsBusyGuard _tls;   // suspend the app's LAN listeners for this session
  if (INTER_FETCH_MS) delay(INTER_FETCH_MS);

  Serial.printf("[net] GET %s -> %s\n", redactUrl(url).c_str(), path);
  Serial.printf("[net] heap before TLS: %u (largest block %u), IP %s, RSSI %d\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());

  // Some hosts (notably NOAA SWPC -- government-hosted, load-balanced, strict TLS)
  // are slow on the FIRST response/handshake from a fresh client/IP. When the
  // caller passes a firstByteMs, give the connect + first-byte phase a longer
  // allowance so that slow-but-healthy negotiation isn't aborted as a timeout.
  uint32_t connectMs = (firstByteMs > 15000) ? firstByteMs : 15000;

  // ESP_SSLClient (BearSSL) is a generic Arduino Client, NOT the ESP32 core's
  // NetworkClient that HTTPClient::begin() requires -- so we can't use HTTPClient here.
  // Instead we hand-roll the GET over the raw TLS client, exactly like the POST paths
  // (and the library's own examples). Supports one redirect hop (AMSAT GP may 301/302).
  String curUrl = url;
  for (int hop = 0; hop < 2; ++hop) {
    // Parse scheme/host/path/port.
    bool https = curUrl.startsWith("https:");
    int sspos = curUrl.indexOf("://"); int st = (sspos >= 0) ? sspos + 3 : 0;
    int slash = curUrl.indexOf('/', st);
    String hostPort = (slash >= 0) ? curUrl.substring(st, slash) : curUrl.substring(st);
    String reqPath  = (slash >= 0) ? curUrl.substring(slash) : "/";
    int port = https ? 443 : 80;
    int colon = hostPort.indexOf(':');
    String host;
    if (colon >= 0) { host = hostPort.substring(0, colon); port = hostPort.substring(colon + 1).toInt(); }
    else host = hostPort;

    WiFiClient transport;          // plain TCP transport under the TLS layer
    ESP_SSLClient client;          // BearSSL TLS with app-sized buffers (see SSL_RX_BUF)
    struct ClientStop { ESP_SSLClient& c; ~ClientStop() { c.stop(); } } _cstop{client};
    client.setClient(&transport);  // MUST precede connect; transport must outlive client
    client.setInsecure();          // no cert verification (public data)
    client.setBufferSizes(SSL_RX_BUF, SSL_TX_BUF);   // small handshake buffers fit the block
    client.setTimeout(connectMs);

    uint32_t t0 = millis();
    if (!client.connect(host.c_str(), port)) {
      lastCode = -1; lastErr = "connect failed";
      noteConnResult(-1);
      Serial.printf("[net] GET connect failed in %ums (host=%s)\n",
                    (unsigned)(millis() - t0), host.c_str());
      return false;
    }

    // Send the request as one blob (single TLS record -> fast, avoids mod_reqtimeout).
    String req;
    req.reserve(reqPath.length() + host.length() + 96);
    req  = "GET " + reqPath + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += "User-Agent: CardSat-Cardputer/1.0\r\n";
    req += "Accept: */*\r\n";
    req += "Connection: close\r\n\r\n";
    client.print(req);
    client.flush();

    // Read + parse the status line.
    uint32_t rdStart = millis();
    while (!client.available() && client.connected() && millis() - rdStart < connectMs) delay(10);
    String statusLine = client.available() ? client.readStringUntil('\n') : String("");
    int code = 0;
    { int sp = statusLine.indexOf(' ');
      if (sp >= 0) code = statusLine.substring(sp + 1, sp + 4).toInt(); }
    if (statusLine.length() == 0) {
      lastCode = 0; lastErr = "no response from server"; noteConnResult(0);
      Serial.println("[net] GET: no response (empty status)");
      return false;
    }

    // Read headers: capture Content-Length, chunked flag, and Location (for redirects).
    int len = -1; bool chunked = false; String location = "";
    uint32_t hdrStart = millis();
    while (millis() - hdrStart < 15000) {
      if (!client.available()) { if (!client.connected()) break; delay(5); continue; }
      String h = client.readStringUntil('\n');
      if (h == "\r" || h.length() == 0) break;   // end of headers
      String hl = h; hl.toLowerCase();
      if (hl.startsWith("content-length:")) len = h.substring(15).toInt();
      else if (hl.startsWith("transfer-encoding:") && hl.indexOf("chunked") >= 0) chunked = true;
      else if (hl.startsWith("location:")) { location = h.substring(9); location.trim(); }
    }

    // Follow a single redirect.
    if ((code == 301 || code == 302 || code == 307 || code == 308) && location.length() && hop == 0) {
      Serial.printf("[net] GET %d -> redirect to %s\n", code, redactUrl(location).c_str());
      // Relative Location -> rebuild against current host.
      if (location.startsWith("/")) curUrl = String(https ? "https://" : "http://") + host + location;
      else curUrl = location;
      continue;   // _cstop closes this connection; loop reconnects to the new URL
    }

    lastCode = code;
    noteConnResult(code);
    if (code != 200) {
      lastErr = "HTTP " + String(code);
      Serial.printf("[net] GET failed: code=%d\n", code);
      return false;
    }

    // Preflight: if the server declared a Content-Length, make sure it will actually fit on
    // the active filesystem before we start writing. On internal-flash-only units the LittleFS
    // partition is small (~1 MB), and a large CelesTrak group ("Active", "Starlink") can be
    // several MB -- without this check the write fills the partition and fails partway, possibly
    // corrupting the previous good catalog. A generous 32 KB margin leaves room for other files.
    // (freeBytes() reports a large value on SD, so this only bites the genuinely-too-big case.)
    if (len > 0) {
      size_t freeFs = Store::freeBytes();
      if (freeFs > 0 && (size_t)len + 32768 > freeFs) {
        lastErr = "file too big for storage (" + String(len / 1024) + "KB > "
                  + String((int)(freeFs / 1024)) + "KB free)";
        return false;
      }
    }

    File f = Store::fs().open(path, "w");
    if (!f) { lastErr = Store::ready() ? "fs open failed" : "no filesystem"; return false; }

    // Stream the body straight to flash. Chunked responses are dechunked inline; with a
    // Content-Length we stop at len; otherwise we stop on close/idle. Each chunk is
    // written and freed so no large contiguous RAM buffer is ever needed.
    uint8_t buf[512];
    size_t total = 0;
    uint32_t lastRx = millis();
    bool writeErr = false, done = false;
    const uint32_t STALL_MS = 10000;
    uint32_t firstWindow = (firstByteMs > STALL_MS) ? firstByteMs : STALL_MS;

    // Chunked decode state: bytes remaining in the current chunk (-1 = need size line).
    long chunkRemain = chunked ? -1 : (len > 0 ? len : LONG_MAX);

    while (total < maxBytes && !done) {
      if (chunked && chunkRemain <= 0) {
        // Need the next chunk-size line.
        if (!client.available()) {
          if (!client.connected()) break;
          if (millis() - lastRx > (total == 0 ? firstWindow : STALL_MS)) break;
          delay(5); continue;
        }
        String szLine = client.readStringUntil('\n');
        szLine.trim();
        if (szLine.length() == 0) { continue; }          // trailing CRLF between chunks
        long csz = strtol(szLine.c_str(), nullptr, 16);  // hex chunk size
        if (csz <= 0) { done = true; break; }            // last chunk (0) -> done
        chunkRemain = csz;
        lastRx = millis();
      }
      size_t avail = client.available();
      if (avail) {
        size_t want = sizeof(buf);
        if ((long)want > chunkRemain) want = (size_t)chunkRemain;
        int r = client.readBytes(buf, min(avail, want));
        if (r <= 0) break;
        if (f.write(buf, r) != (size_t)r) { writeErr = true; break; }
        total += r;
        if (chunked || len > 0) chunkRemain -= r;
        lastRx = millis();
        if (!chunked && len > 0 && total >= (size_t)len) { done = true; break; }
        if (chunked && chunkRemain <= 0) { /* consume trailing CRLF next loop */ }
      } else {
        if (!chunked && len > 0 && total >= (size_t)len) { done = true; break; }
        // With unknown length, a real close ends the body; with a declared length keep
        // waiting through weak-link TLS gaps until the stall window elapses.
        if (len <= 0 && !chunked && !client.connected() && !client.available() &&
            millis() - lastRx > 500) break;

      }
    }
    f.close();
    if (written) *written = total;

    Serial.printf("[net] streamed %u bytes to %s (declared %d), heap now %u (largest %u)\n",
                  (unsigned)total, path, len, (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (writeErr)   { lastErr = "fs write failed"; return false; }
    if (total == 0) { lastErr = "empty body"; return false; }
    if (!chunked && len > 0 && total < (size_t)len) {
      lastErr = "short read " + String((unsigned)total) + "/" + String(len);
      return false;
    }
    return true;   // success on this hop
  }
  // Fell out of the redirect loop without a definitive result (e.g. a redirect with
  // no Location, or a second redirect we don't follow).
  lastErr = "too many redirects";
  return false;
}

bool Net::httpsGetToFileRetry(const String& url, const char* path,
                              size_t maxBytes, size_t* written, int attempts,
                              uint32_t firstByteMs) {
  if (attempts < 1) attempts = 1;
  TlsBusyGuard _tls;   // hold listeners down across the whole retry sequence
  for (int i = 0; i < attempts; i++) {
    if (httpsGetToFile(url, path, maxBytes, written, firstByteMs)) return true;
    // A "low flash" failure won't fix itself on retry -- bail immediately.
    if (lastErr == "low flash") return false;
    // A connect-LEVEL failure (code <= 0, e.g. "connection refused") is a socket-
    // pool / TIME_WAIT problem, not a mid-stream stall. Rapid retries only pile
    // more PCBs into the pool and make it worse (and the heap machinery can't fix
    // a socket refusal), so bail out immediately and let the caller move on --
    // the pool drains on its own far faster when we STOP hammering it. Only a
    // genuine short read (code > 0 but truncated body) is worth another attempt.
    if (lastCode <= 0) {
      Serial.printf("[net] attempt %d/%d failed (%s); connect-level, not retrying\n",
                    i + 1, attempts, lastErr.c_str());
      return false;
    }
    Serial.printf("[net] attempt %d/%d failed (%s); retrying\n",
                  i + 1, attempts, lastErr.c_str());
    delay(400 * (i + 1));   // simple linear backoff
  }
  return false;
}

bool Net::fetchGpToFile(const String& url, const char* path) {
  // GP is the largest and most important download; on a weak link it can be cut
  // short mid-body, so allow a few attempts. httpsGetToFile now reports a short
  // read (got fewer bytes than the declared Content-Length) as a failure, so the
  // retry wrapper re-attempts instead of caching a truncated catalog.
  return httpsGetToFileRetry(url, path, 400000, nullptr, 3);
}

// DEPRECATED -- DO NOT CALL. Reads the entire GP catalog (~400 KB) into a single
// RAM String, which cannot fit on this no-PSRAM heap and reproduces the upload/download
// heap-exhaustion failures cured in 0.9.41. Kept only so the symbol resolves; every
// caller MUST use fetchGpToFile() (streams to flash). See docs/design/STREAMING_TLS.md.
bool Net::fetchGp(const String& url, String& out) {
  // GP/OMM JSON can be a few hundred KB for the full amateur list; cap higher
  // than the old TLE text. MAX_SATS still bounds what we actually store.
  return httpsGet(url, out, 400000);
}

// DEPRECATED -- DO NOT CALL. Reads a full transmitter list (up to ~200 KB) into a RAM
// String; the String reader also silently drops chunks under TLS heap pressure (it
// advances its byte count even when concat() fails to grow). Every caller MUST use
// fetchSatnogsTransmittersToFile() (streams to flash). See docs/design/STREAMING_TLS.md.
bool Net::fetchSatnogsTransmitters(uint32_t norad, String& out) {
  String url = String(SATNOGS_TX_URL) + String((unsigned long)norad);
  // Busy birds return large transmitter lists (the ISS has ~49); allow ample room
  // so the JSON body isn't truncated mid-object, which would fail the parse and
  // leave the satellite with no transponders.
  return httpsGet(url, out, 200000);
}

bool Net::fetchSatnogsTransmittersToFile(uint32_t norad, const char* path) {
  String url = String(SATNOGS_TX_URL) + String((unsigned long)norad);
  // Stream to flash rather than into a String. The String reader advanced its
  // byte count even when concat() failed to grow the buffer under TLS heap
  // pressure, silently dropping chunks and corrupting large lists. 200 KB is
  // ample for the busiest sat (the ISS, ~54 transmitters).
  return httpsGetToFile(url, path, 200000, nullptr);
}

// POST a (small) file as multipart/form-data. Built for the LoTW .tq8 upload:
// the staged file is ~1-2 KB, so it's read into one buffer and framed with the
// standard multipart boundary. Mirrors the GET TLS pattern (insecure CA + busy
// guard + forced socket close). Returns true on HTTP 200; 'response' gets the
// server's reply text (capped at maxResp) for the caller to parse.
bool Net::httpsPostMultipart(const String& url, const char* fieldName,
                             const char* filePath, const char* fileName,
                             const char* contentType, String& response,
                             size_t maxResp) {
  lastCode = 0; lastErr = ""; response = "";
  if (!connected()) { lastErr = "no WiFi"; return false; }
  TlsBusyGuard _tls;
  if (INTER_FETCH_MS) delay(INTER_FETCH_MS);

  // Probe the upload file's size WITHOUT reading it into RAM yet. The ~9 KB body
  // buffer must NOT exist during the TLS handshake: the TLS stack needs a large
  // contiguous block (mbedTLS-era ~32 KB; BearSSL's 16 KB RX buffer today), and on
  // this no-PSRAM heap the largest free block sits right at ~31.7 KB. Holding
  // the body buffer during the handshake was tipping it over into "SSL - Memory
  // allocation failed" every time (the larger LoTW body is why LoTW failed where the
  // smaller Cloudlog body squeaked through). So: get the size, compute Content-Length,
  // do the handshake with maximum free heap, and only AFTER it connects read the file
  // into the body buffer and send it.
  size_t flen = 0;
  {
    File f = Store::fs().open(filePath, "r");
    if (!f) { lastErr = "open upload file failed"; return false; }
    flen = f.size();
    f.close();
  }
  if (flen == 0 || flen > 256000) { lastErr = "upload file size bad"; return false; }

  const String boundary = "----CardSatLoTW8b2f4c1d";
  String head;
  head  = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"" + String(fieldName) +
          "\"; filename=\"" + String(fileName) + "\"\r\n";
  head += "Content-Type: " + String(contentType) + "\r\n\r\n";
  const String tail = "\r\n--" + boundary + "--\r\n";
  size_t bodyLen = head.length() + flen + tail.length();   // Content-Length, no buffer yet

  Serial.printf("[net] POST %s (%u-byte body) -> %s\n",
                fileName, (unsigned)bodyLen, redactUrl(url).c_str());
  Serial.printf("[net] heap before TLS: %u (largest block %u), IP %s, RSSI %d\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());

  WiFiClient transport;          // TCP transport under the TLS layer
  ESP_SSLClient client;          // BearSSL TLS with app-sized buffers
  struct ClientStop { ESP_SSLClient& c; ~ClientStop() { c.stop(); } } _cstop{client};
  client.setClient(&transport);  // MUST precede connect; transport must outlive client
  client.setInsecure();
  client.setBufferSizes(SSL_RX_BUF, SSL_TX_BUF);
  client.setTimeout(20000);

  // --- Manual request (same approach that fixed the Cloudlog POST) --------------
  // HTTPClient::POST was failing here exactly as it did on the JSON path (-1 /
  // connection refused / 408), so send the request by hand over the TLS client we
  // control. Parse host/path/port from the URL, THEN handshake.
  String hostPart, pathPart; int port = 443;
  {
    int s = url.indexOf("://"); int st = (s >= 0) ? s + 3 : 0;
    int slash = url.indexOf('/', st);
    String hostPort = (slash >= 0) ? url.substring(st, slash) : url.substring(st);
    pathPart = (slash >= 0) ? url.substring(slash) : "/";
    int colon = hostPort.indexOf(':');
    if (colon >= 0) { hostPart = hostPort.substring(0, colon); port = hostPort.substring(colon + 1).toInt(); }
    else hostPart = hostPort;
  }

  uint32_t t0 = millis();
  if (!client.connect(hostPart.c_str(), port)) {
    lastCode = -1; lastErr = "connect failed";
    Serial.printf("[net] POST connect failed in %ums\n", (unsigned)(millis() - t0));
    return false;
  }
  Serial.printf("[net] POST TLS connected in %ums; streaming request\n", (unsigned)(millis() - t0));

  // Handshake done. Stream the request instead of building the whole body in one
  // contiguous malloc: with the live TLS session already holding its two ~16 KB
  // buffers, a ~9 KB contiguous malloc fails ("oom building body"). The .tq8 lives on
  // SD, so we never need it all in RAM at once -- send the HTTP + multipart headers,
  // then copy the file to the socket in small blocks, then the multipart tail. Peak
  // RAM is one ~1 KB chunk regardless of how many QSOs the file holds. (This is the
  // mirror of httpsGetToFile, which streams downloads to disk.)
  String reqHead;
  reqHead.reserve(384);
  reqHead  = "POST " + pathPart + " HTTP/1.1\r\n";
  reqHead += "Host: " + hostPart + "\r\n";
  reqHead += "User-Agent: CardSat-Cardputer/1.0\r\n";
  reqHead += "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
  reqHead += "Content-Length: " + String(bodyLen) + "\r\n";
  reqHead += "Connection: close\r\n\r\n";
  reqHead += head;                       // multipart part-header goes out with the request headers

  // write() helper semantics: 0 means "TCP send buffer full, must drain". The ESP32
  // lwip default TCP_SND_BUF is ~5744 bytes; a body larger than that (LoTW's ~9 KB)
  // fills it, write() returns 0, and we must let it drain. On a full buffer we flush()
  // to push the queued segments out so the peer can ACK and reopen the window; without
  // that push the queued data can sit untransmitted and the socket eventually
  // half-closes. Do NOT abort on a transient connected()==false; rely on the write
  // result plus the no-progress timeout. (See docs/design/LOTW_UPLOAD_SIZE_WORKAROUNDS.md;
  // large uploads are additionally split into sub-TCP_SND_BUF batches, one per reboot.)
  // Diagnostic C (0.9.43 ESP_SSLClient eval): measure the effective TCP send behavior so
  // we can tell whether the ~5744B TCP_SND_BUF ceiling that historically stalled large
  // LoTW bodies still applies under BearSSL. maxWrite = largest single write() accepted;
  // zeroWrites = times the TX window was full (had to flush+retry). If a large body now
  // streams with few zeroWrites, the ceiling is no longer a practical limit and the
  // batch/reboot machinery can be relaxed; if it stalls, the ceiling persists below TLS.
  size_t dbgMaxWrite = 0; uint32_t dbgZeroWrites = 0;
  auto writeAll = [&](const uint8_t* p, size_t n) -> bool {
    size_t off = 0; uint32_t lastProgress = millis();
    while (off < n) {
      int w = client.write(p + off, n - off);
      if (w > 0) { off += (size_t)w; lastProgress = millis(); if ((size_t)w > dbgMaxWrite) dbgMaxWrite = (size_t)w; }
      else {
        // Log heap AT THE STALL (once) -- a zero write here means the TCP send window won't
        // drain, and on this no-PSRAM part that is usually LWIP failing to allocate a send
        // pbuf because contiguous heap is tight (the 16 KB BearSSL RX buffer is resident for
        // the whole upload). "before TLS" heap doesn't show this; the stall-time figure does.
        if (dbgZeroWrites == 0) {
          Serial.printf("[net] TX stall: free %u largest %u (sent %u/%u)\n",
                        (unsigned)ESP.getFreeHeap(),
                        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                        (unsigned)off, (unsigned)n);
        }
        dbgZeroWrites++;
        client.flush();                 // push queued segments so the peer can ACK/reopen the window
        delay(10); yield();
        if (millis() - lastProgress > 30000) return false;
      }
    }
    return true;
  };

  bool ok = writeAll((const uint8_t*)reqHead.c_str(), reqHead.length());
  size_t streamed = 0;
  if (ok) {
    File f = Store::fs().open(filePath, "r");
    if (!f) { lastErr = "reopen upload file failed"; return false; }
    uint8_t chunk[1024];
    while (streamed < flen) {
      size_t want = flen - streamed; if (want > sizeof(chunk)) want = sizeof(chunk);
      size_t got = f.read(chunk, want);
      if (got == 0) break;                // short read from SD -> stop; length check below fails
      if (!writeAll(chunk, got)) { ok = false; break; }
      streamed += got;
    }
    f.close();
  }
  if (ok) ok = writeAll((const uint8_t*)tail.c_str(), tail.length());
  client.flush();
  Serial.printf("[net] streamed %u/%u file bytes (+headers/tail), ok=%d  [TX maxWrite=%u zeroWrites=%u]\n",
                (unsigned)streamed, (unsigned)flen, ok ? 1 : 0,
                (unsigned)dbgMaxWrite, (unsigned)dbgZeroWrites);
  if (!ok || streamed != flen) { lastCode = 0; lastErr = "body stream incomplete"; return false; }

  // Read the response (connection-tolerant: poll available(), ignore transient close).
  uint32_t rdStart = millis(); int code = 0;
  while (!client.available() && millis() - rdStart < 20000) delay(10);
  String statusLine = client.available() ? client.readStringUntil('\n') : String("");
  Serial.printf("[net] status: %s\n", statusLine.c_str());
  { int sp = statusLine.indexOf(' ');
    if (sp >= 0) code = statusLine.substring(sp + 1, sp + 4).toInt(); }
  if (statusLine.length() == 0) {
    lastCode = 0; lastErr = "no response from server";
    Serial.println("[net] POST: no response (empty status)");
    return false;
  }

  uint32_t hdrStart = millis();
  while (millis() - hdrStart < 15000) {
    if (!client.available()) { if (!client.connected()) break; delay(5); continue; }
    String h = client.readStringUntil('\n');
    if (h == "\r" || h.length() == 0) break;
  }
  response = "";
  uint32_t bodyStart = millis();
  while (response.length() < maxResp && millis() - bodyStart < 15000) {
    while (client.available() && response.length() < maxResp) response += (char)client.read();
    if (!client.connected() && !client.available()) break;
    delay(2);
  }

  lastCode = code;
  noteConnResult(code);
  if (code < 200 || code >= 300) {
    lastErr = (code > 0) ? ("HTTP " + String(code)) : "no response";
    Serial.printf("[net] POST failed: code=%d\n", code);
    return false;
  }
  return true;
}

// Best-effort heap defragment before a TLS handshake (see net.h). The ESP-IDF heap is
// not a moving collector, so we can't truly compact -- but a failed prior fetch leaves
// LWIP/mbedTLS buffers that the stack frees asynchronously, and the allocator coalesces
// adjacent free blocks as they are returned. Giving that a few short delays usually
// brings the largest contiguous block back above TLS_MIN_BLOCK. We deliberately do NOT
// free the drawing sprite here: doing so was tried and reverted (the freed hole didn't
// reliably merge, and a failed re-create blanked the screen).
size_t Net::reclaimHeapForTls() {
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (largest >= TLS_MIN_BLOCK) return largest;
  // Up to ~5 short waits (≈600 ms total) for in-flight frees to land and coalesce.
  for (int i = 0; i < 5; ++i) {
    delay(120);
    yield();                                     // let the WiFi/LWIP task run its frees
    size_t now = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (now > largest) largest = now;
    if (largest >= TLS_MIN_BLOCK) break;
  }
  // --- BEGIN 0.9.41 proactive WiFi-cycle defrag (REVERSIBLE) ------------------
  // Last resort: if the passive wait still left us short, forcibly flush the LWIP
  // socket pool / mbedTLS async buffers with one disconnect(true)+reconnect so the
  // freed blocks coalesce. Gated hard: only when still below the floor, only when
  // we have a connection to recycle, only when we haven't just done this (so it
  // can't fight the connFails/hardReset recovery or storm reconnects), and only
  // while the master toggle is on. hardResetWifi() reuses the existing recovery
  // path (credential reuse, 12 s bounded reconnect, connFails reset on success).
  // TO DISABLE: set Net::TLS_WIFI_CYCLE = false. (docs/design/HEAP_WIFI_CYCLE.md)
  if (TLS_WIFI_CYCLE && !wifiCycleIneffective && largest < TLS_MIN_BLOCK &&
      connFails == 0 &&                         // not during a connect-refusal streak:
                                                 // that is a socket-pool problem, and
                                                 // cycling WiFi for it is pure cost (it
                                                 // only ever measures block unchanged).
      WiFi.status() == WL_CONNECTED &&
      (lastWifiCycleMs == 0 || millis() - lastWifiCycleMs > WIFI_CYCLE_MIN_GAP_MS)) {
    Serial.printf("[net] proactive WiFi-cycle defrag (largest %u < %u)\n",
                  (unsigned)largest, (unsigned)TLS_MIN_BLOCK);
    lastWifiCycleMs = millis();
    size_t before = largest;
    if (hardResetWifi()) {
      // Give the freed buffers a moment to return, then re-measure.
      for (int i = 0; i < 3; ++i) {
        delay(120); yield();
        size_t now = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        if (now > largest) largest = now;
        if (largest >= TLS_MIN_BLOCK) break;
      }
      Serial.printf("[net] post-cycle largest block %u\n", (unsigned)largest);
      // If the cycle didn't grow the largest block at all, the fragmentation is
      // mid-heap persistent allocations (mbedTLS session state that survives
      // stop()), not freeable socket-pool buffers -- so cycling is pure cost.
      // Remember that and don't cycle again this session (a reboot is the cure;
      // the upload paths offer one on HEAP_ABORT_CODE).
      if (largest <= before) {
        wifiCycleIneffective = true;
        Serial.println("[net] WiFi-cycle had no effect; disabling it for this session");
      }
    }
  }
  // --- END 0.9.41 proactive WiFi-cycle defrag --------------------------------
  Serial.printf("[net] heap reclaim: largest block now %u (need %u)\n",
                (unsigned)largest, (unsigned)TLS_MIN_BLOCK);
  return largest;
}


// JSON document and may live on a LAN over plain HTTP, so both http:// and https://
// are accepted. The request body can carry an API key, so it is never logged verbatim
// unless redactBody is false; we log only its length plus the redacted URL.
bool Net::httpsPostJson(const String& url, const String& body, String& response,
                        const char* contentType, size_t maxResp, bool redactBody) {
  lastCode = 0; lastErr = ""; response = "";
  if (!connected()) { lastErr = "no WiFi"; return false; }
  TlsBusyGuard _tls;
  if (INTER_FETCH_MS) delay(INTER_FETCH_MS);

  Serial.printf("[net] POST(json) %u-byte body -> %s\n",
                (unsigned)body.length(), redactUrl(url).c_str());
  if (!redactBody) Serial.printf("[net] body: %s\n", body.c_str());
  Serial.printf("[net] heap before TLS: %u (largest block %u), IP %s, RSSI %d\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());

  const bool isHttps = url.startsWith("https:");
  WiFiClient transport;          // TCP transport for the TLS layer (https)
  ESP_SSLClient tls;             // BearSSL TLS with app-sized buffers
  WiFiClient plain;              // used directly for plain http
  struct StopAll { ESP_SSLClient& t; WiFiClient& p; bool s;
                   ~StopAll() { if (s) t.stop(); else p.stop(); } } _stop{tls, plain, isHttps};
  if (isHttps) {
    tls.setClient(&transport);   // MUST precede connect; transport must outlive tls
    tls.setInsecure();
    tls.setBufferSizes(SSL_RX_BUF, SSL_TX_BUF);
    tls.setTimeout(20000);
  }

  // --- 0.9.41: MANUAL request instead of HTTPClient::POST -----------------------
  // HTTPClient::POST() was returning 408 (server got headers but not the body) on a
  // completed TLS connection with ample heap, independent of HTTP/1.0-vs-1.1. Rather
  // than keep guessing at HTTPClient's body handling, send the request by hand over the
  // client we control, logging exactly how many body bytes are written and reading the
  // status line directly. This removes HTTPClient's send path (the suspect) entirely.
  // Parse host/path/port from the URL.
  String hostPart, pathPart; int port = isHttps ? 443 : 80;
  {
    int s = url.indexOf("://"); int st = (s >= 0) ? s + 3 : 0;
    int slash = url.indexOf('/', st);
    String hostPort = (slash >= 0) ? url.substring(st, slash) : url.substring(st);
    pathPart = (slash >= 0) ? url.substring(slash) : "/";
    int colon = hostPort.indexOf(':');
    if (colon >= 0) { hostPart = hostPort.substring(0, colon); port = hostPort.substring(colon + 1).toInt(); }
    else hostPart = hostPort;
  }

  Client* cli = isHttps ? (Client*)&tls : (Client*)&plain;
  uint32_t t0 = millis();
  if (!cli->connect(hostPart.c_str(), port)) {
    lastCode = -1; lastErr = "connect failed";
    Serial.printf("[net] POST connect failed in %ums\n", (unsigned)(millis() - t0));
    return false;
  }
  Serial.printf("[net] POST TLS connected in %ums; sending request\n", (unsigned)(millis() - t0));

  // Build the ENTIRE request -- request line, headers, AND body -- into one buffer and
  // send it with as few write()s as possible. Root cause of the 408 (proven host-side:
  // the identical request bytes get an instant 401 from the server via openssl
  // s_client, so the framing is correct): the request was arriving too SLOWLY. Apache's
  // mod_reqtimeout returns 408 if the full request doesn't arrive quickly enough, and
  // splitting headers (print) from body (write) plus per-chunk flushes spread the
  // request across many small TLS records over a marginal WiFi link -> too slow.
  // Sending headers+body as one ~3.8 KB blob goes out in a single TLS record, landing
  // the whole request at once. (3791 bytes < the ~16 KB TLS record limit.)
  String req;
  req.reserve(256 + body.length());
  req  = "POST " + pathPart + " HTTP/1.1\r\n";
  req += "Host: " + hostPart + "\r\n";
  req += "User-Agent: CardSat-Cardputer/1.0\r\n";
  req += "Content-Type: " + String(contentType) + "\r\n";
  req += "Content-Length: " + String(body.length()) + "\r\n";
  req += "Connection: close\r\n\r\n";
  req += body;

  size_t reqLen = req.length();
  const uint8_t* rp = (const uint8_t*)req.c_str();
  size_t sent = cli->write(rp, reqLen);     // one shot: one TLS record if it fits
  if (sent < reqLen) {
    // Short write (TX window full): finish the remainder, treating write()==0 as
    // "full, retry" rather than "dead". Keep records as large as possible.
    uint32_t lastProgress = millis();
    while (sent < reqLen) {
      if (!cli->connected()) { Serial.println("[net] connection dropped mid-request"); break; }
      size_t w = cli->write(rp + sent, reqLen - sent);
      if (w > 0) { sent += w; lastProgress = millis(); }
      else { delay(15); yield();
             if (millis() - lastProgress > 30000) { Serial.println("[net] request write stalled (30s)"); break; } }
    }
  }
  cli->flush();
  Serial.printf("[net] wrote %u/%u request bytes (header+body)\n", (unsigned)sent, (unsigned)reqLen);

  // Read the response. IMPORTANT: with WiFiClientSecure, connected() can momentarily
  // report false between TLS records even while the server's reply is still arriving
  // (the same hazard the GET path documents). So the wait must NOT bail the instant
  // connected() looks false -- it must keep polling available() until data shows up or
  // a real timeout elapses. Earlier this gave an empty status line + code=0: the send
  // completed (3791/3791) but we stopped reading before the reply landed. Host-side the
  // server answers these exact bytes instantly, so the reply IS coming -- wait for it.
  uint32_t rdStart = millis(); int code = 0;
  // Poll purely for decrypted data to become available. Do NOT consult connected():
  // on WiFiClientSecure it can read false between records and immediately after the
  // server's close even while the buffered reply is still readable. If the server
  // closed after replying, available() stays > 0 until we drain it. Wait up to 20s.
  while (!cli->available() && millis() - rdStart < 20000) delay(10);
  String statusLine = cli->available() ? cli->readStringUntil('\n') : String("");
  Serial.printf("[net] status: %s\n", statusLine.c_str());
  { int sp = statusLine.indexOf(' ');
    if (sp >= 0) code = statusLine.substring(sp + 1, sp + 4).toInt(); }
  if (statusLine.length() == 0) {
    // No response arrived within the window. Report it distinctly rather than burning
    // the header/body timeouts on a connection that produced nothing.
    lastCode = 0; lastErr = "no response from server";
    Serial.println("[net] POST(json): no response (empty status)");
    return false;
  }

  // Skip the rest of the headers (read until a blank line).
  uint32_t hdrStart = millis();
  while (millis() - hdrStart < 15000) {
    if (!cli->available()) {
      if (!cli->connected()) break;
      delay(5); continue;
    }
    String h = cli->readStringUntil('\n');
    if (h == "\r" || h.length() == 0) break;
  }
  // Read the body until close or timeout.
  response = "";
  uint32_t bodyStart = millis();
  while (response.length() < maxResp && millis() - bodyStart < 15000) {
    while (cli->available() && response.length() < maxResp) response += (char)cli->read();
    if (!cli->connected() && !cli->available()) break;
    delay(2);
  }

  lastCode = code;
  noteConnResult(code);
  if (code < 200 || code >= 300) {
    lastErr = (code > 0) ? ("HTTP " + String(code)) : "no response";
    Serial.printf("[net] POST(json) failed: code=%d\n", code);
    return false;
  }
  return true;
}

// Streaming JSON POST (body from a file on Store::fs()). See the header note: this is
// the no-PSRAM-safe path for large Cloudlog uploads -- one handshake, body streamed
// from disk in ~1 KB chunks so peak RAM is constant. The body file already contains the
// COMPLETE JSON request body (key + station + escaped ADIF); we only add the HTTP
// request line and headers. https only (Cloudlog/Wavelog API). The file lives wherever
// Store::fs() points (SD when present, else internal LittleFS) -- so this does NOT add
// an SD-card requirement; it works on a card-less device exactly like the QSO log does.
bool Net::httpsPostJsonFile(const String& url, const char* bodyFilePath, String& response,
                            const char* contentType, size_t maxResp) {
  lastCode = 0; lastErr = ""; response = "";
  if (!connected()) { lastErr = "no WiFi"; return false; }
  TlsBusyGuard _tls;
  if (INTER_FETCH_MS) delay(INTER_FETCH_MS);

  // Body size from the file (no read into RAM yet) -> Content-Length.
  size_t bodyLen = 0;
  {
    File f = Store::fs().open(bodyFilePath, "r");
    if (!f) { lastErr = "open body file failed"; return false; }
    bodyLen = f.size();
    f.close();
  }
  if (bodyLen == 0) { lastErr = "empty body file"; return false; }

  Serial.printf("[net] POST(json-stream) %u-byte body -> %s\n",
                (unsigned)bodyLen, redactUrl(url).c_str());
  Serial.printf("[net] heap before TLS: %u (largest block %u), IP %s, RSSI %d\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());

  WiFiClient transport;          // TCP transport under the TLS layer
  ESP_SSLClient tls;             // BearSSL TLS with app-sized buffers
  struct StopT { ESP_SSLClient& t; ~StopT() { t.stop(); } } _stop{tls};
  tls.setClient(&transport);     // MUST precede connect; transport must outlive tls
  tls.setInsecure();
  tls.setBufferSizes(SSL_RX_BUF, SSL_TX_BUF);
  tls.setTimeout(20000);

  // Parse host/path/port.
  String hostPart, pathPart; int port = 443;
  {
    int s = url.indexOf("://"); int st = (s >= 0) ? s + 3 : 0;
    int slash = url.indexOf('/', st);
    String hostPort = (slash >= 0) ? url.substring(st, slash) : url.substring(st);
    pathPart = (slash >= 0) ? url.substring(slash) : "/";
    int colon = hostPort.indexOf(':');
    if (colon >= 0) { hostPart = hostPort.substring(0, colon); port = hostPort.substring(colon + 1).toInt(); }
    else hostPart = hostPort;
  }

  uint32_t t0 = millis();
  if (!tls.connect(hostPart.c_str(), port)) {
    lastCode = -1; lastErr = "connect failed";
    Serial.printf("[net] POST connect failed in %ums\n", (unsigned)(millis() - t0));
    return false;
  }
  Serial.printf("[net] POST TLS connected in %ums; streaming request\n", (unsigned)(millis() - t0));

  // Request line + headers. The body streams from the file right after.
  String reqHead;
  reqHead.reserve(256);
  reqHead  = "POST " + pathPart + " HTTP/1.1\r\n";
  reqHead += "Host: " + hostPart + "\r\n";
  reqHead += "User-Agent: CardSat-Cardputer/1.0\r\n";
  reqHead += "Content-Type: " + String(contentType) + "\r\n";
  reqHead += "Content-Length: " + String(bodyLen) + "\r\n";
  reqHead += "Connection: close\r\n\r\n";

  // write() helper: treat 0 as "TX window full, retry", not "dead".
  auto writeAll = [&](const uint8_t* p, size_t n) -> bool {
    size_t off = 0; uint32_t lastProgress = millis();
    while (off < n) {
      if (!tls.connected()) return false;
      size_t w = tls.write(p + off, n - off);
      if (w > 0) { off += w; lastProgress = millis(); }
      else { delay(15); yield(); if (millis() - lastProgress > 30000) return false; }
    }
    return true;
  };

  bool ok = writeAll((const uint8_t*)reqHead.c_str(), reqHead.length());
  size_t streamed = 0;
  if (ok) {
    File f = Store::fs().open(bodyFilePath, "r");
    if (!f) { lastErr = "reopen body file failed"; return false; }
    uint8_t chunk[1024];
    while (streamed < bodyLen) {
      size_t want = bodyLen - streamed; if (want > sizeof(chunk)) want = sizeof(chunk);
      size_t got = f.read(chunk, want);
      if (got == 0) break;
      if (!writeAll(chunk, got)) { ok = false; break; }
      streamed += got;
    }
    f.close();
  }
  tls.flush();
  Serial.printf("[net] streamed %u/%u body bytes, ok=%d\n",
                (unsigned)streamed, (unsigned)bodyLen, ok ? 1 : 0);
  if (!ok || streamed != bodyLen) { lastCode = 0; lastErr = "body stream incomplete"; return false; }

  // Read the response (connection-tolerant: poll available(), ignore transient close).
  uint32_t rdStart = millis(); int code = 0;
  while (!tls.available() && millis() - rdStart < 20000) delay(10);
  String statusLine = tls.available() ? tls.readStringUntil('\n') : String("");
  Serial.printf("[net] status: %s\n", statusLine.c_str());
  { int sp = statusLine.indexOf(' ');
    if (sp >= 0) code = statusLine.substring(sp + 1, sp + 4).toInt(); }
  if (statusLine.length() == 0) {
    lastCode = 0; lastErr = "no response from server";
    Serial.println("[net] POST(json-stream): no response (empty status)");
    return false;
  }

  uint32_t hdrStart = millis();
  while (millis() - hdrStart < 15000) {
    if (!tls.available()) { if (!tls.connected()) break; delay(5); continue; }
    String h = tls.readStringUntil('\n');
    if (h == "\r" || h.length() == 0) break;
  }
  response = "";
  uint32_t bodyStart = millis();
  while (response.length() < maxResp && millis() - bodyStart < 15000) {
    while (tls.available() && response.length() < maxResp) response += (char)tls.read();
    if (!tls.connected() && !tls.available()) break;
    delay(2);
  }

  lastCode = code;
  noteConnResult(code);
  if (code < 200 || code >= 300) {
    lastErr = (code > 0) ? ("HTTP " + String(code)) : "no response";
    Serial.printf("[net] POST(json-stream) failed: code=%d\n", code);
    return false;
  }
  return true;
}
