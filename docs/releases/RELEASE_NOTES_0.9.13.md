# CardSat v0.9.13 — Release Notes

A networking reliability release. The **Update sequence is reordered** so the
small space-weather and weather feeds are fetched on a clean heap, slow feeds get
a **longer read timeout**, and **truncated downloads are detected and retried**
instead of being silently accepted.

> **Hardware status.** Pass prediction, plots, GPS, the AOS alarm, deep sleep, and
> the offline caches are confirmed on hardware. The networking changes in this
> release are **host-compiled and exercised on a Cardputer** for the Update and
> Space Wx flows (see Fixes), but the radio and rotator paths remain host-tested
> only.

> **ESP32 core requirement.** CardSat shrinks the per-connection TLS record
> buffers via `WiFiClientSecure::setBufferSizes(8192, 2048)` — without it, the
> full 16 KB buffers fragment the no-PSRAM ESP32-S3 heap and repeated HTTPS
> connects fail with `start_ssl_client: -1` (e.g. caching all transponders dies
> partway, and the failure poisons the whole WiFi stack until reboot). Build with
> an esp32 core that still exposes `setBufferSizes()` — the **M5Stack board
> package 3.2.6** (or upstream esp32 **2.0.x**). Upstream core **3.3.x removed the
> method**; do not build against it.

---

## Fixes

- **Solar-flux fetch from the Update screen.** Running an Update first loaded the
  ~220-satellite GP catalog into RAM and *then* fetched the NOAA space-weather
  feeds. The freshly populated catalog legitimately fills and fragments the heap,
  and a subsequent TLS handshake for the small flux/weather JSON could fail to find
  a contiguous block — so the flux fetch failed **only from the Update screen**,
  while the same fetch from the Space Wx screen (clean heap) always worked. The
  Update sequence now fetches **space weather and terrestrial weather before the GP
  download**, on the uncluttered heap. Neither feed depends on GP data, so the
  order change is transparent — and as a side benefit, both now refresh even when
  the GP download itself fails (previously they were skipped on a GP failure).
  AMSAT status is still fetched after the catalog loads, since it tags the loaded
  satellites.

- **Per-call TLS client.** Each HTTPS request uses a fresh `WiFiClientSecure` with
  an explicit `stop()` on every exit path (via a small RAII guard) and HTTP
  keep-alive disabled (`setReuse(false)`), so a failed or timed-out connection is
  discarded rather than reused — one failure never poisons the next request.

- **Longer read timeout for slow feeds.** The NOAA solar-flux feed
  (`f107_cm_flux.json`, ~22 KB) is sometimes slow to begin responding and a 15 s
  read window timed out (`-11`) even on a healthy link. The file-download path now
  uses a 30 s read timeout (connect timeout stays 15 s, since connecting is fast),
  so the slow feed completes instead of failing all three retries.

- **Truncated-download detection.** On a weak link the HTTPS read loop could exit
  early on a transient mid-stream lull and silently accept a partial body — a
  declared-70 KB GP file arriving as 23 KB, parsing to 31 satellites instead of 90,
  with no error. The loop no longer terminates on a transient disconnect while a
  declared Content-Length is still unmet (it waits, bounded by a 20 s hard stall
  timeout), and a body shorter than its declared length is now reported as a
  failure so the retry logic runs instead of accepting the truncated file. Applies
  to both the in-RAM and stream-to-file download paths.

---

## Internals

- `net.cpp` / `CardSat.ino`: `httpsGet` and `httpsGetToFile` set
  `client.setBufferSizes(8192, 2048)` to shrink the TLS record buffers (~14 KB
  heap saved per connection on the no-PSRAM S3). This requires an esp32 core that
  still exposes the method — see the core requirement note above.
- Added an RAII `ClientStop` guard and `http.setReuse(false)` to both HTTPS
  functions; added `#include <esp_heap_caps.h>` and a pre-TLS log of the largest
  contiguous free block (`heap_caps_get_largest_free_block`) alongside total free,
  as a diagnostic. The handshake is no longer pre-emptively aborted on a heap
  threshold — the connection attempt itself is the judge — so marginal-but-workable
  heap states are no longer turned into failures.
- `app.cpp` / `CardSat.ino`: in `doUpdateGp()`, `fetchSpaceWeather()` and
  `fetchWeather()` now run immediately after WiFi connect / NTP sync, before the GP
  download; they no longer run after the catalog parse. `fetchAmsatStatus()` is
  unchanged (still after the parse).
- `FW_VERSION` → **0.9.13**.

> *Known minor:* the full `f107_cm_flux.json` feed (~22 KB) is occasionally slow to
> respond and a single fetch can hit the 15 s read timeout; the existing 3-attempt
> retry normally rides this out. Not changed in this release.

---

## Installing

Two binaries are attached to this release:

- **`CardSat.bin`** — for **[Launcher](https://github.com/bmorcelli/Launcher)**
  (bmorcelli). Copy it to Launcher's bin folder on the microSD (or use Launcher's
  WebUI/OTA), then start Launcher and select **CardSat**; Launcher writes the
  partition table and app for you. This is an **app-only** image with no standalone
  bootloader or partition table, so it works **only** through Launcher — it cannot
  be flashed on its own.
- **`CardSat_Merged.bin`** — a **complete standalone image** (bootloader +
  partition table + app + empty LittleFS) for **M5Burner** or a **direct flash** at
  offset `0x0`. In M5Burner, add it as a custom firmware and burn. To flash
  directly:

  ```
  esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
    write_flash 0x0 CardSat_Merged.bin
  ```

  or the web flasher at <https://espressif.github.io/esptool-js/> (chip
  **ESP32-S3**, file at **`0x0`**).

The merged image ships with an **empty** filesystem — run **Update** once on first
boot to download GP elements. Building from source (Arduino IDE or PlatformIO) is
unchanged; see the manual (§5) or README.

---
