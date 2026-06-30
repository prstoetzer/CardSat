# Porting CardSat

CardSat is written for the **M5Stack Cardputer ADV** (ESP32-S3), but most of what it
does is not actually tied to that board. This guide explains how the code is layered,
which parts lift cleanly onto other ESP32 boards or onto entirely different platforms
(other microcontrollers, a Raspberry Pi, a desktop program), and how to take a **subset**
of CardSat — say, just the pass predictor, or just the CI-V Doppler engine — and reuse
it in your own project.

It is written for someone comfortable in C++ who wants to reuse real code, not a
line-by-line rewrite recipe. Read the [architecture](#1-architecture-the-layers) section
first; it determines how hard any given port is.

---

## 1. Architecture: the layers

CardSat has three conceptual layers. The porting difficulty of any feature depends
entirely on which layer it lives in.

```
┌─────────────────────────────────────────────────────────────┐
│  UI / orchestration         app.{h,cpp}  (~11k lines)        │  ← board-specific
│  state machine, every screen, the service loop, settings UI  │     (M5GFX display,
│                                                              │      M5 keyboard)
├─────────────────────────────────────────────────────────────┤
│  Functional modules         predict, civ/yaesu/kenwood/rig,  │  ← mostly portable
│  (the actual capabilities)  rotator, satdb, net, location,   │     (Arduino + a few
│                             lora, voicememo, irbeacon        │      ESP32 APIs)
├─────────────────────────────────────────────────────────────┤
│  Platform primitives        Arduino core, HardwareSerial,    │  ← replace per target
│                             WiFi, LittleFS/SD, Wire/SPI,     │
│                             Sgp4 library, RadioLib, M5Unified │
└─────────────────────────────────────────────────────────────┘
```

The single most important fact for porting: **`app.cpp` is the only large file, and it
is the least portable.** It holds the UI (1000+ `canvas.` draw calls against M5GFX), the
keyboard handling, and the orchestration that wires the modules together. The
*capabilities* you probably want — pass prediction, Doppler, CAT control, rotator
control — live in the **functional modules**, which are far smaller and far more
self-contained. A good port reuses the modules and rewrites the orchestration to fit the
new platform's display and input.

`main.cpp` is a deliberately thin shell (`App::setup()` / `App::loop()`); all hardware
bring-up is inside `App::setup()`.

---

## 2. Portability tiers (read this before you start)

Every module, ranked by how much work it is to move. The `#include` footprint is the
quickest tell — a module that only pulls in `<Arduino.h>` is essentially pure logic.

| Module | Depends on | Tier | Notes |
|---|---|---|---|
| **predict.{h,cpp}** | `<Arduino.h>`, `Sgp4` lib | **A — lift as-is** | Pure math + the SGP4 library. The crown jewel for reuse. |
| **location.{h,cpp}** | `<Arduino.h>`, `HardwareSerial` | **A** | Maidenhead/geodetic math + NMEA parse; only the serial read is hardware. |
| **irbeacon.{h,cpp}** | `<Arduino.h>` | **A** | A timing/LED utility; trivial to drop or re-target. |
| **satdb.{h,cpp}** | `ArduinoJson`, `LittleFS` | **B — swap I/O** | Element store + GP/OMM JSON parse; replace the filesystem calls. |
| **civ / yaesu / kenwood** | `HardwareSerial` (+ ESP32 GPIO for single-pin CI-V) | **B** | Wire protocols are portable; the UART and one ESP32-specific trick are not. |
| **rig.{h,cpp}** | `<WiFi.h>` (for the rigctl client only) | **B** | The abstract `Rig` interface is pure; only the network backend needs sockets. |
| **rotator.{h,cpp}** | `WiFi`, `WiFiUDP`, `Wire` | **B** | Protocol framing is portable; transport (I²C bridge, TCP, UDP) is per-platform. |
| **net.{h,cpp}** | `WiFi`, `WiFiClientSecure`, `LittleFS`, `esp_heap_caps` | **C — re-implement** | WiFi + TLS + streaming download, with ESP32 heap calls. Replace wholesale on non-ESP32. |
| **lora.{h,cpp}** | `RadioLib`, `Wire` | **C** | RadioLib is cross-platform, but the radio wiring is board-specific. |
| **icomnet.{h,cpp}** | `WiFi`, `WiFiUDP` | **C** | Icom RS-BA1 UDP; portable in spirit, but socket-bound. |
| **voicememo.{h,cpp}** | `M5Cardputer`, `M5Unified` | **D — board-specific** | Tied to the ADV's ES8311 codec via M5Unified. Expect to rewrite for any other audio path. |
| **app.{h,cpp}** | `M5GFX`, `M5Cardputer`, everything | **D** | The UI and orchestration. Rewrite the display/input; reuse the logic patterns. |

**Tier A** modules compile almost anywhere with a C++ toolchain (even on a desktop, with
`<Arduino.h>` shimmed — see §8). **Tier D** is where you do the real work on a new
platform.

---

## 3. Common scenarios

### 3a. "I want CardSat on a different ESP32 board" (e.g. a plain ESP32-S3 devkit, a T-Display, a CYD)

This is the easiest port because the platform primitives (Arduino-ESP32, WiFi, LittleFS,
Wire, SPI) are identical. The work is **display, input, and pins**:

1. **Pins.** Every pin and bus lives in **`config.h`** (CI-V UART pins, SD SPI pins, IR
   LED, I²C for the rotator bridge) and in `settings.h` defaults. Change them to match
   your board. There is no pin assignment hidden in `app.cpp` — they all route through
   `config.h`.
2. **Display.** This is the bulk of the effort. `app.cpp` draws to an M5GFX `canvas`
   (a `M5Canvas` sprite) with ~1000 calls like `canvas.setCursor`, `canvas.print`,
   `canvas.fillRect`, `canvas.drawLine`. If your board uses **LovyanGFX or TFT_eSPI**
   (most do), the call surface is close — M5GFX *is* a LovyanGFX derivative — and you can
   often retarget by pointing `canvas` at your panel driver and fixing the
   240×135 geometry constants. If your panel is a different resolution, you will reflow
   layouts; the geometry is expressed as literal coordinates, so plan to touch many
   `drawXxx` functions.
3. **Keyboard / input.** CardSat reads the Cardputer's matrix keyboard via M5Cardputer
   and maps it to a small set of logical keys: the arrow legends `;` `.` `,` `/`, ENTER,
   and `` ` ``/DEL for back, plus letter shortcuts. Find the key-read in `App::loop()`
   and replace it with your board's buttons/encoder/touch. **Everything downstream is
   driven by single `char` key codes**, so if you can synthesize those chars from your
   input device, the entire UI state machine works unchanged.
4. **Audio / mic (optional).** If your board has no speaker, stub `App`'s beep calls; if
   no ES8311 mic, drop `voicememo` (see §6).
5. **Build flags.** Keep an **8 MB flash, Huge-APP-style partition** (3 MB app / SPIFFS),
   PSRAM matching your board, USB-CDC on boot. CardSat assumes **no PSRAM** and is careful
   about heap fragmentation; having PSRAM only helps.

Result: full CardSat on new hardware, with the predictor, CAT, rotator, and net layers
untouched.

> For a **fully worked example of this scenario on a specific, well-supported board** —
> including a large-screen reflow strategy, an I²C keyboard, PSRAM, and RS-485/USB-host
> CAT options — see **§3e (M5Stack Tab5)** below.

### 3b. "I just want the pass predictor" (any platform)

`predict.{h,cpp}` + the element model in `satdb.h` (the `SatEntry` struct) + the Hopperpop
`Sgp4` library is a self-contained pass/Doppler engine. See §4.

### 3c. "I just want CAT Doppler control of my radio" (any platform with a serial port)

The `Rig` abstraction + one backend (`civ`/`yaesu`/`kenwood`) + the Doppler math in
`predict.h` is a complete radio-tuning core. See §5.

### 3d. "I want this on a Raspberry Pi / Linux / desktop" (non-Arduino)

You are leaving the Arduino world, so the platform tier is replaced entirely. The Tier-A
modules port with an `<Arduino.h>` shim (§8); the network and filesystem modules are
*easier* on Linux (use Berkeley sockets and stdio instead of WiFi/LittleFS); the display
becomes whatever you like (ncurses, a GUI, a web UI). This is a substantial project, but
the valuable algorithms — SGP4 wrapping, Doppler, the One True Rule, CAT framing — move
over intact. (The **Cardputer Zero** is exactly this case on a specific device — see
`docs/design/CARDPUTER_ZERO_PORT_SCOPE.md`.)

### 3e. "I want CardSat on the M5Stack Tab5 (with the Tab5 keyboard)"

**Status: forward-looking guide — not yet built or tested on hardware.** The figures
below are from M5Stack's published Tab5 and Tab5-Keyboard documentation; treat the wiring
specifics as "confirm against the datasheet/M5Unified before relying on them."

> **Going further than a basic port?** For **USB-host radio and rotator control**, a
> **touchscreen-first UI**, and making the **keyboard optional**, see the dedicated design
> scope **`docs/design/TAB5_PORT_SCOPE.md`**. This section is the concise "the base port is
> easy" overview; that document covers the three advanced capabilities and their phased plan.

The Tab5 is the **best-case** non-Cardputer port: it stays inside the Arduino + M5 (M5GFX /
M5Unified) world the firmware already lives in, so unlike the Cardputer **Zero** (a Linux
machine — see `docs/design/CARDPUTER_ZERO_PORT_SCOPE.md`) this is *not* a re-platforming.
It's the §3a "different ESP32 board" path, with two pieces of genuine good news and one
real chunk of work.

**What the Tab5 is, and why it's friendly to CardSat**

| Tab5 fact | What it means for the port |
|---|---|
| **ESP32-P4** (RISC-V dual-core ~400 MHz), Arduino-supported via M5Unified | Same toolchain and `M5Canvas`/`M5GFX` drawing model as the ADV. The SoC is RISC-V rather than Xtensa, but that's the compiler's problem, not the source's — there is **no Xtensa-specific code** in CardSat to unwind. |
| **32 MB PSRAM**, 16 MB Flash | **The single biggest relief:** every no-PSRAM constraint the firmware fights — heap fragmentation, streaming-instead-of-buffering downloads, pre-sized JSON buffers, the LittleFS tightness — simply stops mattering. You can leave the careful code in place (it still works) or relax it; either way the fragile free-list is no longer fragile. |
| **5″ 1280×720 IPS** (MIPI-DSI; ILI9881C/ST7123 auto-detected), **GT911 capacitive multi-touch** | The opposite problem from the ADV's 240×135: you have *room to spare*, not a reflow-to-fit squeeze. This is the real work — see "Display" below. |
| **WiFi 6 + BT 5.2** via an **ESP32-C6** co-processor over SDIO | WiFi works, but it's **not** the in-SoC radio the ADV has — `net.{h,cpp}` talks to the C6 through M5Unified's network bring-up rather than driving WiFi directly. Expect to adjust the connect path, not rewrite TLS/HTTP. |
| **microSD**, BMI270 IMU, RX8130CE RTC | SD logging/caching, tilt-tuning (the IMU path), and a real RTC all map onto existing features. BMI270 is a different IMU than the ADV's — re-wire the tilt read to M5Unified's IMU API. |
| **RS-485 built in** (SIT3088, switchable termination), **Grove**, **M5BUS**, **USB-A host + USB-C OTG** | CAT/rotator wiring options are *richer* than the ADV: native RS-485 for Yaesu/Kenwood-over-RS-232-class rigs and for GS-232 rotators without an external level shifter. The **USB-A host port** additionally enables driving **USB-CDC radios and rotators** directly (the modern USB-CAT rig class that's out of scope on the ADV) — a first-class feature with its own design in **`docs/design/TAB5_PORT_SCOPE.md` §2**. |
| NP-F550 battery + INA226 monitor | The battery/charge screen maps onto INA226 instead of the ADV's gauge — re-target the read. |

**The keyboard — an optional fast path.** The **Tab5 Keyboard** (70 keys, 14×5
matrix, STM32F030C8T6) connects over **I²C on Ext.Port1** with a dedicated interrupt line,
and — crucially — has a **"Character" mode that returns the *name string* of the pressed
key plus the Ctrl/Alt modifier state** (Sym/Aa are handled on the keyboard itself). CardSat's
entire UI is driven by single `char` key codes (see §3a step 3 and `App::loop()`), so this
mode is almost a drop-in: read the character from the keyboard over I²C and feed it into the
same key dispatch the Cardputer matrix feeds today. It's a detachable accessory, so it's the
**fast text-entry path when present, not a requirement** (touch covers the keyboard-less case
— see the touch note below). The mapping work is small and mechanical:

- Map the keyboard's reported characters to CardSat's logical keys — the navigation legends
  `;` `.` `,` `/`, **ENTER**, and `` ` ``/**DEL** for back, plus the letter shortcuts
  (`r` refresh, `t` next TX, `n`, `m`, `d`, `v`, `x`, etc.). Most are literal ASCII and pass
  straight through.
- Decide what plays the role of the ADV's `` ` `` (back) and DEL. The Tab5 keyboard exposes
  **Esc** — but note CardSat's text editors deliberately treat DEL as backspace and only
  `` ` `` as back (so DEL can delete a character); pick the Tab5 keys for "back" and
  "delete-char" with that distinction in mind (see the editor note in §5/the `isBack` helper).
- The **70-key layout has more keys than the ADV's matrix**, so nothing has to be dropped or
  buried under a modifier the way the cramped Cardputer sometimes forces — a usability win,
  not a constraint.
- **Touch and "keyboard optional."** The keyboard is a **detachable accessory**, and the
  GT911 capacitive panel can drive the whole UI by itself, so the intended design is
  **touchscreen-first with the keyboard optional** — both feeding one logical-input layer so
  the existing char-driven state machine is reused unchanged. The minimal version is on-screen
  buttons that emit the existing key chars (zero screen-logic change); the richer version adds
  direct manipulation (tap a list row, drag the passband). This — plus an on-screen text-entry
  path for when no keyboard is attached — is designed in full in
  **`docs/design/TAB5_PORT_SCOPE.md` §3–§4**.

**The real work: the display.** The UI is the only large task, and it's the *pleasant*
kind — too much space rather than too little. Every `draw*()` method positions text and
shapes with **literal 240×135 coordinates**; on a 1280×720 canvas those layouts will sit in
a tiny corner. Two viable strategies, pick per screen or globally:

1. **Integer-scale the canvas (fastest path to "it runs").** Render the existing layout to a
   240×135 (or 320×170) off-screen `M5Canvas` exactly as today, then blit it up with a 4–5×
   integer scale to the big panel. Near-zero layout work; the result is a crisp-but-chunky
   "big pixels" look. Good for bring-up and arguably fine as a permanent low-effort option,
   since M5GFX sprite scaling is cheap and you have PSRAM for the buffer.
2. **Reflow to native resolution (the proper job).** Lay screens out for 1280×720: bigger
   fonts, multi-column lists, a persistent status/▲▼ region, more pass rows visible at once,
   the world map and polar plots drawn larger. This touches **many** `drawXxx` functions
   because the geometry is literal coordinates, but the *logic* inside each is untouched —
   you're moving numbers, not rewriting behavior. A good middle road is to introduce a few
   layout constants (a scale factor, margins, row height) and route the worst-offender
   screens (Track, Passes, the maps) through them first, leaving rarely-seen screens scaled
   per strategy 1 until you get to them.

Either way, **the abstraction seams already exist**: the drawing all goes through one
`canvas`, and the input all arrives as `char`s, so the display and keyboard are genuinely the
only edges you touch. The predictor, Doppler/One-True-Rule engine, CAT backends, rotator,
LoRa, and net layers move with the same edits any ESP32 board needs (pins in `config.h`,
the C6 WiFi bring-up, the IMU/battery reads).

**A sensible order of work**

1. **Build + bring-up.** Get the project compiling for the ESP32-P4 target under
   Arduino/M5Unified; bring up the panel with strategy-1 scaling so the existing UI appears
   (chunky but working) on the 1280×720 screen. Confirm WiFi via the C6 and SD mount.
2. **Keyboard.** Wire the Tab5 Keyboard in Character mode over I²C + interrupt; translate its
   characters into CardSat's key dispatch. At this point the whole UI is drivable and you
   have a usable tracker.
3. **Pins + peripherals.** Point `config.h` at the Tab5's buses; re-target the **BMI270** IMU
   (tilt tuning), **INA226** battery read, and **RTC**. Wire CAT to **RS-485** (native) or
   Grove/USB as appropriate for your rig.
4. **Display reflow.** Iterate screen-by-screen from scaled to native-resolution layouts,
   worst-offenders first (Track, Passes, world map, polar).
5. **Relax the no-PSRAM code (optional).** With 32 MB PSRAM you may simplify the streaming
   parsers / pre-sized buffers, but there's no need — they work as-is.

> The list above is the **base** port. For the **touchscreen-first UI, optional keyboard, and
> USB-host CAT/rotator** — which reorder and extend this plan (touch comes earlier; USB host
> is a focused later phase) — follow the phased plan in **`docs/design/TAB5_PORT_SCOPE.md`
> §5** instead.

**Net result:** full CardSat on a 5″ 720p touch tablet with a real 70-key keyboard, the
entire orbital/CAT/rotator/LoRa core unchanged, the heap-fragmentation constraints gone, and
the only substantial work being a display reflow you can do incrementally (and ship scaled in
the meantime). Of the non-Cardputer targets, this is the **lowest-risk, highest-payoff** port
— it stays in the Arduino/M5 world, so none of the Zero's Linux/Wayland/Hamlib re-platforming
applies here.

> Compared with the **Cardputer Zero**: the Zero is a Linux machine and a substantial rewrite
> (new UI stack, Hamlib, process model — see `docs/design/CARDPUTER_ZERO_PORT_SCOPE.md`). The
> **Tab5 is not** — it's the same firmware on a bigger, faster, PSRAM-equipped ESP32-class
> board. If the goal is "CardSat on a larger screen with a keyboard, soon and safely," the
> Tab5 is the easier target by a wide margin.

---

## 4. Porting just the predictor (Tier A)

The predictor is the most reusable piece. It has no UI, no I/O, and no board dependency
beyond `<Arduino.h>` (for `String`/`millis`, both easily shimmed) and the SGP4 library.

**What you need:**
- `predict.h` / `predict.cpp`
- `SatEntry` (from `satdb.h`) — the orbital element struct (mean motion, ecc, incl, RAAN,
  arg-perigee, mean anomaly, bstar, ndot, epoch). You can copy just this struct.
- `Observer` (from `location.h`) and a handful of constants/structs from `config.h`
  (e.g. the speed of light, the `Transponder` type used by the Doppler helpers).
  `predict.cpp` `#include`s `config.h`, `location.h`, and `satdb.h`, so either bring those
  headers along or copy out the few structs and constants it references — none of them
  pull in hardware.
- The **Hopperpop Sgp4 library** (`<Sgp4.h>`), which CardSat uses via `twoline2rv` /
  the WGS72 propagator. Any SGP4 implementation works if you adapt `predict.cpp`'s one
  call site (`sgp4(wgs72, satrec, tsince, r, v)`).

**The interface** (`class Predictor`):

```cpp
Predictor pred;
pred.setSite(observer);          // Observer{lat,lon,altM} — copy the struct from location.h
pred.setSat(satEntry);           // your SatEntry, elements filled in

LiveLook L = pred.look(unixTime); // az, el, rangeKm, rangeRate, sunlit, sunAz/El, subpoint
bool up   = pred.azelAt(t, az, el);
int n     = pred.predictPasses(fromTime, minEl, outArray, maxN, ...);   // AOS/LOS/peak
```

The Doppler helpers are **static** (no instance state), so you can call them standalone.
The real signature carries per-satellite calibration offsets (pass `0, 0` if you don't
need them) and writes the corrected frequencies through **reference** out-parameters:

```cpp
uint32_t rxHz, txHz;
Predictor::dopplerFreqs(dlNominalHz, ulNominalHz, rangeRateKmS,
                        /*calDlHz=*/0, /*calUlHz=*/0, rxHz, txHz);
