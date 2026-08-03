# CardSat Tiny BASIC examples

A suite of small programs for the on-device BASIC (Settings → BASIC). Paste a
listing in and press **Run**. They follow the interpreter's rules, which are worth
knowing before you write your own:

- **Line numbers are required.** `:` **chains statements** on one line, so
  `10 A=2: B=3: PRINT A*B` prints `6`, and a whole loop fits on a line:
  `10 FOR I=1 TO 3: PRINT I: NEXT`. (Earlier revisions of this file claimed `:` only
  attached a trailing `REM`. That was wrong — verified against the interpreter.)
- **Trig is in degrees** (`SIN`, `COS`, `TAN`, `ATN`), so azimuths and elevations drop
  straight in. The screen is **240×135**; y points **down**.
- After `THEN` you may put a **bare line number** (an implicit `GOTO`) **or any
  statement** — `PRINT`, `LET`, `GOSUB`, `TEXT`, `CIRCLE`, `CLS`, an assignment. (This
  file previously said only a small subset was allowed. That restriction was removed
  from the interpreter; the note was stale.)
- There is **no `INPUT`/`INKEY$`**: a program computes and draws, then `SHOW`s its
  frame. System variables (`SATAZ`, `UTCH`, `SFI`, …) are **read-only snapshots**.
- **A program picks its own satellite — nothing needs selecting first.** `SATSEL i`
  chooses any satellite in the loaded catalog (`i` = `0` … `NSAT-1`) and now brings its
  **transponders with it**: `NTX` becomes that satellite's transponder count and
  `TXSEL k` reads *its* transponder `k`. Before this, `TXSEL` always read whatever the
  operator happened to be tracking, so a program that selected a satellite and then
  asked for its transponder silently got a different bird's frequencies.
  `SATSEL` also clears `TXOK`, so `TXOK=0` means "no transponder chosen yet" rather
  than a stale one — and `TXOK` is now readable, which it never was before. See `PICKSAT.BAS` for the self-sufficient idiom.
- **Immediate mode** (`Fn+i` in the editor) gives a prompt: type one statement, it runs
  at once, and variables persist between lines. Handy for trying an expression before
  committing it to a program. Statements that need a program (`GOTO`, `GOSUB`,
  `RETURN`, `DATA`, `READ`, `RESTORE`) are refused there.
- **Variables are single letters `A`–`Z`** holding numbers (26 of them). There is one
  numeric array, `DIM @(n)` with `n ≤ 256`, indexed `@(i)`. A two-letter name is an
  error, not a variable.
- Graphics colors 0–9: `0` blk `1` wht `2` red `3` grn `4` blu `5` yel `6` cyn
  `7` org `8` gry `9` dgrn. `CLS` clears, `SHOW` pushes the frame to the screen.

Each program was checked with a grammar validator and an execution model of the
interpreter (flow, budget, bounds), but the *visual* result is best judged on the
device. Where a program uses live data it degrades gracefully when that data isn't
available yet (no fix, no clock, no elements).

## System names added in 0.9.70

Data the firmware gained recently, exposed read-only like the rest. All refer to the
**currently selected** satellite (whatever `SATSEL` last chose, or the tracked one):

| Name | Meaning |
| --- | --- |
| `LSHELL` | McIlwain **L** — which geomagnetic shell the satellite is on, from the real IGRF-14 field |
| `BRATIO` | **B/B₀** — how far along that shell from its magnetic equator (`1` = at the equator) |
| `BFIELD` | field strength at the satellite, **nT** |
| `INBELT` | `1` if inside the inner or outer Van Allen belt (needs shell **and** `BRATIO` ≤ 3) |
| `INSAA` | `1` if inside the South Atlantic Anomaly |
| `DECAYD` | days to re-entry (`-1` = no estimate, `1E8` = effectively stable) |
| `DECAYSRC` | how `DECAYD` was derived: **`1` = the element set's measured n-dot**, `2` = modelled from B\*, `0` = none |
| `BATTMV` | battery millivolts (`BATT` is the percentage) |
| `CHARGING` | `1` when charging, inferred from the voltage trend |
| `HEAPBLK` | **largest free block** — the number that actually limits a big allocation, unlike total free heap |
| `DOPPRX` / `DOPPTX` | the Doppler-corrected receive/transmit frequencies CAT would command right now, Hz |

`DECAYSRC` is worth branching on: a measured rate and a modelled one deserve different
trust, and a program that prints a re-entry date without saying which it used is
overstating its case.

## The suite

