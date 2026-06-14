# CardSat v0.9.14 — Release Notes

An offline-readiness release. **"Cache all transponders" now reliably caches the
full catalog** on a real Wi-Fi link by working *with* the ESP32's small socket
pool instead of fighting it: the run is split into small batches across automatic
reboots, each batch starting from a fresh network connection. No satellite is
skipped — a sat that fails its retries is re-attempted after the next reboot. The
**Update** action also runs the space-weather and weather fetches in sequence, and
the slow-first-response NOAA feeds get a longer connect window.

> **Hardware status.** Pass prediction, plots, GPS, the AOS alarm, deep sleep, and
> the offline caches are confirmed on hardware. The batched cache-all flow in this
> release is **confirmed on a Cardputer** (full 90-satellite catalog cached over
> several automatic reboots, including automatic recovery of a satellite that
> failed its in-boot retries). The radio and rotator paths remain host-tested only.

---

## The problem this fixes

Caching every satellite's transponders means ~90 sequential HTTPS requests to the
SatNOGS API. On the no-PSRAM ESP32-S3 the LWIP socket pool is small (~10–16
sockets), and after a few dozen TLS connections in one session the pool is
exhausted: further `connect()` calls return `-1` (connection refused) and the rest
of the run fails. Earlier attempts to fetch the whole transmitter table in one
request were ruled out — the table is ~3.4 MB (too large to stream reliably on a
weak link) and the SatNOGS endpoint supports neither HTTP `Range` requests nor
pagination on that view.

The fix sidesteps pool exhaustion entirely: **cache a small batch, reboot, repeat.**
Every reboot gives a pristine socket pool and a fresh Wi-Fi association (which also
resets the RSSI drift seen on long runs), so each batch runs comfortably under the
socket limit.

---

## What's new

- **Batched cache-all across reboots.** `a` on the Update screen now caches
  `TX_CACHE_BATCH` satellites (default **12**) per boot, persists progress to a
  marker file (`/CardSat/tx_resume.txt`), and reboots to continue. The run resumes
  automatically on the next boot and finishes on **"Cached all N transponders."**
  The first batch also runs in its own fresh boot, so it doesn't inherit a socket
  pool already partly spent by the GP/AMSAT/weather fetches.
- **No satellite is ever skipped.** If a satellite fails all of its retries (the
  pool is wedged for that boot), the batch stops at that satellite and the next
  boot re-attempts it from a clean pool, rather than advancing past it. A guard
  skips a genuinely unreachable satellite after two consecutive stalled boots so a
  run can never loop forever.
- **Resume lands on the Update screen.** Each resume boot returns to **Update**
  with a live progress count instead of the Home screen.
- **Update runs the full refresh chain.** One Update now fetches GP → AMSAT status
  → space weather (F10.7 flux + Kp/A) → local weather, in sequence.
- **Longer connect window for slow-first-response hosts.** NOAA SWPC
  (government-hosted, load-balanced, strict TLS) can be slow on the first
  response/handshake from a fresh client. The flux and Kp fetches now allow a
  longer connect/first-byte window (25 s) before timing out, while keeping the
  normal mid-stream stall timeout once data is flowing. All other fetches are
  unaffected.

---

## Removed

- The dead bulk-table code paths explored on the way to this fix were removed:
  the whole-table fetch URL/scratch file, the streaming JSON splitter
  (`splitBulkTransmitters` and helpers), and the HTTP `Range`/resumable-download
  functions (`httpsGetRange`, `httpsGetResumableToFile`). The per-sat fetch path
  is the one true cache path.

---

## Notes

- The whole cache-all pass takes a few minutes and several automatic reboots —
  this is expected. Let it run to completion before going offline.
- Satellites with no transmitters in SatNOGS are cached as an empty list (`[]`),
  which is correct and handled by the loader.
- To cancel a pending run, delete `/CardSat/tx_resume.txt` from the card.
- The `setBufferSizes()` / ESP32-core-version constraint described in earlier
  release notes no longer applies to cache-all: the batched approach succeeds
  regardless, by keeping each boot's connection count low.
