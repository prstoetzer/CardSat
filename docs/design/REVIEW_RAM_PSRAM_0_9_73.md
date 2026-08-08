# CardSat 0.9.73 — defect review, RAM analysis, and a PSRAM plan for the helper

*Three questions answered from the source and the linker map, not from memory:
(1) any critical bugs; (2) where the RAM actually goes and what can be reclaimed
without losing functionality; (3) what the M5StickS3's 8 MB PSRAM could buy the
USB system. One genuine defect found (D1); it is small and should go in before
release.*

---

## Part 1 — Defects

### D1. Opening the USB-helper screen steals the Grove UART from a live radio — **fix before release**

Settings row 110's ENTER handler calls `UsbHelper::begin(cfg.helperBaud)`
unconditionally so the screen can enumerate before a transport is chosen. But
`begin()` claims UART1 via `civUartOpen()` — **the same wire a wired-CI-V radio, a
Grove GPS, a Grove rotator, or a Grove rigctl link is already using.** Browsing
the helper screen while any of those is engaged silently re-purposes G1/G2 out
from under it, and nothing restores the previous claimant when the screen is left
(`end()` is only called when `applyHelperFromCfg()` next runs with
`helperWanted()` false).

Symptom for a wired-CI-V user: open Settings → USB helper (just to look), back
out, and CAT is dead until the radio is re-engaged — with nothing pointing at the
helper screen as the cause. The engage-path conflict rules are all correct; the
hole is only this screen's convenience bring-up, which bypasses them.

**Fix (small):** in the `case 110:` handler, before `begin()`:

```cpp
if (!helperWanted() && catUsesGroveWire()) { setStatus("Grove wire in use by CAT/GPS", 3000, SEV_WARN); break; }
// plus the same test for Grove GPS / Grove rotator via rotTransportConflict()'s inputs
```

— i.e. bring the link up eagerly only when the Grove wire is genuinely free; when
it is not, open the screen in a "wire busy" state that still shows link
configuration but does not enumerate. A matching `UsbHelper::end()` on leaving
the screen when nothing is configured to use it closes the residual claim.

### D2. Checked and found sound (the suspects that did not pan out)

* **`p` on an empty QSO log** — `keyLogList()` returns at `logRecN == 0` before
  the new handler, so `logRecs[logListSel]` is never touched empty.
* **`serviceHelperCat()`'s `DualRig` cast** — only taken when `hLeg >= 0`, which
  requires `cfg.catType == CAT_DUAL`, which is the only time `rig` is a DualRig.
* **`CAT_HELPER` single-rig `begin()`** — relies on the same
  `setExternalStream()`-skips-UART contract the CAT_USB path has used since it
  was introduced; verified the branch order matches.
* **SCR_USBHELPER's two-row cursor** — up and down both toggle `(uhSel+1)%2`,
  correct for two rows.
* **Deorbit's menu shift** — re-verified: only games 1–5 hard-code `gamesSel`;
  KESSLER's handler sets nothing, so its move to slot 7 is inert.
* **The QSL card's grid refusal** — unreachable-without-fix concern is closed by
  the MyGrid editor; the refusal text points at a control that now exists.

## Part 2 — Where the RAM goes, and what is recoverable

From the map of the shipped binary: **`.bss` 141,040 B + `.data` 22,368 B =
163,408 B static** (49%). The composition:

| block | size | note |
| --- | ---: | --- |
| `App` (the one global) | 85,736 | everything below is inside it unless noted |
| `App::setup()::canvasBuf` | 16,200 | 240×135 @4bpp framebuffer — required |
| IDF/WiFi/lwIP/M5 internals | ~20,000 | not ours to shrink |
| everything else | ~41,000 | hundreds of small statics |

Inside `App`, the largest members (measured or computed from the source):

| member | size | screen-scoped? |
| --- | ---: | --- |
| `SatDb db` (`150 × ~128 B SatEntry`) | ~19,200 | no — live tracking core |
| `hamsatList[20]` + `userSked[12]` (Activations) | 4,576 | **yes** |
| `ao7ObsT[300]` + `ao7ObsMode[300]` | 2,700 | **yes** (AO-7 clock tool) |
| `catMonLines[…]` | 2,560 | **yes** (CAT monitor) |
| `roveList` + `roveTime` | 1,920 | **yes** (rove planner) |
| `msgRing` (LoRa) | 1,704 | no — must survive screen exits |
| telemetry grid scrollback | 1,560 | **yes** |
| `tsNextPass/tsCursor/…` (32 favorites × 4 arrays) | ~1,800 | no — cross-screen schedule |
| `dgSat[14]` (SatEntry copies) | ~1,800 | **yes** (decay graph) |
| ~48 `String` members | ~770 handles + heap | policy-bound (see 0.9.59 audit) |

Context that matters: **the big obvious work is already done.** The three feed
arrays (APRS 9 KB, DX ~14 KB, ADS-B 9 KB) went heap-on-demand in an earlier
cycle, `SatEntry` was hole-packed in 0.9.59 (~1.2 KB reclaimed), and the String
policy is documented. What remains is the second tier.

