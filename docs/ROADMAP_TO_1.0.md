# CardSat — Road to 1.0

*Status as of **v0.9.58.1** (July 2026), at the start of the 0.9.59 cycle. This is the single place to look for what stands between
CardSat and a 1.0 release: what's deliberately deferred, what's blocked on hardware
verification, and what the author has decided not to do. Each item links to the scoping
document that sized it, where one exists.*

CardSat is developed host-side (logic simulations, brace/parity gates, byte-for-byte validation
against reference implementations) and flashed, compiled, and confirmed on real hardware by the
author. That split defines most of what follows: the code is verified as far as a host can verify
it, and the remaining risk is concentrated in **things only real hardware and real radios can
answer**.

---

## 1. Blockers for 1.0

These are the items that a 1.0 label should not ship without.

### 1.1 Hardware verification of unconfirmed interfaces — **the largest blocker**

Most of CardSat's radio and rotator backends are host-tested but have never been exercised
against the hardware they target. A 1.0 that claims broad radio support without that
confirmation would be dishonest.

| Interface | Status |
|---|---|
| **Icom CI-V (single-pin)** | **Confirmed on IC-821H** — bidirectional exchange, Doppler compensation, knob tuning |
| **CAT over USB** | **Confirmed on IC-821H + FTDI (0.9.58)** — engage/disengage/re-engage + Doppler over many cycles; default-on since 0.9.59. Two adapters at once (radio + rotator) untested |
| **Icom LAN (IC-9700)** | Transport **confirmed against an IC-705**; the IC-9700 itself — the intended radio — untested |
| **Yaesu CAT** | Host-tested only |
| **Kenwood CAT** | Host-tested only |
| **GS-232 rotator** | Host-tested only |
| **Easycomm / SPID rotator** | Host-tested only |
| **Rotator over Grove or USB (0.9.58 transports)** | Host-tested only — no physical controller has been driven over either wire |
| **rotctl / rotctld** | Network commands verified; no physical rotator driven |
| **PstRotator** | Network commands verified; no physical rotator driven |
| **Yaesu direct rotator** | Host-tested only |
| **LoRa (SX126x)** | **Messaging confirmed on hardware** (0.9.39, vs. a T-LoRa Pager); the RX/hex monitor and sat-RX paths remain untested |

The authoritative, continuously-updated list is **[THINGS_TO_VERIFY.md](THINGS_TO_VERIFY.md)**.
Everything hardware-facing carries an untested / at-your-own-risk banner in the manual and the
interface docs.

**Outstanding IC-821H work items** (identified in earlier sessions, pending bench confirmation):
a higher default `catDelayMs`; MAIN-read as reference with push-only defaulting on; and PTT
polling defaulting off.

**IC-820H command table** is asserted (by the author) to behave identically to the IC-821H for
the MAIN/SUB band-select commands `D0`/`D1`, but this has not been verifiable from the IC-820H
manual directly. It remains an unverified claim in the code comments.

### 1.2 TLS certificate validation — *security, deferred by decision*

**Status: consciously deferred.** CardSat's HTTPS connections do not validate certificates.

The author's reasoning: CardSat runs on trusted LANs, is a hobbyist device, its users are
informed by the documentation, it is severely memory-constrained (a full CA bundle is not
affordable next to a 31 KB largest-free-block ceiling), and the device is active only a few hours
a day.

This is a defensible position for public-data fetches. It is **less** defensible for the
credential-bearing services (QRZ, Cloudlog, LoTW), and a 1.0 should make a deliberate call rather
than inherit the default.

**Scope:** [design/TLS_VALIDATION_SCOPE.md](design/TLS_VALIDATION_SCOPE.md) — recommends pinning
the specific root CAs for CardSat's fixed hosts (not a full CA bundle) plus an explicit opt-in
insecure valve, rather than all-or-nothing.

### 1.3 LAN control hardening — *security, deferred by decision*

**Status: consciously deferred.** The web-control and rotctld server interfaces are
**disabled by default**, which is the single most important mitigation and is already in place.
When enabled, they accept commands from any host on the LAN without authentication.

These actuate real antennas and radios, so the bar is higher than for an information page. A 1.0
should decide whether default-off is sufficient.

