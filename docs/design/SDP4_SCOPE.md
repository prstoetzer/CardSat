# SDP4 for a future amateur HEO — scope

*Asked: scope adding SDP4 for a future amateur HEO.*

## The answer

**Nothing to add. CardSat already has SDP4, and I was wrong to say otherwise.**

The PREDICT 3.0.0 audit reported "PREDICT has one capability CardSat does not: SDP4 deep-space
propagation." That was false, and this scope exercise is how it was caught.

## What I did wrong

I grepped **CardSat's own source** for `SDP4`, `deep_space`, `dpinit` — found nothing — and
concluded CardSat was SGP4-only.

But CardSat does not implement SGP4. It **delegates**:

```c
// src/predict.h:6
#include <Sgp4.h>
```

That is the **[Hopperpop Sgp4-Library](https://github.com/Hopperpop/Sgp4-Library)**, a `lib_deps`
entry in `platformio.ini` and a required install in `BUILD_AND_FLASH.md`. **I never looked inside
the dependency.** The same failure as the `String` bug: reasoning about a library from the outside
instead of reading it.

## What is actually true

Hopperpop's library is the **Vallado unified SGP4** — the reference implementation, deep-space
model included. Evidence, from the source:

- `elsetrec` carries the **full deep-space state**: `irez`, `d2201`…`d5433`, `xlamo`, `atime`,
  `zmol`, `zmos`, and the lunisolar terms — under a comment literally reading `/* Deep Space */`.
- It implements **`dpper`, `dscom`, `dsinit`, `dspace`** — the complete deep-space routine set.
- It selects the model **automatically**, at `sgp4unit.cpp:1523`:

```c
/* --------------- deep space initialization ------------- */
if ((2*pi / satrec.no) >= 225.0)
  {
    satrec.method = 'd';
    satrec.isimp  = 1;
    ...
    dscom(...); dpper(...); dsinit(...);
```

**The identical 225-minute criterion PREDICT uses** (`predict.c:1104`). Same model, same boundary,
same Vallado lineage.

## Verified by building it, not by reading it

The library was fetched and compiled on the host and driven through **CardSat's exact code path** —
`Predictor::setSat()` → `SatDb::gpToTle()` → `Sgp4::init()` → `twoline2rv()` → `sgp4init()`:

| test satellite | period | `method` | result |
|---|---|---|---|
| ISS (15.5 rev/day) | 92.9 min | `'n'` | near-Earth ✓ |
| IO-86 (14.79) | 97.5 min | `'n'` | near-Earth ✓ |
| AO-7 (12.53) | 114.9 min | `'n'` | near-Earth ✓ |
| **boundary (6.40)** | **225.1 min** | **`'d'`** | **deep space ✓** |
| **AO-40-class HEO** (2.05 rev/day, ecc 0.72) | **702.6 min** | **`'d'`** | `irez` 2, **error 0, propagates** |
| **QO-100** (1.0027) | **1436.2 min** | **`'d'`** | deep space ✓ |

The AO-40-class case is the one that matters for the question. Run end-to-end through
`twoline2rv()` with physically valid elements — perigee **951 km**, apogee **38,644 km**, checked
independently against Kepler — it selects SDP4, initialises resonance handling, and propagates with
**no error**. Radius at epoch came back 7,299 km, matching the computed perigee.

An early attempt used ecc 0.79 at 2.0 rev/day and returned `error 6` — because that combination
puts perigee **below the Earth's surface**. My elements were wrong; the library was right to reject
them. Worth recording, because it is a reminder that a failure is not automatically the library's
fault.

## What else a HEO would need — the parts that are real

The propagator is free. These are not:

| area | status for a HEO |
|---|---|
| **Propagation** | **done** — SDP4, automatic |
| **Eccentricity encoding** | **done** — `gpToTle()` writes a 7-digit field clamped to `0.9999999`; a HEO's ~0.72 round-trips fine |
| **Pass prediction** | **needs thought.** `predictPasses()` uses `nextpass()` with a 200-step search tuned for ~90-minute LEO passes. A HEO pass lasts *hours* and recurs every ~12h; the search would work but the step budget and the 128-pass cap are LEO assumptions |
| **The 10-day chart** | a HEO gives ~20 passes in 10 days, each many hours — the day-strip rendering assumes short passes |
| **Doppler** | **much smaller and slower.** A HEO's range rate is a fraction of a LEO's, and changes over hours not minutes. The Doppler loop's cycle rate and dead-band are tuned for LEO |
| **`aosPossible()`** | **already correct** — it uses apogee, so a HEO's huge apogee makes it visible from almost anywhere, which is right |
| **Rotator tracking** | a HEO barely moves; the tracking loop would send near-identical positions for hours. Harmless, but the pre-AOS and slew logic is LEO-shaped |
| **Antenna/pointing UX** | the "next pass in 43 min" framing suits LEO. A HEO is "up for 8 hours" |

**So the work for a future amateur HEO is not the propagator — it is everything downstream that
quietly assumes a 90-minute orbit.** That is a much more interesting scope, and none of it can be
scoped properly until such a satellite exists and its actual orbit is known.

## What changed in the code

The `isDeepSpace()` helper added during the audit **stays**, but its meaning is corrected:

- It was a **warning** — "CardSat uses SGP4 only; everything below is unreliable." That was written
  on a false premise and is now removed.
- It is now a **label**. The Orbital-analysis title shows **`SDP4`** (not `!SGP4`) on a deep-space
  orbit, and the report says the orbit "is propagated with the SDP4 model." Naming the model is
  useful — a GEO bird's geometry is nothing like a LEO pass — but it is information, not a caveat.

## The lesson, again

This is the fourth time this session that a confident claim came from reasoning about something
instead of reading it: the `String` bug (validated against my own reimplementation), the USB PHY
constraint (half-read a sentence), the VBUS question (didn't read Mini-FT8's README), and now
SDP4 (grepped the wrapper, not the dependency).

**The specific rule earned here: when the code delegates, audit the delegate.** "CardSat has no
SDP4" was true of CardSat's source and false of CardSat.
