# CardSat v0.9.41 — release notes

A heap-focused maintenance release for the no-PSRAM ESP32-S3: it reclaims permanently
resident RAM, makes the awards screens far gentler on the heap, and adds a last-resort
WiFi-cycle that recovers a fragmented heap before a TLS handshake. No new user-facing
screens; the changes are under the hood and are documented as individually reversible.

## Heap & fragmentation

- **Float orbital elements (~3.5 KB reclaimed).** Eight of `SatEntry`'s mean elements
  (inclination, eccentricity, RAAN, argument of perigee, mean anomaly, B\*, and the two
  mean-motion derivatives) are now stored as `float` instead of `double`, shrinking each
  satellite record from 136 to 112 bytes — about **3.5 KB** of permanently resident RAM
  given back across the 150-satellite table, which enlarges the contiguous block the
  mbedTLS handshake needs. This is **safe with no accuracy change**: the elements are
  never handed to SGP4 as raw numbers — `gpToTle()` formats them into a fixed-width TLE
  string that SGP4 re-parses, and `float`'s ~7 significant digits exceed every TLE
  field's precision. Mean motion and the epoch are deliberately **kept as `double`**
  (their values need more precision than `float` offers), so predictions are
  bit-identical to the previous firmware. Fully reversible — see
  `docs/design/HEAP_FLOAT_ELEMENTS.md`.
- **Awards screens no longer churn the heap.** The awards tally previously allocated a
  throwaway `String` per QSO while scanning the log (to upper-case the grid square),
  which fragmented the heap on long logs — the likely cause of the "less free memory the
  more I use it" behaviour. The grid is now decoded directly from its fixed character
  buffer with **zero allocation**, and opening an all-satellites worked-list no longer
  re-streams the whole log when the totals are already in memory. Same results, much less
  heap traffic.
- **Same allocation-free discipline applied elsewhere.** The handful of other file-rewrite
  paths that copied each line into a second `String` just to trim it (saving a calibration,
  tone override, or note; removing a manual sked) now trim in place, and a note lookup
  copies its text straight from the line buffer instead of building a substring. These run
  only on explicit user actions (not in the hot loop, which was already allocation-free), so
  the benefit is small, but each removes a real per-line allocation with no behaviour change.
- **Proactive WiFi-cycle before a handshake (last resort).** If the existing passive
  coalesce-wait still leaves the largest free block below the TLS threshold, CardSat now
  does **one** WiFi disconnect/reconnect (reusing the existing socket-pool recovery) to
  force LWIP/mbedTLS buffers back to the heap so the blocks merge — the final step before
  declining the handshake. It is gated hard: only when actually short, only when
  connected, and at most once per 30 s so it can't storm reconnects or fight the existing
  recovery. Reversible via a single toggle — see `docs/design/HEAP_WIFI_CYCLE.md`.

## HTTPS uploads (Cloudlog & LoTW) — rebuilt around streaming

This release replaces the entire upload transport. Both Cloudlog and LoTW now send their
requests by hand over a `WiFiClientSecure` (not Arduino `HTTPClient`) and **stream the body
from a file** so it is never held whole in RAM. This was the outcome of an extended on-device
debugging effort; the full story, every disproven theory, and the rules for future network
code are in **`docs/design/STREAMING_TLS.md`**. Summary of what changed and why:

- **`HTTPClient::POST` replaced with manual requests.** `HTTPClient` was failing to deliver
  POST bodies (manifesting as `-1`/connection-refused or `408`), and it hid the real causes.
  The POST paths now build and send the request directly, with full byte-level logging.

- **TLS write back-pressure handled.** `WiFiClientSecure::write()` returns 0 when its TX
  window is momentarily full; the old code treated that as a dead socket and gave up partway
  through the body. A 0 return is now treated as "full, retry."

- **The 408 was Apache `mod_reqtimeout` — the request arriving too slowly.** Proven host-side:
  the identical request bytes get an instant `401` from the server, so the framing was always
  correct; the request was just dribbling across many small TLS records on a marginal link.
  Requests are now sent in as few TLS records as possible so the whole request lands at once.

- **Response-read race fixed.** `WiFiClientSecure::connected()` blinks false between TLS
  records and right after the server's close, even while the reply is still buffered. The read
  loop now polls `available()` and never aborts on a transient `!connected()`.

- **Body deferred past the handshake.** The body is no longer allocated before connecting:
  CardSat does the handshake with maximum free heap, then streams the body. This stopped LoTW's
  larger body from starving the handshake's two 16 KB mbedTLS buffers.

- **Streaming, not batching, for large logs.** Cloudlog's JSON body is written to a temp file
  on `Store::fs()` and streamed by the new `httpsPostJsonFile()`; LoTW streams its `.tq8` the
  same way. A batching attempt was tried first and **reverted**: on this no-PSRAM heap each
  batch needs its own handshake, and a completed TLS session leaves the heap too fragmented for
  the next one — batching multiplied the failures and forced a reboot per batch. Streaming keeps
  every upload to a **single handshake at constant ~1 KB RAM, regardless of QSO count.**

- **No SD-card requirement added.** The Cloudlog temp file lives on `Store::fs()` — the SD card
  when present, internal LittleFS otherwise — so Cloudlog still works on a card-less device.

- **`TLS_MIN_BLOCK` gate** on the POST paths still declines a handshake the heap can't satisfy
  (offering a reboot to a clean heap) rather than failing mid-attempt.

- **Reverted dead ends from earlier 0.9.41 drafts:** the `LOG_VIEW_MAX` 60→45 reduction (the
  largest contiguous block is pinned by the sprite's position, not total `.bss`, so it bought
  nothing — restored to 60) and the sprite-free-during-handshake approach (the freed region
  stayed too fragmented to recreate the 32 KB sprite — screen froze). See `STREAMING_TLS.md` §5.

### Known residual

A *cold* handshake can still occasionally fail with `SSL - Memory allocation failed` when the
largest contiguous block sits right at ~31.7 KB; a retry/reboot to a clean heap gets past it.
This is the no-PSRAM contiguous-block knife-edge. It cannot be fully cured in the Arduino IDE
build because the linked mbedTLS is precompiled and ignores buffer-size settings; the permanent
fix is the PlatformIO migration in `docs/guides/PLATFORMIO_MIGRATION.md`
(`CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN=4096`), which drops the handshake's need from ~32 KB to
under 10 KB.

## Audit: no remaining heap-exhaustion paths

Every large or unbounded transfer streams to/from a file (GP catalog, transponder lists, space
weather, weather, AMSAT status, hams.at feed, and both uploads). The only whole-body-in-RAM
network reads are QRZ callsign lookups, hard-capped at 8–16 KB (one callsign's XML, a deliberate
user action). Two unused to-String fetchers that still carried 400 KB/200 KB caps now carry loud
"DO NOT CALL" banners pointing at their streaming replacements, so they can't be reintroduced by
accident. Full path map in `docs/design/STREAMING_TLS.md` §6.

## Notes

- These are internal robustness changes; existing `gp.json` caches and QSO logs are
  unaffected and need no migration.
- The serial console now carries detailed upload diagnostics (byte counts, heap shape, TLS
  status, and the TLS `lastError` string), with secrets redacted. Asking a user to share their
  serial log is the supported way to diagnose a field issue.