// rxHz = corrected downlink to set on RX, txHz = corrected uplink to set on TX
```

Feeding elements: CardSat ingests AMSAT **GP/OMM JSON** and rebuilds a TLE internally
(`satdb.cpp` → `gpToTle()`), then `twoline2rv` parses it. If you have your own TLEs, fill
`SatEntry` directly or call the library's `twoline2rv` yourself and skip `satdb`.

This module also gives you, for free: **sun position and eclipse** (`sunlitAt`,
`eclipseDepthDeg`), **beta angle**, **visual-pass geometry** (sunlit + observer-dark
test), and **Sun/Moon angular separation** primitives — everything CardSat's observer
features are built on.

---

## 5. Porting just the CAT / Doppler radio control (Tier B)

The radio layer is built around an **abstract base class** that hides every protocol
behind one interface. This is the seam to reuse — or to extend with a new radio.

**`class Rig` (rig.h)** — the key pure-virtual methods:

```cpp
virtual void begin(uint32_t baud, int uartNum, int rxPin, int txPin) = 0;
virtual bool setMainFreq(uint32_t hz) = 0;   // uplink (TX)
virtual bool setSubFreq (uint32_t hz) = 0;   // downlink (RX)
virtual bool setMainMode(RigMode m)   = 0;
virtual bool setSubMode (RigMode m)   = 0;
virtual bool readSubFreq (uint32_t& hzOut) = 0;   // for radio-knob "One True Rule" tuning
virtual bool readMainFreq(uint32_t& hzOut) = 0;
virtual bool enableSatMode(bool on)   = 0;
virtual void selectMainBand() / selectSubBand() = 0;
virtual bool setCtcss(bool on, float toneHz);     // optional
// capability flags: canReadFreq(), hasSatMode(), hasTone(), selVerified(), name()
```

Concrete backends implement it: **`CivRig`** (Icom CI-V), **`YaesuRig`** (5-byte CAT),
**`KenwoodRig`** (ASCII CAT), **`IcomNetRig`** (RS-BA1 over UDP), **`RigctlRig`** (Hamlib
network client). The factory `makeRig()` in `rig.cpp` picks one.

**To reuse on another platform:**
- The protocol framing (CI-V byte stuffing, Yaesu BCD, Kenwood ASCII) is **pure logic** —
  it moves unchanged.
- Replace **`HardwareSerial`** with your platform's UART. The backends take a `uartNum`
  and pins in `begin()`; on Linux you'd open `/dev/ttyUSB0` instead.
- **One ESP32-specific wrinkle:** single-pin CI-V. `CivRig::begin()` uses register-level
  ESP32 calls (`gpio` pad driver, `uart_set_line_inverse`, `esp_rom_gpio_connect_in_signal`)
  to share one open-drain GPIO for TX and RX. On a non-ESP32 target, either use a normal
  two-wire (TX/RX) interface and delete that block, or replace it with your MCU's
  equivalent open-drain/half-duplex UART setup. The two-wire path needs none of this.

**To add a new radio** (even on the original hardware): subclass `Rig`, implement the
pure virtuals for your protocol, and add it to `makeRig()` and the radio profile table
(`radio_profiles.h`). Nothing else in the system needs to know.

**Doppler tuning loop:** the actual "keep constant frequency at the satellite" logic
(the One True Rule, the radio-knob detection with the mode-aware threshold and grace
window) lives in `app.cpp`'s service loop, not in the Rig. If you port the radio layer,
you reimplement a small loop: each tick, compute corrected frequencies with
`Predictor::dopplerFreqs`, optionally read the dial back (`readSubFreq`) to honor manual
tuning, and call `setSubFreq`/`setMainFreq`. The pattern is ~100 lines; search `app.cpp`
for `lastRxSet` and `knobMoveThreshHz` to see it.

---

## 5a. Hamlib vs. independent radio control (decision analysis)

On a Linux-class target — the **Cardputer Zero** (CM0/Linux) being the motivating case,
but this applies to any Raspberry Pi / desktop port — you face a real architectural fork
for CAT control that you do **not** have on the ESP32. On the ESP32 the decision is made
for you: there is no room for Hamlib's ~2 MB of library plus its hundreds of rig backends
on the chip, so CardSat carries its own compact per-rig backends (`CivRig`, `YaesuRig`,
`KenwoodRig`, `IcomNetRig`) and that is the only option. On Linux, **Hamlib is available
natively**, so three strategies open up. They are not mutually exclusive, and the
`Rig` abstraction was deliberately built so that the choice is confined to one layer.

This section lays out all three with honest pros and cons so the decision can be made
deliberately rather than by default.

### The three options

1. **Hamlib only** — drop the hand-rolled backends on the Linux target; talk to every
   radio through `libhamlib` (or a supervised local `rigctld`) via the existing
   `RigctlRig` client.
2. **Independent only** — port CardSat's own backends to Linux serial (`/dev/ttyUSB*`),
   keeping the byte-level protocol code that already exists and is, for the IC-821 path,
   hardware-proven.
3. **Both, user-selectable** — ship both paths and let the operator pick, per radio, in
   settings: "use CardSat's built-in driver" or "use Hamlib (model + port)".

### What makes this cheap to reason about

The whole app speaks to the radio through the narrow `Rig` interface
(`setMainFreq`/`setSubFreq`, `readSubFreq`/`readMainFreq`, `readPtt`, `enableSatMode`,
`selectSubBand`, …), and **both** a Hamlib client (`RigctlRig`) and the independent
backends **already exist and already implement that interface**. So none of these options
is a rewrite of the app — they are different implementations behind the same seam, chosen
in `makeRig()`. The Doppler engine, the UI, and everything above the `Rig` interface are
untouched regardless of which you pick.

### Option 1 — Hamlib only

**Pros**
- **Enormous radio coverage for zero protocol work.** Every rig Hamlib supports becomes
  usable immediately — including the entire USB-CDC class that is OUT OF SCOPE on the
  ESP32 today (FT-991/991A, IC-7100, and the rest of `HALFDUPLEX_RADIOS_SCOPE.md`),
  because on Linux they enumerate as `/dev/ttyACM*` and Hamlib already knows them.
- **The verification burden moves off CardSat.** CardSat's own backends (other than the
  IC-821 single-pin path) are host-tested but not all hardware-confirmed; that risk is a
  standing liability documented in `THINGS_TO_VERIFY.md`. Hamlib is the most field-tested
  rig library in the hobby, maintained by many hands across decades of radios. Adopting
  it retires a whole column of "verify on real hardware" items.
- **Less code to carry.** The CI-V/Yaesu/Kenwood/IcomNet `.cpp` files simply aren't
  compiled into the Linux target. No byte tables, no per-rig quirk handling to maintain.
- **Matches the platform idiom.** "Spawn and supervise a helper daemon, talk to it over a
  socket" (`rigctld` on `localhost`) is exactly how the Zero's own OS is structured, and
  keeps CardSat's process model clean. Rotators get the identical treatment via `rotctld`
  and the existing `RotctlRotator`.
- **Network and multi-radio fall out naturally.** One `rigctld` per physical radio means
  the two-radio (separate uplink/downlink) model and mixing wired + USB + LAN rigs is just
  more daemon instances — no new CardSat code.

**Cons**
- **A heavy external dependency.** Hamlib (or `rigctld`) must be installed, version-matched,
  and present at runtime. CardSat goes from self-contained to depending on a large library
  whose packaging, versioning, and occasional regressions are outside the project's control.
  A Hamlib that's too old may lack a rig; one that's too new may have changed a behavior.
- **The satellite/split semantics are Hamlib's, not CardSat's — and they vary by rig.**
  CardSat's whole value is precise full-duplex satellite control (One True Rule, MAIN=TX /
  SUB=RX, sat mode, band assignment). Mapping that convention onto a given Hamlib model's
  VFO/split/sat handling is genuinely new logic and **must be confirmed per rig on the
  air**. Hamlib's sat-mode coverage is uneven; some rigs expose it cleanly, some don't, and
  `rigctld`'s split handling has its own corner cases. You inherit Hamlib's model of the
  radio, which is not always the satellite-operator's model.
- **Less direct control over timing and the knob-read loop.** The One True Rule depends on
  reading the dial back quickly and writing tightly within a deadband. Going through
  `rigctld`'s socket adds a hop and Hamlib's own polling cadence; for most rigs this is
  fine, but you have less control over the exact read/write timing than when you own the
  serial bytes. Tuning the feel (the grace window, thresholds) now interacts with Hamlib's
  behavior.
- **Loss of the one thing CardSat has proven.** The IC-821 single-pin CI-V path is
  hardware-confirmed and is a genuinely bespoke capability (single shared open-drain GPIO,
  full knob tuning + Doppler). Some of that specific, tested behavior would be handed to
  Hamlib's generic Icom backend, which may or may not behave identically on that radio.
- **Harder to debug end-to-end.** When something misbehaves, the fault could be in CardSat,
  in Hamlib, in `rigctld`, or in the rig — more layers between the operator and the bytes.

### Option 2 — Independent only

**Pros**
- **Full control of the bytes and the timing.** CardSat owns the exact CAT exchange, so
  the One True Rule's read-back-and-correct loop, deadband, and grace window behave exactly
  as designed and tested — no intermediary cadence to fight. This is the path the project's
  crown-jewel tuning behavior was built and proven on.
- **Self-contained, no runtime dependency.** The Linux build stays a single program with no
  external library to install, version-match, or break. Simpler packaging, simpler support.
- **Preserves the hardware-proven IC-821 path** and the rest of the bespoke behavior exactly
  as it exists today; porting it is "swap `HardwareSerial` for `/dev/ttyUSB0`," not a
  re-implementation (see the reuse notes in §5 above).
- **Satellite semantics stay CardSat's.** MAIN=TX/SUB=RX, sat mode, and band assignment are
  expressed in CardSat's own terms with no impedance-mismatch against a general library's
  VFO model.

**Cons**
- **The verification burden stays on the project, and grows.** Every radio CardSat wants to
  support needs its own backend written and **confirmed on that physical radio**. Today only
  the IC-821 is hardware-proven; Yaesu, Kenwood, separate-pin CI-V, and IcomNet are all
  host-tested only. Independent-only means owning that long tail forever.
- **The USB-CDC class is a large amount of new work.** Reaching the modern USB rigs (the
  headline capability the Zero's USB host unlocks) means writing and testing those backends
  from scratch — exactly the radios Hamlib already handles for free.
- **Reinventing a solved problem.** Hamlib exists precisely because per-rig CAT is fiddly
  and endless. Choosing independent-only on a platform where Hamlib is freely available is
  re-solving a problem the community already solved, with a fraction of the test coverage.
- **Slower to grow the supported-radio list**, and that growth is gated on having each rig
  on the bench.

### Option 3 — Both, user-selectable (recommended)

Ship both backends on the Linux target and expose the choice **per radio** in settings:
a toggle between "CardSat built-in driver" (the independent backend for that rig family)
and "Hamlib" (enter a Hamlib model id + device/port). `makeRig()` already switches on a
profile; it gains one more axis — *which implementation* — rather than a new app.

**Pros**
- **Best of both, and the cons of each are largely cancelled.** Operators get Hamlib's
  vast coverage (and the USB-CDC class) when they want breadth, and CardSat's tight,
  hardware-proven native control (notably the IC-821 single-pin path) when they want the
  exact tested behavior. Neither audience is forced onto the other's trade-offs.
- **A graceful migration and a safety net.** The native paths that are already proven keep
  working unchanged; Hamlib is available for everything else. If a Hamlib mapping for some
  rig's sat mode misbehaves, the operator can fall back to the built-in driver (or vice
  versa) without waiting on a release. This also de-risks the port: you don't have to get
  the Hamlib sat-mode mapping perfect for every rig before shipping.
- **Lets the hard part be incremental.** The genuinely new work — mapping CardSat's
  satellite convention onto each Hamlib model — can be done rig-by-rig, with the native
  driver as the always-available baseline meanwhile.
- **Future-proof.** New radios can arrive via Hamlib immediately; radios that need
  CardSat-specific tricks can still get a bespoke backend. The project isn't boxed into
  either philosophy.

**Cons**
- **Most code and the largest test matrix.** You maintain (and at least smoke-test) two
  radio paths on Linux, plus the selection UI. Bugs can be reported against either path,
  and "which backend were you using?" becomes a standing support question.
- **More configuration surface for the user.** The radio settings screen must now express
  not just "which radio" but "which driver," with Hamlib's model id / port / serial params
  appearing only in the Hamlib branch. This is the UI work `THINGS_TO_VERIFY.md`-style
  caveats warn about — it has to be clear, or it confuses.
- **Two ways to do the same thing** can be a documentation and explanation burden; the
  manual must guide operators on when to pick which.
- **The independent path still carries its verification debt** — choosing "both" doesn't
  retire the host-tested-only backends; it just stops them from being the *only* option.

### Recommendation and rationale

For the Zero port, **Option 3 (ship both, default to Hamlib, keep native as a selectable
fallback)** is the strongest fit, for three reasons specific to CardSat:

1. **CardSat has exactly one hardware-proven backend (IC-821 single-pin) and it is
   genuinely bespoke.** Throwing that away to go Hamlib-only discards proven, tested
   behavior; an independent-only build can't reach the USB-CDC rigs without a mountain of
   new verification. Only "both" preserves the proven path *and* gains the breadth.
2. **The cost of "both" is mostly UI, not protocol** — both implementations already exist
   behind the `Rig` interface, so the marginal work is the selection screen and the
   per-rig Hamlib sat-mode mapping, which you'd need for any Hamlib adoption anyway.
3. **It de-risks the port's single hardest unknown** — the CardSat-convention-to-Hamlib
   mapping — by keeping the native driver as an always-working baseline while that mapping
   is confirmed rig by rig on the air.

A reasonable **phasing**: bring Hamlib up first (it unlocks the most radios fastest and is
the long-term default), keep the already-ported native backends compiled in as the
fallback, and expose the per-radio selector once both are working. If engineering time is
tight, Hamlib-first with the native paths following is acceptable — but designing the
`Rig` selection with *two axes* (profile × implementation) from the start costs little and
avoids a later refactor.

> The Cardputer Zero scope document (`docs/design/CARDPUTER_ZERO_PORT_SCOPE.md`, §8.1)
> describes the mechanics of wiring Hamlib in on the Zero specifically (collapsing the rig
> factory, supervising a local `rigctld`, the settings changes). Read it alongside this
> analysis: this section is the *why/which*, that section is the *how* on the Zero.

---

## 6. Porting the rotator, network, storage, and optional modules

**Rotator (`rotator.{h,cpp}`, Tier B).** Same shape as the Rig layer: one set of pointing
logic (offsets, deadband, flip, park) feeding several transports — GS-232/Easycomm/SPID
ASCII or binary framing over an **SC16IS750 I²C→UART bridge**, plus `rotctld` (TCP) and
PstRotator (UDP) network backends, and a **direct-Yaesu** backend (ADS1115 ADC +
PCF8574 outputs over I²C). The framing is portable; the transports are not. On a board
with spare UARTs you can drop the I²C bridge and use a real serial port; on Linux, use
sockets for the network paths and a USB-serial for GS-232.

**Network (`net.{h,cpp}`, Tier C).** WiFi + TLS GET + a **streaming** GP download that
parses element-by-element to avoid a large buffer on the no-PSRAM S3 (it uses
`esp_heap_caps` to check the largest contiguous block). On another ESP32 this works as-is;
on non-ESP32, replace with your HTTP client (the streaming-parse idea still matters if
RAM is tight, but on a Pi you can just buffer the whole file). The data **sources** —
AMSAT GP JSON, SatNOGS transmitters — are plain HTTPS endpoints documented in `config.h`.

**Storage (`storage.{h,cpp}`, Tier B).** A thin abstraction: `Store::fs()` returns
whichever filesystem is active (microSD if present, else internal LittleFS), and
everything goes under `/CardSat`. To port, reimplement `Store` against your filesystem
(SD via SPI on another MCU, or stdio `fopen` on Linux). All persistence — settings,
calibration, notes, favorites, cached elements — funnels through this, so it is a single
clean seam.

**Optional modules you can simply omit** (CardSat already gates them):
- **`voicememo`** (Tier D) — ES8311 mic via M5Unified; the most board-specific module.
  Drop it unless you replicate the ADV's audio path.
- **`lora`** (Tier C) — text messaging over an SX1262 via RadioLib. RadioLib is
  cross-platform, but you supply the radio and its pins; guarded by `CARDSAT_HAS_LORA`.
- **`irbeacon`** (Tier A) — an IR-LED pass beacon; pure timing, easy to keep or cut.

When you cut a module, remove its calls from `App` and its `#include`; nothing else
depends on them.

