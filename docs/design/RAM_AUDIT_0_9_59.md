# RAM & heap audit — everything built in the 0.9.59 cycle

Scope: the twenty tools, the debris GP streamer, the CelesTrak search +
auto-refreshed extras, `Predictor::lookFor`, the calculator/grapher expansion,
the BASIC language + system + hooks, and the section-5 printing. Board context:
ESP32-S3FN8, **no PSRAM**; the enemies are permanent `.bss` creep, stack spikes
on the loop task, and heap **fragmentation** (a resident TLS handshake needs a
large contiguous block, so alloc/free churn beside network buffers is the
classic failure).

## Compiler-output audit (ELF/map) and the SatEntry reorder

A pass over the linked image (not the source) with the xtensa `nm`/`readelf` and a
DWARF layout dump established that the `App` object is 93.3 KB -- 72 % of all static
RAM -- and that its single largest member is `db._sats[150]`, i.e. `SatEntry` * 150.
Reading `SatEntry`'s member offsets from DWARF showed four internal alignment holes
(a 4-byte gap before `meanMotion`, tail padding after `name`/`intlDes`, and trailing
struct padding) totalling 8 bytes/entry.

**Fix:** `SatEntry`'s fields were regrouped largest-alignment-first (doubles, then
4-byte words + the float elements, then char arrays, then the 1/2-byte flags). This
is a pure layout change -- every access is by name, there are no positional aggregate
initializers or byte-serialization of the struct, and the host orbital harness
(`gpToTle`, TRACK, PASSES, HIORBIT, BETA, SUNLIT) is byte-identical before and after.
`SatEntry` went 144 -> **136 bytes**; measured globals dropped **1,336 B** (the catalog
plus the several `PassPredict`/`SatEntry` scratch arrays elsewhere in `App`), and
local-variable headroom rose by the same amount.

**Not done, and why:** shrinking `epochUnix` from `double` to a 32-bit type was
considered and rejected. It holds *fractional* Unix seconds, and `gpToTle` renders
that fraction into the TLE epoch as `%012.8f` day-of-year (~0.86 ms), from which
`tsince` is measured. A whole-second integer -- like a float -- would round every
satellite's epoch and corrupt the elements SGP4 re-parses. The field stays double by
design; only the free reorder was taken.

The remaining permanent-RAM verdicts below are unchanged.

## Fixes applied during this audit

1. **Per-line `JsonDocument` churn eliminated in every ctx-file walk.**
   `isCtExtra`, `removeCtExtra`, `listNdjsonNorads`, and the update-time
   `refreshCtExtras` loop each allocated and freed an ArduinoJson document per
   NDJSON line — the exact no-PSRAM fragmentation pattern `satdb.cpp`'s bulk
   parser was built to avoid, and `refreshCtExtras` did it *while TLS fetches
   were in flight*. All four now use a new allocation-free
   `SatDb::gpLineNorad()` (a `gpFindValue` wrapper). The only remaining
   per-line document is `loadCtExtraFile`'s full-entry parse, which mirrors the
   long-standing `loadManualGpFile` pattern and runs before, not during,
   network activity.

2. **`removeCtExtra` no longer holds the whole file in RAM.** It accumulated
   every kept line into one `String` (a ~20 KB heap spike at the cap, growing
   by reallocation). It now streams kept lines to `/CardSat/ctx.rm` and renames
   — flat RAM, and a crash mid-write leaves the original file intact.

3. **The extras file is capped (`CTX_MAX` = 25).** `addCtExtra` refuses beyond
   the cap (surfaced in the UI as "Extras full"). This aligns the file with the
   refresh's 25-object courtesy cap — previously the file could grow unbounded
   while only the first 25 ever refreshed — and it bounds the heap cost of
   every walk above.

## Permanent `.bss` added this cycle — inventory and verdicts

| Item | Size | Verdict |
|---|---|---|
| Grapher column buffers `gGraphBufA/B` | 1,888 B | Keep. Shared three ways (plot samples, trace/roots, CSV min/max cache), file-scope so they never touch the stack. Heap-per-screen would add churn for no win. |
| Link-curve `mbuf[208]` (static) | 832 B | Keep — same reasoning; static keeps it off the draw stack. |
| Debris results `dgSat[14]` + times/miss | ~2.3 KB | Keep. Full `SatEntry` copies are required (the source file is deleted after screening — transient-download design). |
| CelesTrak results `ctsRows[20]` | 800 B | Keep. Slim 40-B rows by design; the full entries stay on disk and are re-streamed on add. |
| Conjunction results (`conjT/Miss/Rvel[?]`) | ~120 B | Fine. |
| BASIC host state (`basFile` + 3 flags) | ~40 B | Fine. |
| CelesTrak throttle timestamps | 12 B + a file | Fine; persistence is the point. |

Net globals across the cycle: 149,360 → 152,232 B (46 %), ~175 KB left for
locals + heap. Every kilobyte above is attributable and argued.

## Heap lifecycle — verified pairings

- **BasicVM**: `new` per run, deleted on every path; the `@()` array is freed
  in the destructor (`new(nothrow)` failure leaves `arr == nullptr`, and
  `delete[] nullptr` is defined). `work` (the tokenized source) outlives the
  run and is deleted with it. Output is hard-capped at 6 KB with reserved
  capacity, so it never reallocs.
- **LPRINT sinks / FOPEN file**: opened lazily, and `basicRun` closes both
  *after* `run()` returns — including on a runtime error or budget halt — so
  no path leaks a socket or file handle.
- **Transient parsing**: the debris and search streams use `Scratch::Lease`
  (arena, not heap) for the 1,200-B object buffer; RAM is flat for any file
  size.
- **`lookFor`**: pure stack (~30 doubles), no allocation, never touches the
  live propagator's state.
- **Section-5 printing**: zero buffering anywhere — every report streams lines
  straight to `Printer::line`, and the universal form print re-runs the
  existing compute with a tee flag (`tfEmit`), so it allocates nothing the
  screen didn't.

## Stack notes

No new function places more than ~100 B of arrays on the loop-task stack; the
two big per-column buffers are deliberately `static`. (Pre-existing, out of
scope but noted for a future pass: `drawBasicRun`'s `NoteVRow rows[256]` is
~2 KB of stack per repaint.)

## Blocking-time notes (not RAM, but same review)

`refreshCtExtras` can hold the update flow for ~2 s × N objects (courtesy
spacing); `delay()` yields, statuses tick per object, and the 2-h throttle
means the common case is a single skipped check. BASIC `SATSEL` yields on
every call and is budgeted at 2,000 per run.
