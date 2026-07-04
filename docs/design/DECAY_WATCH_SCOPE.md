# Scope: Decay / Reentry Watch Flags

**Status: design scope only — not implemented.** This scopes flagging satellites
whose orbits are **decaying** — low and dropping perigee, high drag — so an operator
or observer can see at a glance which birds are near end-of-life or approaching
reentry, and roughly when.

---

## 1. The need

Low-orbit amateur satellites (and the objects space-watchers care about) eventually
decay and reenter. Right now CardSat shows a satellite's elements but says nothing
about whether the orbit is healthy or falling. A simple **decay flag** — plus a rough
lifetime/reentry estimate — turns the satellite list into something educational and
operationally useful: you know to work a bird before it's gone, and the space-curious
get a live "what's coming down" view.

This is a natural fit because everything needed is **already in `SatEntry`** from the
TLE/GP data CardSat already fetches.

---

## 2. What already exists (building blocks)

`SatEntry` (satdb.h) already carries the full decay-relevant element set:

- `meanMotion` (rev/day) — orbital period → semi-major axis → altitude.
- `ndot` (`MEAN_MOTION_DOT`, rev/day²) — **the primary decay signal.** A positive,
  growing `ndot` means the orbit is shrinking (period dropping). This is exactly what
  decay-monitoring tools key on.
- `bstar` — the SGP4 drag term; large `bstar` on a low orbit signals heavy drag.
- `ecc`, `argp` — to compute **perigee** altitude (the part that actually grazes the
  atmosphere), not just mean altitude.
- `epochUnix` — element age, for caveating stale data.

CardSat also already has the constants/util to convert mean motion → semi-major axis
(used throughout the predictor), so perigee altitude is a few lines.

---

## 3. The physics (kept simple)

- **Semi-major axis** from mean motion: `a = (μ / n²)^(1/3)` with `n` in rad/s.
- **Perigee altitude** `hp = a·(1 − e) − Rₑ`. This is the decay-critical number; an
  object can have a comfortable mean altitude but a perigee dipping into drag.
- **Decay indicators (any of these raise the flag):**
  - `hp` below a threshold (e.g. < 300 km → watch; < 200 km → imminent).
  - `ndot` above a small positive threshold (period actively dropping).
  - large `bstar` combined with low `hp`.
- **Rough lifetime estimate (optional, clearly approximate):** from the rate of change
  of mean motion, `ndot`, you can project when `a` reaches reentry altitude. A
  first-order estimate: time-to-reentry ≈ scale of `(n / ndot)` adjusted for the
  nonlinear speed-up near the end. This is **not** a precise reentry prediction (those
  need atmospheric-density models and ongoing tracking); label it as an order-of-
  magnitude "weeks/months/years" bucket, not a date.

---

## 4. How others handle it

- **SatNOGS / SatAOS / Heavens-Above "decay" lists** and CelesTrak's SATCAT use exactly
  these element-derived signals (perigee height, `ndot`, B*) to classify decaying
  objects. CelesTrak publishes a "decay" data set and "reentry predictions"; the inputs
  are the same GP fields CardSat already has.
- **Reentry prediction proper** (Aerospace Corp / Space-Track TIP messages) uses
  high-fidelity drag models and is updated continuously in the final days. CardSat
  cannot and should not try to match that — it should flag and bucket, and defer precise
  timing to those services.

---

## 5. Proposed behavior

### 5.1 Per-satellite decay classification
Compute, per satellite (cheap, from elements already loaded):
- `perigeeKm`,
- `decayLevel` ∈ {none, watch, soon, imminent} from the thresholds above,
- optional `lifetimeBucket` ∈ {years, months, weeks, days} from the `ndot` projection.

### 5.2 Surfacing it
- **Satellite list:** a small down-arrow/⚠ glyph (and/or color) on decaying birds, with
  the level. Optional sort/filter "decaying first".
- **Orbit/elements screen (`SCR_ORBIT`):** add a line —
  `Perigee 248 km · decaying (ndot+) · est. weeks–months` — with an explicit
  "estimate, not a reentry date" caveat.
- **Optional "Decay watch" view:** a cross-favorites (or cross-database) list of objects
  flagged `soon`/`imminent`, sorted by perigee, as a live "what's coming down" board for
  the space-curious.

### 5.3 Element-age guard
If `epochUnix` is old (e.g. > 7–14 days), decay estimates degrade fast near the end of
life. Show the element age and gray/caveat the estimate when stale, and lean on the
Update screen to refresh.

---

## 6. Settings

- **Decay flags** on/off.
- **Perigee thresholds** (watch / imminent), default 300 / 200 km — likely fixed
  constants rather than user settings to keep the menu lean.

---

## 7. Cost / risk

- **Compute:** trivial — a few arithmetic ops per satellite from already-loaded
  elements; recompute on element update, not per tick.
- **Heap:** one byte (level) + optional small fields per `SatEntry`, or computed on the
  fly when drawing.
- **Risk — overclaiming:** the danger is implying a precise reentry prediction. Mitigate
  by bucketing (weeks/months), always showing element age, and pointing to CelesTrak/
  Space-Track for real reentry timing. This must read as "this orbit is decaying," not
  "this reenters Tuesday."
- **No new hardware/toolchain dependencies.**

---

## 8. Out of scope

- Precise reentry epoch/footprint prediction (needs density models + continuous tracking).
- TIP-message ingestion or any Space-Track-authenticated feed.
- Maneuver detection (distinguishing a burn from drag).

---

## 9. Verification

- Host: feed TLEs for known-decaying objects (low perigee, large `ndot`) and confirm
  level/bucket; feed a healthy ISS-altitude TLE and confirm `none`.
- Cross-check perigee altitude against CelesTrak SATCAT for a sample of objects.
- Confirm stale-epoch caveating triggers on an intentionally old element set.