---

## 7. Taking a subset: a dependency cheat-sheet

To compile a given capability standalone, pull these files (plus the platform libs):

| Capability | Files | External libs |
|---|---|---|
| Pass & Doppler prediction | `predict.*`, `SatEntry` from `satdb.h`, `Observer` from `location.h` | Sgp4 |
| + load elements from AMSAT GP | add `satdb.*`, `net.*` (or your own fetch), `storage.*` | ArduinoJson, (WiFi/TLS) |
| CAT radio control | `rig.*`, one of `civ/yaesu/kenwood.*`, `radio_profiles.h`, Doppler from `predict.h` | (HardwareSerial) |
| Rotator control | `rotator.*`, pointing inputs from `predict.h` | (Wire / WiFi / WiFiUDP) |
| Maidenhead / GPS | `location.*` | (HardwareSerial) |

Parentheses mean "only if you keep that transport." Each row compiles without `app.cpp`.

---

## 8. Going off-Arduino (Linux / desktop / other MCU)

The Tier-A and much of Tier-B code uses only a thin slice of Arduino: `String`,
`millis()`/`micros()`, `Serial`-style I/O, and the integer types. To build these on a
non-Arduino target:

- **Shim `<Arduino.h>`** — a small header providing `millis()`/`micros()` (from
  `std::chrono`), `String` (alias `std::string` or a tiny wrapper), and the `uintN_t`
  types. A few hundred lines covers everything the Tier-A modules touch.
