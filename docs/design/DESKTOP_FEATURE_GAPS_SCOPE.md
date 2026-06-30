# Scope: Feature Gaps vs. Desktop Tracking Software

**Status: design scope only — not implemented.** This surveys the features found in
the widely-used desktop satellite programs — **SatPC32** (DK1TB, the de-facto AMSAT
standard), **Nova for Windows** (NLSA), **Gpredict** (OZ9AEC, open-source),
**Orbitron**, and the newer SDR-integrated **SkyRoof** (VE3NEA) — identifies what
CardSat does *not* currently do, and scopes each gap for feasibility **on the
Cardputer hardware as-is** versus **on a future port to a more capable platform**
(the M5Stack Tab5 / Cardputer-Zero-class targets already scoped in
`TAB5_PORT_SCOPE.md` and `CARDPUTER_ZERO_PORT_SCOPE.md`).

This is a *survey-and-scope* document, not a single-feature proposal. Each gap gets a
short feasibility verdict rather than a full design; the ones worth doing get their
own scope later.

---

## 1. Why this comparison

CardSat already covers the **core operating loop** as well as or better than most of
these programs: SGP4 propagation, full-Doppler "One True Rule" tuning, CAT for ten
radios across three families plus Icom LAN, rotator control across six backends
(GS-232, Easycomm, SPID, rotctld, PstRotator, direct Yaesu), pass prediction, a 3D
globe, OSCARLOCATOR, mutual/sat-to-sat windows, footprint grids/states/DXCC,
Sun/Moon/transits, sky sources, 60-day illumination, space/local weather, QRZ,
activations, logging with ADIF/LoTW/Cloudlog, voice memos, notes, and LoRa messaging
— all on a $90 handheld with no PSRAM.

What the desktop programs have that CardSat doesn't tends to fall into a few buckets:
**SDR/DSP-dependent features**, **award/contest workflow**, **multi-station and
external-integration plumbing**, and a handful of **display refinements**. The point
of this document is to be honest about which of those are realistic on an ESP32-S3
and which genuinely need the port.

---

## 2. What each program brings

A condensed feature survey, focused on things CardSat lacks (not an exhaustive list
of each program — and a reminder that CardSat already matches most of their core).

### SatPC32 (DK1TB)
- The reference AMSAT program; proceeds fund AMSAT. Drives a very long list of rotor
  interfaces and controllers.
- **DDE server interface** — other Windows programs (loggers, contest tools, the
  `TxController` client) read live tracking/frequency data from SatPC32 over DDE. This
  is the integration backbone a lot of satellite loggers assume.
- Automatic band-switching on Icom / TS-2000; a dedicated **ISS in-band** variant
  (`SatPC32ISS`) for same-band uplink/downlink.
- Sun terminator / eclipse indicator on the map; multi-satellite display; "Countdown"
  pop-up of upcoming passes.

### Nova for Windows (NLSA)
- **Unlimited observers tracked simultaneously**, with two-satellite **mutual
  visibility** between *different ground stations* (not just sat-to-sat from one site).
- **EME (moonbounce) planning graphs**: spatial polarity, declination, path
  degradation, sky temperature, Earth–Moon distance.
- Printed/exported reports (DOC/XLS/text) of future passes, mutual windows, eclipses;
  pass arcs plotted across the map with rise/set markers.
- **Mode schedule** per satellite (which transponder/mode is active when).

### Gpredict (OZ9AEC)
- **Configurable modules and NxM view layouts** — arbitrary grids of list/map/polar
  views, multiple modules open at once, full-screen.
- **"Sky at a glance"** timeline showing all upcoming passes as colored bars (hover for
  summary, click for detail).
- Multiple ground stations; 2000+ predefined locations; per-module satellite groups.
- Transponder data imported from the **SatNOGS DB**; multiple TLE sources; rich
  tooltips (Az/El/time-to-LOS) on map and polar.

### Orbitron
- Very large object counts (20k objects), fast; the long-time "visual observer" tool.
- Feeds many external programs as a tracking *source* (the DDE/driver model again).

### SkyRoof (VE3NEA) — the modern SDR-integrated entrant
- **SDR waterfall across the whole satellite segment** (435–438 MHz / 145.8–146 MHz),
  with satellite traces *labeled by name* and transponder edges that follow Doppler.
- **Built-in SSB/CW/FM receiver** with visual, mouse-driven Doppler tuning; audio/IQ
  streamed to external decoders for **telemetry / SSTV / weather-image** decode.
- Sky View / Earth View / **Timeline** displays; **voice announcements** of AOS/LOS and
  live az/el (aimed squarely at handheld-antenna ops); remote operation over TCP;
  logger plugins that write QSOs directly.

---

## 3. The gaps, bucketed

Cross-referencing the above against CardSat's actual source, the things CardSat does
**not** currently do:

