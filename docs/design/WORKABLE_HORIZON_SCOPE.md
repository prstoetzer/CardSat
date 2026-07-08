# Scope: 10-day workable horizon (ever-workable DXCC / states / grids)

**Status: design scope for 0.9.52 — not yet implemented.** Target: M5Stack Cardputer ADV
(ESP32-S3, **no PSRAM**). This feature hangs off the **Next Passes (favorites)** screen and
answers a single question: *over the next ~10 days, across one or more of my favorite
satellites, how many — and which — DXCC entities, US states, and grid squares are **ever**
workable at least once?*

It is deliberately a **union over time and satellites**, not a per-pass tally. A grid that is
reachable on any pass of any selected satellite in the window counts once. The output is the
aggregate reach of your station's satellite fleet over the planning horizon.

---

## 1. Why this is different from what already exists

CardSat already has three per-pass counters — `countWorkableStates()`, `countWorkableDxcc()`,
`countWorkableGrids()` — used by the rove planner (0.9.51). Each fills a bitset for **one
satellite over one AOS→LOS window** and returns a *count*, discarding the "which." The rove
planner calls them per pass and stores per-pass numbers.

This feature needs the opposite accumulation pattern: **one persistent union bitset per
category**, OR-accumulated across **every pass of every selected favorite over 10 days**, then
counted and (optionally) listed at the end. The primitives that set the bits
(`addFootprintStates/Dxcc/Grids`) are exactly what we reuse; what changes is that we never clear
the union between passes — we let it grow.

The important consequence for memory: **a union is free.** OR-ing a thousand passes into a
bitset costs the same RAM as one pass. The cost is entirely **CPU** (many passes × footprint
sampling), which is why the whole thing must be jobbed with a progress bar rather than run
synchronously.

---

## 2. Memory model (the crux — no PSRAM)

The union sets are small and fixed. Total persistent footprint for the whole 10-day, whole-fleet
union:

| Set | Representation | Size | Notes |
|---|---|---|---|
| States | `unionStateBits[7]` (.bss) | 7 B | 56 bits; existing `STATE_N ≤ 56` |
| DXCC | `unionDxccBits[43]` (.bss) | 43 B | 340 bits; existing `DXCC_N = 340` |
| Grids | `unionGridBits` (heap) | 4050 B | 1 bit per 4-char grid (32400); reuse `GRID_BITS_LEN` |
| **Total** | | **~4.1 KB** | independent of pass count or favorite count |

**Design rule — one grid bitset on the heap at a time.** The existing
`countWorkableGrids()` allocates the shared `gridBits` (4050 B) via `ensureGridBits()` and, per
the rove-planner postmortem, that block **must be freed at the end of the survey** or it
fragments the no-PSRAM heap and can later starve a WiFi/TLS download. This feature has the same
hazard, made worse because we hold the union *for the duration of the sweep*. Two options:

