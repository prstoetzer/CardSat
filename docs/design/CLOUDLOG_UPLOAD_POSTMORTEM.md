# Postmortem: Cloudlog upload "connection refused" (-1)

**Status: resolved (v0.9.37).** This documents a hard-to-diagnose failure where Cloudlog
/ Wavelog uploads failed with `code=-1 (connection refused)` while every other TLS
operation on the device worked. The root cause was **not** networking — it was the way
the Cloudlog request body was built on the heap. This writeup exists so the dead ends
aren't re-walked and the (counter-intuitive) lesson is preserved.

---

## Symptom

- `POST(json) ... -> https://n8hm.cloudlog.co.uk/index.php/api/qso` returned
  `code=-1 (connection refused)`, `response (0 bytes)`.
- **Everything else TLS worked**: the GP-bulletin fetch, AMSAT status, two NOAA space-wx
  feeds, open-meteo weather, hams.at activations, and — critically — the **LoTW upload**
  (also an HTTPS POST) all succeeded in the same session, on the same WiFi, at the same
  heap level.
- A Cloudlog upload **immediately after a fresh boot succeeded**. The failure only
  appeared after the device had done other network activity (e.g. a GP update).

## What made it hard

On the ESP32 Arduino stack, `HTTPClient` collapses **two completely different failures**
into the same `-1`:
- a TCP connection refused (RST), and
- a TLS handshake that couldn't complete (often an allocation failure inside mbedTLS).

So the error string actively pointed away from the real cause. The investigation spent a
long time treating it as a connection-layer problem.

## Hypotheses that were tested and DISPROVEN

Each was killed by direct evidence (serial logs and/or host tests), in roughly this order:

1. **URL scheme wrong / silent http→port-80 fallback.** Disproven: the serial log showed
   the URL was correctly `https://`.
2. **Server is TLS-1.3-only / cipher mismatch.** Disproven from macOS: the server serves
   TLS 1.2, negotiates ESP-friendly `ECDHE-RSA-AES128-GCM-SHA256`, and returns a correct
   `401 {"reason":"missing api key"}`. The server was healthy and ESP-compatible.
3. **Marginal contiguous heap at the gate.** Disproven: the upload succeeded on a fresh
   boot at the *same* `largest block` value (31732) it failed at later.
4. **Heap size / total free.** Disproven: six TLS GETs succeeded at the same heap, then
   the POST failed. If the heap were too small the first GET would have failed.
5. **TIME_WAIT socket accumulation.** Disproven: a 120-second idle (longer than TIME_WAIT)
   before the POST did not help.
6. **LWIP TCP-PCB pool exhaustion.** Disproven: a full `hardResetWifi()` (drop + reconnect
   + pool flush) fired and the **next POST still failed**.
7. **fd / socket wedge needing a hard reset.** Disproven by the same `hardResetWifi()`
   evidence: a full WiFi teardown didn't clear it.
8. **GP-parse heap aftermath degrading all later TLS.** Disproven: the **LoTW** POST
   succeeded after the identical GP burst. It wasn't "any POST after GP."
9. **The web-server (webd) listener holding a socket.** Disproven: with the web interface
   disabled, the failure was identical.
10. **"Number of prior distinct hosts" exhausting the DNS resolver.** Disproven: the
    *same* one-GET-then-Cloudlog sequence both passed (early in a session) and failed
    (later) — so it wasn't deterministic by host count.

The instrumentation that finally mattered: logging the **full** heap shape
(`free / largest / min_free_ever / free_blocks / alloc_blocks`) right before the connect,
rather than just the largest block. See `logHeapDetail()` in `net.cpp`.

## Root cause

The Cloudlog request body was assembled with **chained `String` operators plus a
`jsonEscape()` that returned temporaries**:

```cpp
String body = "{\"key\":\"" + jsonEscape(cfg.clKey) +
              "\",\"station_profile_id\":\"" + jsonEscape(cfg.clStation) +
              "\",\"type\":\"adif\",\"string\":\"" + jsonEscape(adif) + "\"}";
```

