# Scope: Porting CardSat to the Cardputer Zero

**Status: forward-looking design scope — hardware not yet shipping.** The
CardputerZero (announced May 2026, shipping ~November) replaces the ESP32 with a
**Raspberry Pi Compute Module Zero (CM0)** and runs **Linux**. This is a different
platform class from the ESP32-S3 Cardputer ADV CardSat targets today, so a port is
a substantial rewrite — but it unlocks capabilities the ESP32 can't reach, chiefly
**USB-host radio control** and **multi-radio** operation. This document inventories
the hardware, maps what ports cleanly vs. what must be rebuilt, and lays out a
phased plan so work can start the day hardware arrives.

---

## 1. CardputerZero hardware (as announced)

| Item | Spec | Implication for CardSat |
|------|------|-------------------------|
| SoC | Raspberry Pi **CM0** — BCM2837, quad-core Cortex-A53 @1 GHz | Full Linux; orders of magnitude more compute than ESP32-S3 |
| RAM | 512 MB LPDDR2 | **No more heap-fragmentation gymnastics**; the no-PSRAM constraints vanish |
| OS | Linux (distro TBD by M5; likely Pi-derived) | Userspace process, not bare-metal Arduino |
| Display | 1.9" **ST7789V2** LCD (+ HDMI 1080p30) | Same controller family as today; bigger canvas; HDMI option |
| Keyboard | 46-key matrix | Re-map; fewer keys than the ADV's 56 — **UI key budget matters** |
| **USB** | **3× USB ports, switchable host/device** | **The headline: USB-host CAT for USB-CDC radios** |
| Net | **WiFi + BT + Ethernet** | Wired Ethernet → rock-solid LAN CAT (RS-BA1) and rigctld |
| Storage | microSD (32 GB bundled on full model) | Real filesystem; no LittleFS tightness |
| I/O | I2C, UART, SPI, GPIO, Grove, IR Tx/Rx | Rotator/ADS1115/PCF8574/LoRa paths still reachable via Linux GPIO |
| LoRa | LoRa Cap add-on supported | LoRa messaging path can continue (via Linux SPI) |
| Power | 1500 mAh LiPo / USB-C | Battery screen still relevant, via Linux power sysfs |

The two facts that reshape CardSat: **Linux userspace** (everything becomes a
normal program with threads, sockets, and a real libc) and **USB host** (the
Cardputer can now *be* the host that USB-CDC radios require).

---

## 2. What this unlocks

### 2.1 USB-host radio control (the big one)
Every radio marked OUT OF SCOPE in `HALFDUPLEX_RADIOS_SCOPE.md` because it needs a
USB host — **FT-991/991A, IC-7100**, and the whole class of modern USB-CDC rigs —
becomes reachable. On Linux they enumerate as `/dev/ttyUSB*` or `/dev/ttyACM*`, and
CardSat (or a bundled **Hamlib `rigctld`**) drives them directly. This is the
single biggest capability gain of the port.

### 2.2 Multi-radio operation
With 3 USB ports + Ethernet + WiFi, CardSat could control **separate uplink and
downlink radios** (the SatPC32 "two radios" model) — e.g. an HF/V rig for the
downlink and another for the uplink — and mix wired CI-V, USB-CDC, and LAN radios
**simultaneously**. The rig abstraction already isolates per-radio backends; on
Linux they can each run as an independent connection/thread.

### 2.3 Hamlib instead of hand-rolled CAT
On Linux, CardSat can **link against or shell out to Hamlib** (`libhamlib` /
`rigctld`) instead of maintaining per-rig CI-V/Yaesu/Kenwood byte tables. That
swaps CardSat's bespoke backends (and their untested-on-hardware risk) for the
most battle-tested rig library in the hobby — a major reliability win and a huge
reduction in the per-radio verification burden.

### 2.4 No more ESP32 constraints
The heap-fragmentation work, LittleFS tightness, no-PSRAM streaming reads, and
flash-size limits all disappear. TLS, JSON, and prediction can use ordinary
desktop libraries.

---

## 3. What ports cleanly vs. must be rebuilt

