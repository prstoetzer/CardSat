# CardSat orbital analysis & view screens — technical reference

A technical reference to CardSat's orbital-analysis and visualization screens: what each one
computes, the projection or algorithm behind it, and where it lives in the code. Generated from
the draw/compute functions in `app.cpp` and the prediction primitives in `predict.{h,cpp}`.

Everything here sits **on top of the SGP4 predictor** (`predict.cpp`): each view samples the
satellite's state — sub-point latitude/longitude, azimuth/elevation, range rate, sunlit flag —
via `Predictor::look(t)` / `predictPasses()` / `sunlitAt()` / `rangeRateAt()` and renders it.
For the Doppler-tuning side of the predictor see `docs/guides/CAT_ROTATOR_DOPPLER.md`; for the
orbital data exposed over HTTP see `docs/interfaces/WEB_API.md` (`/api/orbit`).

> **Logging & QSO services** (CloudLog, LoTW, the log, voice memos, notes) have moved to their
> own reference: `docs/guides/LOGGING_AND_QSO.md`.

Contents: [3D globe](#1-3d-globe-scr_globe) · [OSCARLOCATOR](#2-oscarlocator-scr_oscar--scr_eqx) ·
[Illumination](#3-illumination-scr_illum) · [10-day pass progression](#4-10-day-pass-progression-scr_vis) ·
[Mutual window finder](#5-mutual-window-finder-scr_mutual) · [DX Doppler](#6-dx-doppler-scr_dxdopp)

---

## 1. 3D globe (`SCR_GLOBE`)

An **orthographic projection** of the Earth as seen from infinitely far away, with the selected
satellite, its ground track, and the day/night terminator drawn on it. `drawGlobe()` +
`globeProject()`; reached with `3` from the Satellites screen.

**The projection** (`globeProject`, the core math): a lat/lon is converted to a unit vector,
then rotated about the X axis by the **view latitude** so the chosen view center maps to the
point facing the viewer:

```
x  = cos(lat)·sin(lon − viewLon)          // east-west on screen
y  = cos(lat)·cos(lon − viewLon)
z  = sin(lat)
yr = z·cos(viewLat) − y·sin(viewLat)      // screen vertical (+ = north/up)
zr = y·cos(viewLat) + z·sin(viewLat)      // + = toward viewer
if (zr < 0) → hidden (far side, culled)
screen = (cx + x·R, cy − yr·R)
```

The `zr < 0` test is the **far-side cull** — anything on the hemisphere facing away is simply
not drawn, which is what makes it read as a solid globe (radius `R = 56 px`, center `(70,70)`).

**What's layered onto the disc**, each clipped at the limb:
- **Graticule** — meridians and parallels every 30°, drawn as short 10° segments (front side
  only; a segment is dropped the moment `globeProject` returns false).
- **Coastline** — the shared `COAST[]` vertex table, front hemisphere; long jumps across the
  limb are suppressed by a `dx²+dy² < R²` test.
- **Ground-track trail** — the satellite's full-orbit ground track (the *same* cached arc the
  OSCARLOCATOR view builds, `oscarArc*`), reprojected onto the globe in blue, rebuilt once per
  orbit and held static.
- **Day/night terminator** — the great circle 90° from the **sub-solar point**
  (`subSolarPoint(now)`): the set of points exactly one quarter-turn from the Sun's nadir.
- **The satellite marker** and its sub-point.

**Auto-follow:** when `globeFollow` is on, the view center tracks the satellite's sub-point each
frame so the bird stays centered; otherwise the view latitude/longitude are operator-controlled.

---

## 2. OSCARLOCATOR (`SCR_OSCAR` + `SCR_EQX`)

A modern take on the classic **OSCARLOCATOR** plastic-wheel azimuthal map: an azimuthal-equidistant
plot centered on your station (or a pole), with the orbit ground track laid over it, plus an
**equator-crossings (EQX) table**. `drawOscar()` / `buildOscarArc()` / `buildEqx()`; reached with
`k` (locator) and `e` (EQX table) from Satellites.

**Two modes** (`m` toggles `oscarMode`):
- **QTH mode** — centered on your latitude/longitude; the plotted radius `rmax` extends to
  `|lat| + 25°`, clamped to `[50°, 80°]`, so the useful range around your station fills the
  disc.
- **Polar mode** — a full hemisphere (`rmax = 90°`); the N or S sheet is auto-selected from the
  satellite's current sub-point hemisphere so the bird stays on the visible sheet.

**The ground-track arc** (`buildOscarArc`): the orbit is sampled across **one full period**
(`1440/meanMotion` minutes), **centered on now** — half an orbit back, half forward
(`OSCAR_ARC_PTS` samples) — so the satellite marker sits in the middle of the drawn track. The
arc is cached (`oscarArcEnd` marks when it scrolls off and needs a rebuild). AOS/LOS markers for
the current/next pass are found with `predictPasses()` and placed on the track.

**The EQX table** (`buildEqx`, `SCR_EQX`): finds **ascending** (northbound, lat 0 going −→+) or
**descending** (southbound, +→−) **equator crossings** over a **3-day** window. The algorithm
coarse-steps the sub-point latitude at **30 s** resolution (fine enough never to skip a node for
LEO/HEO), detects a sign change, then **bisects 18 times** to pin the crossing time, and records
the crossing **longitude** (converted to the conventional °W). `e` toggles ascending/descending.
These are the longitudes where the satellite crosses the equator each orbit — the numbers you'd
read off an OSCARLOCATOR's equator-crossing scale.

---

## 3. Illumination (`SCR_ILLUM`)

A 60-day **eclipse calendar**: for each day, whether the satellite is in sunlight or Earth's
shadow through one orbit, rendered as a bit-packed raster. `buildIllum()` / `drawIllum()`;
reached with `i` from Satellites.

**The computation** (`buildIllum`): for each of `ILLUM_DAYS` columns (one per day, starting at
UTC midnight of `today + illumDayOff`), one orbital period is sampled in `ILLUM_ROWS` rows; each
row tests `pred.sunlitAt(t)` and **sets a bit if the satellite is eclipsed** (in shadow). The
result is a `illumBits[day][row]` bitmap — set = eclipse, clear = sunlit — drawn as a vertical
strip per day.

**Derived live figures** (from column 0, the current orbit):
- `illumEclMin` / `illumEclPct` — eclipse minutes and percentage this orbit (counting set bits).
- `illumSunNow` — sunlit right now.
- `illumNextSec` — seconds to the next sunlight↔eclipse transition, found by stepping 15 s until
  `sunlitAt` flips.

This is the view for power-budget and thermal reasoning — when a bird is in continuous sun
(`betaStar` season) versus deep into eclipse each orbit.

---

## 4. 10-day pass progression (`SCR_VIS`)

A Gantt-style **timeline of every pass over the next 10 days**, one row per day, modeled on
InstantTrack's "Multiple Days for Single Satellite." `buildVis()` / `drawVis()`; reached with
`d` from Satellites (`;`/`.` shift the window ±1 day, `r` recomputes).

**The computation** (`buildVis`): the window starts at **UTC midnight** of `today + visDayOff`
and spans `VIS_DAYS` **full** days (so the last day-row isn't cut off partway through), then
`predictPasses(winStart, minPassEl, …, winEnd)` enumerates every pass above the minimum-elevation
floor across the whole span into `visPasses[]`.

**The chart** (`drawVis`): each day is a row; the bar area is **196 px wide = 24 h**, with
06/12/18 h gridlines behind. For each pass, the AOS→LOS interval is **clipped to that day's
midnight-to-midnight window** and drawn as a horizontal bar at the correct time-of-day position.
A pass spanning midnight appears as bars on both day-rows. **Bar color encodes max elevation:**

| Max elevation | Color |
|---|---|
| < 15° | dark green (marginal) |
| 15–40° | green (good) |
| ≥ 40° | yellow (excellent) |

so the operator can scan ten days at a glance and pick the high passes.

> **Related: the Orbit screen's "Outlook" page** (`buildOrbit`, page 7) summarizes the same
> kind of span numerically over `ORB_OUTLOOK_DAYS`: total pass count, how many exceed 30°
> (`outlookHi`), the **best** pass (highest max-el, its time and duration), the **longest** pass
> (minutes), and the **average gap** between passes (hours, from first/last AOS over n−1). This
> is exposed in `/api/orbit` as `outlookN/outlookHi/bestEl/bestT/longestMin/avgGapH`.

---

## 5. Mutual window finder (`SCR_MUTUAL`)

Finds **co-visibility windows**: the times a chosen satellite is **simultaneously above the
horizon for both you and a distant (DX) station** — the windows in which a contact through that
satellite is geometrically possible. `computeMutual()` / `drawMutual()`; reached with `M` from
the Passes screen (you enter the DX station's grid).

**The computation:** the DX grid is converted to lat/lon (`Location::gridToLatLon`), an
`Observer` is built for it, and `pred.mutualWindows(now, dxObs, 0°, …, MUTUAL_MAX)` returns the
intervals where **both** stations see the satellite above 0°. Each `MutualWindow` carries the
start/end and **each station's max elevation during the window** (`myMaxEl`, `dxMaxEl`) — so you
can tell not just *whether* but *how well* both stations see it.

**The display:** a scrollable list of windows — start (UTC), duration, and the two max
elevations side by side. From here `d` opens the DX Doppler view (§6) for the selected window.
This is the planning tool for a scheduled satellite QSO with a specific distant station: it tells
you when to be on, and how favourable the geometry is at each end.

---

## 6. DX Doppler (`SCR_DXDOPP`)

For a chosen mutual window, computes **the frequencies *both* stations must tune** to stay in
contact through the satellite, accounting for **each station's own Doppler** — because the two
stations see different range rates and so must tune differently to meet on the same satellite
passband slot. `drawDxDopp()` / `dxDoppFreqs()`; reached with `d` from the Mutual window list.

**The core** (`dxDoppFreqs`, per 30 s step across the window): both stations are evaluated from
**their own range rate** at that instant —

```
operating point in the passband (satellite frame): passbandFreqs(tp, dxdPbOff) → dlOp, ulOp
rrMe = rangeRateAt(t) at MY site          βMe = rrMe·1000/c
rrDx = rangeRateAt(t) at the DX site      βDx = rrDx·1000/c
```

then three modes (`t` cycles `dxdMode`):

- **Mode 0 — "true rule":** both stations work the *same satellite-frame operating point*, each
  Doppler-corrected by its own β: `dopplerFreqs(dlOp, ulOp, rrMe, …) → my RX/TX` and
  `dopplerFreqs(dlOp, ulOp, rrDx, …) → DX RX/TX`. The clean, symmetric answer.
- **Modes 1/2 — fixed downlink / fixed uplink:** an **anchor** station's dial is *locked to one
  real-RF value across the whole window*, and the satellite-frame operating point is solved each
  step so that, at that step's β, the anchor's dial reproduces the locked value; everyone else
  follows from the drifted operating point. The lock value is captured **once** at the window
  start (a fixed reference instant) — a subtlety the code calls out explicitly: deriving the
  "parked" dial from the live per-step β instead made it collapse to zero drift, so the
  supposedly-fixed dial actually swung with Doppler. `a` selects which station is the anchor.

**The display:** for each 30 s step, **two lines** — "me" (green) then "DX" (cyan) — each with
RX and TX in MHz, the time printed once per step. On a linear transponder the passband offset
(`±k` from center) is adjustable (`,`/`/` dial by 1 kHz). The result is a two-column "tune to
this" table both operators can follow through the pass.

---

## Where these live in the code

| View | Screen | Compute | Draw |
|---|---|---|---|
| 3D globe | `SCR_GLOBE` | `globeProject`, `buildOscarArc` (shared track) | `drawGlobe` |
| OSCARLOCATOR | `SCR_OSCAR` | `buildOscarArc` | `drawOscar` |
| EQX table | `SCR_EQX` | `buildEqx` (coarse-step + bisect) | `drawEqx` |
| Illumination | `SCR_ILLUM` | `buildIllum` (`sunlitAt` raster) | `drawIllum` |
| 10-day passes | `SCR_VIS` | `buildVis` (`predictPasses`) | `drawVis` |
| Outlook summary | `SCR_ORBIT` p7 | `buildOrbit` | `drawOrbit` |
| Mutual windows | `SCR_MUTUAL` | `computeMutual` → `pred.mutualWindows` | `drawMutual` |
| DX Doppler | `SCR_DXDOPP` | `dxDoppFreqs` (per-station β) | `drawDxDopp` |

All sampling ultimately calls `Predictor::look()` / `predictPasses()` / `sunlitAt()` /
`rangeRateAt()` on the SGP4 core (`predict.cpp`), so the accuracy and limitations of every view
above are those of the underlying SGP4 propagation and the freshness of the loaded orbital
elements (GP/OMM).
