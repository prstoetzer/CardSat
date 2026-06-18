# CardSat v0.9.18 — Release Notes

Refinements to the large-font readout, a matching large-font Manual view, wider
tilt-tuning coverage, an optional second WiFi network for field use, and tidier
frequency formatting on every band.

> **Hardware status.** All of this is host-verified (tokenizer-balanced, the
> frequency formatter and tilt logic checked off-device) but has not been run on a
> device this release. The large-font layout fit and the tilt feel still want
> confirmation on a real Cardputer ADV.

---

## Large-font readout: bigger numbers, follows the tune mode, full tuning

- The **AOS/LOS countdown was removed** from the large-font view and the **RX/TX
  frequencies enlarged** to use the freed space — they're now the dominant element,
  readable at arm's length. (The countdowns remain on the regular Track and Passes
  screens.)
- The big view now **follows the Doppler tuning option** selected on Track: the
  bottom line shows the active mode (**FULL / DL / UL / TUNE / CAL**) and, on a
  linear bird, the live passband position.
- **Passband tuning works from the big view.** All the in-place Track controls are
  available without leaving it — `,`/`/` tune, `s`/`x` step/recenter, `m` TUNE/CAL,
  `d` cycle mode, `t` next transponder, `r`/`o` radio/rotator, `y` tilt, `l` log.
  This is done by delegating to the Track key handler, so behaviour (including the
  FULL/DL knob-driven guard) is identical and lives in one place.

## Large-font Manual calculator (`z` from Manual)

The no-radio **Manual** frequency calculator gains its own large-font view, opened
with **`z`**. It shows the **HOLD** and **TUNE** legs in big digits with the fixed
and derived legs labelled, using the same round-trip Doppler maths as the normal
Manual screen. The in-place keys (`u` swap leg, `m` CAL, `,`/`/` tune, `s`/`x`,
`t`) work there too; `z` or `` ` `` returns to the standard Manual view.

## Tilt tuning extended to Manual mode

Accelerometer (tilt) passband tuning — opt-in and ADV-only — now also works on the
**Manual** and **Manual large-font** screens, not just Track and the Track
large-font view. A **TLT**/**TILT** marker shows when it's armed. (On Track/Big the
FULL/DL modes are knob-driven so tilt stands aside; the Manual screen has no radio,
so it simply moves the passband.)

## Mobile web control page (opt-in)

CardSat can now serve a **mobile-friendly web page over the WiFi LAN** so a phone
or tablet can drive it without touching the keypad. Enable it under *Settings →
Network / data → Web control*; the row then shows the URL to open (e.g.
`http://192.168.1.42`). From the page you can:

- **select a satellite** from your favourites and start tracking it, or add/remove
  the chosen satellite from favourites with the **★** button beside the selector,
- **see upcoming pass times** (AOS, peak/closest-approach time, max elevation,
  duration, az) for the active satellite,
- **control the radio and rotator** — tune the passband, step the transponder,
  cycle tune mode, recenter, and toggle radio/rotator output.

A **Manual** toggle on the page switches the controls to the no-radio hand-tuning
calculator: it shows the **HOLD** leg (where to park your own radio) and the
**TUNE** leg (the Doppler-corrected frequency to follow), with the same round-trip
correction as the on-device Manual screen, plus a **Swap leg** button to choose
which leg you hold.

An **Orbit** toggle adds a read-only **orbital-analysis** view: altitude/footprint,
period, apogee/perigee, inclination/eccentricity, decay estimate, live range-rate
and sub-point, sunlit/eclipse state, beta angle and eclipse fraction, J2 node and
perigee drift (with a sun-sync flag), mean/true anomaly, time to perigee/apogee,
and the multi-day pass outlook — the same numbers the on-device orbital-analysis
pages show, surfaced as one screen. The underlying computation was kept in one
place (`buildOrbit()` gained a headless mode), so the web view and the device
never disagree.

It's built in the same cooperative, non-blocking style as the existing
rigctld/rotctld servers, and every control drives the **same key handlers** the
physical keypad uses, so the web UI never re-implements radio or rotator logic.
The page itself is a single self-contained file served from program memory, so it
adds no meaningful pressure to the no-PSRAM heap.

**Security note.** This is plain HTTP on the local network with **no
authentication** — anyone on the same WiFi can open it. It's **off by default**;
turn it on only on networks you trust, and it is not intended to be exposed to the
internet.

## Optional second WiFi network

Settings now has an **optional second WiFi network** (*WiFi 2 SSID / WiFi 2 pass*,
under Network / data). If the primary network can't be joined, CardSat
automatically falls back to the second one. This is aimed at field use: keep your
home router as the primary and a **phone hotspot** or portable travel router as the
second, and the device connects to whichever is in range. Leave it blank to keep
the previous single-network behaviour. The fallback applies everywhere CardSat
connects — boot, GP updates, and on-demand fetches.

## Tidier frequency readouts on any band

Frequency displays now **shed decimal places as the integer part grows**, so they
stay within the panel on any band rather than overflowing. Sub-GHz birds keep their
full precision (e.g. `145.99000`); higher bands automatically show fewer decimals
(`1296.500`, and so on up the range). This keeps both the normal and large-font
readouts tidy without truncating the part that matters.

## More readable menu selection

The highlighted (selected) row in menus and lists used a pure, fully-saturated
green bar, which glared and bloomed into the black text — a reader reported it as
hard to read. The selection bar is now a calmer medium forest green, keeping the
"green = selected" cue across the whole UI while making the black text comfortably
legible. The change applies everywhere a selection bar appears (home menu, sat
list, schedule, passes, log, settings, GP-source picker, WiFi scan, and mutual
visibility); the destructive "danger" rows stay red.

---

## Reliable downloads on a weak signal

Large downloads (the GP catalog above all) could be **silently truncated on a weak
WiFi link** — the file would stop part-way, cache as if complete, and only a
handful of satellites would parse. Two causes were fixed: the streaming reader no
longer mistakes a brief TLS burst gap (common at low signal) for the end of the
body when the server has declared a content length, and it now **verifies the full
declared size arrived** before accepting the file. A short read is treated as a
failure, and the GP fetch now **retries** (up to three attempts) instead of caching
a partial catalog. Small downloads were unaffected; this specifically rescues the
big ones on marginal links.

## Web control: UTC times and downloads while connected

Two fixes for the mobile web page:

- **Pass times now show in UTC** (with a `Z` suffix) instead of being converted to
  the phone or laptop's local timezone — matching how CardSat shows time everywhere
  else, which is what satellite operators expect.
- **Downloads no longer fail while web control is on.** Any internet fetch — keps
  (GP), weather, space weather, AMSAT status, transponders, or a QRZ lookup — while
  the web server was running could be refused ("connection refused"): the LAN
  listeners were holding sockets that the outbound HTTPS connection needed on the
  socket-limited, no-PSRAM ESP32-S3. The listeners are now briefly released for the
  duration of *any* download and rebuilt automatically afterward, so the browser
  simply reconnects on its next refresh. This is handled in one place (around every
  TLS session), so it covers all current and future fetches uniformly.

## Notes

- Host-verified only; confirm on hardware before relying on the new views during a
  pass — in particular the large-font layout and the tilt feel on a real ADV.
- Frequencies are stored as 32-bit Hz, so the practical ceiling is about 4294 MHz;
  the formatter is bounded well beyond the amateur satellite bands in use.