### 3.1 Portable as-is (the valuable core)
The **algorithms** are platform-neutral C++ and should move with light edits:
- SGP4/SDP4 propagation, pass prediction, geometry.
- Doppler engine: One True Rule, deadband/lead, `driveDownlink`/`driveUplink…`,
  tune modes, calibration. (This is the project's crown jewel — keep it intact.)
- The rig **interface** (`Rig` abstract class) and the per-rig logic, though the
  **transport** beneath it changes (see below).
- Transponder/SatNOGS data handling, calibration store, doppler math.

### 3.2 Must be rebuilt or re-platformed
- **Display & UI.** Today it's `M5Canvas`/`M5GFX` sprite pushes. On Linux, redraw
  via a framebuffer/DRM, SDL2, or LVGL against the ST7789V2 (or HDMI). The
  `draw*()` methods' *logic* survives; the *blitting layer* is new.
- **Keyboard.** M5 matrix scan → Linux input (evdev). 46 keys vs 56 → re-map the
  key table; some shortcuts need rethinking.
- **CAT transport.** `HardwareSerial`/`WiFiUDP` → Linux `termios` serial,
  POSIX UDP sockets, or Hamlib. The `icomnet.cpp` RS-BA1 code mostly survives (UDP
  is UDP); wired CI-V becomes `/dev/serial`; USB-CDC is new and easy on Linux.
- **GPIO/I2C/SPI peripherals.** ADS1115 (az/el), PCF8574 (rotator), LoRa SX1262,
  Grove → Linux `i2c-dev`, `spidev`, `libgpiod`. The rotator bang-bang logic ports;
  the pin access layer is new.
- **Storage.** LittleFS → normal files on the SD/rootfs. Simplifies a lot.
- **Power/battery.** `M5.Power` → Linux power-supply sysfs (`/sys/class/power_supply`).
  The new Charge/Sleep screen's `batteryPercent()` curve can stay; only the
  voltage read source changes.
- **Build system.** Arduino/PlatformIO → CMake/Make on Linux; cross-compile or
  build on-device (the CM0 can self-host a light build).
- **Concurrency model.** Bare-metal `loop()` → a proper Linux app: a tracking
  thread, a UI thread, and per-radio I/O threads. This is cleaner than the ESP32
  cooperative loop but is a structural change.

### 3.3 Architecture recommendation
Refactor into a **platform abstraction layer (PAL)**: keep the algorithm/Doppler/UI
*logic* in portable modules; isolate display-blit, input, serial/USB/UDP, GPIO/I2C/
SPI, storage, and power behind a thin interface with two implementations
(ESP32-Arduino and Linux). This lets one codebase serve both the Cardputer ADV and
the Zero, instead of forking. The existing modular `src/*` split is a good starting
point; the monolithic `CardSat.ino` is **ESP32-only** and would not apply to the
Linux target (the dual-apply discipline ends at the platform boundary).

---

## 4. Phased plan (start when hardware ships)

1. **Bring-up (week 1–2):** boot Linux, identify the display/keyboard drivers,
   confirm framebuffer + evdev, get a "hello" render on the ST7789V2.
2. **PAL skeleton:** define the platform interface; stub Linux display/input/serial.
3. **Port the core:** compile the propagation + Doppler + prediction modules on
   Linux behind the PAL; verify against known passes (host-testable!).
4. **CAT via Hamlib:** wire the rig interface to `rigctld`/`libhamlib`; validate
   one wired CI-V and one USB-CDC radio.
5. **UI:** re-implement the screen blits and key map; port screen logic.
6. **Peripherals:** rotator (libgpiod/i2c-dev), LoRa (spidev), power sysfs.
7. **Multi-radio:** add the dual-radio (separate up/down) path the platform enables.
8. **USB-CDC radios:** add FT-991/IC-7100 etc. profiles now that USB host exists.

The order front-loads the **host-testable** parts (propagation, Doppler, CAT over
Hamlib on a Linux PC) so much can be validated *before* the hardware even arrives.

---

## 5. Risks & open questions

- **OS/driver unknowns.** M5 hasn't finalized the distro or the display/keyboard
  driver stack; the bring-up effort depends heavily on what they ship. **Open Q:
  which kernel, which display driver, mainline or vendor?**
- **CM0 is modest.** Quad A53 @1 GHz / 512 MB is plenty for CardSat, but a heavy UI
  toolkit (full GTK) would be overkill; prefer LVGL/SDL2/framebuffer.
- **Effort is large.** This is a port, not a feature — display, input, transport,
  GPIO, build, and concurrency all change. Budget accordingly.
- **Two-target maintenance.** Without a disciplined PAL, the Zero port forks the
  project. The PAL is the mitigation but is itself upfront work.
- **USB-CDC radio variety.** Each USB rig still needs a Hamlib model + testing;
  Hamlib does the heavy lifting but per-rig validation remains.
- **Key-count regression.** 46 vs 56 keys may force UI shortcut changes.
- **Power management.** Linux suspend/blank differs from the ESP32 deep-sleep model;
  the Charge/Sleep screen concept ports but the mechanism is new.
- **Hardware not in hand.** Everything here is from M5's announcements; specifics
  (USB host current limits, GPIO mapping, display orientation) need confirmation on
  real hardware.

---

## 6. Recommendation

Treat the Zero as a **second target sharing one portable core**, not a fork.
**Invest first in a platform abstraction layer** so the propagation/Doppler/UI
*logic* is shared, and lean on **Hamlib** for CAT to shed the per-rig byte-table
maintenance and unlock USB-CDC radios. Prioritize, in order: (1) USB-host CAT, the
defining new capability; (2) multi-radio (separate up/down) operation; (3) the
USB-CDC radios that are out of scope on the ESP32 today. Begin with the
host-testable core port before hardware arrives in November.

> **No code is changed by this document.** Forward-looking scoping only.
