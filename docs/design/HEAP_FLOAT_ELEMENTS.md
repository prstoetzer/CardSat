# Heap optimization: float orbital elements (0.9.41)

This note documents the `double` → `float` change to most of `SatEntry`'s mean
orbital elements, why it is safe, exactly how much it saves, and **how to revert it**
if a future change ever needs the extra precision.

## What changed

In `src/satdb.h`, the `SatEntry` struct stores eight of its mean elements as
`float` instead of `double`:

| Field | Type now | Why float is safe |
|-------|----------|-------------------|
| `incl`  | float | TLE field is `%8.4f` — 4 decimal places |
| `ecc`   | float | TLE field is `round(ecc*1e7)` — 7 significant digits |
| `raan`  | float | `%8.4f` — 4 decimal places |
| `argp`  | float | `%8.4f` — 4 decimal places |
| `ma`    | float | `%8.4f` — 4 decimal places |
| `bstar` | float | TLE exponential field — ~5 significant digits |
| `ndot`  | float | TLE exponential field — small magnitude |
| `nddot` | float | TLE exponential field — small magnitude |

Two fields are deliberately **kept as `double`**:

| Field | Type | Why it must stay double |
|-------|------|-------------------------|
| `epochUnix`  | double | A ~1.7×10⁹ absolute value. `float`'s ~7 significant digits would round it to ~128-second steps — a gross timing error. |
| `meanMotion` | double | The TLE field is `%11.8f` (8 decimal places). Near 15 rev/day that needs ~10 significant figures; `float` resolves only ~7, so storing it as float perturbs the 8th decimal. Kept as double so predictions are **bit-identical** to the all-double build. |

## Why it is safe (the elements never reach SGP4 as raw numbers)

`SatDb::gpToTle()` does not hand the stored doubles/floats to SGP4 directly. It
formats them into a **fixed-width TLE text string** with `snprintf`, and the SGP4
library re-parses that string. The TLE field widths are therefore the real precision
ceiling — and `float`'s ~7.2 significant digits exceed every field a converted element
lands in (4-decimal angles, 7-digit eccentricity, ~5-digit bstar/ndot). For `%f`
conversions the float is promoted to double automatically, so the formatting code is
unchanged.

A host-side check (formatting every TLE field from double-stored vs float-stored
values across ISS, sun-synchronous, old-LEO, GEO, Molniya, high-eccentricity and
low-inclination element sets) found **every field identical except mean motion** —
which is exactly why mean motion is the one element kept as double. With mean motion
kept double, the regenerated TLE — and thus every prediction — is identical to the
all-double firmware.

## Accuracy impact

Zero, by construction: the eight converted fields reproduce byte-identical TLE fields,
and mean motion (the only float-sensitive element) is kept as double. Even if mean
motion *were* converted, its worst-case error (~4×10⁻⁷ rev/day) accumulates to only
~20 m of along-track error per day — versus the ~1–3 km/day error already inherent in
an aging TLE (CardSat flags elements stale at ≥14 days). It was kept double anyway as
cheap insurance.

## Memory saved

Measured with matching alignment rules: `sizeof(SatEntry)` drops from **136 to 112
bytes** — **24 bytes per entry**, ~**3.5 KB** across `MAX_SATS = 150`. (The saving is
24, not 32, because the two retained doubles force 8-byte alignment that pads some of
the reclaimed float space.) This is permanently-resident DRAM returned to the heap,
enlarging the pool from which the allocator can coalesce the ~16 KB contiguous block
the mbedTLS handshake needs.

## How to revert

The change is intentionally localised to the struct declaration. **Every** read and
write site relies on implicit `float`↔`double` conversion (JSON parse assigns
`double`→field; `gpToTle`/`%f` promote field→`double`), so reverting needs no other
edits:

1. In `src/satdb.h`, change the eight `float` element lines back to `double`
   (`incl`, `ecc`, `raan`, `argp`, `ma`, `bstar`, `ndot`, `nddot`). The
   `BEGIN/END 0.9.41 float-elements optimization` comment markers bracket them.
2. Mirror the identical change into the inlined `SatEntry` declaration in
   `CardSat.ino`.
3. Rebuild. No call-site changes, no data-migration: existing `gp.json` caches
   re-parse fine either way.

That returns the firmware to the all-double layout with the ~3.5 KB given back to
static DRAM.
