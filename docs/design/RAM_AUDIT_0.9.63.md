# Static-RAM audit (0.9.63)

The ESP32-S3FN8 has **no PSRAM**, so every byte of `.bss`/`.data` is permanent DRAM and
the free *contiguous* block is what large TLS fetches and the audio path compete for.
This is a survey of the largest static consumers and which can move to **heap-on-demand**
(allocate on entering a screen, free from the screen-transition hook in `loop()`) with
**no loss of functionality**.

## Where the RAM is

From `xtensa-esp32s3-elf-nm -S` on the built ELF, the largest static symbols:

- `app` (the `App` object): **~98.5 KB** of `.bss` — the dominant consumer.
- The rest is mostly **`const` lookup data** (`WEB_PAGE`, DXCC/CTY tables, fonts, star
  catalogues, keymaps) that must stay and can't be freed, plus IDF/M5 library buffers.

So savings come from members of `App` that aren't needed continuously.

## Converted (heap-on-demand)

| Buffer | Bytes | Notes |
|---|---:|---|
| `basFileList[64][20]` | ~1.3 KB | BASIC file browser; freed on leaving `SCR_BASICFILES` |
| `catLines[48][56]` | ~2.7 KB | CAT self-test log; freed on leaving `SCR_CATTEST` |
| `polarAz/polarEl[48]` | ~0.4 KB | polar-arc samples; freed on leaving `SCR_POLAR` |
| `logRecs[60] + logFileRows[60]` | ~8.9 KB | **QSO-log view cache**; freed on leaving `SCR_LOGLIST` |
| `illumBits[60][10]` | ~0.6 KB | illumination raster; freed on leaving `SCR_ILLUM` |
| `eqxT/eqxLonW[64]` | ~0.75 KB | equator-crossing table; freed on leaving `SCR_EQX` |
| `noteList[64][32] + noteTime[64]` | ~2.6 KB | Notes browser list; freed on leaving `SCR_NOTES` |
| `ctsRows[20]` | ~0.8 KB | CelesTrak search hits; freed on leaving `SCR_CTSEARCH` |
| `amsRpt[24]` | ~0.75 KB | per-sat AMSAT reports; freed on leaving `SCR_AMSRPT` |
| `wifiAp[16]` | ~0.7 KB | Wi-Fi scan results; freed on leaving `SCR_WIFISCAN` |

Cumulative: from a 0.9.62 static-RAM baseline of 158,664 B down to 140,744 B — about
**17.5 KB** of permanent `.bss` reclaimed, utilization 48% → 42%.

### The print-report hazard (About > Print, and serial)

There are **three** screen-independent ways to print a report: the `p` key on the owning
screen, the serial `print <name>` command, and the **About > Print** menu (`SCR_PRINTABOUT`,
~30 reports). The last two run from any screen, so any buffer a report reads must either be
rebuilt by the print function or stay resident. Reports split cleanly:

- **Rebuild their own data** (safe from any vector): passes, all-passes, orbit, 10-day,
  6-hour timeline, visible-passes, **QSO log** (`loadLog`), **EQX** (`buildEqx`, added this
  cycle), **illumination** (`buildIllum`), workable-horizon (prints from a file), EME,
  awards (from the log file), weather/QRZ/perf (resident singles), and `PR_NOTE` (prints
  `noteBuf`, **not** the browser list).
- **Read data left resident by visiting a screen** ("run the screen first"): mutual
  windows / DX-Doppler (`mutual[]`), pass-polar (`pdAz/pdEl`), workable-states, workable-DXCC,
  target search, rove plan. **These buffers must stay resident and were NOT converted.**

Every buffer converted this cycle was grep-checked against all ~30 print functions: none of
the six is read by any report, and `PR_NOTE` reads `noteBuf`, so making the Notes *browser
list* screen-scoped is safe.

## Conversion notes (the tricky ones)

### The QSO-log cache (biggest single win)

`logRecs[LOG_VIEW_MAX]` is `PendingQso` × 60 = 8,640 B plus `logFileRows` 240 B. It's only
needed while `SCR_LOGLIST` is open or a log print runs. Every consumer already funnels
through `loadLog()` (list entry, edit-commit refresh, and `printLog`), so allocation lives
there. The one hazard: the edit screen (`SCR_LOGENTRY`) commits edits/deletes via the
selected row's **on-disk index**, which lived in `logFileRows[]`. Freeing the cache when
the list is left would strand that. Fixed by copying the on-disk row into a scalar
(`logEditFileRow`) when the edit screen opens, so the save/delete no longer needs the cache
alive. `printLog` from serial is safe because it calls `loadLog()` first (re-allocates).

### Rebuild-before-print (illum + eqx)

These two have an **off-screen reader**: the serial `print illum` / `print eqx` commands
(`runSerialCommand`) call `printIllum()`/`printEqx()` from any screen. The fix that keeps
them screen-scoped is to have the print rebuild the data on demand:

- `printIllum()` already called `buildIllum()`, so `illumBits` needed no print change.
- `printEqx()` did **not** rebuild; a `buildEqx()` call was added.