- **SGP4** — the Hopperpop library is plain C++ and builds off-device; CardSat already
  relies on this fact for host-side testing. Its propagator and `twoline2rv` need no
  Arduino.
- **Serial** — replace `HardwareSerial` with termios (`/dev/tty*`) on Linux or your
  platform's UART driver.
- **Sockets** — the network backends become ordinary TCP/UDP; on Linux this is *simpler*
  than the WiFi versions.
- **Filesystem** — reimplement `Store` over stdio.

CardSat's own development relies on compiling the logic with host `g++` for testing, so
the math and protocol layers are already known to build and run on x86. That host-test
harness is a good starting point for a Linux port.

**What does not come along:** the M5GFX UI and the M5Unified audio. Budget those as a
ground-up rewrite for your target's display and sound, reusing CardSat's *screen designs*
and *state-machine structure* as a reference rather than its draw code.

---

## 9. Worked examples

These are deliberately small, self-contained sketches showing the *shape* of a port for
each platform class. They use the real interfaces verified against the source; treat them
as skeletons to flesh out, not drop-in firmware. The pattern is the same everywhere:
**reuse the modules, supply the platform's display/input/transport.**

### 9a. Another ESP32 board (plain ESP32-S3 devkit + an SSD1306 OLED)

Here CardSat's platform tier is unchanged (Arduino-ESP32, `HardwareSerial`), so you keep
`predict`, `civ`, `satdb`, etc. verbatim and only swap the display and pins. This sketch
tracks one satellite from a hard-coded TLE and shows az/el/range on an I²C OLED — the
"reuse the predictor, replace the UI" port in miniature.

