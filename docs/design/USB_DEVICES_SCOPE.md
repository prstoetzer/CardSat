# USB devices: printers, CAT, and rotators — research

*Research and scoping. Nothing built. Supersedes the printer-only guesswork in
`USB_PRINTER_SCOPE.md`.*

## The finding that reframes all three questions

**Espressif ships official USB host class drivers for exactly the chips involved.** Verified
present on the ESP Component Registry (each checked individually — an earlier pass gave false
positives because the 404 page is large):

| component | version | licence | relevance |
|---|---|---|---|
| `espressif/usb_host_cdc_acm` | 2.4.0 | Apache-2.0 | standard CDC-ACM serial devices |
| `espressif/usb_host_vcp` | 1.0.0 | Apache-2.0 | **unifying wrapper** — see below |
| `espressif/usb_host_cp210x_vcp` | 2.2.0 | Apache-2.0 | **Silicon Labs CP210x — what Icom rigs use** |
| `espressif/usb_host_ch34x_vcp` | 2.2.1 | Apache-2.0 | CH340/CH341 — the cheap-adapter chip |
| `espressif/usb_host_ftdi_vcp` | 2.1.1 | Apache-2.0 | FTDI FT232 etc. |
| `espressif/usb_host_msc` | 1.2.0 | Apache-2.0 | mass storage (declined, but noted) |
| **`usb_host_printer`** | — | — | **DOES NOT EXIST** |
| **`usb_host_lpr`** | — | — | **DOES NOT EXIST** |

The VCP wrapper is the important one. Espressif's own description:

> *"Virtual COM Port (VCP) service manages drivers to connected VCP devices — typically USB↔UART
> converters. In practice, you rarely care about specifics of the devices; you only want uniform
> interface for them all. VCP service does just that... you can just call `VCP::open` and the
> service will load proper driver for device that was just plugged into USB port."*

That is precisely the abstraction all three of these questions need, and it already exists,
maintained by Espressif, Apache-2.0, "supports all targets."

---

# 1. USB printers — "do they all use the same interface?"

**No.** There are three plausible interfaces, and the good news is that two of the three are
already solved by the components above.

| what the printer presents as | how you drive it | status |
|---|---|---|
| **USB Printer Class** (`bInterfaceClass 0x07`, bulk-OUT) | write a small driver on the raw Host Library | **no Espressif component exists** — you write it |
| **USB-serial bridge** (CH340 / CP210x / FTDI) | `usb_host_vcp` picks the driver automatically | **free** |
| **CDC-ACM** | `usb_host_cdc_acm` | **free** |

In **all three cases the payload is the same**: ESC/POS bytes, exactly what CardSat already emits
as `FMT_ESCPOS`. The page language never changes. Only the pipe does.

### The USB Printer Class, precisely

| field | value |
|---|---|
| `bInterfaceClass` | `0x07` |
| `bInterfaceSubClass` | `0x01` (printers) |
| `bInterfaceProtocol` | `0x01` unidirectional · `0x02` bidirectional · `0x03` IEEE-1284.4 |
| endpoints | one **bulk-OUT** (+ bulk-IN if bidirectional) |
| class requests | `GET_DEVICE_ID` (0x00), `GET_PORT_STATUS` (0x01), `SOFT_RESET` (0x02) |

A minimal driver is: enumerate → find the interface with class 0x07 → claim it → find its bulk-OUT
endpoint → `usb_host_transfer_submit()` the ESC/POS bytes. `GET_PORT_STATUS` gives paper-out and
error bits, which is more than the TCP path has today.

### The design, if built

Because ESC/POS is common to all three, the transport is the *only* variable:

```
transport = USB
  -> host init
  -> enumerate; inspect bInterfaceClass
       0x07          -> printer-class driver (bulk-OUT)
       CDC/vendor    -> usb_host_vcp -> VCP::open() -> write()
  -> stream FMT_ESCPOS bytes through the existing sockWrite() chokepoint
  -> deinit; CDC console back
```

`sockWrite()` is one function, `Printer::Sinks` is constructed in one place. Every existing
report, width and wrap is unchanged.

### What I still can't tell you

**Which interface a given cheap printer presents.** I won't guess — this session has produced
four confidently-wrong hardware claims from exactly that habit. One command answers it exactly:

```
lsusb -v -d <vid:pid> | grep -A5 -i "interface descriptor"
```

