# USB CAT (`CAT_USB`) — implementation notes

*Built in 0.9.58, **off by default**, **unproven on hardware**.*

## What was built

A fourth CAT transport: a USB↔serial adapter (FTDI / CP210x / CH34x) or CDC-ACM device on the
Cardputer's USB-C port, instead of the G1/G2 UART and its level shifter.

**It works for every protocol and every radio**, because of a seam that already existed: the three
wire-level backends (`CivRig`, `YaesuRig`, `KenwoodRig`) each hold a `Stream* _stream` and know
nothing about the transport underneath — only their `begin()` binds a UART. So:

```cpp
rig->setExternalStream(UsbSerial::stream());   // Rig base class, rig.h
rig->begin(...);                                // now skips all UART/pin setup
```

Nothing in the Doppler loop, calibration, or UI changes. CI-V, Yaesu 5-byte, and Kenwood ASCII all
ride the new transport unmodified.

## Turning it on

```
-DCARDSAT_HAS_USBCAT=1
```

plus the **[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost)** library (Arduino Library
Manager). Then **Settings → Radio / CAT → CAT type → USB serial**. Two build notes — a one-line
library patch for arduino-esp32 3.2.1 and, for the `.ino` path, a `build_opt.h` — are in
`docs/BUILD_AND_FLASH.md`.

> **Superseded (0.9.58-wip):** this doc originally required `ESP_USB_HOST_MAX_DEVICES=1` as a
> per-file define in `usbserial.cpp`. That **froze the firmware on enable**: the slot array is a
> *member of the `EspUsbHost` object*, so the library's own translation unit (compiled with the
> default of 8) and `usbserial.cpp` (compiled with 1) disagreed about the object's layout — an
> ODR violation, and the first out-of-line library call wrote past the object. The shipped design
> sets **no slot define anywhere**: the host object is heap-allocated on engage and freed on
> disengage, so the default 8 slots (~10–20 KB for the whole object) cost nothing at rest.
> A **global** define is safe (it reaches the library TU too), and the repo now ships one for
> ALL builds: `build_opt.h` with `-DESP_USB_HOST_MAX_DEVICES=4` (the Arduino IDE passes it to
> every translation unit — verified in arduino-esp32 3.2.1 platform.txt; PlatformIO users put
> the same flag in `build_flags` instead, as PlatformIO does not read `build_opt.h`).
> Full analysis: the comment block at the top of `src/usbserial.cpp`.

## The three constraints, and how each is handled

### 1. The console and USB host share the S3's one internal USB PHY

CardSat builds with USB CDC On Boot, so `Serial` rides the S3's USB port — and the ESP32-S3 has
**one internal USB PHY**, shared between the USB-Serial-JTAG/CDC console and the OTG controller
that host mode needs. Only one can own it.

`UsbSerial::begin()` calls `Serial.flush()` then `Serial.end()`; `UsbSerial::end()` calls
`Serial.begin(115200)`. So the console is live **except while the radio is engaged**, which is what
was asked for, and is the pattern Mini-FT8 uses for its FATFS-to-PC mode.

**Consequence to expect on the bench:** the PC sees the CDC port disappear when you press `r` and
reappear when you press it again. Terminal programs generally cope; some hold the handle and need
reopening. **You cannot watch `[mem]` over serial while USB CAT is engaged** — which is part of why
the on-device performance monitor (About → `m`) exists.

### 2. RAM: allocate on engage, free on disengage

The expensive part — the IDF host stack, its daemon task, transfer buffers — exists only between
`host.begin()` and `host.end()`. Estimated ~12–15 KB, **unmeasured**.

The lifecycle is a **reconciliation in `loop()`**, not a hook on the toggle:

```cpp
if (cfg.catType == CAT_USB) {
  const bool want = radioOut && rig;
  if (want && !UsbSerial::active())      { ...begin, bind, rig->setExternalStream... }
  else if (!want && UsbSerial::active()) { rig->setExternalStream(nullptr); UsbSerial::end(); }
}
```