- **(A, preferred) Reuse the existing `gridBits` block as the union.** Allocate it once at
  survey start via `ensureGridBits()`, `memset` it to zero **once** (not per pass — that is the
  whole point), OR each pass's grids into it directly, and **free it when the sweep completes or
  is cancelled** (mirroring the rove planner's `if (gridBits) { free(gridBits); gridBits = nullptr; }`).
  While the sweep runs, `gridBuiltMs = 0` invalidates the live-grid view so the shared block is
  never shown as a stale union — same guard the per-pass counter already uses.
- (B) A separate `unionGridBits` malloc. Rejected: a *second* 4 KB block doubles the
  fragmentation pressure for no benefit, since we never need the live-grid set and the union set
  simultaneously.

State/DXCC union bitsets are tiny and live in **.bss** (no malloc), so they add nothing to heap
pressure. They must be **saved/restored** around the sweep only if the sweep can be interrupted
by a screen that uses the shared `stateBits`/`dxccBits` scratch — but since the sweep owns the
screen while running (see §4), we instead just **zero them once at start** and treat them as the
union directly, then leave them consistent on exit. (The per-pass `countWorkable*` helpers
already save/restore the small scratch, so if any are called mid-sweep they won't corrupt our
union — but the sweep should call the `addFootprint*` primitives **directly** into the union,
not the counting wrappers, to avoid redundant clears.)

**No per-pass, per-frame, or per-favorite allocation.** The pass buffer is a small fixed
stack array (`PassPredict pp[6]`, as in the rove planner). Nothing in the inner loop allocates.

---

## 3. Compute model & cost

For each selected favorite, over `[now, now + HORIZON_DAYS]`:

1. `predictPasses()` in chunks (the predictor returns up to N passes per call; page through the
   window as the rove planner does with a `from`/`to` bound).
2. For each pass, sample the ground track at a coarse cadence (reuse the existing
   `countWorkable*` sampling: 1 sample/minute, capped at 90 samples/pass) and OR each sample's
   footprint into the three union sets via `addFootprintStates/Dxcc/Grids`.

**Cost estimate.** A LEO favorite yields very roughly 5–7 passes/day → ~50–70 passes over 10
days. With, say, 10 favorites that is ~500–700 passes. Each pass samples up to ~90 footprint
evaluations, and the grid footprint fill is the heaviest primitive (thousands of grid cells
ray-tested). This is the same primitive the rove planner runs per pass, but here multiplied by
the full 10-day horizon — **plausibly tens of seconds to a few minutes**. That is acceptable
*only* with a progress bar and cancellable jobbing; it must never block the UI. (Grid footprint
fill dominates; see §6 for a "states/DXCC only, skip grids" fast mode.)

**Jobbing granularity.** The rove planner jobs **one favorite per `loop()`**. That is too coarse
here — a single favorite is ~50–70 passes of grid fills and could stall a `loop()` for seconds.
Job at **one pass per `loop()`** (or a small fixed batch), maintaining a cursor of
`(favIndex, passIndexWithinFav, passWindowCursor)`. Each `loop()` tick: advance the cursor,
process one pass's footprint sampling into the union, update the progress counter, request a
redraw at the existing throttled cadence (~500 ms), and return. This keeps the device
responsive and the watchdog happy.

---

## 4. UX flow

**Entry.** Next Passes (favorites) — this is **`SCR_SCHEDULE`**, handled by `keySchedule()`.
Add a new key (proposed **`w`** for "workable horizon"). Confirmed against `keySchedule`, the
bound keys today are `` ` ``/DEL (back), up/down (select), `r` (recompute), `z` (deep-sleep to
AOS), `m` (world map), `t` (sky-at-a-glance), `p` (rove planner), and ENTER (track) — so **`w`
is free**. Because the home menu is a locked 10×2 grid, this must hang off `SCR_SCHEDULE`, not
add a home slot (same rationale the rove planner used for `p`).

**Scope selection.** Before running, ask *which* satellites:
- All favorites (default), or
- A single satellite (the one currently selected in the Next Passes list).

Keep this to a one-line prompt or a tiny two-option pick — not a new multi-select screen
(multi-select is a possible later refinement; see §6). This directly satisfies the user's "one
or more satellites" requirement without heavyweight UI.

**Progress screen (new, `SCR_WORKHZN` or similar).** While the sweep runs, show a **progress
bar** plus live text:
- A determinate bar driven by `passesDone / passesTotalEstimate`. `passesTotalEstimate` is
  computed cheaply up front by counting passes per favorite with a first `predictPasses` pass
  over the window (AOS timestamps only, no footprint work), so the bar is honest rather than a
  spinner.
- Running text: `Sat 3/10 · pass 27/~58 · states 41 dxcc 96 grids 812` so the operator watches
  the union grow in real time.
- A **cancel** key (`` ` ``/DEL) that stops cleanly, frees the grid block, and returns to Next
  Passes.

**Completion.** When the cursor exhausts all selected favorites:
- Show a clear **"Done"** state (bar full, a "complete" line) — the user explicitly asked for a
  clean finished signal, distinct from mid-run.
- Present the three totals prominently: **DXCC N · States N · Grids N**, labelled as "ever
  workable, next _D_ days, _K_ satellites."
- Offer drill-down keys to list **which**: `s` states, `d` DXCC, `g` grids — reusing the
  existing workable-list renderers, but reading from the **union** bitset instead of a
  single-pass bitset. (Grids: given 800–3000+ entries are plausible, the list must be the
  existing scrollable/paged grid viewer; a raw count is the headline, the list is on demand.)
- Offer **`w` export** to a timestamped text file under `/CardSat/` (e.g.
  `workable_YYYYMMDD_HHMM.txt`): header (horizon, satellite set, totals) then the state list,
  DXCC list, and — because the grid list can be large — the grid list or its count per the
  same policy the rove-plan export uses. Downloadable via the 0.9.51 web Files page.

---

## 5. State variables & screens (proposed)

New `SCR_WORKHZN` (progress + result share the screen; a phase flag distinguishes running vs
done). New members (all fixed-size, mostly .bss):

