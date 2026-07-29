# Orbital-decay model — 0.9.68 rework

Status: **implemented**. Replaces the B*-only estimator that shipped through
0.9.67. Scored against catalogued objects that actually re-entered.

## What was wrong

The 0.9.67 estimator predicted **~1/5 of the true remaining life** and never
landed within ±30% of a real re-entry, at any lead time from 30 days out to 3.
Four distinct defects, found by reading the code and confirmed against data:

1. **A factor-2 error in the decay rate.** `dadt = -2*TP*CdAm*rho*a*a/T` with
   `T = TP*sqrt(a^3/MU)` reduces to `-2*B*rho*sqrt(MU*a)`; the energy-balance
   result is `-B*rho*sqrt(MU*a)`. Per revolution the code removed `4*pi*B*rho*a^2`
   where King-Hele gives `2*pi*B*rho*a^2`. (The debris-lifetime *tool* in the same
   firmware used the correct form, so CardSat contained two decay models
   disagreeing by 6x on the same object.)
2. **A calibration constant hiding it.** `Cd*A/m = 38*B*` is 2.98x the textbook
   `12.741621*B*`; together with the factor-2 that is ~6x textbook drag. It had
   been tuned on ISS — the worst possible target, being actively reboosted, with
   TLE fits that absorb maneuvers and a B* that fluctuates and can go negative.
3. **No eccentricity correction.** Drag was evaluated at perigee density but
   applied as though the orbit were circular there, ignoring that an eccentric
   satellite spends almost none of a revolution near perigee. King-Hele's
   per-revolution factor `exp(-z)(I0(z)+2e*I1(z))`, `z = a*e/H`, is ~0.05 for a
   GTO — the model overstated its drag ~20x, and ~120x with the other factors. A
   GTO read **43 days**; reality is years to decades.
4. **Perigee lowered far too fast.** The apogee/perigee split put ~40% of the drop
   on perigee at `ra/rp = 1.5`; King-Hele holds perigee nearly constant until the
   orbit circularizes. Since perigee altitude sets the density, the error fed back
   on itself.

## What replaced it

**Two anchors, in preference order.**

1. **Observed n-dot.** `MEAN_MOTION_DOT` was already parsed into `SatEntry` and
   unused. It is a *measurement* of the current decay rate:
   `adot = -(2/3)(a/n)*ndot`. Back-solving the ballistic coefficient from it makes
   the present rate correct by construction and eliminates the B* conversion, the
   absolute density normalization, the solar-activity scale, attitude and true
   area — all of which are already folded into the measured number. Usable for
   ~95% of catalogued objects and 86 of 91 satellites in CardSat's own list.
2. **B\* fallback** when n-dot is absent, negative (rising / post-maneuver), below
   the noise floor, above 1000 km, or implies an absurd ballistic coefficient.
   Uses the textbook `12.741621*B*`.

**Shared integration.** Correct `da/dt`, King-Hele eccentricity factor via
exponentially-scaled Bessel `I0`/`I1` (A&S 9.8; the unscaled functions overflow a
double at the `z ~ 350` a GTO reaches), perigee-preserving circularization, and an
**eccentricity-aware re-entry threshold**: 120 km perigee when near-circular, 90 km
otherwise, because an eccentric orbit sweeps through perigee too fast to be stopped
there. That last one came out of the validation set — CZ-3B R/B sat at a 111 km
perigee with e = 0.175 and lived another 2.5 days, which a flat 120 km cut called
an immediate re-entry.

## Calibration and scoring

Data: **244 objects with real re-entries** (Space-Track TIP decay epochs paired
with `gp_history` element sets, 2025-2026, fetched with
`tools/fetch_decay_calibration.py`), scored from element sets at 30/14/7/3 days
before the event. 187 were Starlink; those deorbit under propulsion and were
excluded from the fit, leaving 57 clean drag-decay objects (205 scoring points).
Cross-checked against the observed n-dot of ~1500 catalogued objects and of
CardSat's own 91-satellite list.

| model | median predicted/actual | within ±30% |
|---|---|---|
| 0.9.67 | 0.21x | 0-2% |
| 0.9.68 B* fallback | 1.04x | 86% |
| **0.9.68 shipped (n-dot anchored)** | **1.05x** | **89%** |

Constants, all in one block in `app.cpp`:
- `DECAY_ANCHOR_DRAG = 1.15` — n-dot is a fitted mean over the element set's fit
  span and lags the instantaneous rate while decay accelerates. This must be a
  **drag** factor, not a density one: a density factor cancels out of an anchored
  estimate exactly (verified in the harness).
- `DECAY_DENS_CAL = 1.30` — density multiplier for the B* path at ~250 km.
- `DECAY_DENS_HKM = 300` — how that grows with altitude. **The weakest-constrained
  number in the model**: the re-entry set is entirely low-altitude (final 40 days)
  and cannot constrain it; the value comes from the n-dot ensemble at 400-600 km.

## Verification

`tools/host_decay` (16th check) extracts the estimator from `src/app.cpp` at build
time and pins: twelve real re-entries (each within 2x, median 0.75-1.35),
eccentric-orbit sanity (GTO/Molniya/HEO must not read as imminent), anchor
selection (n-dot preferred, B* on negative/missing n-dot), and the solar-scale
cancellation.

## Known limits

- Every object in the validation set is low-eccentricity — eccentric objects
  rarely re-enter — so the King-Hele correction is **theory-pinned, not
  data-validated**. The harness fixes its qualitative behavior only.
- The fit is drawn from 2025-2026, near one point in the solar cycle. The B*-path
  density constants will drift; the anchored path will not, which is the main
  argument for keeping it primary.
- Objects under active propulsion are not doing drag decay at all (Starlink scored
  1.14x median versus 1.05x for the clean set).
- Re-running the calibration: `tools/fetch_decay_calibration.py` rebuilds the data
  set from Space-Track. Its output stays local — Space-Track's user agreement
  restricts redistribution, so only the derived constants ship.
