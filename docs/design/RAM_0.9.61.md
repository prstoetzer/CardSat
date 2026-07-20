# RAM assessment — 0.9.61 features

Measured from the linked ELF/map (not estimated). Two RAM budgets matter on the
no-PSRAM ESP32-S3: permanent static DRAM (`.bss`/`.data`, sized at link time) and
runtime heap (fragmentation-prone).

## Static DRAM

Total globals: **155,848 B** (`.dram0.data` 22,368 + `.dram0.bss` 133,480), from the
compiler's "Global variables use ..." line. The single global `App` object dominates
at **96,912 B**; the next-largest symbol is a 3.9 KB WiFi-stack global (not ours).
Everything 0.9.61 adds as an `App` member is inside that 96.9 KB.

### 0.9.61 additions (permanent)

| Feature | Members | Bytes |
|---|---|---|
| Space weather | SSN, flare[4], xrayW, flareEpoch, Bz, wind, dens, fcastKp[3] | ~44 |
| Terrain profile | terrainN, maxM, maxKm, clear | 13 |
| EME planner arrays | `emePlanDec[90]`, `emePlanDeg[90]` (grew from [30]) | +480 |
| Settings | `rotMagCorrect` (bool, in Settings struct) | 1 |
| **Total** | | **~540 B** |

That is ~0.5 KB of permanent RAM, about **0.6 % growth** of the `App` object.

### What did NOT cost RAM (lives in flash `.rodata`)

Confirmed by symbol addresses in the `0x3c…` flash-mapped region:
- Magnetic-declination IGRF coefficient tables (`gc`/`hc`, function-`static const`).
- Meteor-shower calendar table (`static const`).
- Rain-fade ITU coefficient table (function-local, on the stack per call).

## Runtime heap

All space-weather fetches stream the HTTP response to a temp **file**
(`FILE_DL_TMP`), never holding it in RAM, then parse it back with a bounded read.
Peak transient heap per path (freed after each; all sequential):

| Path | Peak | Note |
|---|---|---|
| X-ray flare (`scanFileStr`) | ~0.5 KB | file ~450 B |
| Solar-wind mag (`scanArrayLastCol`) | <0.5 KB | file ~286 B |
| Solar-wind plasma (2 cols) | <0.5 KB | file ~202 B — now ONE read |
| Sunspot (`scanFileNum`) | <0.2 KB | streaming sliding window |
| 3-day forecast | ~0.1 KB | line-at-a-time |
| Terrain profiler | ~1 KB | 12 samples; body ~0.5 KB |

No path holds more than ~1 KB at once. Incremental peak heap from 0.9.61 ≈ **1 KB**,
transient.

## Optimization applied

The plasma file was read **twice** (once for `speed`, once for `density`), each a
full file pass + String allocation. Refactored to `scanArrayTwoCols()`: one read,
header parsed once, both columns extracted from the last row. Eliminates a redundant
file read and String alloc on every space-wx refresh. Oversized 16 KB parser caps
tightened to 4 KB (still a safety ceiling far above the ~300 B real files). Costs
+144 B flash; static RAM unchanged.

## Not pursued (and why)

Moving members to heap-on-demand: the scalars are too small and too hot (read every
frame the prop/space-wx screen is open) — pointer overhead would exceed savings. The
720 B EME planner arrays are the only real candidate, but they're accessed every
redraw while the planner is open, so heap-on-demand would allocate on entry/free on
exit, and a 720 B contiguous alloc can fail on a fragmented heap — marginal gain,
real risk. Not worth it for a ~1 KB footprint.
