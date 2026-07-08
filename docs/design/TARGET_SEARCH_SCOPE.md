# Scope: Target search — "when is *this* state / DXCC / grid workable?"

**Status: design scope for 0.9.52 — not yet implemented.** Target: M5Stack Cardputer ADV
(ESP32-S3, **no PSRAM**). Hangs off **Next Passes (favorites)** (`SCR_SCHEDULE`, `keySchedule`).

The user picks **one target** — a US state, a DXCC entity, or a Maidenhead grid — and CardSat
finds **every pass, on any favorite satellite, over the next ~10 days, during which that target
is workable**, and lists those passes with their times and satellites.

This is the **inverse** of the "workable horizon" feature (`WORKABLE_HORIZON_SCOPE.md`). That one
asks *"what is ever workable?"* and returns a **union with the timing discarded**. This one fixes
the target and asks *"when is it workable?"* — so it must **retain per-pass timing** and report
specific pass windows. That single difference drives a different data model (a small result list,
not a union bitset) and a much cheaper inner test (one known target vs. a footprint, not filling
a whole bitset).

---

## 1. What already exists to build on

- **Footprint membership primitives**: `addFootprintStates/Dxcc/Grids(subLat, subLon, altKm)`
  fill a bitset of everything in a sub-point's footprint. For a *single* known target we don't
  need the whole bitset — we invert these into a one-target test (see §3).
- **The spherical-cap test itself** is already a one-liner inside those functions:
  `A + B*cos((lon - subLon)*D2R) >= coslam` decides whether a point is inside the footprint,
  where `coslam = Re/(Re+altKm)`. A **grid** target reduces to exactly this (a grid is one point).
- **A searchable DXCC picker**: `SCR_DXLK` with `dxQuery` + `dxRunFilter()` already does
  case-insensitive prefix/name/code matching over the `DXCC_LK[]` table (`dxcc_lookup.h`). This is
  the model — and largely the code — for the target-selection UI for DXCC.
- **State table**: `STATE_CODE[]` — 51 US entities as 2-char codes (`STATE_N = 51`), with polygon
  data (`STATEPOLY_*`, `statePipAt`) for membership.
- **Grid**: `Location::gridToLatLon()` validates a locator and yields its lat/lon; `gridIdx()`
  maps lat/lon to the grid-bit index.
- **Pass prediction + jobbing** model from the rove planner (`predictPasses`, one-unit-of-work
  per `loop()`, progress line, ~500 ms redraw throttle).
- **Per-pass detail rendering** (`drawPolarGrid`/`drawPolarArc`, AOS/LOS/maxEl) to reuse on a
  result row.

---

## 2. The data-model difference (why this isn't the union feature)

The union feature keeps ~4 KB of bitsets and no timing. This feature keeps **no bitset** — it
keeps a **small fixed list of matching passes**:

```
struct HitRow {
  uint32_t norad;      // which favorite satellite
  time_t   aos, los;   // full pass window
  time_t   inStart;    // first instant the target enters the footprint (<= los)
  time_t   inEnd;      // last instant it's still in the footprint (>= inStart)
  uint8_t  maxElWhole; // max elevation of the pass (context)
};
HitRow tsHits[TS_HIT_MAX];   // .bss, e.g. TS_HIT_MAX = 40  -> ~40 * ~22 B = ~900 B
int    tsHitN;
```

`inStart/inEnd` are the workable sub-window *within* the pass (the target is usually only inside
the footprint for part of a pass), which is the genuinely useful answer — when to point the
antenna at that entity. If more than `TS_HIT_MAX` passes match over 10 days, keep the earliest N
and show a "+more, narrow the window" note (a 10-day horizon on one target rarely exceeds a few
dozen hits, so 40 is generous; size it with a `static_assert`-checked constant).

**RAM/heap:** the result list is small **.bss** — no heap at all for state/DXCC/grid targets.
The one exception is if we choose to reuse `gridBits` for anything; we don't need to (see §3), so
**this feature can run with zero heap allocation**, which is even tighter than the union feature.

