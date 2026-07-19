# Menu-order audit — 0.9.60

Every user-facing menu reviewed for logical consistency; reordered only where a
band was genuinely broken. Verdicts:

**Home (20, two-column grid)** — already deliberately curated (column 1 = sky &
birds, column 2 = station & system), with one stray each way: **Overhead now**
sat at the top of the utility column, and the sky column carried terrestrial
**Weather**. Swapped: Overhead now sits beside World Map (both answer "where
are the birds right now"), Weather opens the station column (it is a
ground-site concern). Activations + AMSAT status stay adjacent. Only four
`keyHome` cases renumbered; letter-jump order unaffected for first hits.

**Tools (54)** — canonical order is accretion order, and form ids, print stems,
and `toolFormInit` all key off it, so the fix is a pure **display permutation**
(`TOOLS_ORDER[]`): calculators & code / satellite & orbit / antennas & feedline
/ RF & measurement / electronics & power / references & lookups. No id moved;
persistence, printing, and the form tables are untouched. New tools append to
`TOOLS_NAMES` as always and slot into a band in `TOOLS_ORDER`.

**Settings (6 categories)** — category order (Radio, Rotor, Pass, Display, Log,
Net) is sound. Within categories, five surgical regroupings, row ids unchanged
and verified as the same set: rigctld host/port join Radio's transport band;
Rotor gains a clean wire → network → motion-shaping (ranges, offsets, deadband,
lookahead, park, pre-point) → manual sequence; Pass puts both prediction
parameters ahead of the visible-pass trio and the alerts; Display orders
screen → audio → units → input/games; Net moves the AMSAT status window beside
the other data sources and lets printer *transport* lead the printer block.
**Log** was already coherent (identity → LoTW → Cloudlog → file gates) — left
alone.

**Games (7)** — chronological, KESSLER last; fine. **Help topics** — fine.
**Print menu (29)** — not reordered this pass; flagged for a look when it next
grows.