**Rebuild cost (researched):** `buildIllum()` runs `ILLUM_DAYS × ILLUM_ROWS` = 4,800
`sunlitAt()` propagations; `buildEqx()` runs ~9,000 `look()` propagations over a 3-day,
30-second scan. At an estimated ~80 µs per SGP4 propagation that's roughly **0.4 s** and
**0.7 s** respectively (up to ~1.5 s if SGP4 is slower on this part). That's the same cost
the on-screen view already pays on entry (both already show a "Computing…" status), and it
is small next to network-printer latency — so the delay is acceptable. A print invoked
off-screen frees the rebuilt buffer immediately afterward (including on the early-return
"no data" paths) so a serial print doesn't leak.

## Must stay resident (do NOT free on screen exit)

- `passScratch[128]` (~5.1 KB) — the visibility list is read by `SCR_VIS`, `SCR_VISLIST`
  **and** `SCR_PASSDETAIL`; it must survive navigation between them.
- `catMonLines[64][40]` (~2.6 KB) — written by the **live CAT trace hook** on any screen.
- `msgRing[24]` (~1.8 KB), `rosterList[16]` (~0.9 KB) — LoRa RX ring and heard-stations
  roster; both are written while **off-screen** and must persist to work at all.
- `ovhName/az/el[40]` (~1.5 KB) — overhead cache refreshed on a timer; may be read
  off-screen (verify before converting).

## Still resident by necessity (rejected as screen-scoped)

- `pdEl/pdAz/pdSunlit[100]` (~0.9 KB) — read by three screens (`drawPlanDetail`,
  `drawPassPolar`, `drawPassDetail`); the source comments "reusing the pdAz/pdEl arrays."
  Could become a **feature-family** context shared across those three, but not screen-scoped.
- `oscarArcLat/Lon[96]` (~0.75 KB) — read by `drawGlobe` as well as the OSCAR screen. Same:
  a shared OSCAR-plus-globe context, not a single-screen free.

The general lesson from this cycle: `runSerialCommand` (the serial `print` handler) and the
world-map/globe renderer are **screen-independent readers**. A buffer they touch is only
convertible if either the reader rebuilds on demand (illum/eqx) or the buffer is scoped to a
family rather than one screen.

## Largest-contiguous-block experiment: static canvas

On-device `memtrace` showed the win from the `.bss` cuts was real (`sizeof(App)` dropped
~17.9 KB, confirmed) **but the largest free block was pinned at ~31.7 KB and never moved** —
because the heap, not `.bss`, is the constraint for big contiguous allocations (TLS). The
16.2 KB canvas sprite is malloc'd once at boot and never freed, so it sits as a fixed wall
in the middle of the DRAM heap, splitting the free space and capping the largest block.

`CANVAS_STATIC` (in `App::setup`) backs the sprite with a **static** 16.2 KB buffer via
LGFX `setBuffer()` instead of `createSprite()`. That moves those bytes out of the heap into
`.bss` below `_heap_start`, so the two heap regions the sprite used to separate can merge —
the intent being to lift the largest contiguous block. The buffer is marked Preallocated
inside LGFX, so it's never freed; `freeCanvasForTls()` was already a no-op, so nothing else
changes. **This trades 16.2 KB of "total free heap" (now static) for a larger contiguous
block.** Whether the regions actually merge depends on the DRAM bank layout, so it must be
confirmed with `memtrace` on hardware: if `largest block` jumps well above ~31.7 KB, keep
it; if it doesn't move, set `#define CANVAS_STATIC 0` to revert (pure compile-time toggle).
Static RAM after this: 156,944 B (still below the 0.9.62 baseline of 158,664).

## BASIC program buffer now releases

`memtrace` also showed ~3.6 KB not returning after running SKYDOME: the program text sits in
`basicBuf`, which `basicFree()` deliberately kept so leaving and returning to BASIC doesn't
lose the program. Two fixes: **Fn+n** now actually releases `basicBuf`/`basicName` (the old
`= ""` frees nothing — Arduino String keeps its capacity; only destruct + placement-new
frees), and **`basicFree()`** (run when leaving BASIC) now also releases `basicBuf` **when
it's empty** — so a cleared/empty editor doesn't hold KBs of stale heap, while a real program
is still preserved across a leave/return.

## Careful (async jobs)

`mutual[24]`, `transitHits[16]`, `satsatWin[16]`, `actMu*[40]` are filled by
**incremental background jobs**; a buffer freed mid-job corrupts the computation. If
converted, allocate at job start and free only after the job completes AND the screen is
left — more state than the simple screen-scoped pattern, so weigh the ~2 KB against the
added complexity.

## Not worth reverting

`Predictor::temeStateAt`'s `static Sgp4 fp` (~1.2 KB) was made static to stop a
**loop-task stack overflow** when BASIC's `SATSEL` drives SGP4 from a deep stack. The
16 KB loop stack is the primary fix, but reverting the static re-introduces the risk on
that deepest path, so the 1.2 KB is left as insurance.