```cpp
// ESP32-S3 devkit + SSD1306. Reuses predict.* / satdb.* unchanged.
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "predict.h"
#include "satdb.h"
#include "location.h"

Adafruit_SSD1306 oled(128, 64, &Wire);
Predictor pred;
SatEntry  sat;

void setup() {
  Wire.begin(8, 9);                       // <-- your board's I2C pins (was Cardputer's bus)
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  Observer me{38.9, -77.0, 50.0, true};   // your QTH: lat +N, lon +E, alt m
  pred.setSite(me);

  // Feed one satellite. CardSat's Predictor takes a SatEntry (its element struct),
  // so fill the orbital fields from your source. The small tleToEntry() helper in the
  // note just below parses a standard TLE into a SatEntry; or fetch GP/OMM via satdb.
  tleToEntry("ISS (ZARYA)",
    "1 25544U 98067A   24001.50000000  .00016000  00000-0  29000-3 0  9991",
    "2 25544  51.6400 200.0000 0006000  60.0000 300.0000 15.50000000    07",
    sat);
  pred.setSat(sat);
}

void loop() {
  LiveLook L = pred.look(time(nullptr));   // needs the RTC/NTP clock set, as on CardSat
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.printf("AZ %.1f\nEL %.1f\nRNG %.0f km\n%s",
              L.az, L.el, L.rangeKm, L.sunlit ? "SUN" : "ECL");
  oled.display();
  delay(1000);
}
```

