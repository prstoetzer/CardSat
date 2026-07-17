# USB printer support — scope

*Assessment only. Nothing built. Companion to `BLE_PRINTER_SCOPE.md`.*

## Short answer

**Viable. There is no hardware obstacle.**

The Cardputer ADV sources VBUS from its own battery — Paul runs Mini-FT8 with a **QMX over USB,
on battery, no external power, no special setup**. Host mode works. A self-powered USB printer is
electrically the same case as the QMX (thermal heads need their own PSU regardless, so USB is
data-only).

The one genuine cost is that **USB Host and CardSat's USB serial console are the same OTG
controller**, so a USB print gives up the console for its duration — including the `[mem]` tracing
that found the last two memory bugs. Mini-FT8 shows the mitigation: flip the controller at
runtime, don't choose at build time.

Whether it's *worth* building is a real question — a WiFi receipt printer in AP mode already does
the job with zero code. But it's a trade-off, not an impossibility.

*(This document has been wrong four times about the hardware, each time corrected by Paul. See
the honest note at the end — the pattern matters more than any individual error.)*

## Prior art: Mini-FT8 does USB host on this exact board

**[Mini-FT8](https://github.com/wcheng95/Mini-FT8)** (Wei, AG6AQ — the project that inspired
CardSat) is the reference point for all of this, because it does on a Cardputer ADV the things
this document was speculating about:

- **USB host, for real** — a **QMX** as a composite audio + CAT device, and a USB-C audio adapter
  as a **UAC** input (`KH1-USBC` mode). Paul runs the QMX case **on battery with no external
  power**, which is the proof that matters: the board sources VBUS unaided.
- **Runtime mode switching** — its `C` key *"stops radio audio and exposes FATFS to the PC"*,
  then remounts on a second press. That is the OTG controller being flipped between **host**
  (audio in) and **device** (mass storage) while running. This is the pattern CardSat would need
  to keep its serial console.
- **A power-budget caveat, specific to one setup** — its KH1 mode asks for external 5 V into
  PORTA. That is *not* a general limit of the board (the QMX needs none of it); it is a quirk of
  that particular arrangement. I misread it twice as a hardware constraint.

Anything CardSat might do with USB should start from that, not from my reading of a datasheet.

## Correction: what the PHY constraint actually says

Mini-FT8 also settles this in passing: with its GNSS-over-LoRa option it notes *"the physical
G4/G5 debug UART path is disabled... **USB Serial/JTAG host commands still work**"* — the two
controllers are independent, exactly as the docs say and contrary to my first reading.

Espressif's USB Host guide:

> The ESP32-S3 contains two USB controllers — the USB-OTG and USB-Serial-JTAG. However, **both
> controllers share a single PHY, which means only one can operate at a time.** To use USB Host
> functionality while the USB-Serial-JTAG is active (e.g., for debugging or flashing), an
> **external PHY is required**.

I originally read this as "Host is impossible here." It isn't. That sentence is about **USB-OTG
vs USB-Serial-JTAG** — two *different* controllers. The S3's OTG controller genuinely supports
host mode; Arduino's own USB API page says so outright: *"USB as Host — you can connect devices
on the ESP32, like external modems, mouse and keyboards"* (with the caveat *"this mode is still
under development"*).

## Cost 1: USB Host and the USB serial console are the same controller

CardSat is built with **USB CDC On Boot enabled** (required, per `BUILD_AND_FLASH.md`), which
puts TinyUSB CDC on the **OTG controller** — the same one host mode needs. So the conflict is
real, but it is a **mode choice**, not a hardware prohibition. Switching to Host means giving up:

- **The serial command console** — 104 `Serial.*` call sites in `app.cpp`.
- **The serial print sink** (`Sinks.toSerial`) — the documented fallback for operators with no
  printer at all.
- **`[mem]` tracing** — the instrumentation that found and then confirmed the 0.9.57 heap bug.
- **Flashing and monitoring over the same port.**

