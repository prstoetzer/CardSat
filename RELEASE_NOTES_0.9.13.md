# CardSat v0.9.13 — Release Notes

A networking compatibility and reliability release. CardSat now **builds against
ESP32 Arduino core 3.3.x** (which replaced `WiFiClientSecure` with
`NetworkClientSecure`), the **HTTPS fetch path is hardened** against a known
core-3.x second-connection failure, and the **Update sequence is reordered** so
the small space-weather and weather feeds are fetched on a clean heap — fixing the
solar-flux fetch that failed only when run after a GP download.

> **Hardware status.** Pass prediction, plots, GPS, the AOS alarm, deep sleep, and
> the offline caches are confirmed on hardware. The networking changes in this
> release are **host-compiled and exercised on a Cardputer** for the Update and
> Space Wx flows (see Fixes), but the radio and rotator paths remain host-tested
> only.

---

## Fixes

- **Builds on ESP32 core 3.3.x.** The newer core aliases `WiFiClientSecure` to
  `NetworkClientSecure`, which no longer exposes `setBufferSizes()`. Earlier builds
  called it to shrink the TLS record buffers and save heap on the no-PSRAM
  ESP32-S3; that call no longer compiles. It has been removed from the HTTPS paths.
  The mbedTLS record buffer sizes are now fixed at core-build time
  (`MBEDTLS_SSL_IN/OUT_CONTENT_LEN`) and are not settable from the sketch.

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

- **HTTPS second-connection hardening.** Under core 3.x, `NetworkClientSecure` can
  leave a TLS/socket resource half-released on teardown, so a following HTTPS
  request in the same session could fail instantly with a `start_ssl_client: -1`
  ("connection refused") that is *not* a heap problem. Both HTTPS paths now force an
  explicit client `stop()` on every exit (via a small RAII guard) and disable
  HTTP keep-alive reuse (`setReuse(false)`), so each request starts from a clean
  socket.

---

## Internals

- `net.cpp` / `CardSat.ino`: removed the `client.setBufferSizes(8192, 2048)` calls
  in `httpsGet` and `httpsGetToFile` (four call sites across both representations),
  with an explanatory note so they are not re-added against core 3.x.
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
