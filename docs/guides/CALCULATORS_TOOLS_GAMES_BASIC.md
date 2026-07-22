# CardSat — Calculators, Tools, Games & BASIC

A standalone guide to the interactive and just-for-fun side of CardSat: the three
calculators, the 60-tool Tools menu (organized into six categories), the seven games, and Tiny BASIC. It stands
apart from the main manual so you can keep it open while you play. Everything here
is reachable without a radio, a rotator, or a network connection.

All of this lives under **About** (press **`z`** from the About screen for Games, or
open the **Tools** menu — **`t`** from Home — for the calculators, BASIC, and the
tool set). Throughout CardSat: **`;`/`.`** move a selection, **ENTER** confirms,
**`` ` ``** goes back, and **DEL** backspaces in an entry field (it never exits).

---

## Part 1 — The calculators

There are three, all in the Tools menu.

### Scientific calculator

A full expression evaluator. Type an expression and press ENTER; the result
appears and becomes available for the next line. Some things worth knowing:

- **Trig is in degrees**, not radians (so `sin(30)` is `0.5`). Use `d2r`/`r2d` if you
  need to convert.
- **Metric-prefix suffixes** work on numbers — `100k`, `2.2n`, `47p`, `146M`. Case
  matters: `M` is mega, `m` is milli. The suffix set runs from `f` (femto) up.
- Press **`\`** to toggle **engineering-notation output** (a mantissa with a metric
  prefix, e.g. `4.7 k`).
- The **function hints** stay pinned just above the footer, and **`'`** flips between
  the general page and a ham-radio page.

**Function set.** Alongside the usual `sin cos tan atn exp log sqrt abs int` and
constants, the calculator carries a batch aimed at radio and orbital work:

- **Math:** `atan2(y,x)` (degrees out), `hypot(a,b)`, `mod(a,b)`, `min`/`max`,
  `ncr(n,r)`/`npr(n,r)`, `sign`, `log2`, `cbrt`, `fact(n)` (n ≤ 170), `d2r`/`r2d`,
  and `rnd()` for a random 0–1.
- **RF:** `swr2rl(s)`/`rl2swr(dB)`, mismatch loss `mml(swr)`, free-space path loss
  `fspl(MHz,km)`, noise-figure ↔ temperature `nf2t(dB)`/`t2nf(K)`, `dbd(dBi)`/`dbi(dBd)`
  (the ±2.15 dB dipole/isotropic shift), and Doppler `dop(MHz, rrKmS)` — Hz straight
  from a range rate.
- **Orbital one-liners:** `porb(altKm)` circular period (minutes), `vorb(altKm)`
  orbital speed (km/s), `fpr(altKm)` footprint radius (km).

### Graphing calculator

Plots **y = f(x)** using the same parser and function set as the scientific
calculator, with **x** added as a variable. Press ENTER to type a function
(`sin(x)`, `x^2-4`, `1/x`, `exp(x/50)`); the curve is sampled one point per pixel
column across a pan/zoomable window.

- **Arrow keys pan**, **`+`/`-` zoom** about the centre, **`a`** auto-fits the vertical
  range to the visible data, **`r`** resets the window.
- Advanced readouts (see the manual for depth): a **second curve** (Y2), a **trace
  cursor** with dy/dx, **zero/intersection finding**, **Simpson integration** between
  marks, a **table view**, and a flat-RAM **CSV plot mode**.
- Trig is in degrees here too, so the default window is sized in degrees.

### Programmer calculator

A 64-bit value shown **all at once in hex / decimal / binary / octal**. Enter a
number in any base and read it in the others — handy for CI-V addresses, bit masks,
and byte math. Bitwise operations and base entry are on-screen; `` ` `` returns.

---

## Part 2 — The Tools menu

Open with **`t`** from Home. The sixty tools are organized into **six categories**,
so Tools opens on a short category list. **`;`/`.`** scroll, **ENTER** opens the
highlighted category, and its tools appear as a second list; **`` ` ``** steps back
from a tool list to the categories (and again to leave Tools). A **letter key jumps**
to the next entry starting with that letter in whichever list you're in, and each
category row shows its **tool count**. The six categories are **Calculators &
programming**, **Satellite & orbital**, **Terrestrial propagation**, **Antennas &
feedlines**, **RF chain & measurement**, and **Electronics & references**.

