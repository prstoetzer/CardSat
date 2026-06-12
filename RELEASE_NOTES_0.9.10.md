# CardSat v0.9.10 — Release Notes

All changes since **v0.9.9**. This release combines a no-radio **Manual mode**, a
**Sun/Beta angle** page in orbital analysis, **live space-weather** input to a
**retuned** decay estimate, quicker access to the two schedule charts, and a
refinement pass on those charts (smoother scrolling, full-window fill, no
placeholder flicker).

> **Hardware status — unchanged from v0.9.8.** Pass prediction, the plots, GPS,
> the AOS alarm, deep sleep, and the offline caches are confirmed on hardware.
> Everything that talks to a **radio, rotator, or network** remains **host-tested
> only** — including the new space-weather fetch. The orbital/astro math (beta
> angle, decay) is first-order analytic and host-verified only.

---

## Highlights

- **Manual mode** — operate without a CAT radio: fix one leg and read the
  Doppler-corrected frequency to tune the other by hand.
- **Sun / Beta angle page** — solar beta angle, full-sun vs eclipsed state,
  eclipse fraction per orbit, and the next full-sun/eclipse transition.
- **Live F10.7 space weather** — fetched with GP data and used by an **auto**
  decay setting; the decay model itself is retuned to realistic lifetimes.
- **10-day & illumination from Satellites** — reachable directly with `d` / `i`.
- **Smoother schedule charts** — one-day-at-a-time scrolling, a fully-filled
  10-day chart, and no placeholder flicker while computing.

---

## New features

### Manual mode (no-radio frequency calculator)

Press **`f`** on the Track screen to open **Manual mode**. It shows the same live
data as Track — az/el, range/range-rate, eclipse flag, transponder, calibration —
but **never commands a radio or rotator**. You **fix one leg** (the frequency you
hold on your own radio) and it shows the **Doppler-corrected frequency to tune
the other leg to**, live, with your saved calibration applied.

- The rows are marked **HOLD** (parked nominal, no Doppler) and **TUNE>**
  (Doppler-corrected). Press **`u`** to toggle which leg is fixed.
- **Linear** birds: `,`/`/` move the fixed frequency through the passband (`s`
  step, `x` center); the other leg follows.
- **FM** birds: pick the fixed leg with `u` (typically the VHF one); the UHF leg
  shows the Doppler-corrected frequency. A hint notes which leg is fixed.
- **Downlink-only** birds: shows the computed downlink to tune your receiver to.
- `m` CAL, `t` next transponder, `l` log, `p` polar, `g` live grids — and the
  log/polar/grid screens **return to Manual** (not Track) when opened here.
