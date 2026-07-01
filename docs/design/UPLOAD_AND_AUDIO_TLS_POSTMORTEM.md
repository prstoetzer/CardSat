# Postmortem: HTTPS upload failures on the no-PSRAM ESP32-S3 (0.9.42)

**Status: resolved (v0.9.42).** This is the long-form writeup of a multi-session
debugging arc that turned out to be **three distinct root causes** wearing the same
disguise — an HTTPS call that failed while WiFi stayed associated and the heap looked
fine. Two were memory-contention bugs; one was a TCP send-buffer ceiling. All three are
fixed. This document exists so the dead ends are never re-walked and the
counter-intuitive lessons are preserved.

The single most important lesson, stated once up front because it recurred at every
stage:

> **Stop theorizing about the black box. Make the device report what it is actually
> doing.** Every wrong turn in this arc came from external reasoning about what "should"
> be happening. Every breakthrough came from a serial diagnostic line or a host-side
> test against the real server. We were wrong at least six times by pattern-matching
> plausible theories before each was disproven by an actual measurement.

---

## The shared symptom

Across several sessions the device would, under some conditions, fail an HTTPS request
with one of:

- `code=-1`
- `SSL - Memory allocation failed`

…while at the same time:

- WiFi stayed associated (IP held, RSSI normal),
- `ESP.getFreeHeap()` and the "largest free block" both looked healthy (~31 KB block),
- and **other** TLS operations in the same session often still worked.

That combination — TLS failing while the general heap looks fine and the radio is up —
is what made this hard. The error string (`SSL - Memory allocation failed`) actively
points at "out of memory" when the general heap is not the constraint. It took
instrumentation to see past it, three separate times.

---

## Cause 1 — Audio ↔ TLS internal-DMA-pool contention

**Status: resolved. Confirmed on-device.**

### Symptom

After the device had played any audio — a game sound effect, or (more insidiously)
recorded/played a voice memo — the *next* HTTPS GET would fail with
`SSL - Memory allocation failed`, even though the "largest free block" log still read
~31732 bytes. A clean boot with no audio never hit it.

### Why it was hard

The heap number we had been logging all along was **the wrong pool**. On the ESP32-S3
there are multiple heap capabilities. The "largest block" we printed was
`MALLOC_CAP_8BIT` (general). But **mbedTLS allocates its handshake buffers from the
*internal, DMA-capable* pool** (`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`), and the
**ES8311 I2S audio driver consumes the same scarce internal SRAM**. So the log we were
staring at could read a healthy 31 KB while the pool that actually mattered had been
eaten by the audio DMA buffer.

The trap within the trap: the speaker's I2S DMA buffer is **not** allocated by
`Speaker.begin()`. It is allocated on the **first actual audio output** (`tone()` /
`playRaw()`). And the voice-memo path leaves the speaker `begin()`'d after playback
(`finalize()` re-inits the speaker). So "I only recorded a memo, I didn't play a game
sound" was not a defense — the memo path alone was enough to leave the internal pool
short for the next handshake.

### The instrumentation that cracked it

Adding the *internal* pool to the heap log — printing
`heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` alongside the
general block — immediately showed the internal block collapsing after audio while the
general block stayed at 31732. That one extra number ended weeks of guessing.

### Dead ends (do not revisit)

- **Priming the audio buffer at boot** (`Speaker.begin()` in `setup()`): made boot-time
  GETs *fail*, because it allocated the DMA buffer before TLS had settled. Reverted.
- **Priming with a boot `playRaw()`**: same failure for the same reason. Reverted.

Both were the intuitive "just initialize it once up front" fix, and both were exactly
backwards on this hardware.

### The fix

In `App::tlsBusyTrampoline()` — the function that already brackets every fetch
(suspending the rigctld/rotctld listeners for the duration) — release the speaker's I2S
DMA RAM back to the internal pool **before every handshake**, and restore audio after:

```cpp
// before the fetch (busy count 0 -> 1):
s_spkWasOnForFetch = M5Cardputer.Speaker.isEnabled();
if (s_spkWasOnForFetch) { M5Cardputer.Speaker.stop(); M5Cardputer.Speaker.end(); }
// ... fetch runs ...
// after the fetch (busy count 1 -> 0):
if (s_spkWasOnForFetch) {
  M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(s_self->cfg.spkVolume);
  s_spkWasOnForFetch = false;
}
```

`Speaker.end()` hands the DMA RAM back; the handshake then has the internal block it
needs. On a clean boot with no audio, `isEnabled()` is false and this is a no-op — which
is why clean boots always worked. Confirmed on-device: record a memo, then run a full
update cycle (7+ TLS fetches) — all succeed at internal block ~31732.

### Why audio and fetches never collide beyond this

The trampoline is safe because on this single-threaded firmware a fetch is a blocking
call — while it runs, `loop()` is *inside* it, so no game sound, AOS alarm beep, or memo
can fire mid-handshake. Voice-memo record and playback are themselves blocking loops, so
a fetch can't start mid-memo either. The only interaction is "audio left the speaker
begun, then a fetch starts," which the trampoline's `isEnabled()` check catches.

---

## Cause 2 — QRZ grid-backfill dual-open-file heap starvation

**Status: resolved.**

### Symptom

The QRZ grid-backfill utility (fills missing `GRIDSQUARE` fields in the log by looking
up callsigns on QRZ) failed its QRZ TLS lookups — but only during the backfill, and the
*internal* pool read healthy (31732) with sockets not exhausted (0/16 open). Free heap,
however, was ~10 KB lower than on a working standalone fetch (~59 K vs ~69 K).

### Root cause

The original `fillGridsQrz()` opened the **log file (read)** and a **temp file (write)**
*simultaneously* and ran the QRZ TLS lookup **inside that loop**. Two open VFS file
handles each hold ~4–5 KB of buffers, so ~10 KB was unavailable during the handshake —
exactly enough that mbedTLS's second (~16 KB content) buffer couldn't be placed, even
though the *largest single block* still looked fine. A classic fragmentation-vs-total
distinction: enough total, not enough contiguous, because two file buffers were sitting
in the middle of it.

### The fix

Restructure into three phases so **no file is open during any TLS call**:

1. **Pass 1** — open the log, collect up to `FILL_CAP = 20` distinct missing callsigns
   into fixed `.bss` arrays (`char wantCall[20][14]` / `gotGrid[20][10]`), close the log.
2. **Lookups** — run the QRZ TLS queries with **all files closed** (full heap for TLS),
   with callsign de-duplication so each unique call is fetched once.
3. **Pass 2** — reopen log + temp and stream-rewrite, filling grids from the results,
   with **no TLS in flight**.

Confirmed on-device: 14 clean QRZ queries at ~68 K heap, followed by a Cloudlog 201.

### Lesson

"Largest free block is fine" is necessary but not sufficient. When TLS fails and the
block looks fine, check **total** free heap too — a shortfall there points at something
holding buffers (open files, a second client, an audio DMA buffer) between you and the
contiguous span mbedTLS needs for its *second* allocation.

---

## Cause 3 — The TCP_SND_BUF upload send ceiling

**Status: resolved via reboot-per-batch (see the next section).**

This is the one that took the longest and produced the most disproven theories, because
the failure was deterministic in a way that looked like a bug in our code but was
actually a fixed platform limit.

### Symptom

The LoTW `.tq8` upload connected fine (`POST TLS connected in ~350 ms`), streamed the
request body to **exactly ~5120–6144 bytes**, then stalled and the connection
half-closed. The exact stall point (5120 on some runs, 6144 on others) was suspiciously
consistent across reboots — network back-pressure varies run to run; this did not.

Meanwhile the **Cloudlog** upload (also an HTTPS POST, byte-for-byte the same streaming
code) always worked. The only difference: Cloudlog's body for the same QSOs was ~3851
bytes; LoTW's gzipped `.tq8` was ~9080 bytes.