- `bInterfaceClass 7` → printer class → small custom driver
- `bInterfaceClass 255` + CH340/CP210x VID:PID → `usb_host_vcp` handles it free
- `bInterfaceClass 2/10` → CDC-ACM → `usb_host_cdc_acm` handles it free

**Do this on the printer that has to work**, before any design.

---

# 2. CAT over USB — re-scoped

Previously out of scope. It should not have been.

## What the radios actually do

CardSat supports ten radios across three protocol families. **Eight of them predate USB entirely:**

| radio | protocol | CAT port | USB? |
|---|---|---|---|
| IC-820, IC-821, IC-970, IC-910 | CI-V | 3.5 mm CI-V jack (TTL) | no |
| FT-736R, FT-847 | Yaesu | CAT DIN | no |
| TS-790, TS-2000 | Kenwood | RS-232 DB-9 | no |
| **IC-9100** | CI-V | 3.5 mm **+ USB-B** | **yes** |
| **IC-9700** | CI-V | 3.5 mm + USB-B + **LAN** | **yes** (CardSat already uses its LAN) |

*(Port details for the eight older rigs are from general knowledge and the CardSat radio table —
worth confirming against the manuals in the project folder before this ships. The IC-9700 facts
below are from its own manual, quoted.)*

## What Icom's own manual says

From the **IC-9700 CI-V Reference Guide** (in the project folder), verbatim:

> *"Select your connection method from the following:*
> *• **A USB cable (A-B type, user supplied)**. The required USB driver and driver installation
> guide can be downloaded from the Icom web site."*

**"The required USB driver"** is the tell: the radio is **not** a standard CDC device — it's a
**USB-serial bridge** needing a host-side driver. That is why Windows/Linux see a virtual COM port.

Two more manual facts that matter:

- *"When you use the [USB] port, select **'Unlink from [REMOTE]'** in the 'CI-V USB port' item"* —
  a **radio-side menu setting**, without which USB CI-V won't behave.
- *"Set 'DATA Function' to 'CI-V.' (SET > Connectors > USB (B)/DATA Function)"* — another one.
- *"Division number (Maximum): 01(LAN), **11(USB)**"* — USB and LAN are not equivalent paths.

## What Hamlib says

Authoritative, because Hamlib drives these radios in the field:

- `RIG_PORT_USB` exists as a port type — and is used **zero times** in Hamlib's entire Icom
  backend. Icom rigs are **`RIG_PORT_SERIAL`**. From the host's perspective, USB CI-V *is* a
  serial port.
- `icom_get_usb_echo_off()` — Hamlib **probes at runtime** whether the radio echoes CI-V frames
  back over USB, because it varies. **CardSat already solves this**: `drainEcho()` in `civ.cpp`
  exists because single-pin CI-V wiring echoes for the same reason. The hard part is already done.

## Why this is now attractive

**The architecture is already right.** CardSat's protocol encoders take a `Stream*`
(`src/civ.h:54`, `src/kenwood.h:52`) — they neither know nor care what's underneath. The factory
is one call:

```c
rig = makeRig(m, cfg.catType, cfg.catHost, cfg.catPort, cfg.catUser, cfg.catPass);
rig->begin(baud, CIV_UART_NUM, CIV_RX_PIN, CIV_TX_PIN);   // "net backend ignores UART args"
```

`CatType` already has `CAT_WIRED` / `CAT_NET` / `CAT_RIGCTL`. Adding **`CAT_USB = 3`** with a
`Stream` adapter over `usb_host_vcp` is a *transport*, not a protocol change. **Every protocol,
every radio, every existing feature works unchanged.**

## Two real uses, not one

1. **Modern rigs with a USB port** — IC-9100, IC-9700. Plug the Cardputer straight into the rig.
   (The IC-9700 already has LAN, which CardSat uses and which needs no adapter — so this mainly
   helps the IC-9100.)
2. **Every old rig, via a USB-serial adapter** — and this is the bigger prize. Today, wired CI-V
   needs G1/G2 and a level shifter; RS-232 rigs (TS-790, TS-2000, FT-847) need a **MAX3232** and
   the wiring documented in `WIRING.md`. A $5 USB-RS232 adapter with an FTDI or CP210x chip
   replaces that whole harness — and `usb_host_vcp` already drives all of them.