**Scope:** [design/LAN_HARDENING_SCOPE.md](design/LAN_HARDENING_SCOPE.md) — recommends a layered
approach: client subnet restriction plus an on-device "listening" and "last command" indicator,
both cheap; a token and read-only mode if more is wanted.

### 1.4 Build pinning — *process*

The PlatformIO platform, every library version, and Git dependencies should be pinned by commit,
with the resolved graph, linker map, and size report archived per release. Low risk, high value:
it makes releases reproducible and makes any future memory measurements comparable.

**Not started.**

---

## 2. Deferred by decision (revisit triggers noted)

These were scoped, measured, and consciously set aside. They are not blockers; they are
judgments that can be revisited if the facts change.

### 2.1 RAM lifecycle refactor — **deferred on evidence**

The proposal: a `ScreenScratch` union/arena so mutually-exclusive foreground screens occupy the
*largest* screen's RAM rather than the *sum* of all of them — plausibly reclaiming 25–35 KB.

**Why it's deferred:** a live memory baseline (v0.9.56, SD card, 92/150 satellites loaded) showed
**no memory pressure**:

- Free heap ~55 KB at idle; **min-ever 43,936 bytes** across a full session including GP refresh,
  seven data fetches, a 14-QSO LoTW upload cycle, and active CI-V + rotator control.
- **Largest free block rock-steady at 31,732 bytes** through every screen transition — zero
  variance, no fragmentation creep. The TLS handshake gate is 28,000, leaving ~3.7 KB of margin
  that never erodes.
- The one screen doing meaningful transient allocation (a game, ~8.3 KB) **fully recovers on
  exit** — proving the on-entry/on-exit pattern already works on this device.

The reclaimable RAM is real, but it is *comfort*, not *need*, and every KB of it carries
use-after-free risk. Measurement instrumentation shipped in 0.9.56 (`mem`, `memtrace`) so this can
be re-measured at any time.

**A note on 0.9.56's own footprint.** The new Tools features prompted a boot-RAM check. The Tiny
BASIC interpreter's ~3.8 KB working state was moved out of `.bss` and is now **heap-allocated only
while a program runs**, and the graphing calculator's default expression is seeded lazily. What
remains of the boot-heap change across 0.9.56 is compiled code footprint (`.data`/`.bss` from the
new features), not runtime allocation — it is the cost of the code existing, and is not
lazy-able. This is the pattern to follow for any future screen-local working memory.

**And a 0.9.57 correction to that pattern.** Freeing heap-allocated state is not enough if a
**surviving `String`** holds the memory instead. A runaway BASIC program's 6 KB output buffer was
stranded for the rest of the session — enough, on this no-PSRAM board, to starve the contiguous
block a TLS upload needs, which is how it surfaced (LoTW failing, not BASIC). Two lessons, both
general:

- **Arduino's `String` never releases its buffer on assignment.** Not `= ""`, not
  `= emptyString`, not `= String()`; `reserve()` cannot shrink either. Verified against the real
  `cores/esp32/WString.cpp`: all of them free **0 bytes**. Only **destroying the object**
  (destruct + placement-new) calls `invalidate()` → `free()`. Any future "release this buffer"
  code must do that, not assign.
- **Release on the screen transition, not in a key handler.** The first fix freed the buffer in
  the BASIC editor's backtick handler, which missed every other exit — `Fn`+`h` to Help walked
  straight past it. `loop()` now has a single transition hook that no path can bypass.

**Revisit triggers:** catalog scaling past ~150 satellites, or a larger TLS trust store (see 1.2)
changing the block-size picture.