**Why reconcile.** `radioOut` is *set* in one place (`keyTrack` `'r'`) but *cleared* in **six** —
the emergency stop, charge mode, losing the tracked satellite, and others. Hanging teardown off the
toggle would miss five of them and strand the host stack *and the console with it*. This is the
`basicFree()` lesson: a handler only knows the exits it implements. Reconciling desired against
actual state cannot be bypassed by a new call site.

Note the teardown order: `setExternalStream(nullptr)` **before** `UsbSerial::end()`, so the rig
never holds a pointer to a dead `Stream`.

If `begin()` fails, `radioOut` is forced back to `false` and the error goes to the status line —
CardSat does not pretend to be driving a radio that is not there.

### 3. Composite devices

**Your testable path is the simple case.** An IC-821 + USB↔CI-V cable means *the cable* is the USB
device — an FTDI FT232 with a single vendor interface. Not composite.

**The composite case is a modern rig plugged in directly** (IC-9100/IC-9700 USB-B: a serial
interface *plus* a USB Audio interface). EspUsbHost's README warns the ESP32-S3 "has a small number
of USB host channels" which "composite devices, hubs, audio... can exhaust quickly."

**What was done:** bind the serial interface, never claim audio (CardSat has no use for rig audio).
`onDeviceConnected` records VID/PID and product string for the status line so a failure is
visible rather than silent.

**What was not done:** anything to guarantee the composite case works. It cannot be tested here or
on your bench. If an IC-9700 over USB exhausts the channels, that is a bench finding — the design
does not pretend to have solved it.

## How the USB serial device is actually detected

Worth being precise, because **CardSat does none of it** — `UsbSerial::begin()` calls
`EspUsbHostCdcSerial::begin()` and the library decides. There are **two independent mechanisms**,
and the difference matters for which adapters work.

### 1. CDC-ACM — by interface class (standards-based)

EspUsbHost walks the configuration descriptor looking for:

| | |
|---|---|
| `USB_CLASS_CDC_CONTROL` | `0x02` — the control interface |
| `USB_CLASS_CDC_DATA` | `0x0A` — the bulk data interface |

Any device that implements the CDC spec properly is found this way, with no per-device knowledge.

### 2. Vendor bridges — by a hardcoded VID:PID allow-list

FTDI/CP210x/CH34x/PL2303 chips present as interface class **`0xFF` (vendor-specific)** with no
standard descriptor — **there is nothing to detect them by.** The library must know them by ID.
From `EspUsbHost.cpp`, `isKnownVendorSerial()`:

| chip | VID | accepted PIDs |
|---|---|---|
| **FTDI** | `0x0403` | `6001`, `6010`, `6011`, `6014`, `6015` |
| **CP210x** | `0x10c4` | `ea60`, `ea70`, `ea71` |
| **CH34x** | `0x1a86` | `5523`, `55d3`, `7522`, `7523` |
| **PL2303** | `0x067b` | `2303`, `23a3` |

Anything else — including a **clone with an unlisted PID** — enumerates perfectly and is simply
not recognised as a serial device.

### What this means for the IC-821 + FTDI cable

A genuine FTDI CI-V cable is almost certainly `0403:6001` (FT232R) or `0403:6015` (FT230X/FT231X)
— **both on the list**. It should be found. A counterfeit or a re-branded chip with a custom PID
would not be.

**The failure is now distinguishable**, which it was not in the first draft. `begin()` tracks
whether *anything* enumerated:

| status line | meaning | fix |
|---|---|---|
| `No USB device detected` | nothing enumerated at all | cable, power, port |
| `Not a known serial adapter: FTDI ... 0403:6001` | it enumerated but the library would not bind it | the VID:PID is not on the allow-list |

