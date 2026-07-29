# CubeSat orbital thermal analysis tool — scope

Status: **scoped, not implemented** (0.9.65 cycle). Scopes a lumped-parameter thermal
analysis tool that estimates a cubesat's temperature over an orbit from the heat balance of
solar, albedo, Earth-IR, and internal loads against radiative cooling, driven by CardSat's
existing eclipse/beta/sun-position machinery.

## Goal

Given a satellite (from the catalog) and a few user-supplied spacecraft properties (size,
mass, surface optical properties, internal power), estimate the **equilibrium and transient
temperature over one orbit** — the hot case (full sun), the cold case (deep eclipse), and the
swing between them as the satellite passes in and out of Earth's shadow. This is a first-order
educational/design-aid model, not a flight thermal analysis; it answers "will this cubesat
roughly survive its thermal environment, and how big is the orbit-to-orbit temperature swing?"

## Why this fits CardSat

CardSat already computes every *orbital* driver a thermal model needs; only the heat-balance
math and a parameter form are new. Reuse:

- `Predictor::sunlitAt(t)` (`predict.h:64`) — is the satellite in sunlight or eclipse at time
  t. This gates the solar and albedo inputs on/off around the orbit.
- `Predictor::eclipseDepthDeg(t)` (`predict.h:70`) — penumbra→umbra depth, for a soft (not
  step) eclipse transition if wanted.
- `Predictor::betaAngleDeg(t, incl, raan)` (`predict.h:77`) — the solar beta angle, which sets
  the eclipse fraction and thus the hot/cold duty cycle. The Illumination screen already uses
  the same beta + eclipse-fraction computation (`app.cpp:5222`), so the orbital thermal
  environment is a solved problem here.
- The SGP4 propagator for sub-solar geometry / altitude (Earth-IR and albedo scale with
  altitude and the sunlit-Earth view factor).
- The **cooperative-job pattern** (`planJobRunning`, `whPhase` at `app.cpp:6023-6031`) for
  stepping the transient integration over an orbit without blocking the UI, one time-step per
  loop.
- The **`SCR_TOOLFORM` form-tool** (`app.cpp:27840`) for the parameter-entry UI, with the
  universal form-tool printing already wired, so results print for free through a `PR_*`.

**What the catalog does NOT provide:** `SatEntry` (`satdb.h:74`) holds orbital elements but
**no physical properties** — no mass, size, or surface optical constants. So this is by
necessity a **parametric** tool: the user supplies the spacecraft properties; the catalog
supplies only the orbit. Sensible per-form-factor defaults (1U/2U/3U/6U) make it one-tap
usable.

**Orbit input — active satellite by default, custom elements accepted.** The thermal
environment depends on the orbit only through **altitude, inclination, and RAAN** (beta angle
is a closed form of inclination/RAAN/epoch via `betaAngleDeg`; the eclipse fraction is a
closed form of beta and altitude via the cylindrical-shadow `betaStar = acos(RE/(RE+h))`
geometry the Outlook screen already uses). So the tool defaults those three from the active
satellite but lets the user **override each** — altitude, inclination, RAAN — to analyze a
hypothetical orbit with no catalog entry. No full propagation is required for the custom
case; the analytic eclipse-fraction/beta path covers it, which also keeps the compute trivial.

## The model (lumped-parameter, single node)

A single-node ("isothermal lump") transient energy balance — the standard first-order cubesat
thermal model:

```
C dT/dt = Q_solar + Q_albedo + Q_earthIR + Q_internal − Q_radiated
```

with, per time-step around the orbit:

- **Q_solar** = α · S · A_sun · sunlit(t) — absorbed solar. S = solar constant (~1361 W/m²,
  seasonally scaled), α = solar absorptivity, A_sun = projected sunlit area, gated by
  `sunlitAt(t)`.
- **Q_albedo** = α · a · S · A_earth · F_earth · sunlit-earth(t) — reflected sunlight off the
  dayside Earth; a = Earth albedo (~0.3), F_earth = Earth view factor from altitude.
- **Q_earthIR** = ε · E_ir · A_earth · F_earth — Earth's thermal emission (~237 W/m²),
  present in sun AND eclipse (this is what keeps the cold case off absolute zero).
- **Q_internal** = user-supplied avg electronics dissipation (W).
- **Q_radiated** = ε · σ · A_total · T⁴ — radiative cooling to deep space (~3 K), the only
  loss term. σ = Stefan-Boltzmann.
- **C** = m · c_p — thermal mass; m user-supplied, c_p defaulted for an aluminum-dominated
  bus.

Integrate forward (explicit Euler or RK2) over one or two orbits from an initial guess until
the periodic steady state is reached; report the min/max/mean node temperature and the
in-sun/in-eclipse asymptotes.