**A measured reduction path now exists** — `docs/design/RAM_REDUCTION_ANALYSIS.md`. The key fact:
`App` is `static` (**`.bss`**, not heap), so shrinking its resident arrays moves the heap floor
down and raises **free heap *and* largest block by the same amount**, un-fragmentably. That is the
best currency for TLS. **Both low-risk items shipped in 0.9.58**, reclaiming **11,776 B**: `memos[64]` (6,656 B — a
rarely-visited screen's directory listing) is now heap-allocated on its screen, and
`visPasses[]`/`vlPasses[]` are merged into one `passScratch[128]` (5,120 B — they were never live
at once, and `buildOrbit()` already reused one of them as scratch informally; the merge makes that
contract explicit). Expected: `largest block` 31,732 → ~43,508 — **more than twice** what an
`EspUsbHost` at `MAX_DEVICES=1` would cost, banked before any USB work. **Pending bench
confirmation of the figure.** Still available: `SatEntry::amsatName[28]` × 150 = ~4,200 B for data
only ~47 satellites have.

**Scope:** [design/RAM_LIFECYCLE_SCOPE.md](design/RAM_LIFECYCLE_SCOPE.md) — sequence the safe,
isolated pieces first (share the two 128-entry pass arrays; allocate the memo directory only while
browsing; allocate transponders only when loaded).

### 2.2 Frequency storage ceiling — *disclosed, migration deferred*

Frequencies are stored as `uint32_t` Hz, giving a **4.294 GHz ceiling**. This is documented in
`satdb.h` and the code reference. Migrating to 32-bit kHz storage would raise the ceiling but is a
data-format change; the **disclosure** shipped in 0.9.56, the **migration** is deferred.

Practical impact: the amateur satellite service's microwave allocations above 4.294 GHz (5.6 GHz
and up) cannot be represented.

### 2.3 BLE printer support — *deferred, pending one measurement*

**What:** connect a Bluetooth LE thermal printer instead of requiring a WiFi one. Attractive
because the primary field use case is a receipt printer with no infrastructure, and today that
still needs an access point or the printer's own AP.

**Why it's deferred:** RAM, on a board that has none to spare. Espressif's own `components/bt`
Kconfig puts the BT controller's text/data/bss at *"~21kB or more of IRAM"* before the NimBLE
host and GATT buffers, against a measured `free 55376 / largest block 31732` at boot — and
`largest block 8180` during a TLS fetch. The 0.9.57 bug where a stranded **6 KB** broke LoTW
uploads is the calibration for how little slack exists.

Two further frictions: Espressif rates **WiFi-STA-connected + BLE** as **C1, "performance is
unstable"** (the pure-BLE field case is stable — it is the print-after-a-fetch case that is
not); and there is no common GATT profile across BLE thermal printers, so "BLE support" really
means "support for the printer on the bench."

**Revisit trigger:** a ten-minute measurement — add `NimBLEDevice::init("")` to a scratch build,
then compare the `mem` baseline and `[net] heap before TLS` against today's numbers. If the
largest contiguous block no longer clears what TLS needs, the answer is settled.

**Scope doc:** `docs/design/BLE_PRINTER_SCOPE.md`.

### 2.4 USB devices: CAT, rotator, printer — *CAT and rotator shipped in 0.9.58; printer declined*

Research: `docs/design/USB_DEVICES_SCOPE.md`. USB **mass storage** was declined separately (the
web UI already gets data out) — `docs/design/USB_MSC_DECISION.md`.

**The finding:** Espressif ships official Apache-2.0 USB **host** drivers for exactly the chips
involved — `usb_host_cdc_acm`, `usb_host_cp210x_vcp` (what Icom rigs use), `usb_host_ch34x_vcp`,
`usb_host_ftdi_vcp`, and a `usb_host_vcp` wrapper that auto-selects the right one. There is **no**
printer-class component.

**CAT over USB was the biggest prize, and it shipped in 0.9.58 — then went
default-on in 0.9.59.** The prediction held exactly: the protocol encoders already
took a `Stream*` (`civ.h`, `kenwood.h`) and there is one `makeRig()` factory, so
`CAT_USB` landed as a pure *transport* — every protocol and radio unchanged. Both
predicted uses apply: modern rigs with a USB port (IC-9100; the IC-9700 already has
LAN), and — the bigger one — **every pre-USB rig via a $5 USB-serial adapter**,
replacing the MAX3232 harness `WIRING.md` documents. Bench-proven on an IC-821 +
FTDI. The Icom USB-echo quirk that Hamlib probes for at runtime was already solved
by CardSat's `drainEcho()`.

**Rotator over USB shipped in the same release**, alongside the Grove transport
(`Rot wire` decouples the serial protocol from the wire). Radio *and* rotator can
share the USB host through a hub, each bound to an explicitly chosen adapter —
built and guarded, **untested with two physical adapters**. See
`docs/interfaces/ROTATOR_TRANSPORTS.md` for what shipped and
`docs/releases/RELEASE_NOTES_0.9.58.md` for the verification story.

