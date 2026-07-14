# Scope: RAM-Lifecycle Refactor

*Design scoping for reducing CardSat's always-resident RAM by giving mutually-exclusive screen
state a bounded lifetime instead of permanent residence in the global `App` object. This is the
largest and highest-regression-risk of the deferred items; the point of this document is to make
it stageable and measurable, not to start cutting.*

## 1. The problem

CardSat declares a single static `App` instance, so every fixed-size member lives for the entire
firmware lifetime — including large arrays that only matter while one specific screen is open.
Because most screens are **mutually exclusive** (you cannot be on the memo browser and the
visual-pass list at once), the *sum* of these arrays is resident when only the *largest active
one* ever needs to be.

Confirmed sizes (measured on the actual structs, not estimated):

| Resident member | Size | Needed only when |
|---|---|---|
| `SatDb::_sats[150]` (144 B each) | **21.1 KB** | always — this is the catalog (mostly must stay) |
| `favs[150]` + `view[150]` (uint32 + int) | 1.2 KB | favorites/list views |
| `visPasses[128]` + `vlPasses[128]` (PassPredict) | ~6–10 KB | two *mutually exclusive* pass screens |
| `memos[64]` (MemoEntry) | ~6.5 KB | only while browsing voice memos |
| `activeTx[64]` (Transponder) | ~4.9 KB | only when a satellite's transponders are loaded |
| AMSAT status/name fields in every SatEntry | ~part of the 21 KB | only for sats that have status |
| target-search / rove / sky / plot / equator / wifi-scan arrays | ~10 KB combined | each only on its own screen |

The reviewer's ~41–49 KB always-resident estimate (excluding the catalog itself) and the 144-byte
SatEntry figure both check out against the code. A focused refactor plausibly reclaims **25–35 KB
at idle** — meaningful headroom on a no-PSRAM part where the *largest contiguous block* (~31.7 KB)
is the binding resource for TLS handshakes.

## 2. Why this is the highest-risk item

- It touches **many screens** and their state assumptions. Every array that becomes lifecycle-scoped
  changes when it's valid, and every reader must be audited for "is this populated right now?"
- The failure mode is **use-after-free / stale-data** bugs that may not surface in host tests and
  can be intermittent on-device — the worst kind to chase.
- The payoff is real but *invisible to the user* (more headroom, fewer fragmentation resets), so it
  competes poorly for attention against features — which is exactly why it should be deliberate.

## 3. The measurement gate (do this first, before any cutting)

The reviewer is right that the numbers must be confirmed with tooling, not estimates:
1. Add `static_assert`/`sizeof` logging for `SatEntry`, `SatDb`, `PassPredict`, `MemoEntry`,
   `Transponder`, and `App` to a debug build.
2. Capture a **linker map** (`.map`) and the size report for a release build; record `.bss`/`.data`.
3. Add **heap telemetry** at key transitions: free heap and *largest free block* at boot, entering
   each heavy screen, and after leaving it.
4. Establish the **baseline** so every later change is a measured before/after, not a guess.

No refactoring should start until this baseline exists — otherwise there's no way to prove the work
helped or catch a regression.

## 4. Design options (the review's own framing, refined)

### The core pattern: a tagged scratch arena for foreground screens
A `union`/arena (`ScreenScratch`) shared by mutually-exclusive foreground workspaces — overhead
results, pass plots, planner rows, target results, rove lists, sky bars, Wi-Fi scan, equator/
illumination. Only the **largest** member occupies RAM, not the sum. Entering a screen claims the
arena (with a tag identifying the current owner); leaving releases it.

- **Pros:** biggest single reclamation; conceptually clean.
- **Cons:** requires that no two of these are ever needed simultaneously (must be verified per pair),
  and that background operations (an armed alarm, active tracking) never depend on a foreground
  scratch. A wrong assumption here is a stale-data bug.

### Lower-risk, high-value pieces that can ship independently *(recommended to sequence first)*
- **Share the two 128-entry pass arrays** (`visPasses`/`vlPasses`). They hold the same class of
  result for two mutually-exclusive screens → one buffer. Cleanest ~3–5 KB win, minimal blast radius.
- **Allocate the memo directory only while browsing** — construct on entry to the memo screen,
  release on exit. ~6.5 KB idle. Self-contained (the memo browser is a leaf screen).
- **Allocate `activeTx[]` only when transponders are loaded** / size to the actual count. ~up to
  4.9 KB. Bounded to the transponder-load path.
- **Sparse AMSAT status side-table** — most sats have no status; move name/status/report/heard out
  of every `SatEntry` into a sparse table keyed by index/NORAD. Shrinks the *catalog* itself, which
  is the 21 KB line — potentially the largest structural win, but it touches the hot catalog struct,
  so it's higher-care.
- **Narrow index arrays** — 150 fits in 16-bit; `favs`/`view` as `uint16_t` (or a bitset for favs)
  saves ~0.6 KB. Trivial and safe.

### Explicitly out of scope here
- Raising `MAX_SATS` or a disk-backed two-tier catalog — that's the *catalog-scaling* work (its own
  scope), which should be sequenced **after** this RAM cleanup because you can't sensibly size an
  active-set cache until the resident baseline is reduced and measured.

## 5. What must stay resident (the decision rule)

Keep data resident only if it must keep working **while another screen is active**: the primary
catalog, current tracking state, active alarm/scheduled-pass state, location/time, minimal enabled-
service state, small config used everywhere, and background LoRa RX if enabled. Foreground display
caches and one-shot job buffers get a bounded lifetime. When in doubt, the test is: *"if the user
switches screens, does this need to survive?"* — if no, it's a candidate.

## 6. Recommended sequencing

1. **Measurement gate** (§3) — land the instrumentation, capture the baseline. *Prerequisite.*
2. **Safe isolated wins** — share the pass arrays, narrow the indexes, memo-on-entry, activeTx-on-load.
   Each its own small change with a measured before/after. Most of the low-hanging KB, low risk.
3. **Sparse AMSAT side-table** — shrinks the catalog struct; higher care, isolate and test.
4. **ScreenScratch arena** — the big structural change, only after the above prove the pattern and
   the pairwise-exclusivity is verified. Heap telemetry must show no regression and no stale-data path.

Each step is independently shippable and independently revertable. Do **not** do #4 in the same
release as #1–3.

## 7. Sizing

High effort overall, but **only #4 is genuinely large**; #1–3 are a series of small, safe,
measurable changes that could deliver much of the reclaimed headroom on their own. The whole thing
is its own release track, gated on the measurement baseline, and sequenced *before* any catalog-
scaling work. Regression-test on real hardware with heap telemetry at every step — this is the item
where host gates are least sufficient.