---

## 3. The inner test — cheap, because the target is known

For each sample along a pass (reuse the existing cadence: 1/min, capped ~90/pass), we compute the
sub-point `L = pred.look(t)` and ask **"is the target inside this footprint?"** — a single boolean,
not a bitset fill:

- **Grid target** — the grid is one lat/lon point (`gridToLatLon`). Test the spherical-cap
  inequality directly: `sin(clatR)*sinSub + cos(clatR)*cosSub*cos((clon-subLon)*D2R) >= coslam`.
  O(1) per sample. Trivial.
- **State target** — the state is a polygon. A state is workable at a sample if **any** point of
  the state lies in the footprint. Cheapest correct-enough test: check the state's **representative
  point / centroid** against the cap (like the grid case) as a fast path, then, only if we want
  edge-accuracy, walk the footprint mesh restricted to the state's bounding box and `statePipAt`.
  Recommend **centroid test** for v1 (a state is small vs. a LEO footprint ~5000 km across, so the
  centroid entering/leaving the footprint is an excellent proxy) and note edge-accuracy as a
  refinement. This avoids the full-mesh cost of `addFootprintStates`.
- **DXCC target** — same as state: centroid-in-cap fast path. Point entities (islands) are already
  a single coordinate. Polygon entities use their centroid.

**Why centroid-first matters for cost:** filling a whole footprint bitset per sample (what
`addFootprint*` does) is the expensive primitive. Testing one known centroid against the cap is
~10 floating-point ops. Over ~700 passes × ~90 samples that is the difference between "snappy" and
"minutes." So this feature is **inherently cheaper than the union sweep**, even though it also
covers 10 days × all favorites.

**Sub-window extraction:** within a pass, record the first sample where the target is inside
(`inStart`) and the last (`inEnd`). Optionally refine each boundary with a short bisection between
the bracketing samples for minute-accurate enter/leave times (cheap: a few extra `look()` calls
per hit, only at the two edges).

---

## 4. The DXCC data-linkage gap (call this out)

The searchable table `DXCC_LK[]` (`dxcc_lookup.h`) has **code / prefix / name / zones / notes but
NO latitude/longitude and NO polygon index.** The footprint geometry lives in a *separate*
dataset (`DXCCPOLY_*` polygons + `DXCCPT[]` point entities), indexed independently. So selecting
"Monaco" in the search UI gives us an ARRL code, not a location or a polygon.

**We must bridge search-entity → footprint-geometry.** Options:

- **(A, preferred) Add a centroid to the geometry side and map by ARRL code.** The
  `DXCCPOLY`/`DXCCPT` build already knows each entity's coordinates; emit a parallel
  `DXCC_CENTROID[]` (lat/lon per geometry index) and a `code → geometryIndex` map (or store the
  ARRL code alongside each geometry entry). Then search → code → centroid → cap test. This is a
  **data/table task in the generator**, not runtime logic, and keeps the runtime test O(1).
- (B) Compute a centroid at runtime from the polygon vertices on selection. Workable but adds
  runtime polygon-walking; unnecessary if (A) precomputes it.
- Note: not every searchable entity has footprint geometry (deleted entities, some that are
  points). If a selected entity has no geometry, the UI must say **"no map geometry for this
  entity"** rather than silently returning zero hits.

This linkage is the **single biggest implementation risk** in this feature and should be resolved
in the data generator **before** the runtime code is written. States and grids have no such gap
(states carry polygons already; grids are self-locating).

---

## 5. UX flow

**Entry.** Next Passes (`SCR_SCHEDULE`, `keySchedule`) → a new key. `w` is proposed for the union
feature; use a **different free key here** — candidates: **`s`** ("search") if free, else `f`
("find"). (Verify against `keySchedule`'s current binds: back, up/down, `r`, `z`, `m`, `t`, `p`,
ENTER — so `s` and `f` are both free.)

