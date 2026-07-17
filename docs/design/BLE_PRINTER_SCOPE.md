# BLE printer support — scope

*Assessment only. Nothing built. For post-1.0 planning.*

## Short answer

**Architecturally easy, and the field case is the stable one — but it is the single most
RAM-expensive thing that could be added to this board, and I cannot measure the cost from here.**
The recommendation is: **not before 1.0**, and gated on one measurement Paul can make in ten
minutes (below).

The appeal is real. The primary field use case is a receipt printer with no infrastructure, and
today that requires the printer to be a **WiFi** device — meaning an access point in a field, or
the printer's own AP. A BLE printer removes that entirely.

---

## What makes it easy

The print architecture already separates **page language** from **transport**, and that is the
whole ballgame:

- **The bytes do not change.** Cheap BLE thermal printers speak **ESC/POS** — which CardSat has
  emitted since 0.9.55 and which is already `FMT_ESCPOS`, the default. No new page language, no
  new renderer, no new width logic.
- **One chokepoint.** Every printer byte goes through `sockWrite()` in `print.cpp`. A BLE sink
  means teaching that one function to write to a GATT characteristic instead of a socket.
- **One config site.** `Printer::Sinks` is constructed in exactly one place (`App::printReport`).
  Adding `transport = BLE` alongside `RAW9100`/`IPP` is a config field and a switch.
- **The streaming model already fits.** Nothing buffers a whole report; it is emitted line by
  line. That is exactly what a 20-byte-MTU link needs.

Rough surface: a `ble_printer.{h,cpp}` module, one new `Transport` enum value, a branch in
`sockWrite()`/`begin()`/`end()`, a settings screen for scan-and-pair, and a stored device
address. **Small — if the RAM is there.**

---

## What makes it expensive

### The RAM, which this board does not have

This is the blocker, and it is not close. From Paul's own 0.9.57 boot log on real hardware:

```
heap: free 55376, min-ever 44036, largest block 31732
[net] heap before TLS: 49132 (largest block 25588)
[net] streamed 70075 bytes ... heap now 24960 (largest 8180)
```

During a GP fetch the heap falls to ~25 KB with the largest contiguous block at **8180 bytes**.
That is the margin CardSat runs on today, with **no PSRAM**. For calibration: the 0.9.57 bug where
a runaway BASIC program stranded **6 KB** was enough to break LoTW uploads.

Against that, Espressif's own `components/bt/Kconfig` describes releasing the BT text/data/bss as
*"total saving ~21kB or more of IRAM"* — and that is the **controller code region alone**, before
the NimBLE host, connection state, and GATT buffers. The realistic all-in cost is a large fraction
of CardSat's entire free heap.

**I could not measure this here** — there is no ESP toolchain in this environment, so the flash
and RAM deltas have to come from a real build. Everything above is from Espressif's published
configuration, not from a compile.

### WiFi + BLE together is rated "unstable" by Espressif

From the ESP32-S3 coexistence guide, the support matrix for **WiFi STA Connected + BLE
(scan / advertising / connected)** is **`C1` — "supported but the performance is unstable."** They
share one radio via time-division multiplexing.

This is less bad than it sounds, because of *which* case is which:

| workflow | rating |
|---|---|
| **Rove: WiFi off, BLE printer only** — the actual field case | **Y (stable)** |
| Print while WiFi is up (after a LoTW upload or GP fetch) | **C1 (unstable)** |
| BLE printer while tracking with the IC-9700 over LAN | **C1 (unstable)** |

The primary use case is the stable one. But CardSat's *current* mental model — "the printer is
reachable because WiFi is up" — inverts under BLE, and the mixed case is common (fetch elements,
then print a pass sheet).

### There is no such thing as "a BLE printer"

