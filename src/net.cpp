// ===========================================================================
//  net.cpp
// ===========================================================================
#include "net.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include "storage.h"
#include <time.h>
#include <esp_heap_caps.h>   // heap_caps_get_largest_free_block(): contiguous block,
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
// mbedTLS needs ~32 KB contiguous for a handshake (16 KB RX + 16 KB TX). With the
// drawing sprite resident the largest block is ~33 KB after the 0.9.41 static-RAM
// trim (LOG_VIEW_MAX 60->48 reclaimed ~1.7 KB, restoring the pre-feature headroom),
// so POSTs complete without freeing the sprite. This floor only gates the POST paths
// (GETs don't call reclaim). 28000 sits below the resident block so real attempts
// proceed, and above a genuinely-exhausted heap so the upload declines + offers a
// reboot rather than failing mid-handshake with a bare -1.
uint32_t Net::TLS_MIN_BLOCK  = 28000;   // below the ~33 KB resident block; catches real OOM

// --- 0.9.41 proactive WiFi-cycle defrag (see net.h / docs/design/HEAP_WIFI_CYCLE.md) ---
// Set TLS_WIFI_CYCLE=false to revert to the passive-wait-only reclaim behaviour.
bool     Net::TLS_WIFI_CYCLE      = true;   // enable the last-resort WiFi cycle
uint32_t Net::WIFI_CYCLE_MIN_GAP_MS = 30000; // don't cycle more than once per 30 s