### Recoverable without losing anything: ~13–15 KB

Extend the exact pattern `aprsSta`/`dxcSpot`/`adsb` already use — allocate on
screen entry, free from the screen-transition hook — to the five screen-scoped
blocks above: **activations (4.6 K), AO-7 observations (2.7 K), CAT monitor
(2.6 K), rove planner (1.9 K), telemetry scrollback (1.6 K), decay graph
(1.8 K)**. None is needed when its screen is closed; all are rebuilt from SD or
network on entry.

The cost is not the `malloc` — it is the discipline the DX-cluster bugs taught:
**every path that can touch the pointer (draw, key, print, parser, and any timer)
must tolerate `nullptr`**, because all of them are reachable before allocation
and after a failed one. Each conversion is therefore a small audit, not a
mechanical edit. Recommend doing them one per cycle, print-path included, rather
than all six in one sweep.

Two smaller notes, both already flagged in the 0.9.59 audit and still true:
`drawBasicRun`'s ~2 KB of stack per repaint, and the pair of 944 B function-static
graph buffers (fine, but they are the pattern to *copy*, not grow).

**Not recommended:** shrinking `MAX_SATS` (150) or the framebuffer — both reduce
functionality, which is out of scope by the terms of this review.

## Part 3 — What the M5StickS3's 8 MB PSRAM can buy the USB system

The helper currently uses PSRAM for nothing: its rings are
`heap_caps_malloc(MALLOC_CAP_8BIT)` at 8 KB (USB→link) and 2 KB (link→USB), and
the library's per-device CDC RX ring is a fixed 512 B of internal RAM.

One constraint frames everything: **USB transfer buffers themselves
(`usb_host_transfer_alloc`) must stay DMA-capable internal RAM — PSRAM is not an
option there.** Everything *behind* the transfer layer is fair game, because the
helper's USB reads and writes all happen on tasks, never in ISRs.

### P1. Move the two rings to PSRAM and grow them 30× — highest value, smallest change

`ByteRing::alloc()` becomes try-PSRAM-first
(`heap_caps_malloc(n, MALLOC_CAP_SPIRAM)`), fall back to internal; sizes go to
**256 KB USB→link, 32 KB link→USB**. Effects:

* A CI-V radio in transceive mode can flood for **minutes** during a Grove-link
  stall without a single byte lost — versus ~2 s of headroom today at 8 KB.
* Because `grantCredit()` grants from ring free space, a 256 KB ring means the
  host-facing window is effectively always full: helper→host starvation stops
  being a state the protocol can enter, while the CSUH credit cap still bounds
  what is in flight on the wire.
* ~10 KB of the Stick's internal heap is returned to the USB host stack — the
  side that actually needs DMA-capable memory.

### P2. Library patch 11: heap-allocated, size-configurable CDC RX ring

`EspUsbHostCdcSerial::RX_BUFFER_SIZE` is a 512 B `static constexpr`. Make the
ring heap-allocated at construction, size from a define
(`CARDSAT_CDC_RX_RING`, default 512 so CardSat is unchanged), PSRAM-preferred
when the cap is available. The helper sets 16 KB. This is the buffer between the
USB IN transfers and the drain loop, so it is the burst absorber for the case
where the *helper's own loop* is briefly busy (an enumeration burst, a screen
wake). Same shape as patch 10 — worth writing upstream-ready, because a
fixed-512 ring is an upstream limitation too.

### P3. A PSRAM flight recorder for the wedge hunt

The standing unsolved problem is USB host wedging that requires a reboot, and the
standing method rule is *measure before patching*. 8 MB makes the instrument
cheap: a **1 MB PSRAM ring** journaling every USB event (attach, detach, claim,
transfer error, halt/flush, enum stage) with millis timestamps, plus the last N
raw CSUH frames. Dump on demand through a new `CSUH_T_LOG_REQ` (or just
`helper_probe.py --monitor` reading a `STAT`-style block). When a wedge finally
happens on the bench, the last ten seconds of bus history are *already
captured* — which converts the wedge from "reproduce it while watching" to "read
the tape".

### P4. Descriptor cache for `--enum` diagnostics

Keep each seen device's full configuration descriptor (a few hundred bytes each)
in PSRAM instead of discarding after parse, and let the probe fetch it. Directly
serves the open IC-705 question — whether the audio function enumerates as its
own device — by letting the bench read the actual descriptor tree instead of
inferring from behaviour.

### Sequencing

P1 is a ~15-line change with an existing fallback path and should go in the next
cycle. P2 is a library patch and gets the patch-10 treatment (PATCHES.md entry,
upstream-ready). P3/P4 are bench instruments — build them when the first
hardware session confirms the helper links at all.

## Recommended actions

1. **D1 before release** — the guard is a few lines and the failure mode
  (wired-CI-V CAT silently dying after a settings browse) is exactly the kind of
  fault that costs a bench evening.
2. RAM: adopt the five screen-scoped conversions as a standing one-per-cycle
  item (~13–15 KB total), each with its own nullptr audit.
3. Helper PSRAM: P1 next cycle; P2 as patch 11; P3/P4 after first hardware
  contact.