**How a form tool works.** Most tools are live-recalc forms: a list of input fields
with a result area below. **`;`/`.`** move between fields, **ENTER** starts editing
the selected field (type a number, ENTER commits), and the result **updates as you
type** — no "calculate" button. Fields with a **`<`/`>` marker** are *pickers*
(cable type, topology, material); use left/right to cycle the choices. Numeric
fields accept metric-prefix suffixes (`146M`, `2.2n`). Every form tool **prints**
its inputs and results with **`p`**, and remembers your last-entered values between
sessions. **`` ` ``** returns; DEL backspaces in a field.

Below, each tool lists its **input fields** (with default) and what it computes. The
four standalone calculators/BASIC are in Parts 1 and 4; the reference/lookup tools
(DXCC, CQ/ITU zones, CTCSS, Char lookup, Operating/Radio references) are browsers,
not forms — open them and scroll.

### Satellite & orbit

- **Location converter** — convert between lat/long, Maidenhead grid, and other
  position formats. Enter a position, read the equivalents.
- **State vector → GP** — turn an ECI state vector (position + velocity) into
  general-perturbation orbital elements you can track.
- **CubeSatSim C2C ref** — a reference screen for the AMSAT CubeSatSim "C2C"
  telemetry format.
- **Conjunction screener** — screens the catalog for close approaches (conjunctions)
  to a chosen object.
- **Orbital neighborhood** — lists what else is orbiting near a selected satellite.
- **Transponder planner** — plan uplink/downlink around a transponder's passband.
- **Link margin vs elevation** — shows how received signal margin changes across a
  pass as elevation (and thus range) varies.
- **Doppler budget (orbit)** — *Apogee alt* (km), *Perigee alt* (km), *Freq*
  (435.5 MHz). Seeds apogee/perigee from the **active satellite** if one is
  selected; shows the peak Doppler shift and rate to expect on that frequency.
- **Orbit lifetime (debris)** — *Alt(perigee)* (550 km), *Mass* (4 kg), *Area*
  (0.03 m²), *Drag Cd* (2.2). Estimates orbital decay lifetime from ballistic
  coefficient and altitude.
- **Delta-v (Hohmann/plane)** — *Alt 1* (400 km), *Alt 2* (800 km), *Plane chg*
  (0°). Hohmann-transfer and plane-change Δv between two circular orbits.
- **Pointing loss** — *HPBW* (30°), *Point err* (deg, **seeded from your rotator
  deadband**). The dB loss from an antenna mispointed by that error at that
  beamwidth.
- **Polarization / Faraday** — *Freq* (145.9 MHz), *Ionosphere* (picker). Estimates
  Faraday-rotation of a linear signal through the ionosphere.
- **Debris group screen** — browse a large CelesTrak debris group.

### Antennas & feedline

- **Dipole length** — *Freq* (14.2 MHz). Half-wave dipole leg and overall lengths.
- **Vertical / ground plane** — *Freq* (146 MHz). Quarter-wave vertical and radial
  lengths.
- **Yagi elements** — *Freq* (144.2 MHz), *Elements* (3). Element lengths and
  spacings for a Yagi of that size.
- **Quad (full-wave loop)** — *Freq* (50.1 MHz), *Elements* (2). Full-wave loop
  dimensions.
- **Helix antenna** — *Freq* (435 MHz), *Turns* (8), *Circumf* (1.05 wl), *Pitch*
  (12.5°). Axial-mode helix gain, dimensions, and beamwidth.
- **Coax loss / power** — *Cable* (picker, default LMR-400), *Freq* (146 MHz),
  *Length* (50 ft), *SWR at load* (1.5). Matched loss, total loss including SWR, and
  power delivered.
- **Phasing line / stub** — *Cable* (picker), *Freq* (146 MHz), *Length* (picker:
  ¼/½/… wave fraction). Physical length of a coax section for a wanted electrical
  length, using the cable's velocity factor.
- **Wavelength / frequency** — *Freq* (146 MHz). Free-space λ and the common
  quarter, half, and 5/8-wave cut lengths.
- **L/Pi/T match network** — *Topology* (picker), *R source* (50 Ω), *R load*
  (200 Ω), *Freq* (14.2 MHz), *Loaded Q* (5). Component values for an impedance-match
  network.
- **Microstrip/stripline Z0** — *Line* (picker), *Er* (4.4), *H sub/gap* (1.6 mm),
  *W trace* (3 mm), *Freq* (435 MHz). Characteristic impedance of a PCB trace.

### RF & measurement

- **Link budget** — a full uplink/downlink signal budget for a satellite path.
- **RF units (dBm/W/V)** — *Power* (100 W). Cross-converts power in W, dBm, dBW, and
  voltage into 50 Ω.
- **SWR / return loss** — *SWR* (2.0). Return loss, reflection coefficient, and
  mismatch loss for a given SWR.
- **Free-space path loss** — *Distance* (1000 km), *Freq* (145 MHz). The FSPL in dB.
- **Attenuator pad** — *Atten* (6 dB), *Z0* (50 Ω). Resistor values for pi- and
  T-topology resistive pads.
- **dB chain sum** — *Stage 1–4* (dB each; +gain, −loss). Sums a gain/loss chain.
- **Cascade NF & G/T** — *Ant gain* (16 dBi), *Sky temp* (150 K), *LNA NF* (0.8 dB),
  *LNA gain* (20 dB), *Coax loss* (3 dB), *Rig NF* (6 dB). Friis cascade for the
  antenna→LNA→coax→rig chain: system noise figure, noise temperature, and G/T.
- **Sun-noise G/T measure** — *Y-factor* (1 dB), *Solar flux* (sfu, **seeded from the
  last-fetched 10.7 cm index**), *Freq* (435 MHz), *Ant gain* (0 dBi). Turns a
  measured sun-noise Y-factor into a real G/T figure for your station.
- **IMD products** — *Freq 1* (145.900), *Freq 2* (145.950), *Band low* (145.800),
  *Band high* (146.000). Lists intermodulation products falling in-band.
- **RF exposure (MPE)** — *Freq* (146 MHz), *Power* (100 W), *Mode duty* (picker),
  *Ant gain* (2.15 dBi). Estimates the controlled/uncontrolled MPE safe distances.

### Electronics & power

- **Complex / polar** — *Real a* (50), *Imag b (j)* (25). Rectangular↔polar
  conversion for impedance and phasor work.
- **Reactance & resonance** — *Freq* (7 MHz), *Induct L* (10 µH), *Cap C* (100 pF).
  Shows Xl, Xc, and the resonant frequency of the L–C pair.
- **RC/RL time constant** — *Resist R* (1000 Ω), *Cap C* (1 µF). τ and the
  charge/discharge percentages at 1–5 τ.
- **Battery runtime** — *Capacity* (20 Ah), *RX draw* (0.5 A), *TX draw* (8 A), *TX
  duty* (30 %), *Usable* (80 %). Operating hours from a battery under a TX/RX duty
  cycle.
- **Cross-section area** — *Form factor* (picker, default 3U), *Body X/Y/Z* (cm),
  *Panel area* (m²). Average cross-sectional area for drag/thermal work.
- **Thermal equilibrium** — *Surface* (material picker), *Custom a* (0.25), *Custom
  e* (0.85). Equilibrium temperature of a surface in sunlight from absorptivity and
  emissivity.
- **Trace & wire ampacity** — *Mode* (picker: PCB trace or wire), *Current* (1 A),
  *Temp rise* (10 °C), *Copper* (1 oz), *Wire* (24 AWG). Safe current / required
  trace width.
- **Toroid winding** — *Core* (picker, default T50-2), *Target L* (10 µH). Turns
  needed on that core for the target inductance.
- **PLL / frequency plan** — *Reference* (10 MHz), *R divider* (1), *N divider* (40),
  *Multiplier* (1). Output frequency and step size of a PLL synthesizer plan.

### Terrestrial VHF/UHF/microwave

- **Radio horizon (VHF+)** — *My ant HAAT* (10 m), *Their ant HAAT* (10 m), *k factor*
  (1.33). Line-of-sight horizon for each station and the maximum LOS path; raise k
  above ~1.6 to model tropospheric ducting.
- **Fresnel zone clearance** — *Path length* (30 km), *Freq* (144 MHz). First Fresnel
  zone radius at the path midpoint and the 60% figure that terrain should clear.
- **Tropo ducting index** — *Surface temp* (from weather), *Dewpoint*, *Inversion dT*.
  A 0–6 ducting-likelihood index from humidity (dewpoint depression) and any
  temperature inversion. A "watch this" flag, not a forecast.
- **Rain fade (microwave)** — *Freq* (10 GHz), *Rain rate* (25 mm/h), *Path length*
  (10 km). ITU-style specific attenuation and total path fade for a microwave hop.
- **Terrestrial path budget** — *TX power*, *TX/RX ant gain*, *Line loss*, *Freq*,
  *Distance*. A two-way link budget: received level and margin over a nominal noise
  floor, with a workable/marginal verdict. Add tropo/terrain effects separately.
- **Terrain path profile** — *Freq* (for the Fresnel overlay). Set a target grid in
  **Grid dist/bearing** first, then press **f** here (WiFi required) to sample ground
  elevation along the great-circle path; shows the highest obstacle and whether the
  path clears it with Fresnel margin.

### References & lookups (browsers, not forms)

- **DXCC entity lookup** — find a DXCC entity by prefix/name.
- **CQ zones (WAZ)** / **ITU zones** — the world zone lists.
- **CTCSS tone reference** — the standard sub-audible tone table.
- **Operating references** / **Radio math reference** — quick operating and formula
  cards.
- **Char lookup (ASCII/RTTY)** — ASCII, and Baudot/RTTY character codes.
- **Unit converter** — *Value* (100). General unit cross-conversion.

## Part 3 — The games

Seven, under **About → `z` (Games)**. Use **`;`/`.`** to pick and ENTER to start.
They're deliberately small and satellite-themed; **Fn+b** screenshots any of them.

- **Zap the Sats** — a Space-Invaders homage where the invaders are satellites and
  you defend from below.
- **Doppler Lock** — hold your marker on a frequency that drifts along a Doppler-like
  S-curve; a tuning-skill trainer.
- **Catch the Pass** — time your move to intercept a satellite as it crosses.
- **Rotor Runner** — a genuine two-axis game: a satellite drifts around the sky and
  you slew an antenna to keep it centred, exactly like driving a real rotator.
- **Morse Meteors** — letters fall and you clear each by keying its Morse code (two
  keys for dit/dah); a code-practice game in disguise.
- **Grid Chase** — a Maidenhead grid-square trainer: a location hint appears and you
  pick the right grid.
- **KESSLER (2-player)** — the headline. A GORILLAS.BAS-style artillery duel on the
  Moon: two ground stations lob CubeSats over an Earth-with-a-shocked-face, cratering
  the lunar surface, with wind and selectable gravity (Moon/Mars/Earth).

### KESSLER in depth

**Local (hot-seat):** on the title screen pick the round count (`1`–`9`), cycle
gravity with **`g`**, and press **ENTER** to start. Each turn shows a compact aim
form on the *aiming player's* side of the screen — **`>`** marks the active field.
Type an **angle**, press ENTER to move to **velocity**, type it, press ENTER to
fire. **DEL** edits digits; **`,`/`/`** switch fields; **`;`/`.`** nudge a value.
Land a CubeSat on (or next to) the enemy base to score. A shot too slow to leave the
pad fizzles and craters in place; arcing one back onto your own roof loses the round.

**Over LoRa (two Cardputers):** press **`n`** on the title screen to host — you
become Player 1 and beacon an invite on your shared LoRa frequency. Another CardSat
running KESSLER joins automatically as Player 2. Both radios build the *same*
battlefield from a shared seed and simulate every shot identically, so the game
stays in sync while only tiny control packets cross the air. You aim only on your
turn; the other station shows "waiting for peer's shot." Range is your LoRa link's
range. (This needs two devices, obviously, and uses the same LoRa settings as the
messaging screen.)

---

## Part 4 — Tiny BASIC (language reference)

CardSat has a genuine line-numbered BASIC (Tools → Tiny BASIC) that can **read the
live satellite/GPS state** and **draw on the screen**. One firm rule: **there is no
`INPUT` and no `INKEY$`** — programs don't take interactive input, by design. System
data is read-only and snapshotted when you call for it.

### Editing and running

- Type a **numbered line** to store it: `10 PRINT "HELLO"`. Lines run in numeric
  order.
- Type an **unnumbered statement** to run it immediately.
- Re-enter a line number with new text to replace it; enter a bare number to delete
  that line.
- Programs are edited on-device; `LPRINT` and the file words below reach storage.

### Values and variables

- Numbers are floating-point. Variables are single letters `A`–`Z` (and letter+digit
  where supported).
- **`LET v = expr`** assigns; the `LET` is optional (`A = 5` works).
- **`DIM @(n)`** declares the numeric array `@()`, indexed `@(0)`…`@(n)`.

### Operators

- **Arithmetic:** `+  -  *  /`, unary minus, and `^` for power. Parentheses group.
- **Relational** (in `IF`): `=  <>  <  >  <=  >=`. A true comparison is non-zero.
- Standard precedence: power, then `* /`, then `+ -`, then comparisons.

### Statements

- **`PRINT` list** — print numbers/strings/expressions. Separators: **`;`** joins
  with no gap, **`,`** advances to the next column. A trailing `;` suppresses the
  newline. `PRINT` with nothing prints a blank line.
- **`REM` text** — a comment to end of line.
- **`IF cond THEN stmt`** (or `THEN linenum`) — run the statement / jump if the
  condition is non-zero.
- **`GOTO linenum`** — jump.
- **`GOSUB linenum` … `RETURN`** — call a subroutine and come back.
- **`ON expr GOTO n1,n2,…`** — computed jump (1 selects the first target, etc.).
- **`FOR v = a TO b [STEP s]` … `NEXT v`** — counted loop.
- **`READ v[,v…]` / `DATA n1,n2,…` / `RESTORE`** — read constants from `DATA`
  lines; `RESTORE` rewinds to the first.
- **`END`** — stop the program.

### Numeric functions

`ABS(x)`, `SGN(x)`, `INT(x)`, `SQR(x)`, `SIN(x)`, `COS(x)`, `TAN(x)`, `ATN(x)`,
`LOG(x)`, `EXP(x)`, `MIN(a,b)`, `MAX(a,b)`, `RND(n)` (random). Trig follows the
calculator convention.

### Output to the printer / files

- **`LPRINT` list** — send a line to the report sink (same destination the tools
  print to).
- **File I/O (enable in Settings first):** `FOPEN`, `FPRINT`, `FCLOSE`, and `FILES`
  to list. Gated so a program can't touch storage unless you allow it.

### Graphics

Drawing goes to the screen and the frame **holds after the program ends**, so you
can plot and look. Colour is an optional last argument (a small palette index);
coordinates are screen pixels.

- **`CLS`** — clear the drawing.
- **`PSET x, y [, colour]`** — set a pixel.
- **`LINE x1, y1, x2, y2 [, colour]`** — draw a line.
- **`CIRCLE x, y, r [, colour]`** — draw a circle.
- **`TEXT x, y, "string" [, colour]`** — draw text.
- **`SHOW`** — present the drawn frame (flush what you've plotted).

### The system bridge (what makes it CardSat's BASIC)

Each of these samples the live state at the moment it's called:

- **`SATSEL i`** — select catalog satellite *i* and run SGP4 for it (host-verified
  accurate). The read-outs below then refer to it. A *bad index* (`i` outside
  `0 .. NSAT-1`) stops the program, but a valid index whose satellite can't be
  propagated right now — no position/time fix, or a decayed / stale element set that
  SGP4 rejects — is not fatal: it sets `SATOK` to 0 and the program keeps running, so a
  catalogue scan can skip dead birds. The idiom is `SATSEL I : IF SATOK = 0 THEN GOTO
  <skip>` before reading any `SAT*` value.
- **`TXSEL i`** — select a transponder.
- **Selected-satellite position:** `SATAZ`, `SATEL` (az/el, degrees), `SATLAT`,
  `SATLON` (sub-point), `SATRANGE` (km), `SATRR` (range rate, km/s — feed it to the
  calculator's `dop()` for a Doppler shift).
- **Upcoming passes:** `PASSAOS(k)`, `PASSLOS(k)`, `PASSMAX(k)` — AOS time, LOS time,
  and max elevation of the *k*-th upcoming pass of the selected satellite.
- **Your station:** `GPSLAT`, `GPSLON`.
- **Space weather & propagation:** `SFI` (10.7 cm flux), `KP`, `AINDEX`, `SSN`
  (sunspot number), `FLARE` (0–5 for none/A/B/C/M/X), `BZ` (solar-wind IMF Bz, nT),
  `SWSPEED` (solar-wind speed, km/s), `MUF` (estimated MUF, MHz), and
  `FCKP1` / `FCKP2` / `FCKP3` (SWPC 3-day max-Kp forecast). Read `SPWXOK` first to
  know whether space-wx has been fetched (values read 0 when a feed is unavailable).
- **Pointing:** `MAGDECL` -- magnetic declination at your QTH in degrees (east
  positive), from the built-in IGRF model. Add it to a true bearing to get a magnetic
  compass heading. Approximate (a few degrees); requires your location to be set.

Because the data is read-only and snapshotted, BASIC can *observe* the sky and plot
it, but never drives the radio or rotator.

### Two worked examples

Print the next three passes of catalog satellite 0:

```
10 SATSEL 0
20 FOR K = 1 TO 3
30 PRINT "AOS "; PASSAOS(K); "  MAXEL "; PASSMAX(K)
40 NEXT K
```

Plot the selected satellite's position as a dot on a simple az/el field:

```
10 SATSEL 12
20 CLS
30 LINE 0,67,239,67 : REM horizon
40 X = SATAZ * 239 / 360
50 Y = 67 - SATEL * 67 / 90
60 CIRCLE X, Y, 3, 2
70 SHOW
```

## Where to go deeper

This guide is the tour. For field-by-field detail on any tool, the full graphing
calculator instrument set, BASIC coordinate/colour specifics, and the exact game
scoring, see the main **MANUAL.md** (Tools, Games, and BASIC chapters). The LoRa
KESSLER wire format is documented in `docs/interfaces/LORA_KESSLER_NETPLAY.md`.
