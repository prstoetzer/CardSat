# RAM reduction — what's actually available

**STATUS (0.9.58): items 1B and 2 are BUILT — 11,776 B of `.bss` reclaimed.**

| item | saving | status |
|---|---|---|
| **`memos[64]` heap-lazy** | **6,656 B** | **done** — allocated in `buildMemoList()`, freed by the `loop()` transition hook |
| **shared `passScratch[128]`** | **5,120 B** | **done** — one array replaces `visPasses[]`/`vlPasses[]`, contract documented at the declaration |
| `amsatName` → lookup | ~4,200 B | not done — touches the AMSAT reporting path |
| `activeTx` heap-lazy | 4,864 B | declined — needed mid-pass |
| `favs`/`view` | 1,200 B | declined — low value, hot path |
| catalog | — | it's the product |

**Expected on the next build:** free heap 55,376 → ~67,152; largest block 31,732 → ~43,508.
**That prediction is testable and should be tested** — if the largest block does not rise by the
full ~11.8 KB, the linker-layout reasoning below is wrong.

The analysis that produced this follows.

---

## The starting picture (measured, real hardware)

```
heap:   free 55376, min-ever 44036, largest block 31732
sizeof: SatEntry 144, SatDb 24496, PassPredict 40, Transponder 76,
        MemoEntry 104, App 101360
resident arrays (always-allocated, refactor candidates):
  catalog _sats[150]          = 21600 B
  favs[150]+view[150]         =  1200 B
  visPasses[128]+vlPasses[128]= 10240 B
  memos[64]                   =  6656 B
  activeTx[64]                =  4864 B
catalog loaded: 92 / 150
```

**44,560 B — 44% of the 101 KB `App` object — is these five arrays.**

## The fact that makes this worth doing

`src/main.cpp:11` and `CardSat.ino:42983`: **`static App app;`**

The 101 KB `App` lives in **`.bss`**, not the heap. On the ESP32-S3 the internal SRAM is one pool:
the linker lays down `.data`/`.bss`, and *what remains becomes the heap*.

```
SRAM = [ .data | .bss (incl. 101 KB App) | -------- HEAP -------- ]
```

So shrinking a `.bss` array by N bytes moves the heap's floor down by N. **Free heap and largest
block both rise by N.** And because it is at the bottom of the pool, it cannot fragment.

**For TLS — which needs one large *contiguous* block — this is the best kind of win available.**
Better than heap-lazy tricks, which only help if the timing is right. This is why the exercise is
worth doing before spending ~4.6 KB on a USB host stack.

---

## 1. `visPasses[128]` + `vlPasses[128]` = 10,240 B — **your instinct was right**

Two `PassPredict[128]` arrays, 5,120 B each:

- **`visPasses`** — the 10-day pass *chart* (`SCR_VIS`), built by `buildVis()`
- **`vlPasses`** — the visible-pass *list* (`SCR_VISLIST`), built by `buildVisList()`

They hold genuinely different data (all passes vs. only optically-visible ones), so they are not
literally duplicates. **But they are never live at the same time**, and that is what matters:

| | |
|---|---|
| both reached **only** from `SCR_PASSES` | `v` → `SCR_VIS`, `V` → `SCR_VISLIST` |
| each **rebuilds on entry** | `keyPasses` calls `buildVis()` / `buildVisList()` before switching |
| neither screen navigates to the other | `keyVisList` → `SCR_PASSES`/`SCR_PASSDETAIL` only |
| both print paths **rebuild** | `printTenDay()` calls `buildVis()`; `printVisList()` now calls `buildVisList()` (see the bug below) |

**Nothing needs either array's contents to survive leaving its screen.** Whichever one you are not
looking at is 5,120 B of pure waste.

### The complication (checked, and it matters)

`buildOrbit()` uses `visPasses[]` as **its own scratch buffer** — it calls `predictPasses()`
straight into it, overwriting the 10-day chart's contents:

```c
int n = pred.predictPasses(now, cfg.minPassEl, visPasses, VIS_PASS_MAX, winEnd);
```

So the array is *already* being shared opportunistically by a third consumer. A naive
`union { visPasses; vlPasses; }` would add a fourth claimant to storage that already has an
undocumented sharing contract.

### The options

| approach | saving | risk |
|---|---|---|
| **A. Union the two arrays** | **5,120 B** | must prove no path holds one while building the other — `buildOrbit` makes this subtler than it looks |
| **B. One shared `passScratch[128]`**, explicitly owned by whoever built last | **5,120 B** | same analysis, but *documents* the contract that `buildOrbit` already relies on implicitly |
| **C. Heap-allocate on screen entry, free on exit** | 10,240 B | the 0.9.56/0.9.57 pattern; but re-introduces the "did the free work?" question, and these are big enough to fragment |
| **D. Shrink `VIS_PASS_MAX`** | tunable | changes behaviour: the cap exists so high-rate LEOs don't lose the tail |

