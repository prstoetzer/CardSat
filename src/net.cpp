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

  Serial.printf(
    "[net] GET %s\n", url.c_str());
  {
    size_t freeHeap = ESP.getFreeHeap();
    size_t largest  = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    Serial.printf("[net] heap before TLS: free %u, largest block %u, IP %s, RSSI %d\n",
                  (unsigned)freeHeap, (unsigned)largest,
                  WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    // NOTE: we log the largest contiguous block (what the TLS handshake allocates)
    // but do NOT pre-emptively abort on it. An earlier guard at 42k turned out to
    // block connections that can actually complete in ~30k, so we let the handshake
    // be the judge -- a real failure surfaces a real transport error below, rather
    // than a manufactured "low heap" abort. The number stays here as a diagnostic.
  }

  WiFiClientSecure client;
  // Core 3.x NetworkClientSecure can leave a TLS/socket resource half-released on
  // teardown, so the *next* HTTPS connect() in the session fails instantly with
  // start_ssl_client:-1 ("connection refused") -- not a heap issue (it fails even
  // with 250k+ free; see arduino-esp32 #6165/#4992). Force an explicit stop() on
  // every exit path via this RAII guard, and disable keep-alive reuse below, so
  // each call starts from a clean socket.
  struct ClientStop { WiFiClientSecure& c; ~ClientStop() { c.stop(); } } _cstop{client};
  // Certificate validation disabled for simplicity (public data). For a
  // security-sensitive deployment, pin the CA root instead of setInsecure().
  client.setInsecure();
  // NOTE: earlier builds called client.setBufferSizes(8192, 2048) here to shrink
  // the TLS record buffers from the 16 KB default and save ~8 KB of heap on the
  // no-PSRAM ESP32-S3. ESP32 core 3.x replaced WiFiClientSecure with
  // NetworkClientSecure, which no longer exposes that method -- the mbedTLS
  // record buffer sizes are fixed at core-build time via MBEDTLS_SSL_IN/OUT_
  // CONTENT_LEN in sdkconfig and can't be set from the sketch. Do NOT re-add the
  // call; it will not compile against core 3.x.
  client.setTimeout(15000);

  HTTPClient http;
  http.setReuse(false);   // no keep-alive: avoid carrying half-open state between calls
  http.setUserAgent("CardSat-Cardputer/1.0");
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // AMSAT may 301/302
  http.useHTTP10(true);   // avoid chunked-encoding edge cases for static files

  if (!http.begin(client, url)) { lastErr = "begin failed"; return false; }
  http.addHeader("Accept", "*/*");

  int code = http.GET();
  lastCode = code;
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
      // Peer closed with nothing buffered: allow a short grace for any final
      // TLS-buffered bytes, then finish. Otherwise wait, up to a hard timeout.
      if (!http.connected() && !stream->available() && millis() - lastRx > 500)
        break;
      if (millis() - lastRx > 10000) break;           // idle/stall timeout
      delay(5);
    }
  }
  http.end();

  Serial.printf("[net] received %u bytes (declared %d), heap now %u\n",
                (unsigned)total, len, (unsigned)ESP.getFreeHeap());
  if (out.length() == 0) { lastErr = "empty body"; return false; }
  return true;
}

bool Net::fetchGp(const String& url, String& out) {
  // GP/OMM JSON can be a few hundred KB for the full amateur list; cap higher
  // than the old TLE text. MAX_SATS still bounds what we actually store.
  return httpsGet(url, out, 400000);
}

bool Net::fetchGpToFile(const String& url, const char* path) {
  return httpsGetToFileRetry(url, path, 400000, nullptr, 3);
}

bool Net::httpsGetToFile(const String& url, const char* path,
                         size_t maxBytes, size_t* written) {
  lastCode = 0; lastErr = "";
  if (written) *written = 0;
  if (!connected()) { lastErr = "no WiFi"; return false; }

  // Log the largest contiguous block (what the TLS handshake allocates) as a
  // diagnostic, but do NOT pre-emptively abort on it -- an earlier 42k guard
  // blocked downloads that can complete in ~30k. Let the handshake be the judge.
  Serial.printf("[net] largest free block before TLS: %u\n",
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  // Guard against a full filesystem (the internal LittleFS partition is small).
  // Need room for the body plus slack; if we can't be sure, don't write a file
  // that would truncate and then fail to parse.
  {
    size_t freeb = Store::freeBytes();
    if (freeb && freeb < maxBytes + 4096) {
      lastErr = "low flash";
      Serial.printf("[net] abort GET: free flash %u < need %u\n",
                    (unsigned)freeb, (unsigned)(maxBytes + 4096));
      return false;
    }
  }
  Serial.printf("[net] heap before TLS: free %u, largest block %u, IP %s, RSSI %d\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());

  WiFiClientSecure client;
  // See httpsGet: force explicit stop() on every exit (core 3.x second-connect bug).
  struct ClientStop { WiFiClientSecure& c; ~ClientStop() { c.stop(); } } _cstop{client};
  client.setInsecure();
  // setBufferSizes() removed: not available on core 3.x NetworkClientSecure (see httpsGet)
  client.setTimeout(15000);

  HTTPClient http;
  http.setReuse(false);   // no keep-alive (see httpsGet)
  http.setUserAgent("CardSat-Cardputer/1.0");
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);

  if (!http.begin(client, url)) { lastErr = "begin failed"; return false; }
  http.addHeader("Accept", "*/*");

  int code = http.GET();
  lastCode = code;
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
      if (!http.connected() && !stream->available() && millis() - lastRx > 500)
        break;
      if (millis() - lastRx > 10000) break;
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
  return true;
}

bool Net::httpsGetToFileRetry(const String& url, const char* path,
                              size_t maxBytes, size_t* written, int attempts) {
  if (attempts < 1) attempts = 1;
  for (int i = 0; i < attempts; i++) {
    if (httpsGetToFile(url, path, maxBytes, written)) return true;
    // A "low flash" failure won't fix itself on retry -- bail immediately.
    if (lastErr == "low flash") return false;
    Serial.printf("[net] attempt %d/%d failed (%s); retrying\n",
                  i + 1, attempts, lastErr.c_str());
    delay(400 * (i + 1));   // simple linear backoff
  }
  return false;
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