**USB printers**: three possible interfaces (printer class 0x07 / serial bridge / CDC-ACM), all
carrying the same ESC/POS bytes CardSat already emits. Only the class-0x07 case needs a driver
written. Smallest payoff of the three — a WiFi printer in AP mode already works with zero code.

**The build blocker does not exist.** [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost)
is a plain **Arduino library** (v2.3.0, Library Manager) doing USB host on the ESP32-S3 — it
includes `<usb/usb_host.h>` directly, so the IDF host stack *is* reachable from a sketch. No
`framework = arduino, espidf`, no PlatformIO-only feature, and the single-file `CardSat.ino` path
survives. Its `EspUsbHostCdcSerial` **is an Arduino `Stream`** covering CDC-ACM + FTDI/CP210x/CH34x
— a drop-in for the `Stream*` that `makeRig()` already passes to the protocol encoder.

**RAM — one build flag decides it.** EspUsbHost's `ESP_USB_HOST_MAX_DEVICES` defaults to **8**, and
its own header warns each slot is *"a sizable static DeviceState (several KB)... this constant
dominates the library's static RAM use."* Eight slots is **≥ ~36 KB of `.bss`** — fatal. But
`-DESP_USB_HOST_MAX_DEVICES=1` is explicitly supported, and CardSat needs exactly one device (one
port). That is **≥ ~4.6 KB permanent** — the same order as the 3.8 KB the BASIC VM cost before
0.9.56 made it lazy — plus an estimated ~12–15 KB only while `begin()` is active, reclaimed by
`end()`.

**Next step is one compile:** build with `-DESP_USB_HOST_MAX_DEVICES=1` and measure the static
delta. Then verify `end()` actually returns the heap rather than assuming it — that assumption is
precisely what made the 0.9.57 `basicFree()` bug.

> **Outcome (0.9.58-wip, 2026-07-15) — the flag is only safe as a *global* flag.** The
> implementation first tried the per-file variant (`#define` in `usbserial.cpp` before the
> include, to keep the Arduino IDE path flag-free). That is a one-definition-rule violation:
> the slot array is a **member of the `EspUsbHost` object**, so the library's own translation
> unit kept the 8-slot layout while `usbserial.cpp` allocated a 1-slot object — the first
> library call wrote past the object and **froze the firmware the moment USB CAT was enabled**.
> Shipped fix: no define at all; the host object is heap-allocated only between `begin()` and
> `end()`. A global `-DESP_USB_HOST_MAX_DEVICES=2` stays available to PlatformIO builds (2, not
> 1: a hub in the chain is itself a device). Sizing revision from the v2.3.0 source: the
> per-slot `hidReportDescriptors` entries are ~8 B each (512 was a parse cap, not storage), so a
> slot's static fields are ~1–2 KB and the whole 8-slot object is on the order of 10–20 KB —
> now transient, not `.bss`. Details: the comment block at the top of `src/usbserial.cpp`.

**Risk to weigh:** EspUsbHost is one maintainer's library, self-described as under active
development with possibly-breaking 2.x APIs. Pin it (§1.4) and keep wired CI-V / SC16IS750 as the
primary paths, with USB as an alternative transport.

### 2.5 Rotator transports: USB and Grove — *designed, not built*

Two related asks, one answer. Scope docs: `docs/design/ROTATOR_USB_OPTIONS.md` and
`docs/design/GROVE_ROTATOR_SCOPE.md`.

**Correction on record:** I first said the rotator had "seven backends, none `Stream`-based,"
framing USB rotator support as a big refactor. It is **six** concrete backends and **only three are
serial** (`Gs232`, `Easycomm`, `Spid`); the rest are TCP, UDP, or an I²C ADC + relay board that
could never use USB. Measured: **~101 lines of triplicated SC16IS750 plumbing** against **~174
lines of unique protocol logic**.