What changed vs. CardSat: only the **display object and the I²C pins**. The predictor,
element store, Doppler, and (if you added it) the CAT layer are byte-for-byte the same. If
your board has a different panel, this is also exactly where TFT_eSPI / LovyanGFX would
slot in — and because CardSat's own UI is M5GFX (a LovyanGFX derivative), porting the
*full* UI to a LovyanGFX panel is mostly geometry, not rewriting draw logic.

> **Element entry note.** `Predictor::setSat()` takes a `SatEntry` and renders it to a
> TLE internally (`SatDb::gpToTle`) because the SGP4 library ingests TLEs. CardSat fills
> `SatEntry` from AMSAT **GP/OMM JSON** (`satdb`), but for a port the simplest source is
> usually a standard **TLE**. The firmware has no built-in TLE→`SatEntry` parser (it only
> goes the other way), so here is a compact one you can drop into any port and reuse in
> the examples below:
>
> ```cpp
> // Parse a standard 2-line element set into a CardSat SatEntry.
> // Field columns follow the NORAD TLE format.
> #include <cstdlib>
> #include <cstring>
> inline void tleToEntry(const char* name, const char* l1, const char* l2, SatEntry& s) {
>   auto fld = [](const char* line, int a, int b) {       // 1-based inclusive cols
>     char buf[32] = {0}; strncpy(buf, line + (a-1), b-a+1); return atof(buf);
>   };
>   strncpy(s.name, name, sizeof s.name - 1);
>   s.norad       = (uint32_t)fld(l2,  3,  7);
>   s.incl        = fld(l2,  9, 16);
>   s.raan        = fld(l2, 18, 25);
>   char ecc[11] = "0."; strncpy(ecc+2, l2+25, 7);  s.ecc = atof(ecc);   // implied decimal
>   s.argp        = fld(l2, 35, 42);
>   s.ma          = fld(l2, 44, 51);
>   s.meanMotion  = fld(l2, 53, 63);
>   s.ndot        = fld(l1, 34, 43);
>   // epochUnix: convert the L1 epoch (cols 19-32, YYDDD.DDDD) with SatDb::gpEpochToUnix
>   // or your own day-of-year math; CardSat measures tsince from this value.
> }
> ```
>
> Alternatively, fill `SatEntry`'s element fields
> (`incl/ecc/raan/argp/ma/meanMotion/bstar/ndot/epochUnix`) from whatever source you have,
> or call the SGP4 library's `init(name, l1, l2)` on the two TLE lines directly and bypass
> `SatEntry` entirely — `predict.cpp`'s `_sat.init(name, l1, l2)` shows that call.

