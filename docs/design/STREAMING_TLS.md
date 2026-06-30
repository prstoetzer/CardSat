# Streaming TLS for all uploads and downloads — and the debugging saga that got us here (0.9.41)

This is the definitive record of how CardSat's HTTPS upload/download paths reached their
current shape on the no-PSRAM ESP32-S3, and **why every large transfer must stream to/from
a file rather than hold its body in a RAM `String`/buffer.** It supersedes the narrower
`CLOUDLOG_UPLOAD_POSTMORTEM.md`, which was written before most of the root causes below were
found.

If you are about to add a network call, read the **Rules** section at the bottom first.

---

## 1. The environment that makes this hard

- **M5Stack Cardputer ADV: ESP32-S3FN8, NO PSRAM, ~140 KB usable heap.**
- A resident **~32 KB drawing sprite** (240×135 @ 8 bpp `M5Canvas`) sits mid-heap for the
  whole session and is never freed (freeing it during a handshake was tried and rejected —
  the post-handshake heap stayed too fragmented to recreate it, and the screen froze).
- **mbedTLS needs ~32 KB of heap for a handshake, as TWO separate ~16 KB blocks** (the RX
  and TX content buffers, `MBEDTLS_SSL_IN/OUT_CONTENT_LEN`, each 16 KB by default).
- With the sprite resident, the **largest contiguous free block sits at ~31,732 bytes** —
  just under one clean 32 KB block. So a handshake's two 16 KB buffers fit only by using
  the largest block plus other free regions, and there is very little slack.
- **The Arduino IDE links a *precompiled* mbedTLS.** Setting `MBEDTLS_SSL_IN/OUT_CONTENT_LEN`
  to 4096 in a sketch or build flag has **no effect** (confirmed: arduino-esp32 #6286, and
  on-device). The only way to actually shrink the TLS buffers is a PlatformIO
  "Arduino-as-ESP-IDF-component" build that recompiles mbedTLS — see
  `docs/guides/PLATFORMIO_MIGRATION.md`. That migration is the permanent cure for the
  knife-edge described throughout this document; everything here is how to live within the
  constraint until then.

The single most important number to watch is **`heap_caps_get_largest_free_block`**, NOT
total free heap. Total free heap was always 55–70 KB during failures; the binding constraint
is the largest *contiguous* block.

---

## 2. The Cloudlog upload bug — a chain of five distinct failures

The Cloudlog HTTPS POST failed "continually," and it turned out to be **not one bug but five**,
each hidden behind the previous one and each found only by **on-device instrumentation**. The
overriding lesson of the whole effort:

> **Stop theorizing about the black box. Make the device report what it is actually doing.**
> Every wrong turn came from reasoning about TLS/HTTP from the outside. Every breakthrough
> came from adding a log line that revealed ground truth — first a `WiFiClientSecure::lastError()`
> probe, then per-step byte counters.

The layers, in the order they were peeled back:

### Layer 1 — `HTTPClient::POST()` does not deliver the body reliably
The original code used Arduino's `HTTPClient`. It returned `-1`/connection-refused or `408`
depending on conditions, and toggling HTTP/1.0 vs HTTP/1.1 (`useHTTP10`) changed the symptom
but never fixed it. **Fix:** abandon `HTTPClient` for the POST paths entirely and send the
request by hand over a `WiFiClientSecure` we control (`cli->connect()`, then `cli->write()`,
then read the response ourselves). This made the next layers *visible* — `HTTPClient` had been
hiding them.

### Layer 2 — TLS write back-pressure stalls the body at 3072 bytes
Sending the body in 512-byte chunks, the socket accepted exactly **6 × 512 = 3072 bytes** and
then `write()` returned 0, and the code treated 0 as "dead" and gave up. The server got a
partial body and timed out. **`write() == 0` does not mean the socket is dead — it means the TX
window is momentarily full.** Fix: treat a 0 return as "full, retry": flush, yield, wait, retry,
with a long overall budget; only give up if the connection actually drops or no progress is made
for ~30 s.

### Layer 3 — the 408 is Apache `mod_reqtimeout`: the request arrived too slowly
Even after the whole body was written (`wrote 3620/3620`), the server returned **408 Request
Timeout**. This was proven host-side: piping the *identical* request bytes through
`openssl s_client` got an instant `HTTP/1.1 401` (clean "missing api key") — so the framing was
perfect and the server was happy with the bytes. The 408 was purely about **speed**: a ~1 s
handshake followed by the request dribbling across many small TLS records on a marginal link
exceeded Apache's request-read timeout. Fix: **build the entire request — request line, headers,
AND body — into one buffer and send it in as few `write()`s as possible**, so it goes out in a
single ~16 KB TLS record and the whole request lands at once.

### Layer 4 — the response read race on transient `connected()`
With the send fixed, the status line came back **empty** (`code=0`). The read loop waited with
`while (cli->connected() && !cli->available())` — but **`WiFiClientSecure::connected()` blinks
false between TLS records and right after the server's `Connection: close`, even while the
buffered reply is still readable.** The instant it blinked false, the loop exited and read
nothing. Fix: **poll `available()` purely; never consult `connected()` to decide whether to keep
waiting.** If the server closed after replying, `available()` stays > 0 until the buffer is
drained. (This exact hazard was already documented in the streaming GET path — and was walked
into anyway when hand-writing the POST read.)

