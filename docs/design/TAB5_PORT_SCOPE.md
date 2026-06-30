# Scope: Porting CardSat to the M5Stack Tab5

**Status: forward-looking design scope — not yet built or tested on hardware.** Hardware
figures are from M5Stack's published Tab5 and Tab5-Keyboard documentation and Espressif's
ESP32-P4 USB/peripheral docs; treat wiring and driver specifics as "confirm against the
datasheet / M5Unified / ESP-IDF before relying on them."

This document is the deep design companion to **`docs/guides/PORTING.md` §3e**, which is
the short "why the Tab5 is the easy port" overview. Read §3e first; this expands the three
features that turn the Tab5 from a screen-and-keyboard reskin into a genuinely more capable
station controller than the Cardputer ADV:

1. **USB-host radio and rotator control** — driving USB-CDC rigs and rotators directly.
2. **A touchscreen-first UI** on the 1280×720 capacitive panel.
3. **An optional keyboard** — the Tab5 Keyboard becomes one input method among several,
   not a requirement.

Unlike the **Cardputer Zero** (a Linux machine — see `CARDPUTER_ZERO_PORT_SCOPE.md`), the
Tab5 stays inside the **Arduino + M5 (M5GFX/M5Unified)** world, so the orbital/Doppler/CAT
core moves with the same light edits any ESP32 board needs. The work here is at the edges:
input, display, and the USB transport.

---

## 1. Tab5 hardware that matters for this scope