| # | Gap | Seen in | Bucket |
|---|-----|---------|--------|
| A | Award progress (VUCC/WAS/DXCC roll-up from the log) | (implied by logger ecosystem) | Workflow |
| B | Contest scoring & **dupe checking** during a pass | N1MM-style loggers via DDE | Workflow |
| C | Day/night **terminator** + eclipse shading on the map | SatPC32, Nova | Display |
| D | **"Sky at a glance"** all-pass timeline | Gpredict | Display |
| E | Multiple ground stations / **inter-station** mutual windows | Nova, Gpredict | Compute |
| F | **EME planning** graphs | Nova | Compute |
| G | **External data interface** (DDE-equivalent: serial/UDP/HTTP push of az/el/Doppler) | SatPC32, Orbitron | Integration |
| H | Voice **AOS/LOS announcements** | SkyRoof, Nova | I/O |
| I | **SDR waterfall** + integrated receiver | SkyRoof | SDR/DSP |
| J | **Telemetry / SSTV / weather-image decode** | SkyRoof (+ external) | SDR/DSP |
| K | Configurable **multi-view layouts** | Gpredict, Nova | Display/UX |

The buckets matter more than the individual rows: **Workflow** and **Display** items
are mostly realistic on the Cardputer today; **Compute** items are feasible but cost
heap/time; **Integration/I-O** items are cheap and high-value; **SDR/DSP** items are
the ones that genuinely need a different platform.

---

## 4. Feasibility on the Cardputer (as-is)

### Realistic now — worth their own scope
- **(A) Award progress tracking.** CardSat already logs QSOs to `qso_log.csv` with
  grid/sat fields and already computes *workable* grids/states/DXCC live on the Track
  screen. The missing piece is the *retrospective* roll-up: scan the log, tally unique
  worked grids / states / DXCC entities (optionally per band/sat), and show
  progress. All data is already on the card; this is parsing + counting, no new
  hardware. **The single highest-value gap to close** — it turns CardSat's existing
  log + footprint machinery into an award dashboard. Heap cost is bounded if the tally
  streams the CSV rather than loading it. → candidate for `AWARD_TRACKING_SCOPE.md`.
- **(B) Dupe checking.** A lighter cousin of (A): when logging during a pass, flag
  "worked before on this sat" by scanning the log for the same call+sat (+band/day per
  the chosen rule). Cheap, genuinely useful for an active operator, and a natural Log
  screen addition. Full *contest scoring* (exchange validation, multipliers) is a
  bigger lift and arguably out of character for a handheld — dupe-checking is the 80/20.
- **(C) Day/night terminator.** CardSat already computes the subsolar point and
  per-sample sunlit/eclipse state for visual passes (`drawVisList`). Drawing the
  terminator as a shaded region on the world map is a display addition using math
  already present — no new data. Moderate pixel-pushing on a 240×135 panel; feasible.
- **(D) "Sky at a glance" timeline.** CardSat already predicts 10-day pass lists and
  draws the 10-day visibility chart; a horizontal all-favorites timeline of upcoming
  passes (bars colored by elevation, like the new "Overhead now" colors) is a
  display reuse of existing predictions. Screen real estate is the only real
  constraint. → could fold into the existing pass-overview screens.
- **(G) External data push.** CardSat already speaks rotctld/rigctl *as a client*; the
  inverse — a small server that *emits* live az/el/Doppler/sat over the existing WiFi
  stack (a UDP broadcast or a one-line HTTP/JSON endpoint, à la the GPredict rigctld
  AOS/LOS signalling) — lets loggers and other tools consume CardSat as a tracking
  source the way they consume SatPC32's DDE. Low heap, no new hardware, and it slots
  next to the already-scoped `WEB_CONTROL_SCOPE.md`. **High value for low cost.**
- **(H) Voice AOS/LOS announcements.** CardSat already has audio (beeps, the sked/AOS
  alarms) and voice memos; speaking "AO-91 AOS, azimuth 230" is within reach via short
  pre-recorded clips or simple TTS-lite tone/sample playback on the existing speaker.
  Especially apt given CardSat's new hand-aiming features (the point-here arrow). The
  constraint is sample storage in 8 MB flash, not compute. → candidate scope.

### Feasible but costly
- **(E) Multiple ground stations / inter-station mutual windows.** The propagator and
  the existing sat-to-sat window search already do the hard part; adding a *second
  observer* and computing when a satellite is mutually visible from both sites is a
  bounded extension. Cost is mostly **heap and per-tick time** (a second site's passes
  computed alongside the first) and UI for entering/storing the second location. The
  no-PSRAM free-list is the thing to watch, exactly as in the existing window search.
- **(F) EME planning.** The Moon position and distance are already computed
  (Sun/Moon/transits). Degenerate EME aids — Moon az/el, Doppler, path loss, simple
  sky-temperature estimate — are arithmetic and could be a Moon-screen extension.
  Full Nova-grade polarity/declination/degradation graphing is more than a handheld
  needs, but a useful subset is cheap.

### Display-bound
- **(K) Configurable multi-view layouts.** Gpredict's NxM grid is a desktop luxury;
  on 240×135 it doesn't translate. CardSat's per-screen model is the right answer for
  the form factor. Not a gap worth closing on the Cardputer — noted for the port.