- `` ` `` or `f` returns to Track.

### Sun / Beta angle page (orbital analysis)

Orbital analysis gains a 7th page, **Sun/Beta**, showing the solar **beta
angle** (the angle between the orbit plane and the Sun), whether the orbit is in
**full sun or eclipsed each rev**, the **beta\*** full-sun threshold for the
orbit's altitude, the **eclipse fraction** (% and minutes per orbit), and a scan
to the **next full-sun/eclipse transition** within 180 days. Useful for judging
solar-panel charging seasons and eclipse-driven battery stress.

### Live space weather + retuned decay

- **F10.7 fetch.** When GP elements are updated, CardSat now also fetches NOAA
  SWPC's **10.7 cm solar flux** (preferring the 81-day average), caches it to
  flash, and reloads it at boot. Best-effort and non-fatal.
- **Auto decay.** The *Settings -> Station / display -> Decay solar* setting gains
  an **`auto`** option that derives the atmospheric-density scale from the live
  F10.7 (~70 sfu -> thin/long-life, ~250 -> thick/short-life), falling back to
  mean with no data. The setting row shows the current flux value.
- **Retuned model.** The B\*->ballistic-coefficient constant was recalibrated
  against ISS-class, ~400 km, and ~550 km objects (the textbook value left LEO
  lifetimes roughly 3x too long); decay numbers are now far more realistic.
  Still order-of-magnitude — B\* is often a fitted term.
- **King-Hele eccentricity decay.** The integrator now tracks **perigee and
  apogee radii separately** and applies the per-orbit energy loss preferentially
  to apogee until the orbit is nearly circular (drag acts hardest at perigee and
  circularizes the orbit), then brings both down together to a ~120 km reentry,
  with an adaptive step that tightens during the fast final plunge. For
  near-circular LEO this matches the old result; for eccentric orbits it is much
  more realistic.
- **Solar-activity range.** The Info page shows two lines: **B\* / decay** (the
  headline estimate at the assumed level) and **Decay rng** — the bracket from
  solar maximum (shortest life) to solar minimum (longest life), e.g.
  `1y-11y (mean)`. The **Decay solar** setting selects the level — **min / mean /
  max / auto** — scaling atmospheric density x0.35 / x1.0 / x3.0 (or live-F10.7
  in auto), and is persisted with the rest of the configuration.

### 10-day & illumination from the Satellites list

The **10-day pass overview** (`d`) and **illumination raster** (`i`) are now
reachable directly from the Satellites list for the highlighted satellite, in
addition to the Passes screen. Each returns to wherever it was opened from.

---

## Schedule-chart refinements

### 10-day pass overview

- **Fills the full ten days.** The window starts at **UTC midnight** of the first
  day and runs ten **complete** days, so the last day-row is no longer cut off.
- **One-day scrolling.** `;`/`.` scroll **one day at a time** — the oldest day
  falls off the top and a fresh day appears at the bottom — instead of paging a
  whole ten days. Forward is unbounded; it will not scroll earlier than today.
- The "Now" tick still marks the current time, shown only when today is the top
  row.

### Illumination raster

- **One-day scrolling.** `,`/`/` shift the 60-day raster **one day at a time**
  (oldest column off the left). Forward is unbounded; it will not scroll before
  today. The raster start is aligned to UTC midnight.

### Both charts — no placeholder while computing

- While a chart is being (re)computed, the screen shows a brief **"Computing..."**
  line instead of momentarily rendering the **"No passes in this window"** /
  **"Press r to compute"** placeholder. Those appear only when a completed
  computation genuinely found nothing.

---

## Notes & caveats

- Manual mode reuses the same Doppler/passband math as Track but its displayed
  values haven't been validated against a real pass — sanity-check against a
  known linear bird (the derived leg should move the right way as range-rate
  changes sign) before relying on it on the air.
- The space-weather fetch is network-dependent and host-tested only; confirm it
  populates on-device.
- Scrolling recomputes the visible window on each step (the predictor is fast for
  a single satellite).
- Illumination eclipse still uses a cylindrical-shadow model (no penumbra) and is
  sampled, so band edges are good to about a minute.

---

## Internals

- New `SCR_MANUAL`; `drawManual`/`keyManual`; `manFixUp` (fixed-leg) and a
  `liveReturn` field so polar/grid/log route back to Track *or* Manual. Manual
  reuses `passbandFreqs`/`dopplerFreqs`; it never sets `radioOut`/`rotOut`.
- `Predictor::betaAngleDeg(t, incl, raan)` (orbit-normal . Sun unit vector);
  orbital analysis extended to 7 pages.
- `fetchSpaceWeather()` (SWPC F10.7, newest-record + 90-day mean), cached to
  `FILE_SPACEWX`, `loadSpaceWeather()` at boot; `decayDensityScale()` maps F10.7
  to a continuous scale in AUTO mode; `SOLAR_AUTO` added; decay constant
  `12.741621*B*` -> `38.0*B*` (calibrated) with King-Hele eccentricity decay.
- `d`/`i` entries in `keySatList`; `visReturn` field for the vis/illum screens.
- `visPage` -> `visDayOff`, `illumPage` -> `illumDayOff` (day offsets >= 0), both
  windows aligned to UTC midnight; `building` flag gates the empty-state
  placeholders; `keyVis`/`keyIllum` step the offset by +/-1, floored at 0.
- `FW_VERSION` -> **0.9.10**.

---

*CardSat is open source — issue reports, on-air test logs, and pull requests are
welcome at <https://github.com/prstoetzer/CardSat>.*
