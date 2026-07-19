# Worst-case resident RAM analysis (0.9.60)

ESP32-S3FN8, **no PSRAM**. Usable DRAM region (linker-reported cap):
**327,680 B**. Every figure below is measured from the current build or
documented in-source, not estimated. The analysis is layered because on this
part the binding question is not total bytes used but whether the largest
transient still fits — as one contiguous block — on top of everything resident.

## Layer 1 — static (always resident)

Measured: **`.data` + `.bss` = 155,304 B** (the linker's "Global variables use
…"). This already includes:

- the `App` object (~96.4 KB — dominated by `_sats[150]` at 20.4 KB post-reorder,
  plus all the per-screen arrays),
- `cfg` / Settings (~1.4 KB),
- the WiFi/BT driver static structures (`g_cnxMgr` etc.),
- statically-allocated FreeRTOS stacks and the fixed line/CAT buffers added this
  cycle.

Large const tables (WEB_PAGE 25 KB, the DXCC tables, fonts) are **flash rodata**
and cost no RAM — verified by address.

## Layer 2 — networking up (dynamic, heap)

The WiFi + LWIP stack allocates from the heap when Wi-Fi starts (not in `.bss`).
Conservative IDF figure while connected: **~48 KB**. Running up to three LAN
listeners (rigctld, rotctld, web dashboard) plus their client sockets adds a
socket-pool / TIME_WAIT allowance of **~6 KB**.

**Baseline with networking fully up: 209,304 B — 63% of the region.**

## Layer 3 — largest transients

These are the big short-lived allocations. **By design most are mutually
exclusive** — the firmware refuses a TLS fetch while USB CAT is engaged, refuses
audio capture while USB CAT is engaged, prints one raster job at a time — so the
true worst case is close to *baseline + the single largest*, not the sum.

| Transient | Bytes | Notes |
|-----------|------:|-------|
| TLS handshake (BearSSL) | 31,700 | 16 KB RX buffer + overhead; the resident block `Net::TLS_MIN_BLOCK` (28 KB) gates |
| USB host (EspUsbHost) | 16,000 | resident tasks + descriptors while USB CAT engaged |
| `.tq8` LoTW body build | 9,000 | one contiguous malloc (documented OOM point) |
| Raster print scanline (×2) | 5,120 | `2 × RAS_SCAN_BYTES` |
| Voice-memo record (×2) | 4,096 | `2 × 1024 × int16` double-buffer |
| LoRa RX + SGP4 scratch | 3,000 | propagation working set |
| JSON GP streaming parse | 2,000 | per-entry document |

## Scenarios

**A — realistic worst case (guards enforced).** Baseline + the single largest
transient (TLS): **241,004 B, 73% of the region, ~86.7 KB heap free.** This is
the honest everyday ceiling: connected, tracking, servicing LAN clients, and
doing a TLS catalog fetch.

**B — pessimistic (every transient co-resident, guards notionally defeated).**
Baseline + the full sum of transients: **280,220 B, 85%, ~47.5 KB free.** Even
this over-pessimistic total, which the design actively prevents from occurring,
still fits with headroom.

## The real limit: contiguous heap, not total heap

Free heap total is not free *contiguous* heap. TLS needs its ~28 KB as one block
— hence the pre-handshake `TLS_MIN_BLOCK` gate that fails fast rather than
crashing mid-handshake. Under fragmentation the effective ceiling is lower than
the totals above imply. That is precisely why the 0.9.60 String→fixed-buffer work
(CAT stores, LAN line buffers) matters: it removes per-byte heap churn on the
hottest paths, keeping the large contiguous block that TLS/USB/`.tq8` each need
available deep into a long session. Static footprint is comfortable at 47–63%;
fragmentation resistance is the thing worth continuing to harden (the staged
`ScreenCtx` of Proposal 1 is the next lever).

## Bottom line

- Everyday worst case: **~73% of DRAM, ~87 KB free.** Comfortable.
- Absolute pessimistic (design-prevented): **~85%, ~47 KB free.** Still fits.
- No single scenario overcommits the 327,680 B region.
- The constraint that actually bounds the device is contiguous-block
  availability under fragmentation, addressed by the ongoing buffer work — not
  total RAM, which has margin.