These are recoverable: the console could move to the Grove UART, and flashing already has a
Launcher/OTA path. But it is a genuine cost, and it removes the instrumentation
(`[mem]`) that found the last two memory bugs.

## Cost 2: VBUS — **not a problem.** (I got here via three wrong explanations.)

**The bottom line, from Paul's bench: he runs Mini-FT8 with a QMX over USB, on battery, with no
external power and no special setup.** The Cardputer ADV sources VBUS from its own battery and
runs a composite USB device (audio + CAT). That is the fact; everything below is just me being
wrong about why.

### What the PORTA micro switch actually does

**It has nothing to do with USB.** It selects the direction of the PORTA (Grove) 5 V pin:

| position | meaning |
|---|---|
| **5VOUT (Left)** | the Cardputer **sources** 5 V on PORTA — powers a Grove device (e.g. the GPS) |
| **5VIN (Right)** | the Cardputer is **powered/charged from** 5 V on PORTA |

Per Paul: *"the micro switch for the grove port only switches 5V out or in (you can charge and
power the Cardputer over the grove port). The grove port switch is irrelevant for USB."*

### So what is Mini-FT8's KH1 note about?

> *"For `KH1-USBC`, supply 5 V to PORTA; otherwise, the USB-C OTG port will not be powered.
> **Make sure the micro switch is on the right**"*

Its KH1 diagram is annotated **`SW: 5VIN (Right)`** with an external *Power Cable* feeding PORTA.
Read correctly, that is **external power going *into* the Cardputer** — a **power-budget**
statement, not a routing one. That specific arrangement (USB-C audio adapter hosted *plus* the
KH1 CAT line) evidently needs the external feed to hold VBUS up.

Paul: *"I believe you need 5V out from grove to use a KH1 with Mini-FT8 but you don't with the
QMX, and you never need to use the 5V in switch."*

**It is a KH1-setup quirk, not a limit of the board.** A QMX needs none of it.

### What this means for a printer

A USB receipt printer is **self-powered** — a thermal head draws 1.5–2 A and cannot run off USB,
so real ones have their own PSU and use USB for **data only**. A self-powered device needs the
host to assert VBUS to enumerate, but draws essentially nothing from it. **That is exactly the
QMX case, which works on battery today.**

There is no VBUS obstacle.

## Cost 3: the interface question — *"do they all use the same one?"*

**No, and this is now the main unknown.** Here is what is spec-checkable versus what I would be
guessing at.

### What the USB spec actually defines

The **USB Printer Class** is `bInterfaceClass = 0x07`:

| field | value |
|---|---|
| subclass | `0x01` — printers |
| protocol | `0x01` unidirectional · `0x02` bidirectional · `0x03` IEEE-1284.4 |
| endpoints | one **bulk-OUT** (plus a bulk-IN if bidirectional) |
| payload | whatever page language the printer speaks |

For an ESC/POS receipt printer the bulk-OUT payload **is ESC/POS** — byte-for-byte what CardSat
already emits as `FMT_ESCPOS`. If a printer implements class 0x07, the driver is genuinely small:
enumerate, find the bulk-OUT endpoint, write bytes.

### What I don't know, and won't guess

**Cheap thermal printers may not implement class 0x07 at all.** The plausible alternatives:

- a **USB-serial bridge** (CH340 / CP210x / FTDI / PL2303) presenting a vendor interface, where
  ESC/POS goes over what is effectively a COM port — a *different* driver entirely
- **CDC-ACM**, same idea via the standard serial class
- a **vendor-specific bulk interface** with no class driver at all

Each needs its own host-side driver. "USB printer support" could therefore mean one small driver,
or three.

**I am not going to assert which is common.** I flagged the same risk for BLE printer GATT UUIDs,
and this session has already produced four confidently-wrong hardware claims from exactly this
habit. The answer is an hour with real hardware, not recollection.