That second case makes CAT-over-USB *more* valuable than the printer work, because it simplifies
existing supported hardware rather than adding new.

---

# 3. Rotator over USB-serial

Same shape, same conclusion, and arguably the cleanest win of the three.

**Today:** `ROT_GS232` goes over an **SC16IS750 I²C→UART bridge** on Wire1, then a MAX3232, then
DB-9 — a documented chain with a known footgun (the Grove/CAT pin conflict noted in `WIRING.md`,
and the LoRa SPI-bus contention that `Store::remount()` exists to work around).

**With USB:** a USB↔RS-232 adapter replaces the SC16IS750 *and* the MAX3232 *and* the level
shifting. `RotType` already has `ROT_GS232 / ROT_NET / ROT_PST / ROT_YAESU`; add **`ROT_USB`** and
point the existing GS-232 encoder at a VCP `Stream`.

**The catch is real, though:** there is **one** USB port. A rotator on USB means the radio is on
G1/G2 or the network — you cannot have USB CAT *and* a USB rotator without a hub, and a bus-powered
hub on a battery device is its own problem. The existing I²C rotator path is not redundant; it is
what makes simultaneous rig+rotator possible.

---

# The build question — **answered by EspUsbHost: it is an Arduino library**

I previously concluded USB host was unreachable from the Arduino IDE, because the Arduino ESP32
**USB library** is device-only (`USB`, `USBCDC`, `USBMSC` — that is its entire `keywords.txt`).
That was right about *that library* and wrong about the conclusion.

**[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost)** (TANAKA Masayuki) is a plain
Arduino library — `library.properties`, v2.3.0, `architectures=*` — that installs through the
Library Manager and does USB **host** on the ESP32-S3. It reaches the IDF stack directly:

```cpp
#include <usb/usb_host.h>     // EspUsbHost.h line 7
```

So `usb/usb_host.h` **is** present in the Arduino core's precompiled libs and includable from a
sketch. My earlier probing for that header failed because I was guessing at paths in the wrong
repo, and I turned "I couldn't find it" into "it isn't there."

**Consequence: no `framework = arduino, espidf`, no IDF component manager, no PlatformIO-only
feature, and the single-file `CardSat.ino` Arduino IDE path survives intact.** The blocker I said
gated all three features does not exist.

## What it gives us

| feature | API | fit for CardSat |
|---|---|---|
| **USB serial** — CDC-ACM **and VCP (FTDI, CP210x, CH34x)** | `class EspUsbHostCdcSerial : public Stream` | **exact** — `civ.h`/`kenwood.h` take a `Stream*` |
| baud / data bits / parity / stop bits, DTR, RTS | `setBaudRate()`, `setConfig()`, `setDtr()`, `setRts()` | everything CAT needs |
| vendor bulk IN/OUT (`bInterfaceClass 0xff`) | `vendorOpen()`, `vendorWrite()`, `vendorRead()` | a route to a printer without a class driver |
| interface enumeration | `getInterfaces()` → `interfaceClass` | lets us *detect* what a printer presents |
| lifecycle | `begin()` / `end()` | install-on-use, free-on-exit |

`EspUsbHostCdcSerial` literally inherits `Stream` and implements `available/read/peek/write/flush`.
CardSat's `makeRig()` factory hands a `Stream*` to the protocol encoder. **That is a drop-in.**

## The author's own caveats — worth repeating

- *"still a practical Arduino USB Host library under active development, not a fully validated
  replacement for every ESP-IDF USB class driver. APIs may still change incompatibly in later 2.x
  releases."*
- *"**ESP32-S3 has a small number of USB host channels.** Composite devices, hubs, audio, MSC, and
  multiple serial devices can exhaust channels quickly."*

That second one bites here specifically: **an IC-9700/IC-9100 over USB is a composite device**
(CI-V serial *plus* audio). A plain USB-serial adapter is not. So the $5-adapter case for the
eight pre-USB rigs may actually be *easier* than the modern-rig case.

# RAM: what USB→Serial actually costs

**The headline: EspUsbHost's default configuration would be fatal, and one build flag fixes it.**

## The static cost — the number that matters

EspUsbHost's own header says it plainly:

> *"Maximum number of concurrently-tracked USB devices. Each slot is a **sizable static
> DeviceState (several KB** — RX ring, NTB reassembly buffer, HID field tables, etc.), so **this
> constant dominates the library's static RAM use**... Override for any target by defining
> `ESP_USB_HOST_MAX_DEVICES`, e.g. build flag `-DESP_USB_HOST_MAX_DEVICES=4`."*

