# 0.9.51/0.9.52 scoping: state-vector -> GP elements tool

Add a Tools entry that computes **GP mean elements** (not a formatted TLE) from an orbital
state vector `(r, v)` as distributed by a launch provider, and optionally saves the result
as a **manual satellite**. Feasibility judged against heap, compute, and -- crucially --
correctness, since the naive path (osculating -> TLE) is wrong.

## Why this is feasible on-device (the good news)

CardSat already has the exact forward model the correct method needs:
- `SatDb::gpToTle(SatEntry, l1, l2)` renders GP mean elements into TLE strings.
- The bundled Vallado/Hopperpop `sgp4(wgs72, satrec, tsince, r, v)` propagates them to a
  **TEME position/velocity**.

So the correct approach -- **differential correction**: find the mean elements whose SGP4
propagation reproduces the target state -- can be done by wrapping the existing SGP4 in an
iteration loop. No new propagator, no big library.

## Heap & compute

Trivial. The fit carries a handful of 6-vectors and one 6x6 Jacobian (288 B of doubles),
all stack/`.bss`. Each iteration is ~7 SGP4 propagations (nominal + 6 perturbed for the
numerical Jacobian); ~5-8 iterations to converge => a few dozen SGP4 calls, milliseconds.
**Heap-flat, no network, no per-frame allocation.** This is a compute-cheap feature.

## The honest correctness constraints (must be built in, not hidden)

1. **Frame.** SGP4 works in **TEME**. CardSat has **no** rigorous ECI(J2000/GCRF)->TEME
   precession/nutation transform (confirmed -- only J2000-day reckoning exists). Building one
   is a lot of code/data. Therefore the tool **requires the input state to be in TEME** and
   says so plainly. If the provider gives J2000/GCRF/ECEF, the user must convert upstream
   (desktop: Orekit/Vallado). This is the single most important caveat -- a wrong frame
   silently corrupts inclination/RAAN. (A future enhancement could add the dominant
   precession term, but partial frame handling is worse than an explicit "TEME in" contract.)
2. **B\*.** A single state vector carries no drag information. The tool sets **B\* = 0** and
   labels the result accordingly. Predictions degrade over days for LEO; the on-screen note
   says to re-acquire real elements once the object is cataloged.
3. **Single-epoch fit.** With one state we fit 6 mean elements to the 6-vector state at
   epoch (a point fit). It reproduces the state at epoch; it is not an ephemeris least-squares
   fit (which the device isn't the place for). Good for the first day or so post-separation.

These three are exactly the caveats from the orbital-mechanics discussion; the tool states
them on-screen so the output is never mistaken for a cataloged element set.

## The algorithm (as implemented on-device)

Input: epoch (UTC), and TEME `(rx,ry,rz)` km, `(vx,vy,vz)` km/s.
1. **Initial guess:** convert `(r,v)` -> osculating classical elements (write a compact
   `rv2coe`: energy -> a, eccentricity vector -> e, h -> i/RAAN, etc.). Seed a `SatEntry`
   with n (from a, rev/day), e, i, RAAN, argp, M, epoch; B\*=ndot=nddot=0.
2. **Iterate (Newton / Gauss-Newton):**
   - Propagate the current guess with SGP4 to tsince=0 -> modeled TEME `(r',v')`.
   - Residual = `(r,v) - (r',v')` (6-vector).
   - Numerical 6x6 Jacobian: perturb each of {n,e,i,RAAN,argp,M}, re-propagate, finite
     difference.
   - Solve `J dX = residual` (6x6 Gaussian elimination with partial pivoting), update, repeat
     until |residual| < tol (e.g. metres / mm/s) or max iters.
3. **Output GP elements:** n (rev/day), e, i, RAAN, argp, M, epoch, B\*=0. Show them, and
   offer **"save as manual satellite"** -> writes a `SatEntry` straight into the manual-sat
   store (the same struct SGP4 already consumes), so it's immediately trackable.

Robustness notes: units and angle wrapping matter (M/RAAN/argp mod 360; e in [0,1)); step
sizes for the Jacobian scaled per element; guard non-convergence (report residual, don't
save garbage). All host-testable against a known TLE: propagate a real TLE to get a TEME
state, feed it back, confirm the fit recovers the original mean elements.

## UI shape

- New Tools standalone screen `SCR_GPFIT` ("State vector -> GP"). A small form: epoch
  (date/time), then rx ry rz vx vy vz (reuse the edit-field idiom; six numeric fields).
- A **Solve** action runs the fit (fast; a brief "solving..." is plenty -- no jobbing needed).
- Results panel: the six mean elements + derived period/apogee/perigee as a sanity check,
  the residual (so the user sees the fit quality), and the frame/B\*/"re-acquire" caveats.
- **Save as manual sat** action: prompts for a name, writes the SatEntry, confirms.

## What NOT to do
- **No formatted-TLE output** (the user asked for GP elements, not TLE; and emitting a
  two-line set invites the double-counting mistake if round-tripped carelessly). Show mean
  elements as numbers; saving as a manual sat stores them as GP elements directly.
- **No frame transforms** beyond the TEME-in contract -- partial precession is a trap.
- **No ephemeris least-squares** -- single-point fit only; that's the device's honest scope.

## Verdict
Buildable, heap-flat, and -- with the TEME-in contract and the B\*/re-acquire notes --
*correct* rather than the naive osculating-stuffing that produces multi-km errors. The fit
reuses CardSat's own SGP4 as its forward model, which is what makes it both small and right.
