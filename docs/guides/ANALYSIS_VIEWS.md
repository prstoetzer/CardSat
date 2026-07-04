# CardSat analysis screens (II) — technical reference

The second technical reference to CardSat's analysis and visualization screens, covering
everything not in `docs/guides/ORBITAL_VIEWS.md` (which holds the 3D globe, OSCARLOCATOR,
illumination, 10-day progression, mutual finder, and DX Doppler). Generated from the
draw/compute functions in `app.cpp` and the prediction primitives in `predict.{h,cpp}`.

As with the orbital views, every screen here samples the SGP4 predictor — `look(t)`,
`predictPasses()`, `sunlitAt()`, `azelAt()`, `rangeRateAt()` — so all share its accuracy and
its dependence on fresh GP/OMM elements. Logging/QSO features (CloudLog, LoTW, the log, voice
memos, notes) are in `docs/guides/LOGGING_AND_QSO.md`.

Contents: [Pass visualization](#1-pass-visualization-passdetail-passpolar-polar) ·
[Visible-pass list](#2-visible-pass-list-scr_vislist) · [Multi-sat schedule](#3-multi-sat-schedule-scr_schedule) ·
[Sun/Moon tracking](#4-sunmoon-tracking-scr_sunmoon) · [Sky sources](#5-sky-sources-scr_skymap) ·
[Sun/Moon transits](#6-sunmoon-transits-scr_transit) · [Simulation](#7-simulation-scr_sim) ·
[Sat-to-sat visibility](#8-sat-to-sat-visibility-scr_satsat) ·
[Footprint coverage: grids / states / DXCC](#9-footprint-coverage-grids--states--dxcc) ·
[Space weather & weather](#10-space-weather--weather-scr_spacewx--scr_weather) ·
[AMSAT status (hams.at)](#11-amsat-status-scr_hamsat)

---

## 1. Pass visualization (`SCR_PASSDETAIL`, `SCR_PASSPOLAR`, `SCR_POLAR`)

The detailed views of a single pass. `drawPassDetail()` / `drawPassPolar()` / `drawPolar()`;
reached from the Passes list.

- **Pass detail** (`SCR_PASSDETAIL`) — the numeric breakdown of one selected pass: AOS/TCA/LOS
  times, max elevation, AOS/LOS azimuths, duration, and (where relevant) optical-visibility and
  sunlit fraction. The single-pass companion to the passes list.
- **Pass polar** (`SCR_PASSPOLAR`) — the **sky track** of one upcoming pass drawn on a polar
  plot (N up, elevation as distance from the rim, zenith at center): the azimuth/elevation curve
  the satellite will trace from AOS to LOS, sampled across the pass. Lets you see where to point
  before it rises.
- **Polar** (`SCR_POLAR`) — the **live** polar plot for the active satellite: a moving dot at the
  current az/el while it's up, and the next-pass arc drawn while it's below the horizon (the same
  arc data the web `/api/passes` endpoint serves). This is the at-a-glance "where is it right
  now" sky view.

All three use the same polar projection: a point at azimuth `az`, elevation `el` maps to
`(cx + r·sin az, cy − r·cos az)` with `r = R·(90 − el)/90`, so the zenith is the center and the
horizon is the rim.

---

## 2. Visible-pass list (`SCR_VISLIST`)

A list of **optically visible** passes over the next 10 days — passes where the satellite is
sunlit while your site is in darkness, so it can actually be seen. `buildVisList()` /
`drawVisList()`; reached with `V` from Satellites (distinct from the `v` 10-day chart, which
shows *all* passes).

**The computation:** the 10-day span is scanned in **1-day windows** and only the optically
visible passes are kept. The windowing is deliberate — a single `predictPasses` call is capped at
`VIS_PASS_MAX` (128), and a high-pass-rate LEO (~15 passes/day × 10 days) would exceed that and
silently drop the tail of the span; per-day windows avoid the cap. `vlPasses[]` holds only the
visible passes. Optical visibility is the same test the pass predictor uses (`visEvalPass`):
satellite sunlit **and** observer in darkness during the pass.

This is the view for **visual satellite spotting** — knowing when the ISS or a bright bird will
make a visible streak across your sky.

---

## 3. Multi-sat schedule (`SCR_SCHEDULE`)

The **next pass for each of your favorite satellites**, merged into one chronological list —
the "what's coming up across everything I care about" view. `drawSchedule()`; reached from the
Passes area.

It iterates your favorites, predicts each one's next pass, and presents them sorted by AOS, so
you see the soonest pass across your whole favorites set rather than one satellite at a time
(needs at least one favorite marked — otherwise it says so). The complement to the single-sat
10-day chart: that one is depth on one bird, this is breadth across many.

---

## 4. Sun/Moon tracking (`SCR_SUNMOON`)

Live **azimuth/elevation of the Sun and Moon** for your location, with a sky dome and rotator
control. `drawSunMoon()`; reached from the main menu.

**The computation** (`skyObjAzEl`): the Sun's and Moon's positions are computed astronomically
for your lat/lon and the current time. The display is a **sky dome** — zenith at center, N up,
elevation as radius — with both bodies plotted; a body below the horizon is shown faintly just
outside the rim so its azimuth is still readable. The Moon's illuminated phase is shown.

**Rotator integration:** from this screen you can **point the antenna rotator at the Sun or
Moon** (`o` to track the selected body, `x` to stop/park) — used for **EME** (moonbounce) work
and for **Sun-noise antenna/gain checks**. The selected body drives the rotator the same way a
satellite does on the Track screen.

---

## 5. Sky sources (`SCR_SKYMAP`)

A **celestial sky plot** of the classical planets and the strongest cosmic radio sources, on the
same sky dome as Sun/Moon. `drawSkyMap()` / `keySkyMap()`; reached with `s` from Sun/Moon.

**What's plotted:** the five naked-eye **planets** (Mercury, Venus, Mars, Jupiter, Saturn,
computed live, drawn as cyan dots) and a catalog of fixed **radio sources** (drawn as orange
crosses) — Cassiopeia A (the brightest galactic source), Cygnus A, the galactic center (Sgr A\*),
the Crab Nebula (Taurus A), and others — at their current az/el for your site. Selecting an
object shows its details (az/el, above/below horizon, type). This is the reference for **an
antenna pointing/gain check against a known strong source**, or simply knowing what's overhead.

---

## 6. Sun/Moon transits (`SCR_TRANSIT`)

Finds upcoming **transits** — times when the **satellite passes close to the Sun's or Moon's
disc** in your sky, the geometry needed to photograph a satellite silhouetted against the Sun or
Moon. `transitStartJob()` / `transitJobTick()` / `drawTransit()`; reached with `t` from Sun/Moon.

**The computation** (`transitJobTick`, an incremental background job): the next interval is
scanned in chunks (`CHUNK_S` seconds per tick so the UI stays responsive); for **each body**
(Sun, Moon) it tracks a **running minimum of the angular separation** `angSepDeg(satAz,satEl,
bodyAz,bodyEl)`, and when the separation starts increasing again after dipping under a threshold
it records that **local minimum** (closest approach) — its time and the minimum separation. The
satellite and body must both be above the horizon. The coarse step (~2 s) sets the reported
precision (good to roughly a degree); a finer refine would need cross-tick state.

The result is a list of close approaches with their separation, so you know when (and how close)
a solar/lunar transit will happen — the planning tool for transit astrophotography.

---

## 7. Simulation (`SCR_SIM`)

A **time-travel** view: scrub the clock forward or back and watch the satellite's position
update, as either a world map or a data readout. `drawSim()`; reached with `s` from Satellites.

`simTime` starts at now and the operator advances/rewinds it; every frame the predictor is
sampled at `simTime` (`pred.look(simTime)`) and the result drawn. Two sub-views (`simMap`
toggles): a **world map** showing the sub-point and footprint at the simulated instant, or a
**data readout** of the simulated az/el/range/sub-point. This is how you answer "where will this
bird be at 0230Z tomorrow" or replay a past pass's geometry — the SGP4 propagator run at an
arbitrary epoch rather than the wall clock.

---

## 8. Sat-to-sat visibility (`SCR_SATSAT`)

Finds windows when **two satellites are simultaneously above your horizon** — for cross-satellite
relay experiments, or to plan back-to-back operating on two birds. `satsatStartJob()` /
`satsatJobTick()` / `drawSatSat()`; reached with `2` from Satellites (you pick the second
satellite).

**The computation** (`satsatJobTick`, a three-phase incremental job over a multi-day span at
`STEP = 60 s` sampling):
1. **Phase 1** — sample satellite A's az/el across the span (`azelAt`), recording when it's up
   (0–50% progress).
2. **Phase 2** — re-point the predictor to satellite B and sample its track (50–100%).
3. **Phase 3** — scan the two tracks for **overlaps**: intervals where both were above the
   horizon at the same sample, merged into windows.

The two-pass-then-intersect structure (rather than interleaving) keeps the predictor pointed at
one satellite at a time, which is how the SGP4 site/sat state is managed. The result is the list
of co-visibility windows for the **pair** — note this is distinct from the **mutual window
finder** (`ORBITAL_VIEWS.md` §5), which finds windows where **one** satellite is visible to
**two stations**; this finds windows where **two satellites** are visible to **one station**.

---

## 9. Footprint coverage: grids / states / DXCC (`SCR_GRID` / `SCR_STATES` / `SCR_DXCC`)

Three views of **what's under the satellite's footprint** — which Maidenhead grid squares, US
states, or DXCC entities the satellite can currently reach (or will reach during a pass). The
"who could I work through this bird" coverage tools. `buildGrids()` / `buildStates()` /
`buildDxcc()` + `addFootprintGrids()`; reached with `g`/`(state)`/`(dxcc)` keys from Satellites
or the live Track screen.

**The footprint geometry** (`addFootprintGrids`): the satellite's radio footprint is a spherical
cap centered on its sub-point. The cap's half-angle λ satisfies

```
cos λ = Re / (Re + altitude)        (Re = 6371 km)
```

A point at latitude `φ`, longitude `c` is **inside the footprint** when its angular distance from
the sub-point is ≤ λ, tested without an arccos via the spherical law of cosines:

```
sin φ · sin(subLat) + cos φ · cos(subLat) · cos(c − subLon)  ≥  cos λ
```

The code bounds the latitude band to `subLat ± λ` and, per latitude row, widens the longitude
span by `λ/cos(lat)` before testing each 2°×1° grid cell, setting a bit in `gridBits[]` for each
cell inside the cap. A popcount of the bitset gives the count.

- **Grids** (`SCR_GRID`) — the set of grid squares in the footprint. In **live** mode it
  refreshes every ~3 s (the footprint moves); over a pass it samples ~1/minute and **unions**
  the footprints (`buildGrids(a,b)` across the pass) so you see every grid the bird will touch.
- **States** (`SCR_STATES`) — the US states whose area intersects the footprint, derived the same
  way.
- **DXCC** (`SCR_DXCC`) — the DXCC entities under the footprint.

These answer "if I call CQ through this satellite right now (or during this pass), who's in the
footprint to hear me" — grid/state/entity chasing for satellite awards.

---

## 10. Space weather & weather (`SCR_SPACEWX` / `SCR_WEATHER`)

Two data-feed views fetched over WiFi.

- **Space weather** (`SCR_SPACEWX`, `fetchSpaceWeather`) — solar and geomagnetic indices from
  **NOAA SWPC**: the **F10.7 cm solar flux** (a proxy for ionospheric ionization / HF
  propagation) and the planetary **Kp** and running **A** index (geomagnetic activity, which
  affects polar-path propagation and can correlate with aurora). Relevant to satellite ops for
  gauging absorption and disturbance conditions.
- **Weather** (`SCR_WEATHER`, `fetchWeather`) — local **surface weather** for your location from
  **Open-Meteo** (CC BY 4.0): the practical "is it worth setting up the portable station"
  readout.

Both follow CardSat's unified fetch pattern — draw the cached value immediately, fetch in the
background with a status banner, then show the updated result — and both stream the response in
RAM (no large buffer) to respect the no-PSRAM heap.

---

## 11. AMSAT status (`SCR_HAMSAT`)

A view of **per-satellite activity status** sourced from **hams.at** (the AMSAT status data):
which satellites are currently heard/active, telemetry-only, or not reported. `fetchHamsat()` /
the hamsat screen. The same data drives the **right-edge activity marks** on the Satellites list
(filled dot = heard, filled square = telemetry only, ring = not heard, none = no reports), so you
can tell at a glance which birds are alive before committing to a pass. Fetched over WiFi and
cached to flash so the marks survive a reboot.

---

## Where these live in the code

| View | Screen | Key functions |
|---|---|---|
| Pass detail / polar | `SCR_PASSDETAIL` / `SCR_PASSPOLAR` / `SCR_POLAR` | `drawPassDetail`, `drawPassPolar`, `drawPolar` |
| Visible-pass list | `SCR_VISLIST` | `buildVisList`, `drawVisList` |
| Multi-sat schedule | `SCR_SCHEDULE` | `drawSchedule` |
| Sun/Moon tracking | `SCR_SUNMOON` | `drawSunMoon`, `skyObjAzEl` |
| Sky sources | `SCR_SKYMAP` | `drawSkyMap` |
| Sun/Moon transits | `SCR_TRANSIT` | `transitJobTick`, `angSepDeg` |
| Simulation | `SCR_SIM` | `drawSim` |
| Sat-to-sat | `SCR_SATSAT` | `satsatJobTick`, `azelAt` |
| Footprint coverage | `SCR_GRID` / `SCR_STATES` / `SCR_DXCC` | `addFootprintGrids`, `buildGrids/States/Dxcc` |
| Space wx / weather | `SCR_SPACEWX` / `SCR_WEATHER` | `fetchSpaceWeather`, `fetchWeather` |
| AMSAT status | `SCR_HAMSAT` | `fetchHamsat` |

Together with `ORBITAL_VIEWS.md`, this covers every analysis/visualization screen in CardSat.
The remaining screens are operational (Track/Manual/Settings/Location/etc., documented in
`MANUAL.md`) or the logging/QSO services (`LOGGING_AND_QSO.md`).