---

## 5. Needs the port (Tab5 / more capable platform)

These are the gaps where the Cardputer's **lack of PSRAM, modest CPU, no real DSP/FPU
headroom for wideband work, and tiny screen** are the binding constraint — not
developer effort. They belong in the port scopes, not on the ESP32-S3.

- **(I) SDR waterfall + integrated receiver.** SkyRoof's defining feature oversamples
  ~3 MHz of spectrum and leans on **OpenGL and a real GPU/CPU**; it explicitly degrades
  to "no waterfall" on weak hardware. There is no path to a meaningful satellite-segment
  waterfall on a no-PSRAM ESP32-S3 with a 240×135 display. A port to a platform with
  PSRAM, a larger display, and an SDR front-end (or an SDR-over-network feed) is the
  only realistic route. → belongs in `TAB5_PORT_SCOPE.md` / a dedicated SDR port scope.
- **(J) Telemetry / SSTV / weather-image decode.** These need either an SDR front-end
  or wideband audio DSP and non-trivial buffers — the same PSRAM/DSP wall. Even the
  decoders SkyRoof relies on are *external* programs fed by IQ/audio. On the Cardputer,
  the most that's plausible is **narrowband, low-rate** decode of a specific beacon
  format (e.g. slow CW telemetry) as a tightly-scoped experiment — see the existing
  `CW_MODE_SCOPE.md` thinking — not a general telemetry suite. General decode is a
  port-class feature.
- **(K) Multi-view layouts** (from §4) — a bigger screen makes this meaningful; the
  port is where it would land if ever.

A useful framing: CardSat's `DUALCORE_OFFLOAD_SCOPE.md` already explores using the
second core for heavier work. Even with that, **wideband SDR/DSP is out of reach
without PSRAM**; the dual-core headroom helps the *Compute*-bucket items (E, F) far
more than the *SDR/DSP* bucket (I, J).

---

## 6. Recommended ordering

If any of this is pursued, a sensible sequence by value-per-effort on the **current**
hardware:

1. **(A) Award progress tracking** — highest value; reuses log + footprint code; no
   hardware. 
2. **(G) External az/el/Doppler push** — unlocks the logger/contest ecosystem the
   desktop programs serve via DDE; low cost; pairs with web control.
3. **(B) Dupe checking** — small, immediately useful to active operators.
4. **(H) Voice AOS/LOS announcements** — complements the new hand-aiming aids; flash-
   storage-bound, not compute-bound.
5. **(C) Terminator** and **(D) sky-at-a-glance timeline** — display reuse of existing
   math/predictions.
6. **(E/F) Second observer / EME subset** — feasible but heap-costly; scope carefully
   against the no-PSRAM free-list.
7. **(I/J) SDR + decode** — **defer to the port.** Track in the Tab5/SDR port scopes.

Items 1–5 are each a plausible standalone scope; none requires new hardware or
toolchain. Items 6 need care around heap. Items 7 are explicitly out of scope for the
Cardputer and are the main argument for the port.

---

## 7. Out of scope (here)

- Detailed designs for any single feature above — each worth-doing item gets its own
  `*_SCOPE.md`.
- Full **contest scoring** engines (exchange parsing, multiplier logic) — heavyweight
  and arguably not a handheld's job; dupe-checking is the proportionate subset.
- Desktop-grade **multi-window layout managers** — wrong fit for 240×135.
- Re-implementing a desktop program's exact UI; CardSat's per-screen model is
  deliberate.

---

## 8. Verification (when any item is built)

- **(A/B)** Host: feed a synthetic `qso_log.csv` with known duplicates and a known set
  of grids/states/DXCC; confirm the tally and dupe flags match a hand-count.
- **(C/D)** Host: render the terminator/timeline against a known date/time and
  eyeball-compare subsolar point and pass bars to an independent source.
- **(E/F)** Host: cross-check second-observer mutual windows and Moon Doppler/path-loss
  against Nova or an online EME calculator for a sample.
- **(G)** Host: point a rigctld/logger client at CardSat's emitter and confirm the
  az/el/Doppler stream parses; bench-confirm against the IC-821H + rotator that values
  track the live pass.
- **(H)** Bench: confirm announcement timing against the existing AOS/sked alarm path
  and that audio doesn't disrupt CAT/rotator timing.
- **(I/J)** Port-only; out of scope for Cardputer verification.

---

*Companion documents:* `TAB5_PORT_SCOPE.md`, `CARDPUTER_ZERO_PORT_SCOPE.md` (target
platforms for the SDR/DSP-class gaps); `WEB_CONTROL_SCOPE.md` (the natural home for
the external-data-push work); `DUALCORE_OFFLOAD_SCOPE.md` (second-core headroom for the
compute-bucket gaps); `CW_MODE_SCOPE.md` (the narrowband-decode precedent); and
`VISUAL_PASSES_SCOPE.md` (the visibility math the terminator/timeline reuse).