**The design (`RotIo`):** extract only the byte-level shim behind a small interface — the existing
SC16IS750 code moves **verbatim** into `RotIoBridge`; `RotIoUsb` wraps `UsbSerial::stream()`;
`RotIoGrove` wraps `HardwareSerial(1)` on G1/G2. Protocol methods are not rewritten, not moved and
not duplicated; only `putc_(c)` becomes `_io->write(&c,1)`.

**Why not a separate backend per protocol per transport:** three protocols × three transports =
nine classes, with the protocol logic copied three times and expected to stay in step forever.

**The Grove case is the surprise: it does not need USB.** The Cardputer's own Grove port *is* G1/G2
— the same pins as wired CI-V (`config.h:78`) — so it is free under `CAT_NET`, `CAT_RIGCTL` *or*
`CAT_USB`. With an IC-9700 on LAN it is testable **today**, which makes it the right way to prove
the `RotIo` seam before anything depends on the unproven USB stack. It drops the SC16IS750 (one
chip, one I²C dependency) but still needs a MAX3232 — GS-232 is RS-232.

**The thing that must not be skipped:** G1/G2 would then be contended three ways (CAT, GPS,
rotator). Today CardSat only *warns* in `WIRING.md`. With three claimants the Settings screen must
**refuse the combination at selection time**, or an operator gets a rotator that silently does not
move.

### 2.6 The single-file `.ino` is near an Xtensa limit — *discovered, not yet a blocker*

Found while first compiling USB CAT (0.9.58). The link failed with:

```
dangerous relocation: l32r: literal target out of range (try using text-section-literals)
  in function `EspUsbHostCdcSerial::~EspUsbHostCdcSerial()'