### The check that resolves it

Plug the target printer into a Linux box (or a Mac) and read what it enumerates as:

```
lsusb -v -d <vid:pid> | grep -A5 -i "interface descriptor"
```

- `bInterfaceClass 7` → USB Printer Class → the small driver, ESC/POS straight to bulk-OUT
- `bInterfaceClass 255` + a CH340/CP210x ID → serial bridge → needs that chip's driver
- `bInterfaceClass 2/10` → CDC-ACM → needs a CDC host driver

**One command, per printer, and the answer is exact.** Worth doing before any design work — and
worth doing on whichever printer is actually on the bench, since that is the one that has to work.

## What's genuinely appealing about it

The attraction is the same as BLE, and stronger: **the bytes are already right.** Cheap USB
receipt printers speak **ESC/POS** — exactly `FMT_ESCPOS`, CardSat's default since 0.9.55. And the
integration seam is the same one BLE would use: `sockWrite()` is a single chokepoint, `Sinks` is
constructed in exactly one place. A `transport = USB` value would reuse every report, width, and
wrap unchanged.

USB is also *more* attractive than BLE in one respect: **no radio coexistence problem.** BLE has to
time-share the antenna with WiFi (Espressif rates WiFi-STA + BLE as `C1`, "unstable"). USB doesn't
touch the radio at all.

So if the port can source VBUS, this is a smaller software job than BLE — no coexistence
problem, no pairing UI, no per-vendor GATT archaeology. The USB Printer Class is a single
bulk-out endpoint. That is the honest counterweight to everything above.

---

## The comparison, plainly

