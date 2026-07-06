# Scoping: Tools enhancements from the ARRL Radio Mathematics supplement

**Source:** ARRL Handbook 2023 "Radio Mathematics" supplement (Metric prefixes,
notation, decibels, coordinate systems, exponentials/time-constants, complex numbers,
vectors/phasors, Boolean algebra, plus a "Radio Math Cheat Sheet").
**Status:** scoping only -- no code written. Heap lens as always: flash is cheap, heap is
scarce; every suggestion below is math-into-text or an evaluator addition, so all are
heap-flat.

## What CardSat already covers (do NOT duplicate)

The supplement's core topics are largely already served:
- **Decibels** -- calculator now has `db()/undb()/dbm()/w()`; RF-units tool does dBm/W/V;
  link budget does the full chain.
- **Frequency <-> wavelength** -- calculator `wl()/fq()`, plus the new Wavelength tool.
- **Metric prefixes / unit conversion** -- Unit converter (ft/m, in/cm, kg/lb, C/F, mi/km).
- **SWR / return loss / line loss** -- SWR tool + coax-loss tool.
- **Path loss, EIRP** -- FSPL tool + link budget.
- **Trig** -- calculator has sin/cos/tan/asin/acos/atan (degrees), sinh/cosh/tanh.
- **Exponentials** -- calculator `exp/ln/log`.

So the gaps are the supplement topics with **no** on-device support yet, chosen for
genuine amateur/bench usefulness (not math for its own sake).

## Recommended additions

### A. Calculator: engineering-notation output + metric-prefix input -- **build**
The supplement spends its first two sections on metric prefixes and engineering notation
(exponent a multiple of 3). CardSat's calculator prints a plain double. Two cheap wins,
both evaluator-local:
- **Accept metric-prefix suffixes on number literals**: `100k`, `2.2n`, `47p`, `146M`.
  A small addition to the number-parse path (after `strtod`, consume an optional
  p/n/u/m/k/M/G and scale). Turns the calculator into something you can type real
  component values into. Watch: `M`=mega vs `m`=milli case-sensitivity (the supplement
  stresses this) -- the parser is already case-sensitive so this is natural.
- **Engineering-notation display toggle**: show results as `4.7 kO`-style mantissa x
  prefix (exponent snapped to multiples of 3), toggled like the new hint-page key. Pure
  formatting on the existing result string. Highest-value calculator change from the
  whole supplement.

### B. Calculator: reciprocal/parallel and a couple of named helpers -- **build (small)**
- **`par(a,b)`**-style is impossible with the single-arg `callFn`, but the supplement's
  "two components in parallel" (a*b/(a+b)) and "reactance" (Xc=1/(2*pi*f*C), Xl=2*pi*f*L)
  are exactly the bench math hams reach for. These need a **2-argument function** path,
  which the evaluator doesn't have yet. Options: (i) add a minimal 2-arg dispatch
  (`fn(x,y)` parse) -- modest evaluator work, unlocks par/xc/xl/hypot/atan2; or (ii) skip
  functions and add a small **"Reactance & resonance" form tool** instead (below, D),
  which is friendlier than typing `xc(...)`. Recommend (ii) for reactance and a 2-arg
  path only if you want `par()`/`hypot()` in-line.

### C. Complex / polar-rectangular tool -- **build (new form or calc mode)**
The supplement devotes whole sections to complex numbers, polar<->rectangular, and
phasors -- central to impedance work and **not** covered anywhere on the device. A small
tool:
- Input a + jb -> show r and theta (r=sqrt(a^2+b^2), theta=atan2(b,a)); and the reverse
  r/theta -> a + jb. Add series (add rectangular) and parallel (Z1*Z2/(Z1+Z2)) of two
  impedances as a second page. This is the single most-requested piece of "radio math"
  the device lacks. Form-tool shaped; heap-flat. The calculator can't do complex results
  (it returns one double), so a dedicated tool is the right home.

### D. Reactance & resonance form tool -- **build**
Xc = 1/(2*pi*f*C), Xl = 2*pi*f*L, and resonance f = 1/(2*pi*sqrt(LC)). Pervasive bench
math, not on the device. A 3-field form (pick which to solve for, or show all three from
f/L/C). Pairs naturally with the complex tool and the existing antenna workbench.

### E. RC/RL time-constant tool -- **build (small)**
The supplement's exponential section is almost entirely capacitor/inductor time
constants. tau = RC (or L/R), plus the charge/discharge percentages at 1..5 tau
(63/86/95/98/99 %). A tiny form: enter R and C (or L), show tau and the 1-5 tau voltages.
Cheap, self-contained, genuinely used.

### F. Cheat-sheet reference screen -- **build (reference, like Q-codes/CTCSS)**
The supplement ends with a "Radio Math Cheat Sheet" (constants, dB-in-your-head table,
AC RMS/peak/pk-pk/average factors, length conversions, trig identities, PEP). A static
scrolling reference screen -- same pattern as the new Operating references / CTCSS
screens, PROGMEM text, ~zero heap. Especially useful: the **dB-ratio table** (0.5=-3,
2=+3, 4=+6, 10=+10) and the **AC conversion factors** (RMS=0.707 peak, PEP from Vpk),
which come up constantly and aren't anywhere on the device.

## Explicitly NOT recommended
- **Boolean algebra / truth tables, log-log graph plotting, radiation-pattern grids** --
  interesting in the supplement but outside CardSat's operating mission and (for the
  graph/pattern plots) not worth the screen real estate.
- Re-implementing anything in "already covered" above.

## Suggested shape / priority
1. **Engineering-notation + prefix input** on the calculator (A) -- biggest reach, small.
2. **Cheat-sheet reference screen** (F) -- cheap, high daily value, matches existing idiom.
3. **Complex / polar-rectangular tool** (C) -- fills the clearest capability gap.
4. **Reactance & resonance** (D) and **time-constant** (E) form tools -- classic bench math.
5. 2-arg calculator functions (B) only if inline `par()/hypot()` is wanted after (C/D).

All are heap-flat (math->text or evaluator-local), follow the existing form-tool /
reference-screen idioms, keep orbital/satellite quantities metric, and add only
static/PROGMEM data plus small code. None require new framebuffers or network.