### The instrumentation that cracked it

We added three diagnostics to the upload write loop that distinguished the three
possible failure modes:

1. `f.read() returned 0 …` → an SD short-read (the file couldn't be fully read back).
2. `write() returned 0 … (connected=1/0)` repeatedly → the send buffer / peer window.
3. `write() returned <0 …` → a TLS write error.

Plus we fixed a real latent bug found while adding them: `client.write()` returns `int`
and can be `-1`, but the code stored it in `size_t`, so a `-1` silently became a huge
unsigned number that looked like success. (It was changed to `int`.)

The next log was decisive:

```
POST TLS connected in 338ms; streaming request
write() returned 0 at off 0/1024 (connected=0, avail=0)
writeAll TIMEOUT after 30s at off 0/1024
writeAll failed at streamed 5120/9080
```

So: ~5 KB flows, then `write()` returns 0 with the socket **already closed**
(`connected=0`), and 30 s of retries get nothing. Not an SD read (that diagnostic never
fired), not a TLS write error (no negative return). The connection is **torn down after
~5 KB of body**.

### Ruling out the server (host-side tests)

Every theory that blamed the server or the request shape was disproven by testing the
**real LoTW endpoint from a host**:

- A 9 KB multipart body over HTTP/1.1 with `Connection: close` and our exact
  `User-Agent`: **HTTP 200**.
- The same body **throttled to 2 KB/s** (to mimic the ESP32's slow trickle): **HTTP
  200**. So it is *not* a server-side slow-body (`mod_reqtimeout`) close.
- The same body **dribbled in 512-byte TLS records with 20 ms gaps** (to mimic many
  small ESP32 writes): **HTTP 200**. So it is *not* the server rejecting many small
  records / a slowloris heuristic.

The server is patient and accepts our exact request at any speed and record size. **The
close is entirely on the ESP32 side.**

### Ruling out TLS 1.3

A tempting theory was a TLS 1.3 large-body send bug. Research and the arduino-esp32
source confirmed `WiFiClientSecure` already runs **TLS 1.2**, not 1.3 — so pinning to
1.2 would have been a no-op. Disproven before shipping.

### The actual root cause

The numbers finally lined up quantitatively: the ESP32 lwip default **`TCP_SND_BUF` is
~5744 bytes**. The two stall points, 5120 and 6144, **straddle 5744**. When the
application writes the body faster than the peer (LoTW behind an AWS ELB) drains it, the
send buffer fills at ~5744, `write()` returns 0, and — because the queued data isn't
being ACKed and the window never reopens — the socket eventually half-closes.
**Cloudlog's 3851-byte body fits *under* `TCP_SND_BUF`**, which is precisely why Cloudlog
always worked and LoTW did not. Same code, different body size relative to a fixed
buffer.

### Dead ends (do not revisit on the Arduino build)

- **`client.flush()` on the write stall.** The intuitive fix — force a TCP push so the
  window drains. Tested on-device: the stall persisted at the same ~5 KB.
  `WiFiClientSecure::flush()` does not force the push we needed on this core. (The
  `flush()` call was kept in the retry path anyway — it is harmless and helps edge
  cases — but it is not sufficient.)
- **One big `write()` of the whole body.** Disproven as necessary (server takes small
  records fine) *and* it does not change the ceiling: a 9 KB `write()` still cannot
  *queue* 9 KB when the send buffer caps at ~5744.
- **`setsockopt(SO_SNDBUF, …)` to enlarge the buffer.** On lwip this is generally
  compile-time-capped (`TCP_SND_BUF`), so `setsockopt` is a no-op or capped, and the fd
  is hidden behind `WiFiClientSecure` anyway.
- **Buffering the file in RAM instead of streaming.** The stall is in the *send* path,
  which is identical regardless of where the bytes come from.

The only Arduino-IDE-viable, reliable fix that remained was to keep every upload body
**below `TCP_SND_BUF`** — i.e. batch large uploads. The permanent cure (raising
`TCP_SND_BUF` and shrinking the TLS record buffers) requires a project-owned
`sdkconfig`, which is only reachable via a PlatformIO migration; that is documented
separately in `docs/design/LOTW_UPLOAD_SIZE_WORKAROUNDS.md` and
`docs/guides/PLATFORMIO_MIGRATION.md`.

---

## The solution: reboot-per-batch uploads

**Status: implemented and confirmed on-device for LoTW (normal and resend modes).**

### Why batching can't be a simple loop

The obvious fix — split the QSOs into sub-5744-byte uploads and POST them in a loop —
fails on a *second* no-PSRAM constraint this project had already hit elsewhere:

> **Multiple cold-start TLS handshakes cannot run in one session on this part.** The
> first handshake needs ~32 KB contiguous; post-handshake heap fragmentation collapses
> the largest block to ~18–23 KB, so a *second* handshake in the same session fails with
> `SSL - Memory allocation failed`.

This is the same "-1 wall" the **"cache all transponders"** feature works around: it
does **one batch per boot, then `ESP.restart()`**, and a resume hook in `setup()` runs
the next batch in a **pristine boot** (a cold boot has no prior TLS fragmentation).

> Note: back-to-back *streamed downloads* (GP, weather, QRZ, Cloudlog) do succeed in one
> session, because they reuse the settled post-first-handshake layout (~21 KB block),
> which is enough for those handshakes. The multi-handshake wall is specifically about
> *many sequential uploads* each needing a fresh ~32 KB.

### The design

Mirror the transponder-cache pattern for uploads. Constants (`config.h`):

- `LOTW_BATCH_QSOS = 6` — ~4.1 KB compressed `.tq8` body, safely under 5744.
- `CL_BATCH_QSOS = 15` — Cloudlog's ADIF is ~275 B/QSO, so 15 ≈ 4.1 KB.
- `SAFE_UPLOAD_BODY = 5000` — the documented ceiling, under 5744 with margin for the
  multipart headers/tail.

**LoTW** (`doLotwUpload`):
- Gather at most `LOTW_BATCH_QSOS` QSOs, sign, POST.
- On `.UPL. accepted`, mark those QSOs uploaded. If any un-uploaded QSOs remain,
  **cache the key passphrase + a resend cursor in RTC RAM and reboot**;
  `resumeLotwIfPending()` in the fresh boot continues **without re-prompting**. The whole
  multi-batch run is one passphrase entry.
- The passphrase lives in `RTC_NOINIT_ATTR` memory, which survives `ESP.restart()` but
  **not** power loss. At boot it is trusted only if `esp_reset_reason()` is a
  software/deep-sleep reset **and** a magic value matches; otherwise it is scrubbed. It
  is also scrubbed on completion and on any failure, so it never lingers and never
  touches flash/SD.
- **Resend ("upload ALL") mode** re-sends already-uploaded QSOs, so the uploaded-bit
  can't track progress. A **resend cursor** (`g_lotwResendSkip`) in RTC records how many
  have been sent this run; each batch skips that many and advances the cursor, gating
  continuation on `total − cursor > 0`.

**Cloudlog** (`doCloudlogUpload`): same idea, simpler — Cloudlog authenticates with an
API key (no passphrase), so it uses only the existing one-shot **file marker** for the
reboot. The marker carries the resend flag + cursor (`"1 12"` format) so Cloudlog resend
mode also batches.

**Safety against reboot loops**: the marker/RTC state is consumed (cleared) *before* the
upload runs, and failure paths do not re-arm it — so a server reject, WiFi failure, or
bad passphrase stops the chain and shows an error instead of rebooting forever. This is
the same guard the transponder cache uses (a zero-progress batch stops).

**UI**: `doLotwUpload` and `doCloudlogUpload` both end with an explicit `draw()` because
the upload screens are static (the main loop does not auto-refresh them) — without it the
final status banner would stay on "Uploading…" until a keypress. (This was a real bug
fixed during bring-up: `doCloudlogUpload` had the `draw()`; `doLotwUpload` did not.)

### Confirmed behavior (on-device serial log)

A 14-QSO LoTW **resend** run:

```
resend batch n=6 (cap=6), sent=6/14, remaining=8   -> reboot
[fresh boot] continuing batched upload (..., resend)
resend batch n=6 (cap=6), sent=12/14, remaining=2  -> reboot
[fresh boot] continuing batched upload (..., resend)
signing 2 QSOs ... streamed 3108/3108 ok=1
resend batch n=2 (cap=6), sent=14/14, remaining=0  -> done
```

Cursor advances 6 → 12 → 14, the resend flag survives each reboot, the final partial
batch sizes itself to 2 QSOs, and the run stops cleanly at `remaining=0`. Normal-mode
LoTW batching was confirmed the same way. Cloudlog uses the identical design; its
large-log path is logically verified but had not yet been exercised on real >15-QSO data
at the time of writing.

---

## What survived into the release (operational logging)

The heavy debug scaffolding (per-`write()` return values, the socket-FD probe with its
redundant second TLS connect, the internal-pool heap term) was **removed** for release.
Deliberately **kept** is the low-noise operational logging that lets a user paste a
console log for field diagnosis:

- `[net] heap before TLS: <free> (largest block <n>), IP …, RSSI …`
- `[net] streamed <x>/<y> file bytes …, ok=<0|1>`
- `[lotw] batch marked=<n> (cap=<c>), remaining un-uploaded=<m>`
- `[lotw] resend batch n=<n> (cap=<c>), sent=<x>/<y>, remaining=<m>`
- `[cloudlog] …` equivalents.

These are the difference between "the upload didn't work" and being able to see exactly
which batch stopped and with how much heap.

---

## Consolidated lessons

1. **Instrument before theorizing.** Every root cause here was found by a diagnostic or a
   host test, never by reasoning about the black box. Ship a diagnostic, not a
   speculative fix.
2. **On the ESP32-S3, "largest free block" is not the whole story.** mbedTLS pulls from
   the *internal DMA-capable* pool; audio (ES8311/I2S) competes for it; and the handshake
   needs a *second* large allocation, so total free heap and open-buffer count matter as
   much as the single largest block.
3. **Audio DMA buffers are allocated on first use, not on `begin()`**, and the voice-memo
   path leaves the speaker begun — so any fetch must release the speaker first (the
   trampoline does).
4. **`HTTPClient` and `WiFiClientSecure` collapse different failures into one error.**
   `-1` and `SSL - Memory allocation failed` each cover multiple distinct causes; the
   error string points away from the truth.
5. **A deterministic stall point is a fixed-limit fingerprint.** 5120/6144 straddling
   5744 was `TCP_SND_BUF`, not a network glitch. Network problems vary; buffer limits
   don't.
6. **Test the real server from a host before blaming it.** One `curl`/`openssl` run
   proved LoTW accepts our exact request at any speed and record size, which redirected
   the whole investigation to the client side.
7. **Multiple cold TLS handshakes don't fit in one session on no-PSRAM** — reboot per
   batch, run each in a pristine boot. RTC RAM carries just enough state (and a scrubbed,
   reset-reason-validated passphrase) to make a multi-batch run feel like one action.
8. **Static screens need an explicit final `draw()`.** If the main loop doesn't refresh a
   screen, the completing operation must, or the UI looks hung until a keypress.

---

*Cross-references: `docs/design/LOTW_UPLOAD_SIZE_WORKAROUNDS.md` (Option A batching vs.
Option B PlatformIO, with the exact sdkconfig keys), `docs/design/STREAMING_TLS.md`
(the streaming upload/download architecture), `docs/design/CLOUDLOG_UPLOAD_POSTMORTEM.md`
(the earlier, related heap-build POST bug), `docs/design/HEAP_WIFI_CYCLE.md` and
`docs/design/HEAP_FLOAT_ELEMENTS.md` (the heap-headroom work that enlarges the contiguous
block).*
