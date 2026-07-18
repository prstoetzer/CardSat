# Tools roadmap — candidate additions (0.9.59 planning)

> **STATUS: SHIPPED IN 0.9.59.** All twenty candidates in this document were built
> and merged in the 0.9.59 cycle (Tools hub 35 → 55). This file is kept as the design
> rationale and the record of what each tool is for; see the MANUAL "Tools" section for
> user docs, `RELEASE_NOTES_0.9.59.md` for the summary, and
> `docs/design/ORBITAL_MATH_AUDIT_0.9.59.md` for the math audit behind the satellite tools.


Written against the 0.9.59 tool set (35 entries) after the orbital-math audit.
Aim: a genuinely robust bench-and-field kit for **amateur satellite operating,
station and CubeSat construction, transponder work, and orbital-environment
awareness** — favoring closed-form math and tiny tables (flash is at ~85%;
each Tier-1 item below is estimated ≤ 3–6 KB of code, no new libraries).

A recurring theme: several of the highest-value tools are **integrations** —
CardSat already computes eclipse fraction, fetches solar flux, and carries an
SGP4 pair-propagator; tools that reuse those beat standalone calculators.

---

## Tier 1 — highest value, small, mostly reuse existing infrastructure

**1. Doppler budget calculator.** Inputs: orbit (circular alt, or apo/peri) and
frequency. Outputs: max Doppler shift (±kHz), max rate at TCA (Hz/s) for an
overhead pass, and the same for a mid-elevation pass. This is the number that
sizes tuning steps, loop cadence (`doppLeadMs` has to beat it), beacon
bandwidth, and FT4/FT8-on-satellite feasibility. Closed form from the audit's
own geometry (ω at altitude, worst rr = orbital velocity component). Pairs
naturally with the Orbital-analysis Doppler page (that shows *one real pass*;
this answers *design* questions for any orbit).

**2. Cascade noise figure & G/T (Friis).** Up to ~5 stages of NF+gain (coax as
a negative-gain stage seeded from the existing coax-loss tool), antenna gain,
sky-temperature presets per band → system NF, T_sys, G/T. Answers the eternal
"do I need the preamp at the antenna" with numbers. The single most-missing
station-engineering tool in the current set.

**3. Sun-noise G/T measurement helper.** The operator measures Y-factor (sun
vs. cold sky, in dB, off any S-meter or SDR); CardSat already **fetches the
live 10.7 cm solar flux** and **knows the sun's az/el** — so it can supply the
current solar flux density at the operating band (scaled from F10.7), beam-fill
correction from a beamwidth input, and output measured G/T. Turns the space-wx
fetch and the sun ephemeris into a station-validation instrument. (Reference:
the classic AMSAT/EME sun-noise procedure.)

**4. Helix antenna designer.** Circumference, pitch angle, turns → axial-mode
gain, beamwidth, impedance, boom length, and the peripheral-feed match note
(Kraus formulas). *The* satellite antenna, absent from the current
dipole/vertical/Yagi/quad set. Tiny.

**5. L / Pi / T matching-network designer.** R_source, R_load, frequency (and Q
for Pi/T) → component values, both topologies. The construction classic the
attenuator-pad and reactance tools gesture at but don't provide.

**6. Conjunction screener (two loaded objects).** Pick any two satellites from
the loaded GP set → propagate both (the pairwise SGP4 machinery already exists
in `temeStateAt`) over N hours on a coarse grid, refine minima → table of
closest approaches: time, miss distance, relative velocity. With the honest
caveat printed on-screen: public GP elements are km-class accurate — this is
**awareness, not collision avoidance**. Nothing else in a shirt pocket does
this. CPU-bound but fine (two propagations per step; a 6 h scan at 30 s is
1,440 pairs).

**7. Orbital neighborhood (altitude-band occupancy).** For the active
satellite: scan the loaded catalog for objects whose perigee–apogee band
overlaps the bird's, sorted by overlap and inclination proximity — "who shares
this shell." A one-screen debris-awareness view that costs one pass over the
elements already in RAM, no propagation. Natural on-ramp to the conjunction
screener (`ENTER` → screen the pair).