The second message carries the actual VID:PID, so an unlisted adapter can be identified in one
glance — and adding it upstream (or locally) is a one-line change to `isKnownVendorSerial()`.

This was worth fixing: the two failures need completely different responses, and *"No USB serial
device found"* gave the operator no way to tell them apart.

## Reversibility

The whole feature is behind `CARDSAT_HAS_USBCAT`, default **0**. With it off:

- `usbserial.h`/`.cpp` compile to nothing; `EspUsbHost.h` is never included
- `CAT_TYPE_N` is 3, so the Settings row cannot even reach `USB serial`
- the `loop()` lifecycle and the `applyRadioFromCfg()` branch vanish
- the only unguarded addition is `Rig::setExternalStream()` — an inert setter with no dependency

A default build is byte-for-byte what it was. **If this turns out to be infeasible, delete two
files and three guarded blocks.**

## What needs proving on the bench

In order, because each is a prerequisite for the next:

1. **It compiles** with the flags and the library installed.
2. **The default build still compiles** without the library present. *This matters more than the
   feature* — it is the promise that everyone else's build is untouched.
3. **The IC-821 + FTDI CI-V cable enumerates.** The status line shows the VID/PID on engage. If it
   says `Not a known serial adapter: ... 0403:xxxx`, the chip is real but its PID is not in
   EspUsbHost's allow-list — note the number, because that is a one-line fix. If it says
   `No USB device detected`, nothing enumerated at all: cable, power, or port.
4. **CI-V actually works** over it — frequency set/read, the same as the wired path.
5. **The console comes back.** Disengage the radio; the serial monitor should reappear.
6. **RAM returns.** About → `m`: largest block should drop on engage and come back on disengage.
   **As with the 0.9.57 `String` bug, `end()` returning is not proof the memory came back.**

## Honest status

- **Not compiled.** No toolchain here.
- **Not tested.** Nothing about this has touched hardware.
- **The API was verified against the real header** — `EspUsbHost.h` was fetched and every symbol
  checked. One error was caught that way: I had written `cfg.baudRate`; the field is `cfg.baud`.
- **The seam was verified against the real source** — all three backends do hold `Stream* _stream`
  and only bind the UART in `begin()`.
- **The estimate of ~12–15 KB is an estimate**, not a measurement.

## The rotator: not done, and why

The request covered rotator-over-USB too. **It is a materially bigger job than CAT, and it is not
built.**

CAT was small because the three protocol backends already share a `Stream*`. **The seven rotator
backends do not.** `Gs232Rotator`, `EasycommRotator`, `SpidRot2Prog` and the rest each hold an I²C
address and drive the **SC16IS750 bridge registers directly** via their own `bridgeInit()` /
byte-level helpers. There is no `Stream` to swap.

USB rotator support would mean **refactoring seven working, bench-verified backends onto a `Stream`
abstraction first** — a change to hardware code that has been proven on real rotators, that I
cannot test, in the same session as an unproven USB stack.

And the payoff is limited by the port count: **there is one USB port**. A USB rotator and USB CAT
are mutually exclusive, so a station wanting both still needs the SC16IS750 for one of them.

**Correction, after actually counting:** it is **three** serial backends, not seven —
`Gs232Rotator`, `EasycommRotator` and `SpidRotator`. The other three are network (`RotctlRotator`
TCP, `PstRotator` UDP) or relay-driven (`YaesuRotator`, an I²C ADC + PCF8574), and could never use
USB. Measured: ~101 lines of triplicated SC16IS750 plumbing against ~174 lines of unique protocol
logic.

**Recommendation:** prove the CAT path on the bench first. If USB CAT works, the rotator work
becomes a known-value proposition rather than two unknowns stacked. If it does not, it was never
worth doing. The three ways to do it — including "separate USB backend per protocol", which avoids
touching bench-verified code at the cost of duplicating the protocol logic — are laid out in
`ROTATOR_USB_OPTIONS.md`.