**Recommend B.** It banks the same 5,120 B as A, and it turns `buildOrbit`'s existing informal
reuse into a stated invariant instead of a trap for the next person.

## 2. `memos[64]` = 6,656 B — **the best ratio on the list**

`VoiceMemo::MemoEntry memos[MEMO_LIST_MAX]`, 104 B each, dominated by `char file[64]`.

Used by exactly one screen (`drawMemos` / `keyMemos`, plus a serial command). It is a **directory
listing of a rarely-visited screen**, held permanently.

**This is the cleanest heap-lazy candidate on the list** — better than the pass arrays, because:

- it is only ever populated by a directory scan when you open the screen
- nothing else reads it
- the screen already rebuilds the list on entry
- 6,656 B is the second-largest single item

**Recommend:** allocate on entry to `SCR_MEMOS`, free on the screen-transition hook that
`loop()` already has (added in 0.9.57 for `basicFree()`). **Saving: 6,656 B.**

## 3. `SatEntry::amsatName[28]` × 150 = 4,200 B — **real, but touches the AMSAT path**

`SatEntry` is otherwise a carefully-tuned struct — the comments show floats chosen per TLE field
precision, doubles only where required (`epochUnix`, `meanMotion`). Someone thought hard about it.

But it carries **AMSAT status fields** (34 B of 144), dominated by `amsatName[28]` — the AMSAT
API's name for the matched status row. Two observations:

- Paul's own log: **`[amsat] catalog map: 47 entries`**. We store 28 B × **150** slots for data
  that exists for **47** satellites.
- It is *derived* — `applyAmsatCatalogFile()` fills it from the cached catalog, and it is read by
  `fetchAmsatReports()` / `amsPickNameFor()`.

**Recommend:** consider, don't rush. Replacing a stored field with a lookup changes the AMSAT
reporting path, and that path has its own subtleties (`amsPickNameFor` handles ambiguous
multi-mode matches). **Saving: ~4,200 B**, at real risk. Worth doing *after* 1 and 2.

## 4. `activeTx[64]` = 4,864 B — **justified, leave alone**

`config.h:182` says it plainly: *"transmitters held for active sat (e.g. **ISS has ~49 on
SatNOGS**)"*. 64 is sized from real data, not padding. Could be heap-lazy on the Track screen, but
it is needed *during a pass* — exactly when you least want an allocation to fail.

**Recommend: leave it.**

## 5. `favs[150]` + `view[150]` = 1,200 B — **not worth it**

Two parallel `uint32_t`/`int` arrays. Small, hot, used constantly by the Satellites list.
1,200 B for the code churn is a bad trade.

## 6. `catalog _sats[150]` = 21,600 B — **the elephant, and mostly untouchable**

The largest item by far, and it *is* the product: the satellite catalog. Paul's log says **92 of
150** slots are loaded, so ~8,350 B is currently empty slack — but that is headroom for a larger
catalog, not waste, and §2.1 of the roadmap already names ">~150 satellites" as the trigger to
revisit RAM.

**Recommend: leave**, except for the `amsatName` sub-item above.

---

## Summary

| item | saving | risk | do it? |
|---|---|---|---|
| **`memos[64]` heap-lazy** | **6,656 B** | low — one screen, rebuilds on entry | **yes, first** |
| **shared pass scratch** | **5,120 B** | medium — must document `buildOrbit`'s reuse | **yes, second** |
| `amsatName` → lookup | ~4,200 B | medium-high — touches AMSAT reporting | later |
| `activeTx` heap-lazy | 4,864 B | needed mid-pass | no |
| `favs`/`view` | 1,200 B | low value | no |
| catalog | — | it's the product | no |

**Realistic near-term: ~11.8 KB of `.bss`**, which should raise *both* free heap and largest block
by the same amount — from `largest block 31732` to roughly **43 KB**.

For scale: that is **more than twice** the ~4.6 KB an `EspUsbHost` at `MAX_DEVICES=1` would cost,
and it is contiguous-block headroom, which is exactly the currency TLS spends.

## A bug found while doing this analysis

**`printVisList()` did not rebuild before printing** — it read `vlN`, which is 0 unless you had
visited the Visible-pass screen. Printing that report from the **About → Print** menu would say
*"(none in the window)"* for a satellite that has passes. `printTenDay()` calls `buildVis()` for
exactly this reason; I missed it when adding the report earlier this session.

**Fixed** — `printVisList()` now calls `buildVisList()` first. This also *helps* the union
proposal: with both print paths rebuilding, nothing depends on either array persisting.

## Method note

Every number here is from Paul's measured boot log or read out of the source — not estimated. The
one thing I have **not** done is compile: the `.bss` savings should be confirmed with a build and
the `mem` command, and the "largest block rises by the same amount" claim is a linker-behaviour
inference that a real build would settle in one line of output.