| Item | Spec | Relevance |
|------|------|-----------|
| SoC | **ESP32-P4** (RISC-V dual-core ~400 MHz), Arduino via M5Unified | Same toolchain/drawing model as the ADV; no Xtensa-specific code to unwind |
| RAM | **32 MB PSRAM**, 16 MB Flash | No-PSRAM constraints vanish; room for USB driver buffers, a full-res framebuffer, multiple radio connections |
| Display | 5″ **1280×720** IPS, MIPI-DSI (ILI9881C/ST7123) | The canvas for a touch-first UI |
| Touch | **GT911** capacitive multi-touch (I²C) | The enabler for the touchscreen UI; multi-touch capable |
| **USB** | **USB-A host** + USB-C OTG (USB 2.0) | **The headline for this scope: USB-host CAT/rotator** |
| Wireless | WiFi 6 + BT 5.2 via **ESP32-C6** co-processor (SDIO) | WiFi works, but via the C6 (not in-SoC) — net bring-up differs from the ADV |
| Serial I/O | **RS-485 built in** (SIT3088, switchable termination), Grove, M5BUS, GPIO_EXT | CAT/rotator over RS-485 or Grove **without** an external level shifter |
| Storage | microSD | Logs/caches with no LittleFS tightness |
| Sensors | **BMI270** IMU, **RX8130CE** RTC | Tilt tuning + real RTC (different parts than the ADV → re-target the reads) |
| Power | NP-F550 battery, **INA226** monitor | Battery screen re-targets to INA226 |
| Keyboard (accessory) | **Tab5 Keyboard**: 70 keys, 14×5 matrix, STM32F030C8T6, **I²C on Ext.Port1** + interrupt; Normal/HID/**Character** modes | Optional input; "Character" mode returns key-name strings + Ctrl/Alt |

The three facts that drive this scope: a **USB-A host port** (the Tab5 can *be* the host a
USB-CDC radio needs), a **capacitive touch panel** (a real pointer, not just keys), and a
**detachable keyboard** (so it can't be assumed present).

---

## 2. USB-host radio and rotator control

### 2.1 Why this is the big capability gain

On the ADV (and on a wired CI-V/RS-232 setup generally), CardSat talks to radios over a
UART. Modern rigs increasingly expose a **USB port** instead of (or in addition to) a serial
jack — and that USB port requires the *computer* to be the USB **host**. The ESP32-S3 in the
ADV can do USB device or host, but the ADV exposes it as device (for flashing/CDC). The
**Tab5's USB-A port is a host port**, so CardSat can enumerate and drive a radio's USB-CDC
interface directly — reaching the class of rigs that are explicitly **OUT OF SCOPE** on the
ESP32 today in `HALFDUPLEX_RADIOS_SCOPE.md` (FT-991/991A, IC-7100, and similar USB-CAT rigs).

The same applies to rotators: a **USB-connected rotator controller** (or a USB-UART bridge to
a GS-232/SPID box) becomes reachable without the I²C→UART bridge the ADV needs.

### 2.2 How it works technically (and the honest caveat)

This is the **most technically demanding part of the whole port** — flag it as such in
planning. The relevant facts:

- **The right stack is the ESP-IDF USB *host* stack, not the Arduino/TinyUSB *device*
  stack.** ESP-IDF ships a `usb_host` driver plus a **CDC-ACM host driver with a VCP
  (Virtual Communication Port) service** (`esp-idf/examples/peripherals/usb/host/cdc/`),
  which **auto-loads the correct driver for CP210x, FTDI FT23x, and CH34x** USB-UART chips —
  exactly the converters most ham-radio USB interfaces use (Icom/Yaesu USB ports are
  typically Silicon Labs CP210x or FTDI). Enumerate the device, open the CDC-ACM interface,
  set line coding (baud/parity), and you have a byte pipe — the **same byte pipe the existing
  CAT backends already expect**.
- **Because the seam is "a byte stream," the protocol layer is untouched.** CardSat's
  `CivRig`/`YaesuRig`/`KenwoodRig` already separate *framing* (portable) from *transport*
  (the UART). A **`UsbCdcRig` transport** — or, cleaner, a transport adapter that any
  existing backend can sit on — feeds those same framing routines bytes from the USB-CDC
  endpoint instead of `HardwareSerial`. This mirrors how `IcomNetRig` reuses CI-V framing
  over UDP rather than a UART.
- **The caveat that bites in practice:** some USB-UART chips (notably **CH34x**) present
  *vendor-specific* descriptors rather than clean CDC-ACM, and the host stack needs a
  per-VID/PID **shim** to bind them and stub the control requests they ignore. CP210x and
  FTDI are well-behaved; CH34x and oddballs may need quirk handling. Plan to test against the
  **specific** rigs/adapters you intend to support, not "USB radios" in the abstract.
- **Arduino vs ESP-IDF reality:** the USB host CDC-ACM work lives in ESP-IDF. The Tab5
  Arduino core is ESP-IDF underneath, so this is reachable from an Arduino-framework project
  by pulling in the ESP-IDF USB host component — but it is **not** a one-line `Serial`
  swap; it's the part of the port that needs real USB bring-up and per-device testing.

### 2.3 Where it sits in the architecture

It stays behind the existing interfaces — that is the whole point:

- **Radios:** add a USB-CDC **transport** under the `Rig` abstraction. The cleanest shape is
  a thin `UsbSerialPort` class (open/close/read/write/setLineCoding over the host CDC-ACM
  driver) that the CI-V/Yaesu/Kenwood backends can be constructed on, selected by a new
  "connection = USB" option alongside today's wired/LAN choices. No new *protocol* code.
- **Rotators:** the same `UsbSerialPort` feeds the GS-232 / SPID / Easycomm framing the
  rotator layer already has, as a new rotator transport alongside the I²C-bridge, TCP, and
  UDP ones.
- **Hot-plug:** USB host means devices can appear/disappear at runtime. The settings/status
  UI should reflect enumerate/attach/detach events (the host stack provides them) rather than
  assuming a fixed port — a small amount of new state, not a redesign.

### 2.4 Multi-radio note

With the USB-A host **plus** RS-485 **plus** the C6's LAN, the Tab5 can in principle mix a
USB-CDC downlink rig and a wired/LAN uplink rig — the SatPC32 "two radios" model. The `Rig`
abstraction already isolates per-radio backends; two independent transports is a
configuration question, not new core logic. Treat this as a **later** phase (see §5) — get
one USB radio solid first.

---

## 3. Touchscreen-first UI (keyboard optional)

### 3.1 The goal

Make the Tab5 fully operable **by touch alone**, with the keyboard as an accelerator rather
than a requirement. Today CardSat's UI is a state machine fed by single `char` key codes;
the design principle here is to **keep that state machine and feed it from touch as well as
keys**, so neither input path is privileged and the existing screen logic is reused intact.

### 3.2 The enabling abstraction: a logical-input layer

Introduce one thin layer that converts *any* input device into the same logical events the UI
already consumes:

- **Keys → already chars.** The existing path: a `char` (`;` `.` `,` `/`, ENTER, `` ` ``/DEL,
  letter shortcuts) drives the state machine.
- **Touch → synthesized logical events.** The GT911 reports touch points; a translation layer
  turns gestures into either (a) the **same `char` codes** (e.g. an on-screen ▲/▼ emits
  `;`/`.`, a back chevron emits `` ` ``), or (b) a small set of **new high-level events**
  (`tap(x,y)`, `swipe`, `long-press`) that screens can opt into for direct manipulation
  (tap a pass row to select it, tap a transponder to pick it, drag the passband).

The minimal, lowest-risk version is **(a) alone**: render on-screen buttons that emit the
existing chars, and the entire UI is touch-drivable with *zero* changes to screen logic —
only the draw layer adds tappable affordances. Option (b) is the richer, more native
experience layered on top, screen by screen.

### 3.3 What a touch-first layout looks like at 1280×720

The ADV crams everything into 240×135; the Tab5 has ~28× the pixels. A touch layout should
spend them on **finger-sized targets and persistent affordances**, not just bigger text:

- A persistent **on-screen control strip** (back, ▲/▼, ENTER/select, and a few context
  actions) so no physical key is needed to navigate.
- **Direct selection** on lists (satellites, passes, transponders, log, messages): tap a row
  to select, tap again or tap a "go" affordance to open — replacing scroll-to-highlight.
- **Direct manipulation** where it's genuinely better than keys: drag the **passband** marker
  on a linear transponder, tap a point on the **polar/sky** plot, tap the **map**.
- An **on-screen text entry** path (a simple tap keyboard or numeric pad) for the few places
  CardSat needs typed input (callsign, manual frequency, grid, WiFi credentials) **when no
  physical keyboard is attached** — see §4.
- Larger, glanceable **Track** and **Big** readouts using the room for real typography.

### 3.4 Reuse, not rewrite

Crucially, the **screen *logic* is untouched** — `drawTrack()`, `drawPasses()`, the tuning
state, the One True Rule, all of it. What changes is: the **draw layer** gains tappable
regions and bigger geometry (the same reflow work §3e already calls out), and the **input
layer** gains a touch source. The interaction model widens; the application core does not move.

---

## 4. Optional keyboard

### 4.1 The principle

The Tab5 Keyboard is a **detachable accessory**, so CardSat must run fully without it. The
keyboard becomes **one input method among several**, all feeding the same logical-input layer
from §3.2:

- **Keyboard attached** (Tab5 Keyboard over I²C/Ext.Port1, **Character mode**): characters
  flow straight into the existing dispatch — the fast path for heavy text entry (logging,
  messages). Detection is clean: the keyboard is an I²C device with an interrupt line, so
  CardSat can probe for it at boot and watch for attach/detach.
- **Keyboard absent:** the **touch UI (§3) is the primary interface**, including the
  on-screen text-entry path for the handful of typed-input fields. Nothing about the UI
  should *assume* a physical keyboard exists.
- **USB HID keyboard (bonus):** since the Tab5 has a USB host port, a **standard USB keyboard**
  could also be supported via the host HID driver, feeding the same char path — a natural
  extension once the USB host stack (§2) is up, useful for a desk setup.

### 4.2 Input-source matrix

| Source | Transport | Feeds | Role |
|--------|-----------|-------|------|
| Tab5 Keyboard | I²C (Ext.Port1) + IRQ, Character mode | existing `char` dispatch | Fast text entry when attached |
| Touchscreen | GT911 (I²C) | synthesized chars + high-level events | **Primary** when no keyboard |
| USB HID keyboard | USB host HID | existing `char` dispatch | Optional desk-setup convenience |
| (ADV matrix) | M5Cardputer | existing `char` dispatch | N/A on Tab5 — reference only |

The design win is that **all of these converge on one logical-input layer**, so screens never
branch on "which input device." Add a source, and the whole UI works with it.

### 4.3 The text-entry gap to close

CardSat's text editors today assume a full keyboard (and deliberately treat **DEL as
backspace** while only `` ` `` is back, so DEL can delete a character — see the `isBack`
helper and the editor notes in `PORTING.md` §5). For a keyboard-optional device this means:
provide an **on-screen entry method** (a tap keyboard, or field-appropriate pickers — numeric
pad for frequency, callsign character grid, a WiFi password field) for those editors, and
keep the physical-keyboard fast path when one is present. This is the one place "keyboard
optional" needs genuinely new UI rather than just an input adapter.

---

## 5. Phased plan

Each phase is independently shippable; the order front-loads the lowest-risk, highest-value
work and defers the hardest (USB host) until the device is otherwise usable.

1. **Base port (PORTING.md §3e).** Compile for ESP32-P4/M5Unified; bring the panel up with
   integer-scaled rendering; WiFi via the C6; SD mount. Result: existing UI runs (chunky) on
   the big screen, driven by whatever input is wired.
2. **Touch input, char-level (§3.2a).** Add the GT911 source and an on-screen control strip
   that emits the existing chars. Result: **fully touch-operable** with zero screen-logic
   changes.
3. **Optional-keyboard plumbing (§4).** Tab5 Keyboard over I²C in Character mode with
   attach/detect; on-screen text entry for the editor fields so the device is usable with **no
   keyboard attached**. Result: keyboard genuinely optional.
4. **Display reflow to native 1280×720 (§3.3).** Screen by screen, worst offenders first
   (Track, Passes, world map, polar); add direct-manipulation touch (tap-to-select, drag the
   passband) where it beats keys.
5. **Peripherals.** Re-target BMI270 (tilt), INA226 (battery), RTC; wire CAT to RS-485/Grove.
6. **USB-host CAT (§2).** The hard part: ESP-IDF USB host + CDC-ACM VCP; a `UsbSerialPort`
   transport under `Rig`; test against the **specific** CP210x/FTDI/CH34x rigs you target;
   hot-plug status in the UI. Then the rotator transport.
7. **Stretch:** second radio (USB + wired/LAN), USB HID keyboard, relax the no-PSRAM code.

Phases 1–5 need only the Tab5 (and the keyboard for phase 3); they're the bulk of a usable
port. Phase 6 is where the real USB engineering and per-rig verification live.

---

## 6. Risks & open questions

- **USB host CDC-ACM is the principal risk.** It needs ESP-IDF host-stack bring-up inside the
  Arduino/M5 project, and **per-device descriptor quirks** (CH34x and vendor-specific
  adapters need shims; CP210x/FTDI are clean). This must be validated against the actual rigs
  and adapters intended — "supports USB radios" is only true per tested VID/PID.
- **USB power/role:** the USB-A host port supplies VBUS to the radio/adapter; confirm the
  Tab5's host-port current budget and that host mode doesn't conflict with charging/flashing
  over USB-C. Some rigs are bus-powered, some aren't.
- **WiFi via the C6 co-processor** (SDIO), not in-SoC — `net.{h,cpp}`'s connect path goes
  through M5Unified's bring-up of the C6. Expect to adjust the connect/credentials path, not
  rewrite TLS/HTTP. Verify on hardware.
- **GT911 multi-touch mapping** (orientation, coordinate transform vs the panel's native
  landscape orientation) needs confirming on the real panel — the same orientation/offset
  caveat any new display has.
- **Touch text entry ergonomics** on a 5″ panel: an on-screen keyboard is usable but slower;
  the design should lean on **pickers** (numeric pad, character grid) over a full QWERTY where
  the field allows, to keep keyboard-less operation pleasant.
- **Tab5 Keyboard Character-mode specifics** (exact reported strings for the non-letter keys,
  how Sym/Aa/arrows are encoded) need confirming against the keyboard's actual I²C register
  output before the char mapping is finalized.

---

## 7. Recommendation

The Tab5 is the **highest-payoff non-Cardputer target**: it stays in the Arduino/M5 world
(no Linux/Wayland/Hamlib re-platforming), and it adds three real capabilities the ADV can't
match — USB-host CAT for the modern rig class, a true touch UI, and flexible input. Sequence
it as in §5: ship a touch-operable, keyboard-optional tracker early (phases 1–4), then invest
the focused USB-host engineering (phase 6) to unlock the USB-CDC radios. Keep every new
capability behind the seams that already exist — the `Rig` transport, the rotator transport,
and a single logical-input layer — so the orbital/Doppler/CAT core that defines CardSat moves
to the Tab5 unchanged.