```
enum { WH_IDLE, WH_COUNTING, WH_RUNNING, WH_DONE, WH_CANCEL } whPhase;
uint8_t  whUnionState[7];         // .bss union (states)
uint8_t  whUnionDxcc[43];         // .bss union (DXCC)
// grid union reuses the shared gridBits heap block (see §2 option A)
int      whFavCursor;             // which favorite (index into favs)
int      whPassCursor;            // pass # within current favorite's window
time_t   whWindowFrom, whWindowTo;// per-favorite predictPasses paging cursor
int      whPassesDone, whPassesTotal; // progress-bar numerator/denominator
int      whStateN, whDxccN, whGridN;  // live counts (popcount of unions)
bool     whAllFavs;               // scope: all favorites vs single sat
uint32_t whSingleNorad;           // when !whAllFavs
uint32_t whLastDrawMs;            // redraw throttle
```

Reused, unchanged: `predictPasses`, `addFootprintStates/Dxcc/Grids`, `ensureGridBits`,
`GRID_BITS_LEN`, the workable-list draw routines (parameterized to read the union bitset), the
grid-index↔locator conversion, and the DXCC/state name tables for listing "which."

**Dual-apply reminder:** every new function body and the `SCR_WORKHZN` dispatch cases must be
mirrored byte-identical into `CardSat.ino`; the enum addition, any `static_assert` on row/array
sizes, and the key-dispatch switch all need parity + balance + mirror checks (watch the
consumed-signature trap on inserts).

---

## 6. Deliberate scope boundaries (and future refinements)

**In scope for 0.9.52:**
- Union over 10 days × {all favorites | one selected sat}.
- Determinate progress bar, live growing counts, clean cancel, clean done.
- Headline totals + on-demand "which" lists (states/DXCC/grids) + text export.
- Strict heap discipline: single reused 4 KB grid block, freed on done/cancel; everything else
  .bss; no per-pass/per-frame allocation.

**Explicitly out of scope (call out to the user):**
- **Arbitrary multi-select** of a subset of favorites (beyond all-vs-one). A checkbox screen is
  a clean later addition; the union engine already supports "one or more," so this is purely UI.
- **Configurable horizon** other than the default 10 days. Make `HORIZON_DAYS` a constant now;
  a Settings knob can come later.
- **"When" information** (first date each entity becomes workable). The union discards timing by
  design; adding per-entity first-AOS would need per-entity storage (340 DXCC × 4 B, 32400 grids
  × … — the grid case is prohibitive on this heap), so it is a non-goal.
- **Per-satellite attribution** (which sat contributes which grid). Same storage objection as
  timing; the headline is fleet reach.

**Performance escape hatch.** Grid footprint fill dominates cost. Offer a fast mode — **states +
DXCC only, skip grids** — selectable at entry, which drops the heaviest primitive and the 4 KB
allocation entirely (states/DXCC unions are .bss-only). This also gives users on the largest
favorite lists a bounded-time option. Recommend defaulting to "all three" but surfacing the
fast mode in the entry prompt.

---

## 7. Open questions for Paul

1. **Entry key** on Next Passes (`SCR_SCHEDULE`) — **`w`** is confirmed free (bound today:
   back, up/down, `r`, `z`, `m`, `t`, `p`, ENTER). Good to use `w`, or prefer another letter?
2. **Horizon** — fix at 10 days to match "10-day pass overview," or align to whatever
   `predictPasses` horizon the Next Passes list itself uses?
3. **Grid list on completion** — full scrollable list, or count-only headline with export-only
   detail (given lists of 1000–3000+ grids)? The rove-plan export chose count-only for grids.
4. **Fast mode default** — should "skip grids" be the default for large favorite lists, or always
   opt-in?
5. Should completion offer a **one-key "save as a note"** in addition to the text export, so it
   shows up in the Notes screen?

---

## 8. Verification plan (host, before device)

- **Union correctness**: OR-ing N per-pass bitsets equals the set-union of their individual
  workable sets (property test against the existing per-pass counters on a few known passes).
- **Popcount**: union counts match a brute-force enumeration on a small synthetic footprint set.
- **Heap flatness**: instrument free-heap/largest-block at survey start, mid, and end; assert the
  4 KB grid block is the *only* growth and that it is returned on both done and cancel paths
  (largest-block returns to baseline — the rove-planner fragmentation lesson).
- **Progress monotonicity**: `whPassesDone` never exceeds `whPassesTotal`; bar reaches 100% exactly
  at `WH_DONE`.
- **Cancel path**: cancelling mid-sweep frees the block, restores `gridBuiltMs`/live-grid view,
  and leaves state/DXCC scratch consistent.
- Then the usual gate: `balance.py` 0, `parity.py` green, dispatch-case audit, mirror-identical
  bodies in `CardSat.ino`, `static_assert`s, and an on-device timing pass to confirm the sweep
  feels responsive and the bar tracks honestly.