`ESP_USB_HOST_MAX_DEVICES` defaults to **8** on the S3. Sizing just the arrays I could read:

| per `DeviceState` slot | bytes |
|---|---|
| `hidReportDescriptors[8]` × 512 B | 4096 |
| `usbVendorRxBuffer[512]` | 512 |
| `interfaceInfos[16]`, `endpointInfos[16]`, `audioStreamInfos[8]` | not sized here |
| **≥ per slot** | **~4.6 KB** |

| config | static `.bss` | verdict |
|---|---|---|
| default, 8 slots | **≥ ~36 KB** | **fatal** — more than CardSat's whole free heap |
| **`-DESP_USB_HOST_MAX_DEVICES=1`** | **≥ ~4.6 KB** | affordable |

> **Correction (0.9.58-wip, 2026-07-15).** Two revisions to this table, from the v2.3.0 source
> and from a firmware freeze. (1) **The sizing over-counts:** `HIDReportDescriptorState` is a
> ~8-byte bookkeeping struct — the 512 is a descriptor-parse cap, not embedded storage — so a
> slot's static fields total ~1–2 KB and the 8-slot object is ~10–20 KB, not ≥36 KB. (2) **The
> override is only safe as a global build flag** that reaches the library's own translation
> unit (PlatformIO `build_flags`; impossible in the Arduino IDE). The quoted "defining ...
> before this header is compiled" invites a per-file `#define` — but the slot array is a
> *member* of `EspUsbHost`, so a per-file define diverges `sizeof(EspUsbHost)` between
> translation units and the library corrupts adjacent memory: that was the 0.9.58-wip
> enable-USB-CAT freeze. The shipped design needs neither: the host object is heap-allocated
> only while USB CAT is engaged (see `src/usbserial.cpp`), so the default 8 slots cost nothing
> at rest.

**CardSat needs exactly one device** — a rig, *or* a rotator, *or* a printer. Never two: there is
one port.

For scale, ~4.6 KB permanent is comparable to the **3.8 KB** the BASIC VM occupied before 0.9.56
made it heap-lazy. That is a real cost on a board with `largest block 31732` at boot, but it is
the same order as things CardSat already carries — not a showstopper.

## The dynamic cost

The IDF host stack underneath (daemon task + class-driver task + library state + transfer buffers)
is an **estimated ~12–15 KB while `begin()` is active**, reclaimed by `end()` →
`usb_host_uninstall()`.

That fits the model you asked for exactly:

```
idle           -> end();  console live;   ~4.6 KB static only
CAT / printing -> begin(); ~12-15 KB more, for the duration
```

And the two big consumers do not overlap: a TLS upload and a USB CAT session are not simultaneous;
printing is a discrete action.

## What must be measured, not trusted

- **The real static figure.** I sized two arrays out of a 163-line struct. The rest needs a build.
- **The real dynamic figure.** ~12–15 KB is an estimate from component sizing; Espressif publishes
  no total and I have no toolchain here.
- **That `end()` actually returns the heap.** The API existing is not evidence. **This is exactly
  the shape of the 0.9.57 bug** — `basicFree()` called the right-looking API and freed nothing,
  because Arduino's `String` never releases on assignment. `[mem]` before/after a full
  `begin()`/`end()` cycle is the check, and the 0.9.58 performance monitor exists to make it
  visible without a laptop.

# Recommendation

**EspUsbHost changes this from "blocked on a project-shape decision" to "buildable now, measure
it."**

1. **CAT over USB (`CAT_USB`)** — the clear first target. `EspUsbHostCdcSerial` **is** an Arduino
   `Stream`; CardSat's `makeRig()` already hands a `Stream*` to the protocol encoder. Every rig,
   every protocol, unchanged. The $5 USB-serial adapter case (all eight pre-USB rigs, replacing
   the MAX3232 harness) is the real prize — and it is *simpler* than the modern-rig case, because
   an IC-9700 over USB is a composite device and the S3 has few host channels.
2. **Rotator over USB (`ROT_USB`)** — same `Stream` seam, replaces the SC16IS750 chain. **One
   port**: mutually exclusive with #1.