### User-supplied parameters (the form)

| Parameter | Symbol | Default (per U) |
|-----------|--------|-----------------|
| Form factor | — | 1U / 2U / 3U / 6U picker (sets areas + mass guess) |
| Mass | m | 1.3 kg/U |
| Solar absorptivity | α | 0.6 (typical anodized/painted mix) |
| IR emissivity | ε | 0.8 |
| Internal power | Q_int | 1.0 W |
| Spin/attitude | — | tumbling (area-averaged) vs sun-pointing (fixed A_sun) |

Areas are derived from the form factor; a "tumbling" spacecraft uses the area-averaged
projected area (A/4 for a convex body), a "sun-pointing" one uses a fixed face. These two
attitude cases bracket most real behavior without a full 6-face geometric model.

## Fidelity ladder (pick a rung)

- **Rung 1 (recommended first):** single-node, area-averaged (tumbling) or single-face
  (pointing), step vs sunlit — the model above. Handful of parameters, robust, honest about
  being first-order. This is the whole tool for v1.
- **Rung 2:** soft eclipse using `eclipseDepthDeg()` for the penumbra ramp, and beta-angle
  duty-cycle so the hot/cold split reflects the actual orbit geometry rather than a nominal
  half-and-half.
- **Rung 3 (future, heavier):** a small multi-node model (e.g. +/−sun face, interior,
  battery node) with conductive links — meaningfully more RAM/compute and more parameters;
  probably past the point of diminishing educational return on a no-PSRAM device. Note as a
  possible extension, not v1.

## Outputs

- **On-screen:** min / max / mean node temperature (°C and K), the in-sun and in-eclipse
  equilibrium asymptotes, the peak-to-peak orbital swing, and eclipse fraction + beta angle
  (already computed). A small ASCII/temperature-vs-orbit-phase sparkline using the existing
  canvas primitives (the Illumination screen's strip-plot style is a ready template).
- **Printable:** a `PR_THERMAL` report through the universal form-tool print path — inputs,
  assumptions, the hot/cold/mean results, and the swing — respecting the narrow-paper helpers
  added in 0.9.65.

## Compute budget (no-PSRAM discipline)

- The integration is a scalar ODE stepped ~100–200 points per orbit for 1–2 orbits — trivial
  arithmetic, no arrays beyond a small ring for the sparkline. Run it as a **cooperative job**
  (a few steps per loop) like the rove-planner/workable-horizon sweeps, so the UI never
  stalls; a full run completes in well under a second of wall time spread over a few frames.
- No new heap of consequence: a fixed sparkline buffer (~200 floats) and the parameter struct.
  Flash-resident defaults tables. No `.bss` growth of concern.

## What must not regress

- Purely additive: a new tool screen + `PR_THERMAL`, reachable from the Tools menu. No change
  to prediction, tracking, or any existing tool.
- The model is clearly labeled **first-order / educational** in the UI and the printout, so it
  is never mistaken for a flight thermal analysis. Assumptions (single node, area-averaging,
  fixed optical properties, no conduction) are stated on the report.

## Verification plan

- All nine gates + `src`/`.ino` body parity.
- A **host-side thermal unit test** (mirroring the orbit/wrap harnesses): drive the heat
  balance with known inputs and assert the equilibrium temperatures against hand-calculated
  Stefan-Boltzmann results (e.g. a sphere at 1 AU with α/ε = 1 should sit near 279 K; a plate
  with α/ε mismatch should shift predictably). This validates the physics without hardware.
- Sanity-check a couple of real cubesats (ISS-orbit 3U, sun-synchronous 1U) against published
  first-order thermal ranges to confirm the model lands in the right ballpark.

## Effort estimate

- Model + integrator: ~120 lines (scalar ODE + heat-balance terms, reusing `sunlitAt` /
  `betaAngleDeg` / `eclipseDepthDeg`).
- Parameter form (reuse `SCR_TOOLFORM`) + defaults tables: ~100 lines.
- Results screen + sparkline (reuse Illumination strip-plot style): ~90 lines.
- `PR_THERMAL` print path: ~40 lines.
- Host-side thermal unit test: ~150 lines.
- All mirrored `src`↔`.ino`; one compile. No hardware needed — the physics is
  bench-verifiable on the host.

## Open items before implementing

1. Confirm the default optical properties and c_p to cite (pick defensible textbook values and
   state them on the report).
2. Decide Rung 1 vs Rung 1+2 for v1 (soft eclipse + beta duty-cycle is cheap and improves
   realism; leaning toward including it).
3. Pick the Tools-menu placement and whether it shares the CubeSim reference screen's slot or
   gets its own entry.
