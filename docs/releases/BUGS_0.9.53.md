# CardSat 0.9.53 — heap reduction + upload-reliability investigation

Status: **DONE** — on-device confirmed (see the heap log excerpt at the end).

## The problem

LoTW upload sessions (QSOs uploaded in batches, one TLS connection per batch) failed
partway through: the first batch succeeded, a later batch stalled mid-body
(`streamed 3072/5094 ... zeroWrites=1984`) and never recovered. Total free heap was fine
(~40 KB); the failure tracked the **largest contiguous block**, which had eroded as
0.9.51/0.9.52 added static state. On a no-PSRAM S3, a TLS handshake + the LWIP send path
need contiguous room the fragmented heap couldn't provide.

## Approach — measure before cutting

Rather than guess, we instrumented first: a heap reading **after each POST** (client stopped)
and a one-shot reading **at the first send stall**. That distinguished the two candidate
mechanisms:

1. a per-connection *ratchet* (largest block steps down and never recovers) → would justify a
   persistent/reused TLS client; or
2. transient *contiguity pressure* (block dips during the upload, recovers after) → fixed by
   freeing resident memory, no client refactor needed.

An earlier mbedTLS-era postmortem had ruled out session-reuse as the cause, but that was under
mbedTLS; the current transport is **BearSSL (ESP_SSLClient)**, so the question was genuinely
open for this stack. The instrumentation answered it with data instead of assumption.

## What we changed (heap reductions)

- **Display sprite 8bpp → 4bpp** (`CANVAS_DEPTH`, app.cpp setup). The canvas draws entirely
  through a 13-entry palette (indices 0–12; verified zero raw-565 draw calls, so 4bpp is
  colour-identical). Frees ~16 KB and raises the contiguous-block ceiling. Reversible in one
  line. The BMP screenshot path uses `readRectRGB()` (depth-agnostic) so it's unaffected.
- **`VIS_DAY_MAX` (32)** — the one-day scratch buffer in `buildVisList` was sized to
  `VIS_PASS_MAX` (128) but only ever holds one day's passes. ~3.5 KB, zero behaviour change.
- **On-demand audio** (0.9.52) already removed the ~8 KB speaker buffer from steady-state
  residency.

Deliberately **not** done: `ROVE_LIST_MAX` trim (cap is applied before the newest-first sort,
so halving it could hide recent plans); conditional LoRa buffers (a conditional malloc is the
fragmentation anti-pattern); `SSL_RX_BUF` shrink (4096 was already proven on-device to break
NOAA/hams.at — the 16 KB RX buffer must hold a full TLS record); persistent TLS client (the
data below shows no ratchet to fix).

## On-device result (the evidence)

```
signing 6 QSOs ... largest 31732
after POST: ... largest 26612 (posted=1)     1st upload
signing 6 QSOs ... largest 31732             <- RECOVERED
after POST: ... largest 26612 (posted=1)     2nd upload
signing 2 QSOs ... largest 31732             <- RECOVERED
after POST: ... largest 26612 (posted=1)     3rd upload
resend batch ... sent=14/14, remaining=0
```

- Boot/idle heap up from ~40 KB to ~70 KB free; largest block a rock-solid **31732** before
  every upload (no ratchet — mechanism #2 confirmed).
- The block dips to 26612 *during* each POST (the resident 16 KB BearSSL RX buffer) and
  **fully recovers** on `stop()`.
- All three LoTW batches accepted, **`zeroWrites=0`** on every one. The send stall is gone.

Conclusion: it was transient contiguity pressure, not a per-connection leak. The reclaim work
(4bpp sprite doing most of it) was the correct fix; the persistent-client refactor is
unnecessary. The instrumentation earned the certainty to stop here.

## Also in 0.9.53

- **Multi-file download** on the web Files page: client-side checkboxes + "Download selected",
  sequenced in the browser; the device still streams one file per request (no server-side
  heap cost). `WEB_FILES_PAGE` (PROGMEM) only; `webdSendFile` unchanged.

## Docs

Release notes; MANUAL Files section updated for multi-select; README callout + FEATURES note;
regenerated `CardSat_Manual.pdf` and `CardSat_CheatCard_4x6.pdf`. Documentation audit run: the
mbedTLS references in current docs were verified **correct** (LoTW *signing* genuinely uses
mbedTLS crypto — `mbedtls/pk.h`, `sha1.h` — even though the TLS *transport* is BearSSL), so
they were left as-is. Design-history docs/postmortems left unchanged (archival record).

**Gate:** balance 0, parity green, all changed/new bodies mirror-identical src<->ino.
Instrumentation lines present in both files.