### 9b. Another ESP32 with no display at all (headless tracker → MQTT)

A "subset" port: no UI, no keyboard — just the predictor feeding pointing data to your
home automation. This is the kind of thing the tier-A predictor makes trivial.

```cpp
#include <WiFi.h>
#include <PubSubClient.h>          // MQTT
#include "predict.h"
#include "satdb.h"
#include "location.h"

WiFiClient   wifi;
PubSubClient mqtt(wifi);
Predictor    pred;
SatEntry     sat;

void loop() {
  LiveLook L = pred.look(time(nullptr));
  char buf[96];
  snprintf(buf, sizeof buf, "{\"az\":%.1f,\"el\":%.1f,\"vis\":%d}",
           L.az, L.el, L.el > 0);
  mqtt.publish("sat/iss/look", buf);       // your dashboard / rotator controller subscribes
  delay(1000);
}
```

The point: once the predictor is in, the "application" is a handful of lines. You are not
porting CardSat so much as *embedding* its engine.

### 9c. A non-ESP32 microcontroller (e.g. Teensy 4.x / RP2040 under Arduino)

These run the **Arduino core** but are not ESP32, so the only things that don't carry over
are the genuinely ESP32-specific bits: the single-pin CI-V register tricks in
`CivRig::begin()` and the `esp_heap_caps`/WiFi calls in `net.cpp`. For CAT control you use
the **two-wire** CI-V path (TX/RX on two pins), which needs none of the ESP32 register
code:

```cpp
// Teensy 4.1 driving an Icom over CI-V on Serial1, with Doppler from predict.
#include "rig.h"
#include "predict.h"
#include "civ.h"

CivRig    rig;
Predictor pred;

void setup() {
  // CivRig::begin(baud, uartNum, rxPin, txPin). On Teensy you pass the UART index
  // and the matching pins; the protocol framing inside CivRig is platform-neutral.
  rig.begin(19200, 1, 0, 1);               // Serial1 RX=0 TX=1 on Teensy
  rig.setAddress(0xA2);                     // your radio's CI-V address
}

void loop() {
  LiveLook L = pred.look(time(nullptr));
  uint32_t rx, tx;
  Predictor::dopplerFreqs(145800000, 435000000, L.rangeRate, 0, 0, rx, tx);
  rig.setSubFreq(rx);                       // corrected downlink
  rig.setMainFreq(tx);                      // corrected uplink
  delay(500);
}
```

