# Postmortem: the HTTPS reliability arc — mbedTLS → BearSSL, and the death of the reboots (0.9.43)

**Status: resolved and verified on-device (v0.9.43).**

This is the long-form writeup of the debugging arc that finally made HTTPS on the
no-PSRAM Cardputer ADV *reliable at scale* — 91 back-to-back TLS handshakes in a single
session with zero failures — and, as a direct consequence, let us delete every
reboot-between-batches workaround that had accreted around the upload paths.

It is the sequel to `UPLOAD_AND_AUDIO_TLS_POSTMORTEM.md` (which fixed three earlier,
distinct causes of the same symptom in 0.9.42) and `CLOUDLOG_UPLOAD_POSTMORTEM.md`. Read
those first for the earlier history. This document covers what came after: the discovery
that the *residual* post-first-download failure had a fourth, different mechanism, the
decision to change TLS stacks rather than keep fighting the allocator, and the cleanup
that removed the scaffolding all of these theories had left behind.

The single most important lesson, stated once up front because it is the same lesson as
every prior TLS postmortem and it was learned the hard way *again* this time:

> **The ground truth is the physical device, not log inference.** Every wrong turn in
> this arc — and there were six root-cause theories before the correct one — came from
> reasoning about what *should* be happening in the allocator. Every step forward came
> from a serial line that reported what the device was *actually* doing. "It compiles /
> the log looks right / this should work" was wrong often enough this session that it is
> worth treating as a red flag, not a conclusion.

---

## The shared symptom

After the 0.9.42 fixes, one failure pattern remained: **the first HTTPS download of a
session would succeed, and a later one — often the very next — would fail** with

- `code=-1` ("connection refused" as surfaced to the app), or
- `MBEDTLS_ERR_SSL_ALLOC_FAILED` / `SSL - Memory allocation failed`

…while, as in every previous instalment of this saga:

- WiFi stayed associated (IP held, RSSI normal),
- `ESP.getFreeHeap()` looked healthy, and
- the *largest free block* we printed read a steady **31732 bytes**.

That last number is the whole story, and it is why this was hard. The error string says
"out of memory." The heap says "plenty free." Both are true, and both are misleading,
because **the number that mattered was a number we were not printing.**

---

## What made it hard

Three compounding traps, each of which sent us down a wrong path at least once:

1. **The error string lies about the cause.** `SSL_ALLOC_FAILED` reads as "the heap is
   exhausted." The heap was not exhausted. mbedTLS needs a single *large contiguous*
   block for its handshake, and the failure is about *contiguity/fragmentation*, not
   *total free bytes*. Free heap can be 60 KB and the call still fails if no single
   block is big enough.

2. **The largest-block log looked stable at 31732 — a trap, not a comfort.** That
   number was steady precisely because the thing pinning it was a *resident, unchanging*
   allocation (see root cause). A steady healthy-looking number invited the conclusion
   "memory is fine, the problem is elsewhere (sockets, TIME_WAIT, the server, the TLS
   session cache…)." Every one of those elsewheres was investigated. None was the cause.

3. **The symptom is intermittent and stateful.** "First download works, second fails"
   invites socket-lifecycle theories (TIME_WAIT accumulation, PCB exhaustion, a wedged
   LWIP pool). Those are *real phenomena* and they *pattern-match perfectly*, which is
   exactly why they cost the most time. They were all disproven by instrumentation.

---

## Hypotheses tested and DISPROVEN

In roughly the order we chased them. Each was killed by a device measurement, not by
argument.

1. **TIME_WAIT socket accumulation / LWIP PCB exhaustion.** Theory: each TLS session
   leaves a socket in TIME_WAIT; after a few, the pool wedges and `connect()` returns
   -1. *Disproven:* we instrumented the actual PCB counts and the socket table. The pool
   was not exhausted at the point of failure. (This theory had already spawned the 90 s
   "network cooling down" lockout and `SO_LINGER`/`armLinger` code — all of which turned
   out to be guarding against a non-problem. See "The cleanup" below.)

2. **TLS session-cache / session-ticket memory growth.** Theory: mbedTLS retains
   per-session state that grows across handshakes. *Disproven:* disabling reuse changed
   nothing; the failure tracked the *block size*, not the session count.

3. **A leak in our own GET/POST paths.** Theory: we were failing to free a buffer
   between fetches. *Disproven:* the largest block *returned* to 31732 after each fetch —
   there was no monotonic downward leak. The block was being *transiently* consumed and
   released cleanly.

