# CardSat v0.9.12 — Release Notes

A performance and fixes release. Computing **workable DXCC** and **workable US
states** for a *future pass* is now roughly **12–15× faster** with identical
results; the **illumination and orbital-analysis screens now agree** on whether an
orbit is full-sun or eclipsed; the **Update screen** names the GP source that will
actually be downloaded; and **CelesTrak** GP queries use the correct request
format (with a migration for sources saved by older builds).

> **Hardware status — unchanged.** Pass prediction, plots, GPS, the AOS alarm,
> deep sleep, and the offline caches are confirmed on hardware. Radio, rotator,
> and network paths remain host-tested only.

---

## New in this release

- **QRZ callsign lookup (off the Home menu).** A new "QRZ Lookup" screen looks up a
  callsign in the **QRZ.com** database over its XML data service and shows the
  operator's name, mailing address, country, grid square and licence class. It
  needs a **QRZ XML-data subscription**: enter your QRZ username and password in
  *Settings → Network / data* (password stored masked, like the other
  credentials). The screen handles the edge cases plainly — no WiFi says so, no
  credentials explains the subscription requirement, and QRZ errors (not found /
  bad password) are shown in the status line. The session key is cached and reused,
  with automatic re-login on expiry.
- **TCA and LOS pass sounds.** In addition to the AOS countdown beeps, the alarm
  now chirps at **TCA** (closest approach / peak elevation, a double mid-tone) and
  at **LOS** (a descending two-tone) so you can work a pass by ear. All three
  (AOS, TCA, LOS) are gated by the single AOS-alarm setting — turning alerts off
  silences every pass sound.
- **Space Weather screen (off the Home menu).** A new "Space Wx" entry shows the
  solar **10.7 cm flux** and the planetary **Kp index** (fetched from NOAA SWPC
  with GP updates, or on demand with `r`), each labelled (quiet/storm, low/high)
  and colour-coded, with a plain-language operating outlook for HF and satellite
  conditions and a data-freshness note.
- **Transponder browser (off Satellites, key `t`).** A new screen lists every
  transponder/beacon entry the on-device catalog holds for the selected satellite
  in a scrollable up/down/mode/tone layout — handy for checking a bird's
  frequencies without a radio connected.
- **Two new orbital-analysis pages.** The analysis screen gains a **Pass outlook**
  page — a 7-day planning summary (total passes, how many clear 30°, the longest
  pass, the mean gap between passes, and the single best upcoming pass with its
  elevation, date/time, countdown and duration) — and an **Orbit position** page
  (mean and true anomaly, argument of latitude, time to the next perigee and
  apogee, argument of perigee, RAAN, current revolution number, and element age).
  The outlook is computed once per recompute; the position page is live. Page
  order is now Info / Live / Next pass / Ground track / Doppler / Nodal / Sun-Beta
  / Pass outlook / Orbit position.