### Layer 5 — the handshake OOM knife-edge (the residual, never fully cured in Arduino)
Independently of all the above, a *cold* connect attempt sometimes fails at the handshake with
`WiFiClientSecure::lastError()` = **"SSL - Memory allocation failed"**, because the two 16 KB
buffers can't be placed when the largest block is only ~31,732. A retry/reboot to a clean heap
gets past it. This is the knife-edge from §1. It is mitigated, not cured, in the Arduino build
(see §4); the permanent cure is the PlatformIO 4 KB-buffer migration.

After all five: Cloudlog returns **`201 Created`**, sprite resident, no reboot.

---

## 3. LoTW — same transport, two extra problems, and the streaming insight

LoTW uses `multipart/form-data` with a larger (~9 KB) **binary** `.tq8` body read from SD. Porting
the manual-request fix from Cloudlog exposed two more issues:

### 3a — handshake OOM *every* time, because the body competed with the handshake
LoTW built the ~9 KB body buffer **before** connecting. With ~9 KB committed, the handshake's
two 16 KB buffers no longer fit (Cloudlog had succeeded with only ~4 KB of slack to spare; LoTW's
larger body left ~5 KB *less*, so it failed consistently). **Fix:** reorder — probe the file's
*size* (cheap, no big allocation), do the handshake with maximum free heap, and only **after** it
connects read the body. The handshake's slot is then unobstructed. This alone took LoTW from
"fails every time" to "handshake succeeds."

### 3b — `oom building body` after the handshake → stream instead of buffer
With the handshake holding its two 16 KB buffers, a ~9 KB *contiguous* `malloc` for the body then
failed. **The realization that reshaped both upload paths:** the body lives in a file, so it never
needs to be in RAM as one buffer. **Stream it** — send the headers, then copy the file to the
socket in ~1 KB chunks, then the multipart tail. Peak RAM becomes one ~1 KB chunk regardless of
body size. This is the mirror of the existing `httpsGetToFile` (which streams downloads to disk).

After 3a + 3b: LoTW returns **`.UPL. accepted` / "File queued for processing"**, sprite resident.

---

## 4. The batching dead-end — why "upload in chunks" made it WORSE

When asked how Cloudlog would scale past ~50 QSOs, the first attempt was **batching**: upload N
QSOs per POST in a loop, marking each batch uploaded. On this hardware **batching is actively
worse**, and the on-device log proved it:

- Each batch needs **its own TLS handshake**. One upload became many handshakes — and each
  handshake is a fresh gamble against the ~32 KB knife-edge.
- **Worse:** after a successful TLS session the largest block collapsed to **~18–23 KB and stayed
  there** (the session's buffers fragment the heap and don't fully coalesce on `stop()`). So the
  *second* batch's handshake couldn't get its block and aborted → reboot. The result was a
  **reboot-per-batch** ordeal.

The lesson: **on a no-PSRAM heap, the number of TLS handshakes is the scaling risk, not body
size.** Fewer handshakes is strictly better. Batching optimizes the wrong axis.

**The right fix is streaming, not batching:** one handshake, one streamed body, constant ~1 KB
RAM, unlimited QSO count. Cloudlog's JSON body is built to a temp file
(`FILE_CLOUDLOG_TMP` on `Store::fs()`) and streamed by `httpsPostJsonFile()`, exactly as LoTW
streams its `.tq8`. Verified working: three back-to-back 14-QSO uploads, all `201`, largest block
a healthy 31,732 before each, **no reboots**.

### Storage note (no SD requirement)
The Cloudlog temp file goes on **`Store::fs()`**, which is the SD card when present and **internal
LittleFS otherwise** — the same filesystem the QSO log itself lives on. Cloudlog is advertised as
working without an SD card, and this preserves that: a card-less device writes the temp file to
internal flash transparently. **Do not** hard-code uploads to SD.

---

## 5. Things that were tried and DISPROVEN (do not revisit)

- **Heap fragmentation from String churn was the cause.** No — host paths were already audited
  clean, and the real failures were an Apache timeout (Layer 3) and a contiguous-block knife-edge,
  not churn. (String hygiene is still good practice, just not *the* bug.)
- **HTTP/1.0 vs HTTP/1.1 framing.** `useHTTP10` was toggled four times; irrelevant to the 408.
- **Reducing `LOG_VIEW_MAX` (60→45) to free RAM and grow the largest block.** Disproven
  on-device: the largest block stayed pinned at exactly 31,732 regardless, because it's bounded by
  the sprite's fixed mid-heap position, not by total `.bss`. Reverted to 60.
- **Freeing the sprite during the handshake + reboot-to-recover.** Fixed the handshake but the
  post-TLS heap stayed too fragmented to recreate the 32 KB sprite → screen froze. Rejected; all
  the machinery (`freeCanvasForTls`/`restoreCanvasAfterTls`/reboot-recovery) was reverted to
  no-ops.