3. **USB printer** — no printer class in EspUsbHost, but `getInterfaces()` reports
   `interfaceClass` and `vendorOpen()`/`vendorWrite()` give raw bulk access, so a class-0x07
   printer *may* be drivable through the vendor path (unverified — worth checking whether the
   library will claim a class-7 interface). Serial-bridge printers work for free via
   `EspUsbHostCdcSerial`. Still the smallest payoff: a WiFi printer in AP mode needs no code.

## The order of work

1. **`-DESP_USB_HOST_MAX_DEVICES=1` and build it.** Measure the static delta. If it is ~4–5 KB,
   proceed; if the unsized arrays make it much worse, stop here. **This is one build flag and one
   compile** — the cheapest decisive experiment available.
2. **Measure `begin()` / `end()`** with `[mem]` and the performance monitor. **Verify `end()`
   returns the heap** rather than trusting it did — the 0.9.57 lesson.
3. **`CAT_USB` as a `Stream` adapter**, install-on-engage / uninstall-on-disengage, console live
   otherwise.
4. **`lsusb -v` the printer**, only if #3 is wanted.

## The risk worth naming

EspUsbHost is one maintainer's library, self-described as *"under active development, not a fully
validated replacement for every ESP-IDF USB class driver"*, with *"APIs may still change
incompatibly in later 2.x releases."* CardSat would be taking a dependency on that for a
hardware-control path. Mitigations: pin the version (§1.4 build pinning is already a 1.0 blocker),
and keep the existing wired CI-V / SC16IS750 paths as the primary, with USB as an alternative
transport rather than a replacement.

## What is established, and from where

- **EspUsbHost is an Arduino library doing USB host on the ESP32-S3** — its `library.properties`
  (v2.3.0, `architectures=*`) and README, fetched and read.
- **`usb/usb_host.h` is includable from an Arduino sketch** — `EspUsbHost.h` line 7 includes it
  directly. This disproves my earlier claim that the IDF host stack was unreachable from Arduino.
- **`EspUsbHostCdcSerial : public Stream`, covering CDC-ACM + FTDI/CP210x/CH34x** — the header,
  line 1674.
- **`ESP_USB_HOST_MAX_DEVICES` defaults to 8 and dominates static RAM; overridable by build flag**
  — the header's own comment, lines 100–112.
- **`hidReportDescriptors[8] × 512 B` + `usbVendorRxBuffer[512]` per device slot** — the header.
- **The S3 has few USB host channels; composite devices exhaust them** — the README's limitations.
- **The library is under active development with possibly-breaking 2.x APIs** — the README.
- **Espressif also ships CDC-ACM/CP210x/CH34x/FTDI/VCP host components; no printer-class
  component** — ESP Component Registry, each checked individually. (Now a fallback rather than
  the primary path.)
- **`framework = arduino, espidf` is an official PlatformIO example** —
  `platform-espressif32/examples/espidf-arduino-blink/platformio.ini`. (No longer needed if
  EspUsbHost works.)
- **The Arduino ESP32 USB *library* is device-only** — its `keywords.txt`: `USB`, `USBCDC`,
  `USBMSC`. True, but it does not imply what I concluded from it.
- **Icom USB CI-V is a driver-backed serial bridge needing radio-side menu settings** — the
  IC-9700 CI-V Reference Guide, quoted.
- **Hamlib treats Icom rigs as `RIG_PORT_SERIAL`, never `RIG_PORT_USB`, and probes for USB echo at
  runtime** — `rigs/icom/icom.c`.
- **CardSat's protocol layer is already `Stream*`-based with a single rig factory** — `src/civ.h`,
  `src/kenwood.h`, `src/rig.h`, `src/app.cpp:367`.
- **CardSat already handles CI-V echo** — `drainEcho()`, `src/civ.cpp:181`.

### Not established — do not treat as fact

- **The real static RAM figure.** I sized two arrays out of a 163-line `DeviceState`. ~4.6 KB per
  slot is a **floor**, not a measurement.
- **The dynamic figure.** ~12–15 KB is an estimate from component sizing.
- **That `end()` actually returns the heap.** See the 0.9.57 `basicFree()` bug.
- **Whether EspUsbHost can claim a class-0x07 printer interface** via its vendor-bulk path.
- **Which interface any given cheap printer presents.** One `lsusb -v` answers it.
- **The port details of the eight pre-USB radios.** Confirm against the manuals before shipping.