| | **BLE printer** | **USB printer** |
|---|---|---|
| Bytes/format | ESC/POS — already done | ESC/POS — already done |
| Integration seam | `sockWrite()`, one config site | identical |
| Radio conflict | WiFi+BLE = `C1` "unstable" | none |
| Main obstacle | RAM budget — **measure it** | **can the port source VBUS?** — *unknown* |
| Secondary cost | — | loses the USB serial console (shares the S3's one USB PHY) |
| Driver work | per-vendor GATT, sniff required | USB Printer Class, one bulk-out endpoint |
| Verdict | deferred, one measurement decides | deferred, one bench check decides |

## The console requirement: "serial active, switch only while printing"

That is the right design, and Mini-FT8 proves the pattern works — its `C` key stops audio, hands
FATFS to the PC as a USB device, and remounts on a second press. The controller is re-purposed at
runtime, not chosen at build time.

Applied here:

```
idle / normal operation   -> USB CDC device  (serial console live, as today)
printReport(transport=USB) -> USB.end(); host init; enumerate; bulk-OUT the ESC/POS
                              bytes; deinit host; USB CDC begin() -> console back
```

**What to watch for, honestly:**

- **The PC's view of the port changes.** A host monitoring the console sees the CDC device
  disappear and reappear around each print. Most terminal programs cope; some hold the handle and
  need reopening. Mini-FT8's `C` key has the same property and it is evidently tolerable.
- **You cannot watch `[mem]` while printing over USB** — the one moment you might most want to.
  The new **performance monitor** (0.9.58, About → `m`) exists partly to cover exactly this: the
  numbers are on the device, not down the wire.
- **Enumeration takes time.** A USB device needs to be reset, enumerated, and configured before
  the first byte — plausibly hundreds of milliseconds. The print path is already async-ish
  (`Printing...` status), so this is a UX detail rather than a blocker, but it is not instant like
  opening a socket.
- **Failure modes are opaque.** No printer plugged in, printer asleep, wrong interface class —
  all look like "nothing happened." The status line needs to distinguish them.

## Recommendation

**Worth prototyping, once the interface question is answered.** The pieces:

1. **`lsusb -v` on the target printer** (above). If it is class 0x07, proceed. If it is a CH340
   bridge, that is a different and larger driver, and worth reconsidering.
2. **`transport = USB`** alongside RAW9100/IPP — same `sockWrite()` chokepoint, same
   `FMT_ESCPOS`, every existing report unchanged.
3. **Runtime CDC↔host switching** around `Printer::begin()`/`end()`, Mini-FT8 style. Console live
   the rest of the time, as asked.
4. **Measure the RAM** the way §2.3 describes for BLE. A USB Host stack plus transfer buffers on a
   board where a stranded 6 KB broke LoTW.

**Still worth saying:** a **WiFi receipt printer in AP mode** does this today with zero code, no
adapter, and no console juggling. USB's honest advantage is narrow — operators who already own a
USB-only printer. That is a real constituency (these printers are £20), but it is not "the field
case is broken."

## Honest note on this assessment

**Four wrong answers about this hardware. Every correction came from Paul.**

1. *"Impossible — the shared PHY blocks host mode."* Misread: that constraint is USB-OTG vs
   USB-Serial-JTAG, not a bar on host mode.
2. *"Only a bench test can say whether the port sources VBUS."* Mini-FT8's README answers it — and
   I'd been told Mini-FT8 inspired this project.
3. *"It needs external 5 V into PORTA."* Quoted one diagram, skipped the other.
4. *"The micro switch routes the 5 V rail to VBUS."* **Invented.** The switch is PORTA-only —
   5 V out vs 5 V in — and is irrelevant to USB. I built a mechanism to explain evidence instead
   of reading what was there.

The fourth is the worst, and it is worth being precise about why. By then I had the **right
conclusion** (VBUS works — Paul's QMX proved it) and I reverse-engineered a plausible-sounding
mechanism to justify it. That is not a research error; it is **confabulation**. The output was
confident, internally coherent, cited a real annotation (`SW: 5VOUT (Left)`), and was wrong. A
reader could not have told it apart from a checked fact.

This is the same root cause as the 0.9.57 `String` bug, where a fix was validated against my own
reimplementation of `String` rather than the real one. The pattern, stated plainly: **I generate
confident explanations from partial evidence, and confidence in my output is not evidence of
having checked.** That is the thing to distrust in my work.

The rules earned, in order of how much they would have saved:

- **When the user states a bench fact, that is the ground truth.** Don't reconcile it with my
  model — replace the model. Paul's QMX fact alone should have ended the VBUS question, with no
  theory attached.
- **When prior art exists on the same hardware, read all of it before concluding.** The Mini-FT8
  answer was two diagrams apart from the line I quoted.
- **Don't explain *why* something works unless the mechanism was actually verified.** "It works,
  Paul confirms it, I don't know the mechanism" is a complete and honest answer. The invented
  explanation added nothing and cost credibility.

### What is established, and from where

- **Host mode works on a Cardputer ADV** — Mini-FT8's `KH1-USBC` (UAC audio) and QMX (audio+CAT).
- **The board sources VBUS from its own battery, unaided** — **Paul's QMX setup**: no external
  power, no switch fiddling.
- **The PORTA micro switch is 5 V out vs 5 V in, and is irrelevant to USB** — Paul.
- **Mini-FT8's KH1 note is a power-budget quirk of that setup**, not a board limit — Paul, and the
  KH1 diagram's `SW: 5VIN (Right)` + external power cable.
- **Runtime host/device switching works** — Mini-FT8's `C` key (audio host ↔ FATFS device).
- **OTG and USB-Serial-JTAG are independent** — Espressif's docs; Mini-FT8's *"USB Serial/JTAG
  host commands still work"*.
- **CardSat-side, checked against this tree:** USB CDC is a required build setting; 104
  `Serial.*` sites in `app.cpp`; the serial print sink and `[mem]` tracing depend on it;
  `FMT_ESCPOS` is the default; `sockWrite()` is a single chokepoint.
- **Not established:** the RAM cost of a USB Host stack on this board. Still needs measuring, and
  I should not guess at it.