4. **Server-side / redirect behaviour.** Theory: one endpoint (AMSAT GP with its
   redirect hop) behaved differently. *Disproven:* the failure reproduced across
   unrelated endpoints (NOAA, hams.at, open-meteo) and was independent of which server
   went first.

5. **General heap fragmentation we could defragment.** Theory: if we could reclaim a
   larger contiguous block on demand, the handshake would fit. This one was *half right*
   about the mechanism and *wrong* about the remedy — we wrote a `reclaimHeapForTls()`
   probe and a `TLS_MIN_BLOCK` guard, and they confirmed the block genuinely could not be
   grown past 31732 while the display sprite was resident. That confirmation was the
   bridge to the real cause.

6. **"It's just barely too small; nudge the budget."** Theory: shave a kilobyte
   somewhere and mbedTLS will fit. *Disproven as a strategy:* the gap was structural, not
   a rounding error. Chasing kilobytes against a 32 KB contiguous requirement on a part
   with ~31.7 KB available was a losing game — which is what finally motivated changing
   the TLS stack instead of the budget.

---

## Root cause

**mbedTLS's TLS handshake needs a single contiguous heap block of roughly 32 KB. On this
board that block tops out at exactly 31732 bytes, because the resident 8bpp display
sprite (the `M5Canvas` we draw every frame into) pins the largest free region just below
what mbedTLS needs.**

Put concretely:

- The Cardputer ADV is an **ESP32-S3FN8 with no PSRAM**. All allocations compete for the
  same internal SRAM.
