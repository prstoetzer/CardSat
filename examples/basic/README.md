# CardSat Tiny BASIC examples

A suite of small programs for the on-device BASIC (Settings → BASIC). Paste a
listing in and press **Run**. They follow the interpreter's rules, which are worth
knowing before you write your own:

- **Line numbers are required**, and there is **one statement per line** — `:` only
  attaches a trailing `REM` comment, it does *not* chain statements.
- **Trig is in degrees** (`SIN`, `COS`, `TAN`, `ATN`), so azimuths and elevations drop
  straight in. The screen is **240×135**; y points **down**.
- After `THEN` you may only put a **line number, `PRINT`, `LET`, `GOTO`, or an
  assignment** — not `GOSUB`/`CIRCLE`/etc. Use a `GOTO`-guard instead.
- There is **no `INPUT`/`INKEY$`**: a program computes and draws, then `SHOW`s its
  frame. System variables (`SATAZ`, `UTCH`, `SFI`, …) are **read-only snapshots**.
- **Variables are single letters `A`–`Z`** holding numbers (26 of them). There is one
  numeric array, `DIM @(n)` with `n ≤ 256`, indexed `@(i)`. A two-letter name is an
  error, not a variable.
- Graphics colours 0–9: `0` blk `1` wht `2` red `3` grn `4` blu `5` yel `6` cyn
  `7` org `8` gry `9` dgrn. `CLS` clears, `SHOW` pushes the frame to the screen.

Each program was checked with a grammar validator and an execution model of the
interpreter (flow, budget, bounds), but the *visual* result is best judged on the
device. Where a program uses live data it degrades gracefully when that data isn't
available yet (no fix, no clock, no elements).

## The suite

### `SKYDOME.BAS` — a live all-sky radar
The sky as a polar dome: horizon = outer ring, zenith = centre, North = up. Elevation
rings + compass rose, the **Sun** and **Moon** from live data, then the whole loaded
catalogue walked with `SATSEL` and dotted at each bird's real az/el (green = up). A
3-petal rose and UTC clock finish it. *Graphics · trig · `SATSEL` · sun/moon/time.*

### `PASSES.BAS` — upcoming pass table
Prints the next passes CardSat has predicted for the **active** satellite — minutes to
AOS, minutes to LOS, length, and max elevation — with a `*` bar per 10° so the good
passes stand out. Uses `PASSAOS/PASSLOS/PASSMAX(k)`. *Text · `PRINT` tables · pass data.*

### `CLOCK.BAS` — analog UTC clock
Draws a clock face and sets the hour/minute/second hands from the live UTC clock, with
a digital readout beside it. A clean demo of the polar hand transform. *Graphics ·
trig · `UTCH/UTCM/UTCS`.*

### `DOPPLER.BAS` — transponder Doppler
Snapshots transponder 0 of the active satellite with `TXSEL`, then from the current
range-rate `SATRR` computes the Doppler-corrected downlink you **hear** and the uplink
you must **send**, printed in kHz with a centre-zero shift bar (green = approaching,
red = receding). *Ham-radio maths · `TXSEL` · `SATRR`.*

### `HARMONO.BAS` — harmonograph
Traces a damped two-pen harmonograph: detuned sine terms per axis, each fading with an
`EXP` decay, the colour drifting as it winds down. Pure maths — always draws.
*Trig art · no system data.*

### `MANDEL.BAS` — the Mandelbrot set (coarse)
Escape-time Mandelbrot over the classic view, drawn as coloured blocks. Coarse on
purpose (a 3-pixel grid, iteration cap 12) so it stays inside the interpreter's work
budget — it still takes a few seconds. *Compute + graphics · nested loops · complex
iteration.*

### `GROUND.BAS` — live ground-track map
An equirectangular world grid with the equator and prime meridian marked. Plots the
active satellite's sub-point from `SATLAT`/`SATLON`, your station from `MYLAT`/`MYLON`,
and walks the catalogue with `SATSEL` to dot every bird's sub-point. *Graphics ·
geography · `SATSEL` · sub-point data.*

### `SPACEWX.BAS` — space-weather dashboard
Bar gauges for `SFI`, `Kp`, the A-index and sunspot number, plus `MUF` and `Bz`, topped
with a one-line HF-conditions verdict derived from flux and the K index. Degrades to a
notice when the feed hasn't loaded (`SPWXOK`). *Graphics gauges · solar/geomag data.*

### `RANKPASS.BAS` — rank the best upcoming passes
Walks the catalogue with `SATSEL`, reads each bird's next-pass peak elevation and
minutes-to-AOS, packs a score into the `@()` array and **bubble-sorts** it to print the
best passes first. *Arrays · sorting · `PASSMAX(1)`/`AOSIN` · `PRINT` tables.*

### `SIEVE.BAS` — primes by the Sieve of Eratosthenes
A pure-maths classic: `DIM @()` as a flag table for 2..N, strike every multiple of each
prime, print what survives ten per line. No system data, so it always runs. *Arrays ·
`@(i)` as l- and r-value · `MOD` · nested loops.*

### `STARFLD.BAS` — a projected 3-D star field
Seeds stars from `DATA` triples with `READ`, then perspective-projects each onto the
screen; nearer stars land bigger and brighter. *`DATA`/`READ` · 3-D maths · no system
data.*

### `SUNMOON.BAS` — sun & moon horizon dial
A horizon dial that places the Sun and Moon by their live azimuth and elevation, with a
drop-line to the horizon, plus a sidereal-time (`LSTHR`) and magnetic-declination
(`MAGDECL`) readout for true-north correction. *Graphics · sun/moon/time · sidereal.*

## Writing your own

Start from `HARMONO.BAS`, `CLOCK.BAS`, `SIEVE.BAS`, or `STARFLD.BAS` (self-contained, no
live data) or `PASSES.BAS`/`RANKPASS.BAS` (text + system data). The full language,
function list, and every system-variable name are in
`docs/guides/CALCULATORS_TOOLS_GAMES_BASIC.md`, on the printed reference card, and now on
the device itself via **Fn+T** in the BASIC editor.