**8. Eclipse-aware power budget (CubeSat / portable solar).** CardSat already
computes **eclipse fraction per orbit** for any loaded satellite. Feed that
into a panel/battery sizing tool: panel watts, battery Wh, load in
sunlight/eclipse → margin, depth-of-discharge per orbit, days of autonomy.
For portable ops, the same tool with "eclipse fraction" = nighttime hours.
This is the existing battery-runtime tool grown into the tool CubeSat builders
and Field-Day rovers actually need.

**9. Pointing-loss calculator.** Beamwidth + pointing error → dB loss
(Gaussian-beam approximation), with a "your rotator deadband is X°" preset
pulled from the live rotator settings. Directly informs the deadband/beamwidth
trade the rotator section of the manual discusses in prose.

## Tier 2 — strong candidates, slightly more niche or slightly larger

**10. IMD & mixing-products finder.** Two (or three) carriers → 2f1−f2-family
products with order, flagged when they land inside a transponder passband or a
receiver IF. Serves both transponder operating (finding your own third-order in
an inverting passband) and construction (spur charts for an IF scheme).

**11. Transponder passband planner.** Transponder edges + inversion → a printed
crib table of dial pairs at N points across the passband (the printing system
is already there: this is a natural addition to the 29-report Print menu), plus
"given this downlink dial, where am I in the passband." The live loop already
does this in real time; this is the offline/coordination version.

**12. Microstrip / stripline impedance.** W/H/εr → Z0 and back. CubeSat RF
board work. Closed-form (Hammerstad-Jensen), tiny.

**13. Toroid winding calculator.** Turns for a target L from A_L, with a small
built-in table of the common Amidon FT/T cores (~1 KB of data). The
construction staple.

**14. Delta-v mini-set.** Hohmann between two circular altitudes, plane-change
cost, and deorbit burn estimate — three closed forms on one screen. Rounds out
the education story next to Orbit lifetime and the Explore sandbox.

**15. Flat-plate thermal equilibrium.** Absorptivity/emissivity presets
(black anodize, white paint, solar cell, bare aluminum) → equilibrium
temperature in sunlight and eclipse. Pairs with the eclipse tools for a
first-order CubeSat thermal sanity check.

**16. Polarization & Faraday estimator.** Linear↔circular mismatch table
(the famous 3 dB and its friends), cross-pol loss vs angle, and an
order-of-magnitude Faraday-rotation estimate at 145/435 MHz from the live
Kp/solar data — explains "why did the linear Yagi fade" with numbers.

**17. Link margin vs. elevation curve.** Extend the link-budget tool with an
orbit altitude → range(el) → FSPL(el) sweep drawn with the existing plotting
code: the margin curve across a pass, with the horizon-vs-TCA delta called out.

## Tier 3 — larger or more speculative

**18. Debris-group fetch mode.** A transient fetch of a CelesTrak group (e.g.
the major fragmentation clouds), band-filtered on device against the active
bird, screened pairwise, then discarded (the 150-sat resident cap stays
untouched). The full conjunction story without resident cost; needs careful RAM
staging like the existing GP fetch. *(Shipped using GP JSON, streamed and
parsed with the same allocation-free parser as the main catalog.)*

**19. PLL / frequency-plan helper.** Reference, dividers, multipliers → output
grid and spur candidates. Useful for homebrew transverters; niche enough to
wait for demand.

**20. Wire ampacity / PCB trace width.** IPC-2221 approximation. Trivial to
add; low glamour, occasionally exactly what's needed at the bench.

---

## Notes on fit

- **Flash:** all of Tier 1 together is plausibly ≤ 40 KB — well inside the
  ~460 KB app headroom at 0.9.59, and none of it needs a new library.
- **UI:** every item fits the established tool form (rows + `;`/`.`/type/ENTER)
  or the plotting frame the graphing calculator already owns.
- **Printing:** items 1, 2, 6, 8, 11, and 17 all produce natural one-page
  reports for the existing Print menu — cheap wins for the field notebook.
- **Ordering:** if only three are built next, the audit's recommendation is
  **2 (cascade NF/G/T), 1 (Doppler budget), 6 (conjunction screener)** — the
  first two are the most-asked station questions, the third is the signature
  capability nothing else pocket-sized has.