- We keep a **full-screen 8-bit canvas sprite resident** so the UI can repaint at any
  time (including *during* fetches, so the screen doesn't freeze). That sprite is a
  large, long-lived allocation.
- With the sprite resident, the largest contiguous free block is **31732 bytes** — a
  stable number, which is why the log looked calm.
- mbedTLS's handshake wants **~32 KB contiguous**. 31732 < 32768. It fails, every time
  the sprite is up, once the specific allocation pattern lines up — which is why the
  *first* fetch (fired early, before the heap settled into its resident shape) could
  sneak through and a *later* one could not.

The earlier 0.9.42 postmortem had already found and fixed *three other* ways this same
symptom appeared (audio/TLS DMA-pool contention, a second contention path, and a
TCP send-buffer ceiling). This was the fourth and most fundamental: **mbedTLS simply does
not fit alongside a resident full-screen sprite on a no-PSRAM S3, and no amount of
budget-shaving closes a ~1 KB structural gap against a 32 KB contiguous requirement.**

We proved this the only way that counts: by **instrumenting the actual largest-block
value at the moment of the failing allocation** (not the general free-heap number the
old logs printed), and by confirming with `reclaimHeapForTls()` that the block could not
be grown while the sprite lived.

---

## The fix: change the TLS stack, not the budget

Rather than free/recreate the sprite around every fetch (fragile, and it made the screen
freeze), or keep hunting kilobytes, we **migrated all five HTTPS paths off
`WiFiClientSecure` (mbedTLS) onto `ESP_SSLClient` (BearSSL, by Mobizt / Suwatchai
Kamonsantiroj).**

Why this works where budget-shaving could not: **BearSSL's largest single allocation is
its record buffer, not a monolithic ~32 KB handshake arena.** Sized deliberately, BearSSL
fits in the block we have:

- `client.setBufferSizes(SSL_RX_BUF, SSL_TX_BUF)` with **`SSL_RX_BUF = 16384`** and
  **`SSL_TX_BUF = 512`**.
- The 16 KB RX buffer is the critical figure: it must be able to hold a **full TLS
  record** (up to 16 KB) for servers that don't negotiate a smaller Maximum Fragment
  Length (MFLN). We learned this the hard way — a 4096-byte RX buffer made NOAA return an
  empty status line and truncated hams.at to 3827 bytes. 16384 is not optional.
- The 512-byte TX buffer is fine because **BearSSL fragments large writes** rather than
  needing the whole body buffered; on-device the uploads stream with `zeroWrites=0` and
  an effective per-write ceiling around 1024 bytes, and the TX buffer size does **not**
  cap upload size.
- Total BearSSL footprint (~23 KB across its buffers) fits comfortably in the block that
  mbedTLS's monolithic ~32 KB arena could not, and it fits *with the sprite resident* —
  so the screen never freezes and no free/recreate dance is needed.

### The GET paths had to be hand-rolled

`HTTPClient::begin()` on ESP32 core 3.2.1 requires a `NetworkClient&`. `ESP_SSLClient`
extends the generic `Client`, **not** `NetworkClient`, so it cannot be handed to
`HTTPClient`. Both GET paths (`httpsGet`, `httpsGetToFile`) are therefore **hand-rolled
over the raw `ESP_SSLClient`**: open the connection, send the GET request blob, parse the
status line, parse headers, then stream the body — mirroring the POST paths and the
library's own examples. Along the way the hand-rolled reader gained:

- **chunked-transfer-encoding decode** (AMSAT GP and open-meteo return chunked bodies),
- **one redirect hop** (AMSAT GP redirects),
- **`Content-Length` handling**, and
- `#include <limits.h>` for the `LONG_MAX` "read to EOF" sentinel.

### The five client sites

Each of the five HTTPS paths now uses the same construction:

```
WiFiClient   transport;
ESP_SSLClient client;
client.setClient(&transport);
client.setInsecure();
client.setBufferSizes(SSL_RX_BUF, SSL_TX_BUF);
```

The five: `httpsGet`, `httpsGetToFile`, `httpsPostMultipart` (LoTW),
`httpsPostJson` (Cloudlog), `httpsPostJsonFile`.

### Verified on-device

Every fetch completes the correct byte count: GP 70306, AMSAT (chunked) 13769, NOAA flux
22484, NOAA Kp 4643, open-meteo (chunked), hams.at 9734, QRZ. Both uploads succeed
(Cloudlog `201 Created`, LoTW `200 OK`). The largest block dips transiently to ~15860 /
~19444 mid-run under load, and **every fetch still succeeds**, because BearSSL's 16 KB RX
allocation fits even the dipped block — the exact margin the migration bought us, where
mbedTLS's 32 KB arena would have failed at 15860.

---

## The consequence: the reboots could die

The most valuable outcome wasn't just "downloads work." It was proving that **many TLS
handshakes can run in one session**, because a large body of code existed *solely* to
work around the belief that they could not.

### The 91/91 single-session proof

We added a temporary diagnostic (`TX_DIAG_SINGLE_SESSION`) that fetched **all 91
satellites' transponder data in one session, no reboots**, and counted successes. The
result, verified on the bench:

```
[tx-diag] single-session done: 91 ok, 0 failed, of 91
```

91 back-to-back TLS handshakes, zero failures, the largest block dipping and recovering
throughout. That single line invalidated every remaining reboot workaround.

### What was removed (0.9.43 cleanup)

- **Transponder "cache all" reboot-per-batch → single session.** The old design wrote a
  resume marker and `ESP.restart()`ed between small batches so each batch ran in a
  pristine boot. Now `doCacheAllTransponders()` fetches everything in one session.
  `resumeCacheIfPending()` remains only to *clear a stale on-disk marker* an older
  firmware may have left.

- **LoTW upload batch reboots → in-session iteration.** The old `continueLotwBatch()`
  cached the key passphrase in RTC RAM and `ESP.restart()`ed between batches. Removed
  entirely; batches now continue in one session. **Batching itself is kept** (each `.tq8`
  holds ≤ `LOTW_BATCH_QSOS` QSOs) purely to bound the RAM used building the file — not
  because of any per-session handshake limit.

- **Cloudlog upload batch reboots → in-session iteration.** Same treatment; Cloudlog had
  the identical reboot-per-batch pattern.

- **The 90-second "network cooling down" lockout → removed.** `netCooldownOk()` used to
  refuse a network action within `NET_COOLDOWN_MS` (90 s, explicitly a "~TIME_WAIT
  window") of the previous one. That gate existed to stop button-mashed uploads from
  "stacking TIME_WAIT PCBs and wedging the pool" — the disproven theory (1) above. It now
  always allows the action.

- **`SO_LINGER` / `armLinger`, the PCB-count probes, the INTERNAL-SRAM probes, and the
  TLS `lastError` diagnostic** — all instrumentation and workarounds from the disproven
  theories — were retired.

The remaining `ESP.restart()` calls are all legitimate and **user-confirmed**: factory
reset, the network-recovery reboot prompt (only after the socket-recovery path genuinely
exhausts its hard resets), and the manual "reboot to upload?" prompts for LoTW/Cloudlog
(only offered when a connect actually fails — rare now, and the user chooses).

---

## A self-inflicted sequel: the recursion stack overflow

Converting the upload batches from "reboot between batches" to "continue in-session" was
done, on the first pass, as **recursion**: a batch finished, then *called* the upload
function again for the next batch. This looked clean and passed every structural check.

It crashed on the bench:

```
Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception).
Debug exception reason: Stack canary watchpoint triggered (loopTask)
```

on the **third batch** of a 14-QSO LoTW resend (cap 6 → 3 batches). The tell was in the
log: the largest free block shrank batch-over-batch — **31732 → 26612 → 20468** — as each
recursive call stacked another full frame (the `PendingQso` array, the entire `.tq8`
signing call tree, and the whole BearSSL TLS POST chain) on top of the previous call that
had never returned. Three of those heavy frames overflowed the ~8 KB loopTask stack.

**The fix was to convert recursion to iteration.** Each batch now sets a `moreBatches`
flag and *returns* (fully unwinding its TLS+signing frame); a `while` loop at the single
top-level call site re-invokes the upload for the next batch **at constant stack depth**.
Applied to both LoTW and Cloudlog, with a hard batch cap as a safety net. The stack no
longer accumulates regardless of batch count.

The lesson here is narrow but sharp: **"a few frames deep is fine" is not an argument when
each frame is a full TLS + crypto call tree.** The reboots we removed had, as a side
effect, kept the stack shallow. Iteration restored that property without the reboots.

---

## The build dependency this introduced

Because the fix changes TLS stacks, **the firmware no longer builds without
`ESP_SSLClient` (by Mobizt)**, installed from the Arduino Library Manager. This is now a
first-class dependency alongside M5Cardputer, ArduinoJson (v7), TinyGPSPlus, RadioLib,
and the Hopperpop SGP4 library. See `docs/BUILD_AND_FLASH.md` and
`docs/guides/ARDUINO_SETUP.md`.

---

## Lessons for the next maintainer

1. **The ground truth is the device.** Print what the code is *doing*, not what it
   *should* do. The single largest-block figure at the moment of the failing allocation
   was worth more than every plausible theory combined. Six theories died to one
   instrumented number.

2. **Read the error, then distrust it.** `SSL_ALLOC_FAILED` means "no single block big
   enough," which is *not* the same as "heap exhausted." When an OOM error fires next to a
   healthy free-heap reading, suspect **contiguity/fragmentation**, and log the
   **largest block**, not the total.

3. **Log the pool that actually matters.** As the prior postmortem found, "largest block"
   in `MALLOC_CAP_8BIT` can look fine while a *different* capability pool is the
   constraint. Know which allocator your TLS stack draws from.

4. **When the gap is structural, change the structure.** A ~1 KB shortfall against a
   32 KB contiguous requirement is not a tuning problem. Changing the TLS stack (to one
   whose largest allocation is a *sizeable buffer* rather than a *monolithic arena*) was
   the right move, and it should have come sooner than six theories in.

5. **Size the BearSSL RX buffer to a full TLS record (16 KB) unless every server you talk
   to negotiates MFLN.** A too-small RX buffer fails *quietly* — empty status lines and
   truncated bodies, not a clean error. `SSL_RX_BUF = 16384`.

6. **Workarounds outlive the problems they were built for.** A large fraction of this
   cleanup was deleting code that guarded against theories long since disproven — the
   cooldown lockout, the linger dance, reboot-per-batch. When you fix a root cause, go
   *find and remove* the scaffolding the wrong theories left behind, or the next
   maintainer will assume it's load-bearing.

7. **Recursion whose frames are TLS+crypto call trees will overflow shallow embedded
   stacks fast.** Prefer iteration for anything that loops over network operations. "Only
   a few iterations" is not a safety argument when each iteration is thousands of bytes of
   stack.

8. **`balance.py` passing ≠ correct.** Structural checks (brace balance, signature parity)
   catch mechanical damage but are blind to duplicated-but-balanced code, reused case
   numbers, forward references, and the Arduino auto-prototype gotcha (a free function
   returning a sketch-defined struct needs that struct defined before the first function,
   or the injected prototype won't see it). Compile-testing a suspect fragment standalone
   with `g++` isolated real defects from cascade more than once this arc.