To make `CivRig` compile off-ESP32, delete (or `#ifdef`-guard) the single-pin branch in
`civ.cpp` that includes `<driver/gpio.h>` / `<driver/uart.h>` and touches
`GPIO.pin[].pad_driver`; keep the two-wire path. Everything else — the CI-V byte framing,
the read-back parsing used for One True Rule knob tracking — is portable C++. The same
approach works for `YaesuRig`/`KenwoodRig`, which have no ESP32-specific code at all.

For **networking** on these boards (W5500 Ethernet, an ATWINC WiFi module, etc.), don't
try to port `net.cpp`; write a thin fetch against your network library and hand the
downloaded GP/TLE text to `satdb`'s parser, or skip live download entirely and compile in
a TLE.

### 9d. Raspberry Pi / Linux / desktop (off-Arduino)

Here you leave Arduino behind, so the platform tier is replaced wholesale — but the
tier-A modules need almost nothing. `predict.cpp` uses only `min`/`max`/`PI`/`radians`/
`degrees` from Arduino and no `String` at all, so the shim is tiny:

```cpp
// arduino_shim.h  -- enough of <Arduino.h> for predict.* / location.* on a PC
#pragma once
#include <cstdint>
#include <cmath>
#include <ctime>
#ifndef PI
#define PI 3.14159265358979323846
#endif
template<class T> T  min(T a, T b){ return a < b ? a : b; }
template<class T> T  max(T a, T b){ return a > b ? a : b; }
inline double radians(double d){ return d * PI / 180.0; }
inline double degrees(double r){ return r * 180.0 / PI; }
inline unsigned long millis(){ return (unsigned long)(clock() * 1000UL / CLOCKS_PER_SEC); }
```

With that on the include path (and the plain-C++ Hopperpop SGP4 sources compiled in), the
predictor builds and runs natively:

```cpp
// track.cpp  -- g++ -I. -Ishim track.cpp predict.cpp satdb.cpp sgp4.cpp -o track
#include "arduino_shim.h"
#include "predict.h"
#include "satdb.h"
#include "location.h"
#include <cstdio>

int main() {
  Observer me{38.9, -77.0, 50.0, true};
  Predictor pred; pred.setSite(me);

  SatEntry sat;
  tleToEntry("ISS (ZARYA)",                 // the helper from section 9a
    "1 25544U 98067A   24001.50000000  .00016000  00000-0  29000-3 0  9991",
    "2 25544  51.6400 200.0000 0006000  60.0000 300.0000 15.50000000    07", sat);
  pred.setSat(sat);

  PassPredict passes[10];
  time_t now = time(nullptr);
  int n = pred.predictPasses(now, 5.0f /*min elev*/, passes, 10);
  for (int i = 0; i < n; ++i)
    printf("AOS %ld  LOS %ld  max elev %.1f\n",
           (long)passes[i].aos, (long)passes[i].los, passes[i].maxEl);
  return 0;
}
```

On Linux the modules that were *hard* on the MCU become *easy*:

- **Networking** — replace `net.cpp` with `libcurl` (or just `wget` the GP/TLE file out of
  band). No TLS gymnastics, no heap-fragmentation care.
- **Serial / CAT** — drive the radio through `/dev/ttyUSB0` with termios, or skip your own
  CAT code and let CardSat's `RigctlRig` talk to **Hamlib `rigctld`**, which already
  supports virtually every radio on Linux.
- **Rotator** — same: `rotctld` over a local socket, or termios to a GS-232 box.
- **Storage** — reimplement the tiny `Store` seam over stdio (`fopen` under `~/.cardsat/`).
- **Display** — anything you like: a terminal UI, a GTK/Qt app, or a small web server.
  Reuse CardSat's *screen layouts and state-machine structure* as a spec; the M5GFX draw
  code does not come along.

This is the most work of the four, but the irreplaceable parts — the SGP4 wrapping, the
One True Rule Doppler, the CAT and rotator protocol framing — all move over intact, and
CardSat already compiles its logic under host `g++` for testing, so you are starting from
known-buildable code.

## 10. Practical tips and gotchas

- **`config.h` first.** Before anything compiles on new hardware, reconcile every pin,
  UART number, I²C address, and the data-source URLs. This is the highest-leverage file.
- **No-PSRAM assumptions.** CardSat is deliberately frugal: it streams downloads, caps
  the catalog (220 sats / 64 transponders per sat), and reads files into RAM only in
  bounded chunks. If your target has PSRAM or plenty of RAM, these constraints are free
  to relax, but they will not hurt.
- **The display is the long pole.** On an ESP32 port, expect the panel/geometry work to
  dominate. Keep the logical key-char interface intact and the state machine ports for
  free.
- **Capability flags, not assumptions.** The Rig and rotator layers expose
  `canReadFreq()`, `hasSatMode()`, `hasTone()`, etc. Honor them rather than assuming a
  feature exists; this is how CardSat supports everything from an IC-820 to an IC-9700
  through one code path.
- **Keep the abstractions.** The value of CardSat for porting is its seams: `Rig`,
  the rotator backend split, and `Store`. Re-implementing *behind* those interfaces is
  far easier than threading a new platform through `app.cpp`.
- **Licensing.** CardSat is MIT-licensed (see MANUAL §25); the SGP4 library, RadioLib,
  ArduinoJson, and M5 libraries carry their own licenses — check them for your
  redistribution.

---

## 11. Where to look in the source

For a full file-by-file annotation — every module's purpose, public interface, key
functions with line anchors, and data flows — see the companion
**[CODE_REFERENCE.md](CODE_REFERENCE.md)**. The quick map:

| You want… | Start here |
|---|---|
| The orbital math & Doppler | `predict.h` (interface), `predict.cpp` (SGP4 call site, eclipse, passes) |
| The radio abstraction | `rig.h` (base class + capability flags), `rig.cpp` (`makeRig` factory) |
| A specific CAT protocol | `civ.cpp` / `yaesu.cpp` / `kenwood.cpp` |
| Single-pin CI-V (ESP32-specific) | `CivRig::begin()` in `civ.cpp` |
| Rotator framing & transports | `rotator.cpp` |
| Element store & GP/OMM parsing | `satdb.cpp` (`gpToTle`, the streaming JSON parse) |
| Network fetch & TLS | `net.cpp` |
| Filesystem seam | `storage.{h,cpp}` (`Store::fs`, `Store::begin`) |
| Pins, URLs, limits | `config.h` |
| Persisted settings model | `settings.h` / `settings.cpp` |
| The service loop & Doppler tuning logic | `App::loop()` in `app.cpp` (search `lastRxSet`, `knobMoveThreshHz`) |
| Entry point / bring-up | `main.cpp` → `App::setup()` |

The interface docs under **docs/interfaces/** (CI-V, single-pin CI-V, Icom LAN, rotator,
RS-232) document the wire protocols themselves, which are the same on any platform.