```

**Not a library bug and not really a USB bug.** Xtensa's `l32r` reaches a literal pool through a
**signed 16-bit offset** — roughly 256 KB backwards. `CardSat.ino` is a **~2.1 MB single
translation unit**; adding one header-defined class with a vtable pushed a literal reference out
of reach. USB CAT was the straw, not the cause.

**Workaround** (documented in `BUILD_AND_FLASH.md`): a `build_opt.h` next to the sketch containing
`-mtext-section-literals`, which places literal pools beside the code that uses them. That is the
fix the linker itself suggests, and it is per-sketch — the Arduino IDE already reads `build_opt.h`.

**PlatformIO is unaffected** — `src/*.cpp` are separate TUs, each far below the limit. So this is
specifically a cost of the dual-representation invariant.

**Why it matters beyond USB CAT:** the `.ino` has finite headroom and this is the first thing to
reach it. Anything that adds a header-defined class with virtual methods — a BLE printer, a USB
rotator, another third-party wrapper — will hit the same wall. Options if it recurs: ship
`build_opt.h` in the repo (simple, but the Arduino IDE silently ignores it if the sketch folder
name and `.ino` name ever diverge), or accept that some features are PlatformIO-only, or split the
`.ino` — which would end the single-file build that Launcher users rely on.

**No action needed yet.** The stock build is nowhere near the limit; only the opt-in USB CAT path
crosses it, and it has a documented one-line fix.

### 2.7 what3words support — *declined, by design*

Deliberately excluded from the location converter. It is a proprietary, network-only wordlist
lookup rather than an offline algorithm: it cannot be computed on-device, the wordlist is
licensed, and it would break the "all Tools math is local" contract every other tool honors.
Documented in the manual so the omission is explained rather than looking like a gap.

### 2.8 Ideas from Mini-FT8 — *two worth doing*

[Mini-FT8](https://github.com/wcheng95/Mini-FT8) (Wei, AG6AQ) — the project that inspired CardSat,
same board. Reading it turned up two gaps worth filling:

- **USB mass storage** (its `C` key exposes FATFS to a PC, then remounts). CardSat has none, so
  getting reports/logs/memos/`calib.txt` off means **physically removing the microSD**. This also
  caught a **documentation bug**: the manual told operators to use "USB mass storage" that does
  not exist (now fixed). Independent of the USB *printer* question, and the stronger idea.
- **An on-device performance monitor** (its `P` key). CardSat's `mem`/`memtrace` are serial-only
  — the 0.9.57 heap bug was found *because* a laptop was attached. A live heap/largest-block
  readout on the About screen would make that visible in the field and give §2.1 a live number.

Also considered and mostly declined: uniform `1`–`6` row selection (good idiom, but CardSat's
Satellites screen already binds `2`/`3` — only worth it if genuinely uniform), flat menu pages
(doesn't scale to ~90 settings), an ignore list (favorites mostly cover it), copy-to-SD (CardSat
is already SD-first; MSC solves the LittleFS case better).

**Ideas doc:** `docs/design/IDEAS_FROM_MINI_FT8.md`.

### 2.9 UX items

Scoped but not scheduled: a task-hub navigation model, a screen registry, accessibility modes, and
a two-tier disk catalog.

---

## 3. Feature-completeness assessment for 1.0

**What's solid.** The core mission — track satellites, predict passes, tune radios for Doppler,
point rotators, log and upload QSOs, plan roves, and work offline — is complete and, for the
CI-V path, hardware-confirmed. Printing is comprehensive (twenty-nine menu-listed reports plus
context-only ones, three sinks, nine page-description formats including on-device PWG/URF
raster). The Tools hub is a genuine offline bench (55 tools). The documentation is thorough: a
140-plus-page manual, a features list, per-interface wiring guides, and a design-decision archive.

**What a 1.0 needs beyond the blockers above.** Nothing structural. The gap between 0.9.57 and
1.0 is mostly *confidence*, not *scope*: hardware confirmation of the radio/rotator matrix, a
deliberate security decision, and reproducible builds.

---

## 4. Honest notes on verification method

The distinction matters for anyone evaluating a 1.0 claim:

- **Host-validated** means: logic simulated on x86, algorithms checked byte-for-byte against
  reference implementations (the UTM/MGRS/Plus Code projections, the Maidenhead subsquare math,
  the PWG raster encoder, the Tiny BASIC interpreter), brace/parity/screen gates green, and the
  final in-firmware source re-extracted and re-run to catch transcription errors.
- **Bench-confirmed** means: flashed to a real Cardputer ADV and exercised against real hardware
  by the author.

Host validation has repeatedly caught real bugs (a Maidenhead subsquare error, a Plus Code
float-drift error, a PRINT newline bug in the BASIC interpreter, all found by validating against
reference implementations or by testing the documented examples). It has also repeatedly *missed*
a whole class of bug that only the compiler catches — **anonymous-namespace scoping and
definition-ordering errors** — because the host harnesses flatten code structure into a single
scope. Five such bugs were caught by `arduino-cli` during 0.9.56 development: private class
constants unreachable from an anonymous namespace; a helper defined outside the namespace of the
type it took; and three use-before-declaration errors where a method was placed earlier in the
file than the file-scope helpers it called.

**The resulting rule:** a method that calls tool/screen helpers belongs *after* them in the
translation unit, not grouped with its logical siblings. The gates cannot see this; only the
compiler can.

**A second class the compiler cannot catch either: wrong assumptions about library behavior.**
0.9.57 shipped a memory fix that did nothing, because it was validated against a *hand-written
model* of Arduino's `String` rather than the real one — the model freed on assignment; the real
implementation never does. It compiled, the host test passed, and the bug survived to the field.
The fix was only found by fetching `cores/esp32/WString.cpp`, compiling it, and measuring. **When
behavior depends on a library's internals, test against the library, not a stand-in.** A host
harness that models the dependency is testing the model.

**Neither substitutes for the other, and neither substitutes for on-air use.**

---

## 5. Where the details live

- **[THINGS_TO_VERIFY.md](THINGS_TO_VERIFY.md)** — the authoritative, per-feature verified /
  unverified list. The most important companion to this document.
- **[design/REVIEW_0.9.55_ASSESSMENT.md](design/REVIEW_0.9.55_ASSESSMENT.md)** — the codebase
  review that produced this sequencing. All five of its 0.9.56 items shipped.
- **[design/TLS_VALIDATION_SCOPE.md](design/TLS_VALIDATION_SCOPE.md)**,
  **[design/LAN_HARDENING_SCOPE.md](design/LAN_HARDENING_SCOPE.md)**,
  **[design/RAM_LIFECYCLE_SCOPE.md](design/RAM_LIFECYCLE_SCOPE.md)** — the three deferred items,
  scoped in full.
- **[releases/](releases/)** — per-release notes and test checklists.
