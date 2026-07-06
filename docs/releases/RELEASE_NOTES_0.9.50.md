# CardSat v0.9.50 — release notes

This is a large release. It fixes AMSAT status reporting, makes a satellite's transponder
list far more useful, turns the link budget into a pass-planning aid, and adds a substantial
round of new tools — a much bigger Tools hub, an orbit explorer and animated orbit
explainer, and a set of radio-math calculators drawn from the ARRL *Radio Mathematics*
supplement. Upgrading from 0.9.49 is drop-in — no settings, log, or on-air format changes.

> **Note on screenshots:** the images in the manual and README were captured on an earlier
> build and do **not** yet show the new 0.9.50 screens (orbit explorer, orbit animations,
> the new Tools reference and form screens). The text is current; the pictures will catch
> up in a later round.

## AMSAT reporting: choose the transponder / mode

Reporting status for a multi-mode bird was stuck on a single option — AO-7, for example,
could only be reported on one mode. The cause was a catalog-parsing bug (the AMSAT API
pretty-prints its JSON with spaces the parser didn't expect), so the report picker fell back
to a single name. The parser is now whitespace-tolerant; the full catalog loads (~80 names)
and the report picker offers every mode a satellite has — **AO-7 as both U/V and V/A** — with
readable names. When a transponder is selected, the one-key "heard it" report auto-picks the
matching mode.

## Transponder list ordered by usefulness

A satellite's transponders (Satellites → `t`) are now ranked so the most workable entry is
first: **two-way transponders before one-way beacons**, **amateur-band before non-amateur**
(out-of-band TT&C and telemetry downlinks sink to the end), then active before inactive.
Transmitters SatNOGS marks **inactive** are **dimmed and tagged "(off)"**.

## Link budget from the live pass

The **Link budget** tool now opens pre-filled from the tracked satellite: **Distance** takes
the current **live slant range** (marked "(live)") and the frequency comes from the selected
transponder downlink. Press **`p`** to re-sync as the pass progresses; editing Distance by
hand drops the live tag.

## A much bigger Tools hub

The Tools menu grew from 20 to **30 tools**:

- **Antenna & RF:** phasing line / stub (velocity-factor-aware electrical length for
  circularly-polarized satellite antennas and matching stubs), wavelength / frequency,
  attenuator pad (pi / T), and a dB-chain sum.
- **Radio math (from the ARRL supplement):** a **complex / polar** impedance tool, a
  **reactance & resonance** tool (Xl / Xc / f0), and an **RC/RL time-constant** tool, plus a
  scrolling **Radio math reference** cheat sheet (dB table, AC voltage factors, constants,
  formulas).
- **References:** an **operating references** card (Q-codes / phonetics / RST) and a **CTCSS
  tone reference**.
- **Refinements:** the menu has **first-letter jump** and remembers your last tool; form
  tools now **remember their field values** between sessions (`x` resets one to defaults);
  antenna/feedline lengths can display in **metric or imperial** (Settings → Display) while
  orbital and satellite dimensions always stay metric; RF-exposure gained per-mode duty
  presets; and the character lookup adds a browsable full ASCII/Morse/Baudot table (shift+T).

## Orbit explorer and animations

The **Orbital-analysis** pager gains an **Explore** sandbox page: seed apogee, perigee and
inclination from the tracked satellite, edit them, and watch period, velocity, footprint,
longest pass, nodal drift and sun-sync status recompute live — without touching the real
elements. A new **Orbit animations** screen (Help → `o`) shows each orbit archetype — LEO,
polar / sun-sync, MEO, GEO, Molniya / HEO, and the amateur-MEO case — as an animated,
to-scale ellipse with a moving satellite and a caption on what it's used for.

## A more capable scientific calculator

The calculator gains amateur-radio helpers — `dbm()` / `w()`, `db()` / `undb()`, `wl()` /
`fq()` for wavelength, hyperbolic and rounding functions, and the constants `c`, `kB`, `Re`,
`mu`, `g0`. It now accepts **metric-prefix suffixes** on numbers (`100k`, `2.2n`, `146M` —
case-sensitive, `M` mega vs `m` milli) and has an **engineering-notation** display mode
(`\` toggles it; results shown as `4.7 k`-style).

## On-screen documentation

The on-device Help gained a Tools section and an orbit-animations entry; Learn gained a coax
/ velocity-factor explainer and a fuller circular-polarization note; and the amateur-satellite
history page gained a "2020s" section (FM workhorses, the linear-CubeSat wave, GreenCube /
IO-117, ARISS).

## Under the hood

The Tools menu index math is guarded by a compile-time check; the About screen's heap line
now also shows the largest allocatable block (useful on this no-PSRAM device); and two
build-time issues found on-device (a scope error and an Arduino `TWO_PI` macro collision in
the new orbit code) are fixed.

## Upgrading

Flash as usual. Everything new is additive; existing tracking, logging, and interfaces are
unchanged. See **[TEST_CHECKLIST_0.9.50.md](TEST_CHECKLIST_0.9.50.md)** for the on-device
verification list and **[BUGS_0.9.50.md](BUGS_0.9.50.md)** for the full itemized trail.