Unlike raw-9100, where any ESC/POS printer on TCP works, BLE thermal printers have **no common
GATT profile**. The widespread pattern is a vendor service with a write-without-response
characteristic carrying raw ESC/POS, but the UUIDs differ by vendor, and some families
(Phomemo, the "cat printer" clones) wrap ESC/POS in proprietary framing or need an unlock
sequence.

**I am not confident in specific UUIDs from memory, and I am not going to state them as fact** —
that is precisely the mistake that produced the 0.9.57 `String` bug, where I validated against my
own recollection instead of the real thing. Any implementation must start by **sniffing the actual
target printer**, not by trusting a table I wrote from memory.

Practical consequence: "BLE printer support" is really **"support for the printer Paul owns,"**
plus a scan/pair UI and a hope that others match. That is a much narrower promise than the WiFi
path makes today.

---

## Recommendation

**Not before 1.0**, for three reasons that compound:

1. It spends the scarcest resource on this board, and §1.1 of the roadmap already lists hardware
   verification as the largest 1.0 blocker — adding a new radio stack enlarges that surface rather
   than shrinking it.
2. It cannot be verified host-side. Unlike the interpreter, the projections, or the raster
   encoder — all of which were validated against reference implementations before touching
   hardware — a BLE stack can only be proven on the device, against one specific printer.
3. The existing WiFi path already covers the field case *if* the printer is a WiFi model, which
   the reference targets (TM-P20II, GZM8022) are.

### A data point from next door

**[Mini-FT8](https://github.com/wcheng95/Mini-FT8)** (Wei, AG6AQ) runs FT8/FT4 DSP *and* a USB-C
audio host on the same Cardputer ADV — a heavier compute and buffer load than anything CardSat
does. It uses **no Bluetooth at all**. That is not proof BLE won't fit, and its author may simply
have had no use for it; but the most memory-hungry Cardputer ham application in the wild reaching
for USB audio rather than BLE is at least suggestive of where the headroom is.

### The one measurement that decides it

Before any design work, Paul can settle this in ten minutes:

1. Add `#include <NimBLEDevice.h>` and a single `NimBLEDevice::init("")` call to a scratch build.
2. Compile and note the **flash delta** against the 3 MB Huge-APP partition.
3. Flash it and read the **`mem` baseline** — specifically `free` and `largest block` — against
   today's `free 55376 / largest 31732`.
4. Then run a GP update and watch `[net] heap before TLS`.

**If `largest block` after BLE init leaves less headroom than the ~25 KB that TLS currently needs
at its worst, the feature is dead on this board** and the answer is "use a WiFi printer." If it
survives with margin, the design below is small.

### If it goes ahead

- **`transport = BLE`** as a third value, not a fourth sink — it is the same ESC/POS bytes to a
  different pipe. Reuses format, widths, wrapping, and every existing report unchanged.
- **NimBLE, not Bluedroid** — materially smaller, which on this board is the only thing that
  matters.
- **BLE off unless in use.** Init on `Printer::begin()` when the transport is BLE, deinit in
  `end()`, and release the controller memory otherwise. Do *not* leave a radio initialised for a
  feature used once a session. (The 0.9.56/0.9.57 lesson: allocate on use, free on the screen
  transition, and verify the free actually happens — for a radio, "deinit" needs measuring too.)
- **Warn on the C1 case.** If WiFi is connected when a BLE print starts, either say so or offer to
  drop WiFi for the duration.
- **Scan-and-pair UI** in Settings → Network / data, storing the device address; a "test print"
  button matters more here than in the WiFi path because failures are opaque.
- **One printer family first** — the one on the bench — with the UUIDs *sniffed*, not assumed, and
  documented as "verified against X" with the same untested/at-your-own-risk banner the other
  hardware interfaces carry.

## Verdict

Genuinely attractive, structurally cheap, and the field case is the stable one. But it is the
wrong thing to spend the last of a no-PSRAM heap on while 1.0 is still gated on confirming the
radios that already exist. **Revisit after 1.0, and only after the ten-minute measurement above
says there is room.**