- **`build_opt.h` / `-DCONFIG_MBEDTLS_SSL_*_CONTENT_LEN` to shrink TLS buffers in Arduino IDE.**
  Confident dead end — the precompiled mbedTLS ignores it (arduino-esp32 #6286). Only the
  PlatformIO Path B build recompiles mbedTLS.
- **`std::vector` for favorites/view arrays.** Rejected — introduces heap churn/realloc on a
  fragmentation-sensitive heap; nets negative versus stable static arrays.
- **The proactive WiFi-cycle defrag** (`reclaimHeapForTls` → `hardResetWifi`) genuinely helps some
  cases but **could not** move the largest block in the batching scenario (it self-disabled after
  "no effect"). Useful as a last resort, not a fix.

---

## 6. The current TLS path map (audited 0.9.41)

**All large/unbounded transfers stream to/from a file. Both uploads stream.** Full audit:

Streaming (body never wholly in RAM) — SAFE:
- `fetchGpToFile` → `httpsGetToFileRetry` — GP catalog (~400 KB)
- `fetchSatnogsTransmittersToFile` → `httpsGetToFile` — transponder lists (~200 KB)
- `fetchSpaceWeather` → `httpsGetToFileRetry` — F10.7 (~40 KB), Kp (~80 KB)
- `fetchWeather` → `httpsGetToFileRetry` (~16 KB)
- `fetchAmsatStatus` → `httpsGetToFileRetry` (~200 KB)
- `fetchHamsat` → `httpsGetToFileRetry` (~60 KB)
- per-satellite TX cache → `httpsGetToFileRetry`
- **Cloudlog upload** → `httpsPostJsonFile` — streams JSON body from `FILE_CLOUDLOG_TMP`
- **LoTW upload** → `httpsPostMultipart` — streams `.tq8` from SD

Whole-body-in-RAM (`String`) — BOUNDED, acceptable:
- `qrzLookup` login → `httpsGet`, cap **8 KB**
- `qrzLookup` query → `httpsGet`, cap **16 KB**
- One callsign's QRZ XML is realistically <4 KB and is a deliberate one-shot user action, far
  below the handshake block. Not a heap-exhaustion risk.

DEAD CODE — defined but never called, now carrying loud "DO NOT CALL" banners:
- `Net::fetchGp(String&)` (400 KB cap) — use `fetchGpToFile`
- `Net::fetchSatnogsTransmitters(String&)` (200 KB cap) — use `fetchSatnogsTransmittersToFile`
- `Net::httpsPostJson(const String& body, …)` — superseded by `httpsPostJsonFile`; kept as a
  harmless small-body helper, but new large uploads MUST use the streaming version.

---

## 7. RULES for any new network call (read before adding one)

1. **Downloads:** if the response can exceed a few KB, or its size is not strictly bounded, use
   `httpsGetToFile`/`httpsGetToFileRetry` and parse from the file with a streaming parser.
   Never `httpsGet`-into-a-`String` for anything but a small, hard-capped, single response (the
   QRZ pattern is the *only* acceptable use).
2. **Uploads:** stream the body from a file. JSON → write the body to a temp file on
   `Store::fs()` and use `httpsPostJsonFile`. Multipart/binary → `httpsPostMultipart` (streams).
   Never build a multi-KB body in one RAM buffer.
3. **One upload = one handshake.** Do NOT batch to manage body size; streaming removes the size
   problem without multiplying handshakes (see §4).
4. **Watch `largest_free_block`, not total free heap.** Gate handshakes on `TLS_MIN_BLOCK` and
   bail cleanly if it can't be met, so a doomed connect doesn't fragment the heap further.
5. **Defer the body until after the handshake** when the body is large — connect first (max free
   heap), then read/stream the body. Don't let the body buffer compete with the handshake's two
   16 KB allocations.
6. **Reading a response:** poll `available()`; never let a transient `!connected()` abort the
   read. Short-circuit with a clear "no response" if nothing arrives within the timeout.
7. **Writing:** treat `write() == 0` as "TX window full, retry," not "dead."
8. **Temp files on `Store::fs()`**, never hard-coded to SD — preserves no-SD operation.
9. **Keep the serial diagnostics.** The byte-count / heap / status / `lastError` logs cost
   ~nothing (stack buffers, no heap churn, format strings in flash) and are the only reason this
   was solvable. Redact secrets (`redactBody`/`redactUrl`), but log freely otherwise. A user
   pasting their serial console is the supported way to diagnose field issues.

---

## 8. The one honest residual

The cold-start handshake **`SSL - Memory allocation failed`** can still occur on a fresh attempt
when the largest block sits right at ~31,732. It is mitigated (body deferral, single handshake,
widest possible margin, reboot-resume safety net) but **not eliminated** in the Arduino build,
because the Arduino-linked mbedTLS cannot be told to use smaller buffers. The permanent fix is the
PlatformIO "Arduino-as-ESP-IDF-component" migration with `CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN=4096`,
which drops the handshake's need from ~32 KB to under 10 KB and dissolves the knife-edge entirely.
See `docs/guides/PLATFORMIO_MIGRATION.md`.
