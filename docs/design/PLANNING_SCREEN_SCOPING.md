# 0.9.51 scoping: rove pass-planning screen

The desired feature, restated: enter a **grid square, date, time, and a +/- window** (all
passes within X hours of the entered time); for **all favorite satellites**, list each pass
with **AOS, LOS, max elevation, and the number of workable US states and DXCC entities**
during that pass. Select a pass to see a **polar plot** plus the workable-state and
workable-DXCC counts for that pass. This is a *from-a-hypothetical-location, all-favorites*
survey -- distinct from the live single-sat tracking screens.

Heap/flash lens throughout: no-PSRAM Cardputer; reuse existing buffers; no per-frame alloc.

## The good news: almost every primitive already exists

- **Pass prediction:** `pred.predictPasses(from, minEl, out, maxN)` already returns AOS/LOS/
  max-el passes for a satellite from the current site.
- **Workable-state / workable-DXCC counting:** `buildStates(a,b)` and `buildDxcc(a,b)`
  already sample a satellite's footprint across a time window with a point-in-polygon test
  and tally the result into **bitsets** (`stateBits[7]` = 7 bytes, `dxccBits[43]` = 43
  bytes). Counting workable entities for a pass is exactly "call these over [AOS,LOS]."
- **Polar plot:** `drawPolarGrid()` + `drawPolarArc()` already render an az/el pass arc
  (used by the pass-detail screen). Reusable as-is.
- **Grid -> lat/lon:** `Location::gridToLatLon()` exists and is widely used.
- **Favorites:** the favorites list (`favN`) is already iterated by the "next passes (favs)"
  and "sky at a glance" screens.

So this feature is mostly **orchestration of existing, tested pieces** behind a new input
form and results list -- which is why it can fit the device.

## The two real changes needed

1. **Predict from an ENTERED site, not the live GPS site.** `predictPasses`, `buildStates`
   and `buildDxcc` currently call `pred.setSite(loc.obs())` internally (the live site). The
   planner must run them against a **temporary Observer built from the entered grid**. Two
   clean options:
   - (a) add an overload / parameter so these take an explicit `Observer` (cleanest), or
   - (b) save the live site, `pred.setSite(enteredObs)`, run the survey, restore. Simple and
     localized, but must guarantee restore on every exit path (including aborts).
   Recommend (a) for `buildStates/buildDxcc` (small signature change, they already take the
   time window) and (b)-style save/restore only if a is too invasive. Either is heap-flat.

2. **Predict from an ENTERED time, not now.** `predictPasses(from, ...)` already takes a
   `from` time, so this is just passing `enteredTime - window` as the start and stopping at
   `enteredTime + window`. No engine change -- only the caller's bounds.

## Cost analysis (heap, RAM, CPU)

- **Result storage:** the per-pass rows are small. A row = {satIdx, AOS, LOS, maxEl,
  stateCount, dxccCount} ~ 20 bytes. Cap the list (e.g. `PLAN_MAX = 24-32` passes across all
  favorites in the window) -> ~640-768 bytes, a fixed static array. The two bitsets (7 + 43
  bytes) are reused per pass, not stored per pass. **Tens to hundreds of bytes total, static
  -- heap-flat.** No new large arrays.
- **CPU:** this is the honest cost. For each favorite, predict passes in the window, then for
  each pass run the footprint sampler over [AOS,LOS] (buildStates samples up to 90 points;
  buildDxcc similar). With N favorites x a few passes each x ~tens of SGP4+PIP samples, this
  is **seconds of compute**, not milliseconds. It must therefore be a **jobbed/incremental
  build** like the existing sat-to-sat screen (`satsatComputed` pattern: compute a chunk per
  frame, show a progress line, stay responsive). Not a heap risk -- a UX/timing design point.
- **Precompute discipline:** compute state/DXCC counts **once per pass at build time** and
  store just the two integers in the row. Do NOT recompute on every draw. The polar plot and
  the two counts for the *selected* pass are already stored (or a single re-run on select).

**Verdict: heap-flat and RAM-cheap.** The feature's cost is CPU time, which is managed by
making the survey a background/jobbed compute with a progress indicator -- a pattern the
codebase already uses. No PSRAM pressure, no fragmentation risk, no per-frame allocation.

## Where to put it (the placement problem)

The home menu is a **fixed 10x2 grid of exactly 20 items**, locked by
`static_assert(... == 20)` and hardwired layout math (`col = i/10`, 10 rows). A 21st
top-level item would force scrolling or a 3rd column and break the "everything visible at
once" design. Options, best first:

- **(1) Sub-screen under "Next Passes (favs)" [RECOMMENDED].** This planner *is* a
  passes-for-all-favorites view -- just from a chosen place/time instead of here/now. Add a
  key on that screen (e.g. `p` = "plan from grid/time") that opens the planner with the
  entered-site form. Zero new home slots; lives exactly where an operator already thinks
  about favorite passes. Most discoverable, most logical home.
- **(2) A Tools entry.** Tools already holds 30 items and scrolls, so there's room. But Tools
  is "calculators/references," and this is a core operating feature -- a slight mismatch, and
  less discoverable for a headline capability.
- **(3) Replace/rename a home slot.** e.g. fold it into a combined passes entry. Riskier --
  touches the locked grid and muscle memory.
- **(4) A "Planning" sub-menu** if more planning tools are coming (rove target planner from
  the OrbitDeck scope, visible passes, etc.). Reached from "Next Passes (favs)" or Tools;
  becomes the home for a small family of planning features without a home slot. Good if the
  roadmap has 2+ planning tools; overkill for just this one.

**Recommendation:** ship it as **(1)** -- a sub-screen off "Next Passes (favs)" -- now; if
the rove target planner and/or visible passes follow, promote to **(4)** a small Planning
sub-menu reached from the same place. Neither consumes a home slot.

## Suggested screen shape

- **Input form:** Grid (maidenhead entry, default = current grid), Date, Time (default =
  now), +/- window hours (default 3). A "compute" action starts the jobbed survey.
- **Results list:** one row per pass across all favorites, sorted by AOS:
  `NAME  AOS-LOS  maxEl  Sxx Dyy` (states / DXCC counts). Progress line while building.
- **Pass detail (on select):** the polar plot (reuse drawPolarArc) + AOS/LOS/maxEl + the two
  workable counts, optionally with a key to open the full workable-states / workable-DXCC
  lists for that pass (reuse the existing drawStates/drawDxcc against the pass window+site).

## What to confirm before building
- Whether `buildStates/buildDxcc` should gain an explicit-Observer overload (option 1a) --
  affects a couple of call sites; the cleanest route and worth doing.
- `PLAN_MAX` cap (how many passes to list across all favorites in the window) -- sets the
  static array size; 24-32 is ample for a +/- few-hour window.
- Default +/- window and whether date/time entry reuses the existing edit-field idiom.

All of it is heap-flat and reuses tested engines; the only genuine engineering is the
jobbed-compute orchestration and the entered-site plumbing. Placement is solved without a
home slot by hanging it off "Next Passes (favs)."
