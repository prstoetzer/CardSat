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
uint32_t Net::TLS_MIN_BLOCK  = 28000;   // mbedTLS handshake needs a contiguous block at
                                        // least this big; below it we defragment first and
                                        // decline if still short. Set from observation: an
                                        // upload connected fine at largest-block ~31.7 KB,
                                        // so the true floor is under that -- 28 KB leaves a
                                        // margin while still catching real exhaustion (the
                                        // post-failed-upload fragmentation the user hit).

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

  Serial.printf("[net] received %u bytes (declared %d), heap now %u\n",
                (unsigned)total, len, (unsigned)ESP.getFreeHeap());
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

  Serial.printf("[net] streamed %u bytes to %s (declared %d), heap now %u\n",
                (unsigned)total, path, len, (unsigned)ESP.getFreeHeap());
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

bool Net::fetchGp(const String& url, String& out) {
  // GP/OMM JSON can be a few hundred KB for the full amateur list; cap higher
  // than the old TLE text. MAX_SATS still bounds what we actually store.
  return httpsGet(url, out, 400000);
}

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

  // Read the file to upload (small; the .tq8 is a few KB).
  File f = Store::fs().open(filePath, "r");
  if (!f) { lastErr = "open upload file failed"; return false; }
  size_t flen = f.size();
  if (flen == 0 || flen > 256000) { f.close(); lastErr = "upload file size bad"; return false; }

  const String boundary = "----CardSatLoTW8b2f4c1d";
  String head;
  head  = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"" + String(fieldName) +
          "\"; filename=\"" + String(fileName) + "\"\r\n";
  head += "Content-Type: " + String(contentType) + "\r\n\r\n";
  const String tail = "\r\n--" + boundary + "--\r\n";

  size_t bodyLen = head.length() + flen + tail.length();
  uint8_t* body = (uint8_t*)malloc(bodyLen);
  if (!body) { f.close(); lastErr = "oom building body"; return false; }
  memcpy(body, head.c_str(), head.length());
  size_t got = f.read(body + head.length(), flen);
  f.close();
  if (got != flen) { free(body); lastErr = "read upload file short"; return false; }
  memcpy(body + head.length() + flen, tail.c_str(), tail.length());

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
  // in httpsPostJson). free() the assembled body first so the abort doesn't leak it.
  if (reclaimHeapForTls() < TLS_MIN_BLOCK) {
    free(body);
    lastErr = "low memory; try again";
    Serial.println("[net] aborting TLS POST: insufficient contiguous heap");
    return false;
  }

  HTTPClient http;
  http.setUserAgent("CardSat-Cardputer/1.0");
  http.setConnectTimeout(20000);
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);

  if (!http.begin(client, url)) { free(body); lastErr = "begin failed"; return false; }
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  int code = http.POST(body, bodyLen);
  free(body);
  lastCode = code;
  noteConnResult(code);
  if (code != HTTP_CODE_OK) {
    lastErr = (code > 0) ? ("HTTP " + String(code))
                         : HTTPClient::errorToString(code);
    Serial.printf("[net] POST failed: code=%d (%s)\n", code, lastErr.c_str());
    http.end();
    return false;
  }

  response = http.getString();
  if (response.length() > (int)maxResp) response = response.substring(0, maxResp);
  http.end();
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
    lastErr = "low memory; try again";
    Serial.println("[net] aborting TLS POST: insufficient contiguous heap");
    return false;
  }

  HTTPClient http;
  http.setUserAgent("CardSat-Cardputer/1.0");
  http.setConnectTimeout(20000);
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);

  bool began = isHttps ? http.begin(tls, url) : http.begin(plain, url);
  if (!began) { lastErr = "begin failed"; return false; }
  http.addHeader("Content-Type", contentType);

  int code = http.POST((uint8_t*)body.c_str(), body.length());
  lastCode = code;
  noteConnResult(code);
  // Accept any 2xx (Cloudlog returns 201 Created on success).
  if (code < 200 || code >= 300) {
    lastErr = (code > 0) ? ("HTTP " + String(code))
                         : HTTPClient::errorToString(code);
    Serial.printf("[net] POST(json) failed: code=%d (%s)\n", code, lastErr.c_str());
    // Capture the error body too -- callers surface the server's reason string.
    response = http.getString();
    if (response.length() > (int)maxResp) response = response.substring(0, maxResp);
    http.end();
    return false;
  }

  response = http.getString();
  if (response.length() > (int)maxResp) response = response.substring(0, maxResp);
  http.end();
  return true;
}
