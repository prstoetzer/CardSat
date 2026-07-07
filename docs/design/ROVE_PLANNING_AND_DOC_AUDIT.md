# 0.9.51 scoping: documentation audit + OrbitDeck rove-planning tools

Two parts: (1) a documentation audit with concrete fixes, and (2) scoping the rove-planning
tools from OrbitDeck's "Planning" screen, judged against heap and flash cost on the
no-PSRAM Cardputer.

---

# Part 1 — Documentation audit

## FEATURES.md staleness (confirmed against source)

- **Tool count wrong in two places.** Line 229 says the Tools menu "carries **26 tools**";
  the actual `TOOLS_NAMES` count is now **30**. The bottom summary paragraph (around line
  410) predates the newest tools entirely -- it doesn't mention the ARRL radio-math tools,
  the orbit explorer/animations, or the reference screens (operating refs, CTCSS, radio
  math). It also still enumerates the tool list as if the 0.9.50 additions don't exist.
- **DXCC entity-count inconsistency.** Lines 160 and 326 say "**340 DXCC entities**"; the
  bottom summary (line 410) says the DXCC lookup has "**402 entities**." These describe two
  different things (the workable-DXCC reference-point set vs the lookup database), but a
  reader sees a contradiction. Needs one clarifying clause, not a silent number change.
- **Recommendation:** a consolidation pass -- update 26 -> 30; fold the 0.9.50 tools into
  (or delete) the redundant bottom summary paragraph since the detailed per-tool entries
  above already cover them; add a half-sentence distinguishing the two DXCC counts. This is
  pure text; no code. Do it as the first 0.9.51 commit so the rest of the release documents
  against a clean baseline.

## MANUAL.md / README.md

- Both are current for 0.9.50 features (verified: 75 + 15 image refs resolve; the 0.9.50
  additions were documented as text-only). No corrections needed beyond the standing
  **screenshot debt** (new 0.9.50 screens still lack images; carried, not blocking).
- If rove-planning ships in 0.9.51, it will need manual entries + a screenshot each.

## On-device docs

- Help/Learn/History are current as of 0.9.50. The "THE 2020s" history facts remain flagged
  for Paul's vetting (SO-50 2002; IO-117 ~5800 km "first amateur MEO digipeater"; Phase-3
  comparison; AO-91 status). Not doc-audit issues -- author verification.

---

# Part 2 — Rove-planning tools (from OrbitDeck "Planning")

## What OrbitDeck's Planning screen does

Goal-directed planning, four features: (a) **best time to work a target** -- pick a grid
square, US state, DXCC entity, or lat/lon and find the windows when you and the target are
under the same satellite footprint; (b) **visible (optical) passes** with magnitude estimate
and twilight filter; (c) **satellite-to-satellite** line-of-sight windows; (d) an
**element-set trust** panel (epoch age, drift estimate). All four export to CSV on desktop.

## The key realization: CardSat already has the engine

The rove planner's core -- "when do I and an arbitrary target point share a footprint" -- is
**exactly what CardSat's existing mutual-windows predictor already computes**. The signature
is `mutualWindows(from, const Observer& dx, minEl, out, maxN)`: the target is an `Observer`
(lat/lon/alt), not necessarily a real DX station. And the pieces to turn a rove target into
that Observer already exist: `Location::gridToLatLon()` is used in ~8 places; the Workable
screens already carry US-state and DXCC reference points. So a rove "work-a-grid" tool is
**mutual-windows pointed at a target-of-interest, with a target picker on the front** --
mostly new UI over an existing, tested compute path. That is what makes this cheap.

Likewise: (c) sat-to-sat already exists as its own screen (`SCR_SATSAT`, `SATSAT_MAX=16`).
So of OrbitDeck's four Planning features, **two are already present** in some form.

## Heap & memory analysis (the deciding lens)

The compute path is the concern, not the result storage:
- **Result storage is trivial.** `MutualWindow` is ~20 bytes; the existing `mutual[]` buffer
  is `MUTUAL_MAX=24` (~480 B) and is already allocated. A rove list reuses that exact buffer
  and struct -- **zero new heap**. A target-of-interest picker adds a few tens of bytes of
  state. Flash cost: a screen + picker, a few KB.
- **Compute cost is bounded and already proven.** Mutual-windows already runs on-device over
  a multi-day search; the rove planner runs the *same* search against a target point. No new
  large arrays, no per-frame allocation, no network. It is CPU-heavy (many SGP4 steps) but
  that is a *time* cost, not a *heap* cost -- and it is already incremental/jobbed
  (`satsatComputed` pattern) so the UI stays responsive. **Heap-flat.**
- **The one thing to NOT do:** don't try to pre-compute a full multi-target matrix (e.g.
  "best satellite for every US state at once") and hold it in RAM -- that would be a large
  array and is the desktop's job. Keep it one target at a time, reusing `mutual[]`.

**Verdict: the "best time to work a target" rove tool is heap-safe** because it reuses the
existing mutual-window struct, buffer, and jobbed-compute path. It is the highest-value,
lowest-risk piece to port.

## What to build (recommended), by cost

### A. Rove target planner -- **build (heap-flat, high value)**
"Best time to work a grid / state / DXCC / lat-lon." A target picker (reuse the DXCC-lookup
and grid-entry UI already on the device) feeds a target `Observer` into the existing
`mutualWindows()` call; results render in the existing mutual-window list style. Reuses the
`mutual[]` buffer and the jobbed-compute pattern. New surface is almost entirely UI. This is
the heart of "rove planning" and the single best addition.

### B. Grid/state/DXCC "next workable window" shortcut -- **build (small)**
The Workable screens already show what's in the footprint *now / next pass*. Add a "when
next?" action that runs (A) for the highlighted grid/state/entity -- i.e. wire the existing
Workable lists to the rove planner. Cheap once (A) exists; makes the feature discoverable
from where operators already look.

### C. Visible (optical) passes -- **consider (medium, self-contained)**
Sunlit-satellite + observer-in-darkness passes with a rough magnitude estimate. CardSat
already computes eclipse (cylindrical shadow) and pass geometry, so this is a filter over
existing pass prediction plus a twilight test -- heap-flat, but more genuinely new math than
A/B and less central to "rove planning." Build only if wanted; not required for roving.

### D. Element-set trust panel -- **already largely present; enhance if desired**
CardSat already surfaces GP-age warnings. OrbitDeck's panel adds an along-track drift
estimate. A small addition to an existing Orbit page rather than a new screen; heap-flat.
Optional.

## What NOT to port
- **Multi-target matrices / "best sat for every state at once"** -- large RAM, desktop's job.
- **CSV export of planning results** -- there's no comfortable file/serial export idiom for
  this on the device the way OrbitDeck writes files; skip unless a serial-dump is wanted.
- Sat-to-sat is already on the device; no re-port needed (enhance in place if anything).

## Suggested 0.9.51 shape
1. **Doc consolidation pass** (Part 1) -- first commit, pure text.
2. **A. Rove target planner** -- the core, heap-flat, reuses `mutualWindows()` + `mutual[]`.
3. **B. "Next workable window"** wired into the existing Workable screens -- small, high
   discoverability.
4. (Optional) **C. visible passes** and/or **D. drift estimate** if scope allows.

All recommended items are heap-flat: they reuse the existing mutual-window struct/buffer and
jobbed-compute path, add only static/UI code and a few tens of bytes of picker state, need
no network beyond existing fetches, and keep orbital quantities metric. The heavy lifting
(the co-visibility engine) is already written and tested on-device -- this is largely a new
front-end onto it, which is exactly why it fits the Cardputer's constraints.