// Flush the LWIP socket pool by tearing the STA association down hard, then
// reconnect with the credentials WiFi already holds. This is the reliable cure
// once fds wedge and connect() starts returning -1.
bool Net::hardResetWifi() {
  Serial.println("[net] hard WiFi reset (flushing socket pool)");
  WiFi.disconnect(true);                 // true => also turn the radio off briefly
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.reconnect();                      // reuse the last good credentials
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) delay(150);
  bool ok = (WiFi.status() == WL_CONNECTED);
  Serial.printf("[net] hard reset %s\n", ok ? "recovered" : "still down");
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
  return WiFi.status() == WL_CONNECTED;
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

  WiFiClientSecure client;
  // Force an explicit client.stop() on EVERY exit path (including early returns
  // and mid-read timeouts). Relying on the destructor alone leaves a socket that
  // stalled mid-stream lingering in the LWIP pool on this core; after enough such
  // leaks every subsequent connect() returns -1. stop() closes the fd at once.
  struct ClientStop { WiFiClientSecure& c; ~ClientStop() { c.stop(); } } _cstop{client};
  // Certificate validation disabled for simplicity (public GP data). For a
  // security-sensitive deployment, pin the CA root instead of setInsecure().
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient http;
  http.setUserAgent("CardSat-Cardputer/1.0");
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // AMSAT may 301/302
  http.useHTTP10(true);   // avoid chunked-encoding edge cases for static files

  if (!http.begin(client, url)) { lastErr = "begin failed"; return false; }
  http.addHeader("Accept", "*/*");

  int code = http.GET();
  lastCode = code;
  noteConnResult(code);   // track consecutive connect failures; auto-recover the pool
  if (code != HTTP_CODE_OK) {
    lastErr = (code > 0) ? ("HTTP " + String(code))
                         : HTTPClient::errorToString(code);
    Serial.printf("[net] GET failed: code=%d (%s)\n", code, lastErr.c_str());
    http.end();
    return false;
  }

  int len = http.getSize();          // -1 if server didn't send Content-Length
  out = "";
  if (len > 0) out.reserve(min((size_t)len + 16, maxBytes));  // avoid realloc churn

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[512];
  size_t total = 0;
  uint32_t lastRx = millis();
  // Read until the declared length arrives, or (no Content-Length) until the
  // stream has been idle long enough to be sure the transfer is done. We must
  // NOT stop the instant connected()/available() momentarily go false: with TLS
  // the socket can report no data between records mid-stream, which previously
  // truncated large bodies (the GP file) to whatever had arrived so far -- the
  // amount varying run to run with network timing.
  while (total < maxBytes) {
    size_t avail = stream->available();
    if (avail) {
      int r = stream->readBytes(buf, min(avail, sizeof(buf)));
      if (r <= 0) break;
      out.concat((const char*)buf, r);
      total += r;
      lastRx = millis();
      if (len > 0 && total >= (size_t)len) break;     // whole body received
    } else {
      if (len > 0 && total >= (size_t)len) break;
      // Only treat a closed connection as end-of-body when the length is UNKNOWN.
      // With a declared Content-Length we keep waiting (up to the stall timeout)
      // so a momentary TLS burst gap on a weak link can't truncate the body.
      if (len <= 0 && !http.connected() && !stream->available() &&
          millis() - lastRx > 500)
        break;
      if (millis() - lastRx > 10000) break;           // idle/stall timeout
      delay(5);
    }
  }
  http.end();

  Serial.printf("[net] received %u bytes (declared %d), heap now %u (largest %u)\n",
                (unsigned)total, len, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  if (out.length() == 0) { lastErr = "empty body"; return false; }
  // Declared length but got less -> truncated; report failure so callers/retries
  // don't parse a partial body.
  if (len > 0 && total < (size_t)len) {
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
  TlsBusyGuard _tls;   // free the app's LAN listener sockets for this session
  if (INTER_FETCH_MS) delay(INTER_FETCH_MS);   // let a just-closed socket leave the pool

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

  WiFiClientSecure client;
  // Force client.stop() on every exit (see httpsGet) -- closes the socket fd
  // immediately so a stalled/timed-out transfer doesn't leak it from the pool.
  struct ClientStop { WiFiClientSecure& c; ~ClientStop() { c.stop(); } } _cstop{client};
  client.setInsecure();
  client.setTimeout(connectMs);

  HTTPClient http;
  http.setUserAgent("CardSat-Cardputer/1.0");
  http.setConnectTimeout(connectMs);
  http.setTimeout(connectMs);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);

  if (!http.begin(client, url)) { lastErr = "begin failed"; return false; }
  http.addHeader("Accept", "*/*");

  int code = http.GET();
  lastCode = code;
  noteConnResult(code);   // track consecutive connect failures; auto-recover the pool
  if (code != HTTP_CODE_OK) {
    lastErr = (code > 0) ? ("HTTP " + String(code))
                         : HTTPClient::errorToString(code);
    Serial.printf("[net] GET failed: code=%d (%s)\n", code, lastErr.c_str());
    http.end();
    return false;
  }

  File f = Store::fs().open(path, "w");
  if (!f) {
    lastErr = Store::ready() ? "fs open failed" : "no filesystem (SPIFFS/SD)";
    http.end(); return false;
  }

  int len = http.getSize();          // -1 if server didn't send Content-Length
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[512];
  size_t total = 0;
  uint32_t lastRx = millis();
  bool writeErr = false;
  // The grace window before the FIRST byte can be longer than the mid-stream
  // stall window (for slow-first-response hosts); once any byte arrives we use
  // the normal 10s stall timeout for the rest of the body.
  const uint32_t STALL_MS = 10000;
  uint32_t firstWindow = (firstByteMs > STALL_MS) ? firstByteMs : STALL_MS;
  // Stream straight to flash: each chunk is written and freed, so no large
  // contiguous RAM buffer is ever needed (which is what truncated the String
  // version). Terminate on declared length / idle timeout, not on a transient
  // connected()/available() lull.
  while (total < maxBytes) {
    size_t avail = stream->available();
    if (avail) {
      int r = stream->readBytes(buf, min(avail, sizeof(buf)));
      if (r <= 0) break;
      if (f.write(buf, r) != (size_t)r) { writeErr = true; break; }  // flash full?
      total += r;
      lastRx = millis();
      if (len > 0 && total >= (size_t)len) break;
    } else {
      if (len > 0 && total >= (size_t)len) break;
      // Only treat a closed connection as end-of-body when the length is UNKNOWN
      // (chunked / no Content-Length). When the server declared a length and we
      // haven't reached it, a momentary connected()==false with no available
      // bytes is just a TLS burst gap on a weak link -- keep waiting up to the
      // stall window instead of declaring a truncated download complete.
      if (len <= 0 && !http.connected() && !stream->available() &&
          millis() - lastRx > 500)
        break;
      // Use the longer first-byte window until the first byte lands, then the
      // normal mid-stream stall timeout.
      uint32_t window = (total == 0) ? firstWindow : STALL_MS;
      if (millis() - lastRx > window) break;
      delay(5);
    }
  }
  f.close();
  http.end();
  if (written) *written = total;

  Serial.printf("[net] streamed %u bytes to %s (declared %d), heap now %u (largest %u)\n",
                (unsigned)total, path, len, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  if (writeErr)    { lastErr = "fs write failed"; return false; }
  if (total == 0)  { lastErr = "empty body"; return false; }
  // If the server told us how big the body is and we got less, the transfer was
  // cut short (a stall timeout or a dropped TLS burst). Report failure so the
  // retry wrapper re-attempts rather than parsing a truncated file.
  if (len > 0 && total < (size_t)len) {
    lastErr = "short read " + String((unsigned)total) + "/" + String(len);
    return false;
  }
  return true;
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
  // buffer must NOT exist during the TLS handshake: mbedTLS needs ~32 KB contiguous,
  // and on this no-PSRAM heap the largest free block sits right at ~31.7 KB. Holding
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

  WiFiClientSecure client;
  struct ClientStop { WiFiClientSecure& c; ~ClientStop() { c.stop(); } } _cstop{client};
  client.setInsecure();
  client.setTimeout(20000);

  // Defragment if needed, then bail before a handshake we can't complete (see the note
  // in httpsPostJson). No body buffer allocated yet, so nothing to free on this abort.
  if (reclaimHeapForTls() < TLS_MIN_BLOCK) {
    lastCode = HEAP_ABORT_CODE;        // negative: lets callers offer reboot-to-clean-heap
    lastErr = "low memory; try again";
    Serial.println("[net] aborting TLS POST: insufficient contiguous heap");
    return false;
  }

  // --- Manual request (same approach that fixed the Cloudlog POST) --------------
  // HTTPClient::POST was failing here exactly as it did on the JSON path (-1 /
  // connection refused / 408), so send the request by hand over the TLS client we
  // control. Parse host/path/port from the URL, THEN handshake with max free heap.
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
    char eb[128] = {0}; client.lastError(eb, sizeof(eb));
    Serial.printf("[net] POST connect failed in %ums; TLS lastError: %s\n",
                  (unsigned)(millis() - t0), eb[0] ? eb : "(none)");
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

  // write() helper semantics: treat 0 as "TX window full, retry", not "dead".
  auto writeAll = [&](const uint8_t* p, size_t n) -> bool {
    size_t off = 0; uint32_t lastProgress = millis();
    while (off < n) {
      if (!client.connected()) return false;
      size_t w = client.write(p + off, n - off);
      if (w > 0) { off += w; lastProgress = millis(); }
      else { delay(15); yield(); if (millis() - lastProgress > 30000) return false; }
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
      if (got == 0) break;                // short read -> stop; length check below fails
      if (!writeAll(chunk, got)) { ok = false; break; }
      streamed += got;
    }
    f.close();
  }
  if (ok) ok = writeAll((const uint8_t*)tail.c_str(), tail.length());
  client.flush();
  Serial.printf("[net] streamed %u/%u file bytes (+headers/tail), ok=%d\n",
                (unsigned)streamed, (unsigned)flen, ok ? 1 : 0);
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
  WiFiClientSecure tls;
  WiFiClient plain;
  struct StopAll { WiFiClientSecure& t; WiFiClient& p; bool s;
                   ~StopAll() { if (s) t.stop(); else p.stop(); } } _stop{tls, plain, isHttps};
  if (isHttps) { tls.setInsecure(); tls.setTimeout(20000); }

  // Defragment if needed, then refuse to start a handshake we can't complete: a doomed
  // connect() fails with -1 and fragments the heap further (the cascade the user hit
  // after a mistyped URL led to repeated failures). Bailing here with a clear message
  // keeps the heap intact so a retry can succeed. (Plain HTTP needs no big TLS block.)
  if (isHttps && reclaimHeapForTls() < TLS_MIN_BLOCK) {
    lastCode = HEAP_ABORT_CODE;        // negative: lets callers offer reboot-to-clean-heap
    lastErr = "low memory; try again";
    Serial.println("[net] aborting TLS POST: insufficient contiguous heap");
    return false;
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

  logHeapDetail("pre-POST-connect");   // full heap shape right before the handshake

  Client* cli = isHttps ? (Client*)&tls : (Client*)&plain;
  uint32_t t0 = millis();
  if (!cli->connect(hostPart.c_str(), port)) {
    lastCode = -1; lastErr = "connect failed";
    if (isHttps) { char eb[128] = {0}; tls.lastError(eb, sizeof(eb));
      Serial.printf("[net] POST connect failed in %ums; TLS lastError: %s\n",
                    (unsigned)(millis() - t0), eb[0] ? eb : "(none)"); }
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

  WiFiClientSecure tls;
  struct StopT { WiFiClientSecure& t; ~StopT() { t.stop(); } } _stop{tls};
  tls.setInsecure();
  tls.setTimeout(20000);

  // Refuse a handshake we can't complete; bailing keeps the heap intact for a retry.
  if (reclaimHeapForTls() < TLS_MIN_BLOCK) {
    lastCode = HEAP_ABORT_CODE;
    lastErr = "low memory; try again";
    Serial.println("[net] aborting TLS POST: insufficient contiguous heap");
    return false;
  }

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
    char eb[128] = {0}; tls.lastError(eb, sizeof(eb));
    Serial.printf("[net] POST connect failed in %ums; TLS lastError: %s\n",
                  (unsigned)(millis() - t0), eb[0] ? eb : "(none)");
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