- **Eclipse depth (orbital analysis).** Bringing CardSat in line with PREDICT,
  the orbital-analysis screen now reports **eclipse depth** — the angle (degrees)
  by which the satellite sits inside Earth's umbral shadow cone, computed as
  Earth's angular radius seen from the satellite minus the satellite's angular
  distance from the anti-solar axis. Positive means eclipsed (deeper = more
  positive, peaking at Earth's angular radius when dead-centre in shadow);
  negative is a sunlight clearance margin. It appears live on the **Live** page
  beside the sunlit/eclipse flag, and as a **peak eclipse depth** on the **Next
  pass** page when the bird transits shadow during the pass. The value reuses the
  same Sun/shadow geometry as the existing sunlit test, so it stays consistent
  with the illumination and Sun/Beta screens.

## Fixes

- **Space Wx now returns Kp (and adds the A index).** The Kp fetch was coupled to
  the solar-flux fetch — if the flux request hiccuped, Kp was silently skipped. The
  geomagnetic fetch is now independent, and the screen also shows the **running A
  index** when the NOAA feed provides it. The screen header now reads "Space Wx".
- **Orbital-analysis page headers corrected.** The page counter showed "/7" and the
  two newest pages (Pass outlook, Orbit position) read past the end of the page-name
  list, so they showed a wrong count and a missing/garbled name. The counter now
  reads "/9" and all nine pages are named (compactly, so the counter survives on
  long satellite names).
- **Pass-outlook counts fixed (e.g. AO-7).** The 7-day outlook used a repeated
  single-pass scan that mis-behaved for high orbits — AO-7 hit the 200-iteration
  guard with almost no high passes. It now uses one `predictPasses` call over the
  whole window (the same proven path as the 10-day overview), giving correct pass
  counts and >30° tallies.
- **QRZ callsign field defaults to uppercase**, like the other callsign entry
  fields.

- **Update screen makes clear it refreshes more than GP.** Pressing `k` on the
  Update screen has long fetched the AMSAT activity marks and (more recently) the
  space-weather data alongside the GP elements; the screen now says so, with the
  GP line reading "update GP (source)" and a note that the same action also
  refreshes AMSAT status and space weather. No behaviour change — just an honest
  label.

- **10-day overview now fills every day.** The chart cached at most 64 passes,
  but a busy LEO can have 70–100+ passes over ten days, so the buffer filled
  before the last day-rows and they appeared empty. The cache is raised to 128
  passes (~3 KB), enough for ~12 passes/day across the full window. *(The fetch is
  still bounded by the 10-day time horizon, so it stops as soon as the window is
  covered.)*
- **Illumination and orbital-analysis agree on full-sun vs eclipse.** The
  Sun/Beta page decided "full-sun orbit" vs "eclipsed each rev" from an analytic
  beta-vs-beta* threshold using mean altitude, while the illumination screen used
  a direct geometric shadow test sampled over the orbit. Near the threshold the
  two could disagree. The Sun/Beta page now derives its verdict and eclipse
  percentage from the **same per-orbit geometric sampling** the illumination
  screen uses, so they're consistent; beta and beta* are still shown as context.
  The orbital-analysis Info page's pass-scoped eclipse row is relabelled
  "Ecl (pass)" to make clear it refers to shadow transit during the current pass,
  not the per-orbit eclipse state.
- **Update screen now names the actual GP source.** The Update screen always read
  "download GP (AMSAT)" even when a CelesTrak category or a custom URL was
  selected. It now shows the configured source — e.g. "download GP (CT:amateur)"
  or "(Custom)" — using the same label logic as the Settings screen.
- **CelesTrak GP downloads.** CelesTrak queries were built with a lowercase
  `FORMAT=json-pretty` token; the documented token is uppercase, and CelesTrak's
  edge can reject or redirect malformed/legacy requests (and will temporarily
  firewall an IP that produces repeated errors — which surfaces as a
  *connection refused*). New CelesTrak sources now use the documented
  `FORMAT=JSON` (compact, valid, smaller), and a URL saved by an older build is
  **migrated automatically** to the correct token on the next download. If a
  CelesTrak fetch still fails with a refused/blocked-style error, the on-screen
  message now suggests trying AMSAT or waiting, since the IP may be temporarily
  blocked. *(Note: the underlying HTTPS fetch path is unchanged and remains
  host-tested only; this corrects the request the device sends.)*

---

## Performance

- **Workable DXCC / states: per-pass build ~12–15× faster.** The footprint walk
  now (1) skips any entity already found, doing zero work for it instead of
  re-walking its polygon, and (2) rejects each mesh point against a per-entity
  **bounding box** with four integer comparisons before ever running the
  point-in-polygon ray-cast — so the ~99% of entities nowhere near a given mesh
  point cost almost nothing. Point entities also get a cheap latitude-band
  pre-reject before the exact great-circle distance test.
- The optimization is **exactly equivalent** to the previous algorithm — verified
  identical results across hundreds of randomized footprints — so the lists you
  get are unchanged; they just appear far sooner. On a future MEO pass the build
  drops from several seconds to a fraction of a second on the device.

---

## Internals

- Added per-polygon index tables (`*_START`, `*_LOMIN/LOMAX/LAMIN/LAMAX`) in
  flash for both `DXCCPOLY` and `STATEPOLY`, derived from the polygon data
  (~2.1 KB total, no extra RAM).
- `addFootprintDxcc` / `addFootprintStates` rewritten to iterate entities with
  bounding-box rejection and skip-if-found; new single-polygon helpers
  `dxccPipAt` / `statePipAt` (test by start offset, no array advance). The old
  walk-and-advance `dxccPolyTest` / `statePolyTest` are removed (no longer used).
- Point-entity loops gain a conservative latitude pre-reject; the exact haversine
  distance is retained for the final decision (no accuracy change).
- New `App::gpSourceLabel()` helper (used by both the Update and Settings
  screens); CelesTrak URL builder uses `FORMAT=JSON`; `doUpdateGp()` migrates a
  stale `FORMAT=json-pretty` CelesTrak URL and adds a block-aware error hint.
- `FW_VERSION` → **0.9.12**.

---