### `SKYDOME.BAS` — a live all-sky radar
The sky as a polar dome: horizon = outer ring, zenith = center, North = up. Elevation
rings + compass rose, the **Sun** and **Moon** from live data, then the whole loaded
catalog walked with `SATSEL` and dotted at each bird's real az/el (green = up). A
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
**Finds its own satellite** (first one up with a transponder), snapshots transponder 0
with `TXSEL`, then from the current range-rate `SATRR` computes the Doppler-corrected
downlink you **hear** and the uplink you must **send**, printed in kHz with a
center-zero shift bar (green = approaching, red = receding). Cross-checks its own
arithmetic against `DOPPRX`/`DOPPTX`. *Ham-radio maths · `SATSEL`+`TXSEL` · `SATRR`.*

### `PICKSAT.BAS` — pick a satellite from inside BASIC
The idiom for a self-sufficient program: walk the catalog with `SATSEL`, skip birds that
won't propagate (`SATOK=0`), and list the ones that are **up now and have a
transponder** with elevation, range rate and downlink. Nothing has to be selected before
you run it. *Catalog scan · `NSAT`/`NTX` · `SATSEL`+`TXSEL`.*

### `BELT.BAS` — radiation environment
Where the satellite sits in the geomagnetic field: McIlwain `LSHELL`, `BRATIO`
(displacement from the shell's magnetic equator), `BFIELD` in nT, and whether that adds
up to being inside a Van Allen belt or the South Atlantic Anomaly. Explains *why* a
high-latitude pass on a belt field line is not a belt transit. *IGRF-14 data ·
`LSHELL`/`BRATIO`/`INBELT`/`INSAA`.*

### `DECAY.BAS` — re-entry watch
Scans the catalog for the satellites closest to re-entry and prints days remaining,
**flagging how each estimate was derived** — a measured n-dot or a modelled B\*. Shows
the difference between "we can see it coming down" and "we think it is". *Catalog scan ·
`DECAYD`/`DECAYSRC`.*

### `HEALTH.BAS` — station health
A one-screen readout of the things that decide whether the device will finish a pass:
battery percent and millivolts, whether it is charging, free heap versus **largest free
block** (the number that actually limits an upload), uptime and element age. *Housekeeping
· `BATT`/`BATTMV`/`CHARGING`/`HEAPBLK`/`GPAGE`.*

### `HARMONO.BAS` — harmonograph
Traces a damped two-pen harmonograph: detuned sine terms per axis, each fading with an
`EXP` decay, the color drifting as it winds down. Pure maths — always draws.
*Trig art · no system data.*

### `MANDEL.BAS` — the Mandelbrot set (coarse)
Escape-time Mandelbrot over the classic view, drawn as colored blocks. Coarse on
purpose (a 3-pixel grid, iteration cap 12) so it stays inside the interpreter's work
budget — it still takes a few seconds. *Compute + graphics · nested loops · complex
iteration.*

### `GROUND.BAS` — live ground-track map
An equirectangular world grid with the equator and prime meridian marked. Plots the
active satellite's sub-point from `SATLAT`/`SATLON`, your station from `MYLAT`/`MYLON`,
and walks the catalog with `SATSEL` to dot every bird's sub-point. *Graphics ·
geography · `SATSEL` · sub-point data.*

### `SPACEWX.BAS` — space-weather dashboard
Bar gauges for `SFI`, `Kp`, the A-index and sunspot number, plus `MUF` and `Bz`, topped
with a one-line HF-conditions verdict derived from flux and the K index. Degrades to a
notice when the feed hasn't loaded (`SPWXOK`). *Graphics gauges · solar/geomag data.*

### `RANKPASS.BAS` — rank the best upcoming passes
Walks the catalog with `SATSEL`, reads each bird's next-pass peak elevation and
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

## New in 0.9.71

| Program | Shows |
| --- | --- |
| `DXPATH.BAS` | The **pre-run input form**, `DXCC$`/`DXCCLAT`/`DXCCLON`, `GCDIST`/`GCAZ`, `GRID$`, `TIME$`/`DATE$` |
| `CALLPARSE.BAS` | String variables and the text functions: `LEFT$ RIGHT$ MID$ LEN INSTR VAL UCASE$ TRIM$` |
| `PASSTATS.BAS` | Named arrays: `DIM A(n), B(n)`, element read/write, `ERASE` |

**The input form.** A program declares what it needs and CardSat asks once, before the
run:

```basic
110 INPUT "DXCC code"; C
120 INPUT "Your note"; N$
```

Press Fn+R and a single form appears with both fields; the program then runs to
completion with `C` and `N$` already set. Programs that declare no `INPUT` run
immediately, exactly as before. Nothing pauses mid-run to ask — that is deliberate, and
it is why BASIC stays safe to run while a radio is being tuned.

**Text function index rules follow Microsoft BASIC**, because that is what a ported
program expects: `MID$(s, start, len)` counts `start` from **1**, and
`INSTR(hay, needle)` returns a **1-based** position with `0` meaning "not found".

**Arrays share a budget.** All of `A()`..`Z()` together may hold 2048 elements. That is
a device with ~76 KB of free heap running a radio, not a desktop. A subscript outside an
array stops the program rather than corrupting memory.