**Target selection (new `SCR_TGTSEARCH`).** First choose the **category** (State / DXCC / Grid),
then specify the target:
- **State**: a scrollable pick list of the 51 codes (or type-to-jump by 2-letter code).
- **DXCC**: reuse the `SCR_DXLK` search idiom — a text field with live prefix/name/code filtering
  over `DXCC_LK[]`. This is the strongest reason the DXCC picker already pays off.
- **Grid**: a short text field; validate with `gridToLatLon` (reject "Bad grid" like existing grid
  inputs). 4-char (field+square) is the natural granularity; 6-char is fine (we just take its
  lat/lon).

**Run (progress).** Same jobbing discipline as the union feature and rove planner:
- Determinate **progress bar** driven by `passesDone / passesTotalEstimate` (cheap up-front AOS-only
  pass count over all favorites gives the denominator).
- Live text: `Sat 4/10 · pass 31/~60 · hits 3` so the operator sees matches accrue.
- **Cancel** (`` ` ``/DEL) stops cleanly (no heap to free in the zero-alloc path; just reset state).
- Job **one pass per `loop()`** (a pass is ≤90 cheap centroid tests — safe within a `loop()`).

**Result (`SCR_TGTHITS`).** On completion, a **clear Done state** and a list of matching passes,
earliest first, each row: **satellite · date · AOS→LOS (or the workable sub-window inStart→inEnd)
· max El**. `ENTER` on a row opens the existing pass-detail view (polar arc from the home site,
full geometry). Footer offers:
- `w` **export** to `/CardSat/` (timestamped `search_<TARGET>_YYYYMMDD_HHMM.txt`): header (target,
  horizon, favorites searched) then one line per hit (sat, date, window, maxEl). Downloadable via
  the 0.9.51 web Files page.
- If **zero hits**: an explicit "No workable passes for _TARGET_ in the next _D_ days across _K_
  favorites" — distinct from an error, and a hint to widen favorites or horizon.

---

## 6. State variables & screens (proposed)

New screens: `SCR_TGTSEARCH` (category + target entry) and `SCR_TGTHITS` (progress reuses this
with a phase flag, then shows results — same pattern as the rove planner / union feature).

```
enum { TS_PICK, TS_RUNNING, TS_DONE, TS_CANCEL } tsPhase;
uint8_t  tsKind;            // 0=state 1=dxcc 2=grid
int      tsStateIdx;        // when kind=state (index into STATE_CODE)
int      tsDxccGeoIdx;      // when kind=dxcc (index into footprint geometry, via §4 bridge)
double   tsLat, tsLon;      // resolved target centroid/point (all kinds resolve to this)
char     tsGrid[8];         // when kind=grid (display)
HitRow   tsHits[TS_HIT_MAX];// .bss result list (see §2)
int      tsHitN;
int      tsFavCursor;       // which favorite
time_t   tsWinFrom, tsWinTo;// per-favorite predictPasses paging cursor
int      tsPassesDone, tsPassesTotal;  // progress bar
uint32_t tsLastDrawMs;
```

Reused: `predictPasses`, `pred.look`, the spherical-cap inequality (factor it into a tiny
`bool pointInFootprint(lat,lon, subLat,subLon,altKm)` helper — used by both this feature and,
optionally, refactored into the existing `addFootprint*`), `gridToLatLon`, `STATE_CODE` /
`statePipAt`, the DXCC search filter, and the pass-detail renderer.

**Dual-apply reminder:** the two new `SCR_*` enum values, their dispatch cases (draw + key), every
new function body, and any `static_assert` on `TS_HIT_MAX` sizing must be mirrored byte-identical
into `CardSat.ino`; run balance (0) + parity (green) + dispatch audit + mirror-identity after each
function, and watch the consumed-signature trap on inserts.

---

## 7. Relationship to the union feature (shared building blocks)

If both features ship in 0.9.52, they should share:
- **`pointInFootprint()`** — the extracted spherical-cap test, used by this feature's inner loop
  and available to refactor the union feature's grid path.
- **The up-front pass-count** (AOS-only sweep) for the progress-bar denominator.
- **The jobbing skeleton** (favorite/pass cursor, per-`loop()` step, ~500 ms redraw, cancel).
- **The export idiom** (timestamped file under `/CardSat/`, web-downloadable).
- **The DXCC centroid table** from §4 — the union feature doesn't need it (it fills bitsets), but
  building it here makes a future "which DXCC, and where" map overlay cheap.

Recommend implementing **`pointInFootprint()` and the jobbing skeleton first**, as shared
infrastructure, then layering both features on top.

---

## 8. Deliberate scope boundaries

**In scope for 0.9.52:**
- One target (state | DXCC | grid), all favorites, ~10-day horizon.
- Centroid-based membership (states/DXCC), exact point (grid).
- Determinate progress bar, live hit count, clean cancel, clean Done.
- Result list with per-pass workable sub-window + satellite, pass-detail drill-down, text export.

**Out of scope (flag to user):**
- **Multiple simultaneous targets** (e.g. "any of these 5 grids") — the engine supports it, the UI
  doesn't; later refinement.
- **Edge-accurate state/DXCC membership** (full mesh vs. centroid). Centroid is an excellent proxy
  at LEO footprint scale; polygon-edge accuracy is a refinement, worth it only if users report a
  large entity clipping the footprint edge being missed.
- **Optimizing antenna aim within the sub-window** (the feature reports *when*, not *where to
  point moment-to-moment* — that's what Track already does once you pick a pass).
- **Configurable horizon / min-elevation** beyond the defaults (`cfg.minPassEl` already applies via
  `predictPasses`); a Settings knob can come later.

---

## 9. Open questions for Paul

1. **Entry key** on `SCR_SCHEDULE` — `s` ("search") or `f` ("find")? (Both free; `w` is reserved
   for the union feature if that ships too.)
2. **DXCC centroid bridge (§4)** — OK to add a generated `DXCC_CENTROID[]` + code→geo map to the
   data side? That's the critical enabler and the main new data artifact.
3. **State/DXCC membership** — centroid-only for v1 (fast, simple), or invest in edge-accurate mesh
   now?
4. **Sub-window precision** — report enter/leave to the nearest **sample minute**, or bisect each
   edge for ~second accuracy (a few extra `look()` calls per hit)?
5. **`TS_HIT_MAX`** — 40 enough, or do you expect targets/horizons that routinely exceed it? (Drives
   the "+more" behavior.)

---

## 10. Verification plan (host, before device)

- **Membership correctness**: for a set of known sub-points and targets, `pointInFootprint()`
  agrees with a brute-force great-circle-distance-to-cap-edge computation.
- **Grid vs. state/DXCC consistency**: a grid centroid inside a state gives consistent hit/no-hit
  with that state's centroid test on the same pass.
- **Sub-window monotonicity**: `inStart <= inEnd`, both within `[aos, los]`; the target is actually
  inside the footprint at `inStart` and `inEnd` and outside just beyond (if bisecting).
- **Timing/paging**: passes are enumerated once each over the 10-day window per favorite (no double
  counting across `predictPasses` pages); earliest-N truncation keeps the earliest hits.
- **Zero-alloc claim**: instrument free-heap/largest-block across a full run for a state, a DXCC,
  and a grid target; assert **no** net heap change (this feature should touch no heap at all).
- **Progress**: `tsPassesDone <= tsPassesTotal`; bar hits 100% exactly at `TS_DONE`; cancel leaves
  state clean.
- Then the standard gate: `balance.py` 0, `parity.py` green, dispatch audit, `static_assert`s,
  mirror-identical bodies in `CardSat.ino`, and an on-device timing pass confirming the search
  feels responsive (it should be noticeably faster than the union sweep, per §3).