Each `+` and each `jsonEscape()` return is a separate allocate-copy-free cycle. On the
no-PSRAM ESP32-S3, that cascade **churns the heap free-list** in the instant right before
the TLS handshake. mbedTLS then needs its own sequence of allocations to complete the
handshake; against a freshly-churned free-list, that sequence can't be satisfied even
though the coarse metrics (`largest block`, total `free`) still look healthy. Result:
`connect()` returns false → `-1`.

Why the asymmetry that misled everyone:

- **LoTW upload builds its body in a single `malloc(bodyLen)`** (one clean contiguous
  block, see `httpsPostMultipart` in `net.cpp`). It never churned the free-list, so it
  never hit this — which is exactly why LoTW succeeded where Cloudlog failed.
- **Fresh boot worked** because the free-list was pristine and absorbed the `String`
  churn.
- **A `hardResetWifi()` didn't help** because it frees network allocations but doesn't
  restore the app-heap free-list structure.
- **Only a reboot reliably worked** because it resets the heap to pristine.

The single most diagnostic number was `min_free_ever` (the heap low-water mark): failures
correlated with it having dropped low earlier in the session, successes with it staying
high.

## The fix (v0.9.37)

Build the Cloudlog body the way the LoTW path already did — **one pre-sized buffer,
append-only, escaping done inline**, with no `String`-operator cascade and no second
`jsonEscape()` copy:

```cpp
String body;
body.reserve(adif.length() + adif.length()/8 + 96);
body += "{\"key\":\"";
jsonEscapeAppend(body, cfg.clKey);          // escapes directly into `body`
body += "\",\"station_profile_id\":\"";
jsonEscapeAppend(body, cfg.clStation);
body += "\",\"type\":\"adif\",\"string\":\"";
jsonEscapeAppend(body, adif.c_str());
body += "\"}";
```

`jsonEscapeAppend()` (in `app.cpp`) is the in-place version of `jsonEscape()` — identical
escaping, but it writes straight into the caller's buffer. The produced JSON is
**byte-for-byte identical** to the old version (host-verified across normal records,
embedded quotes/backslashes, multiple records, and control characters); only the heap
allocation pattern changed. The `adif.reserve()` was also right-sized (per-record) instead
of a flat 8 KB.

**Confirmed on hardware**: the exact GP-update-then-Cloudlog sequence that had failed every
prior attempt now returns `{"status":"created","imported_count":1}`, and the heap at the
POST is measurably cleaner (more free, fewer transient allocations).

## Secondary fix shipped alongside (separate issue)

The AMSAT status parse (`SatDb::applyAmsatStatusFile`) previously loaded the entire
`summary.php` array into one `JsonDocument`, spiking free heap to a few KB (down to ~530
bytes on a double-GP run). That near-exhaustion also churns the free-list and would
degrade long sessions over time. It was rewritten to **parse one array object at a time**
(a small brace/string state machine that extracts each `{...}`, deserializes just that
object, applies it, and discards it), keeping peak heap to a few hundred bytes. This is
independent of the Cloudlog body fix — it addresses session longevity, not the connect
failure — and was identified because the heap instrumentation showed the spike.

## Belt-and-suspenders

The Cloudlog screen also gained an **`R` = reboot-and-upload** action (writes a one-shot
marker, reboots, uploads in a pristine boot, returns to the screen). With the body fix in
place this is no longer load-bearing, but it remains as a resilient fallback for any future
heap-state edge case.

## Lessons for the next maintainer

- On a no-PSRAM ESP32, **how you build a buffer matters as much as how big it is.** Prefer
  a single pre-sized allocation (or one `malloc`) for anything assembled right before a TLS
  handshake; avoid `String` operator chains and functions that return large temporaries on
  that path.
- `HTTPClient`'s `-1` is ambiguous (TCP refuse vs TLS-alloc fail). Don't trust the
  "connection refused" wording — instrument the heap.
- `largest_free_block` alone is **blind** to free-list churn. Log `free_blocks` and
  `min_free_ever` too.
- When two near-identical code paths behave differently (LoTW vs Cloudlog here), **diff
  them line-by-line early** instead of theorizing about the environment. The decisive
  difference was `malloc` vs `String` concatenation, visible in a direct read of the two
  functions.
