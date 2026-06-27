# Scope: Porting CardSat to the Cardputer Zero

**Status: forward-looking design scope — hardware not yet shipping.** The
CardputerZero (announced May 2026, shipping ~November) replaces the ESP32 with a
**Raspberry Pi Compute Module Zero (CM0)** and runs **Linux**. This is a different
platform class from the ESP32-S3 Cardputer ADV CardSat targets today, so a port is
a substantial rewrite — but it unlocks capabilities the ESP32 can't reach, chiefly
**USB-host radio control** and **multi-radio** operation. This document inventories
the hardware, maps what ports cleanly vs. what must be rebuilt, and lays out a
phased plan so work can start the day hardware arrives.

> **Starting the actual port?** Read **`CARDSATZERO_PORT_HANDOFF.md`** (same folder)
> alongside this — it's the "read me first" kickoff for the port work: where things
> live, the decisions already made, the traps, and a concrete first-session plan.

---

## 1. CardputerZero hardware (as announced)

| Item | Spec | Implication for CardSat |
|------|------|-------------------------|
| SoC | Raspberry Pi **CM0** — BCM2837, quad-core Cortex-A53 @1 GHz | Full Linux; orders of magnitude more compute than ESP32-S3 |
| RAM | 512 MB LPDDR2 | **No more heap-fragmentation gymnastics**; the no-PSRAM constraints vanish |
| OS | **Raspberry Pi OS / Debian** (M5 community profile; see §7) | Userspace process under a labwc Wayland session, not bare-metal Arduino |
| Display | 1.9" **ST7789V2** LCD (+ HDMI 1080p30) | Same controller family as today; bigger canvas; HDMI option |
| Keyboard | 46-key matrix | Re-map; fewer keys than the ADV's 56 — **UI key budget matters** |
| **USB** | **3× USB ports, switchable host/device** | **The headline: USB-host CAT for USB-CDC radios** |
| Net | **WiFi + BT + Ethernet** | Wired Ethernet → rock-solid LAN CAT (RS-BA1) and rigctld |
| Storage | microSD (32 GB bundled on full model) | Real filesystem; no LittleFS tightness |
| I/O | I2C, UART, SPI, GPIO, Grove, IR Tx/Rx | Rotator/ADS1115/PCF8574/LoRa paths still reachable via Linux GPIO |
| Cap LoRa | M5Stack Cap LoRa add-on (advertised with the Zero) | **Kept for both roles** — GNSS (AT6668 serial) and SX1262 LoRa messaging, via Linux serial/SPI/GPIO. See §8.5 |
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
- **Display & UI.** Today it's `M5Canvas`/`M5GFX` sprite pushes. On the Zero the
  internal screen is a **labwc Wayland** session (DRM/KMS via `panel-mipi-dbi-spi`),
  so CardSat must become a **Wayland (or Xwayland) client** — see §7. The
  `draw*()` methods' *logic* survives; the *blitting layer* becomes Wayland
  shared-memory buffers (e.g. via SDL2/Xwayland for the quickest port, or native
  Wayland for the cleanest). Canvas is 320×170 landscape.
- **Keyboard.** M5 matrix scan → Wayland/libinput key events delivered to the
  focused client. 46 keys vs 56 → re-map the key table. Note that **`Tab` and
  `Esc` are reserved** by the OS key policy for task switching / minimize / close
  (§7), so CardSat can't use them as in-app shortcuts the way it does today.
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

1. **Bring-up (week 1–2):** the display/input stack is known (§7) — labwc Wayland
   at 320×170. Prototype the UI as a Wayland/Xwayland client (SDL2 bridge) against
   M5's **emulator** (`M5CardputerZero-Emulator`) before hardware, then confirm on
   the real panel (orientation/offset, keyboard map) when it ships.
2. **PAL skeleton:** define the platform interface; stub Linux display/input/serial.
3. **Port the core:** compile the propagation + Doppler + prediction modules on
   Linux behind the PAL; verify against known passes (host-testable!).
4. **CAT via Hamlib:** wire the rig interface to `rigctld`/`libhamlib`; validate
   one wired CI-V and one USB-CDC radio.
5. **UI:** re-implement the screen blits as a Wayland/Xwayland client and port the
   key map (remembering `Tab`/`Esc` are reserved by the OS — §7.5); port screen logic.
6. **Peripherals:** rotator (libgpiod/i2c-dev), LoRa (spidev), power sysfs.
7. **Multi-radio:** add the dual-radio (separate up/down) path the platform enables.
8. **USB-CDC radios:** add FT-991/IC-7100 etc. profiles now that USB host exists.

The order front-loads the **host-testable** parts (propagation, Doppler, CAT over
Hamlib on a Linux PC) so much can be validated *before* the hardware even arrives.

---

## 5. Risks & open questions

- **OS/driver stack — now largely known (see §7).** M5's community OS profile
  (`cardputer-zero-os`) and shell (`cardputer-zero-shell`) document the stack:
  Raspberry Pi OS / Debian, a DRM/KMS + **labwc Wayland** internal-screen session,
  and a `.desktop`-based app contract. The remaining unknown is hardware
  confirmation (panel orientation/offset, USB host current, GPIO map) on a real
  unit. **Open Q: are the community OS profile and shell what ships by default, or
  an opt-in install?**
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

See **§8** for the concrete design direction this recommendation points to:
**Hamlib-exclusive** radio/rotator control (§8.1), a **two-radio** uplink/downlink
model (§8.2), and an **abstracted, SDL2-first UI** that also runs on other
small-form-factor Linux devices such as the ClockworkPi uConsole (§8.3).

## 7. How the Zero OS functions (and what CardSat must do to run on it)

M5's community has published the system software for the Zero, so the OS is no
longer a black box. Two repositories under `github.com/CardputerZero` define it,
and together they pin down exactly how an app like CardSat boots, launches, draws,
and takes input. This section summarizes that and translates it into porting
requirements. (Source: the `cardputer-zero-os` and `cardputer-zero-shell` READMEs;
verify against the hardware when it arrives.)

### 7.1 The software stack, top to bottom
- **Base OS:** Raspberry Pi OS / Debian, built from M5's fork of `pi-gen` (the
  official Pi OS image builder). Users are created the normal way (Raspberry Pi
  Imager / standard Linux tools) — there is **no fixed `zero` user and no
  autologin**.
- **`cardputer-zero-os` (system profile):** owns the internal-screen login,
  session, and display policy. It is explicitly *not* the desktop, launcher, or
  app UI.
- **`cardputer-zero-shell` / ZeroShell (post-login shell):** the launcher/task UI
  that runs as a normal Wayland client after login. It discovers and launches apps
  but is *not* a privilege boundary.
- **Applications (CardSat's tier):** own their own windows and domain UI. To exist
  as a "task," an app must create a Wayland or Xwayland toplevel window.

### 7.2 Graphics path — it's Wayland, not a framebuffer
The internal 1.9" ST7789 is driven through the **standard Linux display stack**,
not direct SPI blits:

```
internal ST7789 panel
  -> panel-mipi-dbi-spi (DRM/KMS)
  -> labwc Wayland compositor
  -> Wayland / Xwayland clients
```

Practical facts for CardSat:
- The user view is **320×170, landscape**. HDMI is a separate output on its own
  seat (the internal panel is `seat-cardputer-zero`; HDMI stays on `seat0`).
- The compositor is **labwc** (a wlroots compositor). CardSat must present as a
  Wayland client (native Wayland, or X11 via **Xwayland**). There is no supported
  "just write the framebuffer" path — if the standard path fails it is meant to
  fail visibly and be fixed via SSH/HDMI, not fall back to a private backend.
- Easiest port route: render the existing canvas into an **SDL2** window (SDL2
  speaks Wayland, or runs under Xwayland) and declare the app as `xwayland`.
  Cleanest route: a native Wayland client drawing shared-memory buffers.

### 7.3 Boot and login
`systemd` brings up a logind greeter session on the internal seat; a small Wayland
**greeter** (running as the unprivileged `_greetd` user) draws the 320×170 login
form; **PAM** authenticates an existing Linux user through a restricted root
helper; **logind** then starts the user session, labwc, ZeroShell, and finally the
launched apps. CardSat never deals with any of this — it just needs a normal user
account to log into. There's no kiosk/auto-start of a single app by default; the
device boots to the ZeroShell launcher.

### 7.4 The application contract (how CardSat gets listed and launched)
ZeroShell scans exactly one directory:

```
/usr/share/APPLaunch/applications/*.desktop
```

(*not* the usual `/usr/share/applications`). So a CardSat package must install a
`.desktop` entry there. A minimal entry adapted for CardSat:

```
[Desktop Entry]
Name=CardSat
Exec=cardsat
TryExec=cardsat
Icon=share/images/cardsat.png
Categories=HamRadio;Utility;
X-Zero-Display=xwayland
StartupWMClass=cardsat
```

Contract notes:
- **`X-Zero-Display` is required** and must be `wayland` or `xwayland`; entries
  without it are not launched. This is the field that tells the shell how CardSat
  presents its window.
- `Categories=` drives the launcher's category drawer (uncategorized apps land
  under "Other"; "All" always shows everything).
- `StartupWMClass` (Xwayland) or `X-Zero-AppId` (Wayland) lets the shell match the
  app's window back to its launcher entry for the "running" badge.
- A "task" is a **compositor window**, not a PID. CardSat is only considered
  running once labwc sees its toplevel; a headless process won't register.

### 7.5 Input and the reserved global keys
Key events arrive via Wayland/libinput to whichever window has focus. Crucially,
the OS key policy **reserves three global gestures** and routes them to the shell,
not to the focused app:

```
Tab        -> toggle the running-tasks panel
short Esc   -> minimize the active app, return to ZeroShell
long Esc    -> request the active app to close, return to ZeroShell
```

CardSat today leans on `Esc`/`Tab` for in-app navigation; on the Zero those are
taken, so the key map must move those functions elsewhere. Everything else
(letters, arrows, Enter, etc.) is delivered to CardSat normally while it has
focus. Closing is cooperative: a **long Esc** asks CardSat's window to close, so
CardSat should handle a Wayland close request by shutting down cleanly (flush
config, stop radios) rather than being killed.

### 7.6 Privilege (rotator, serial, GPIO, USB)
Privileged actions go through **polkit** and a restricted helper (`zero-helper`);
there is no blanket root path. For CardSat this mostly matters for device access —
serial ports, `spidev`, `i2c-dev`, `gpiochip`, USB-CDC rigs. The clean approach is
to put the CardSat user in the right groups (e.g. `dialout`, `i2c`, `spi`, `gpio`,
`plugdev`) via the package/udev rules rather than escalating at runtime, so the app
runs unprivileged like every other Zero app.

### 7.7 What this means for the port (delta vs §3)
The earlier "rebuild the display/input layer" items (§3.2) now have concrete
targets instead of open questions:
- Display → a **Wayland/Xwayland client** at 320×170 (SDL2 is the pragmatic
  bridge), not a raw DRM/framebuffer renderer.
- Input → Wayland key events, with **`Tab`/`Esc` reserved** — re-map those.
- Packaging → ship an **APPLaunch `.desktop`** entry with `X-Zero-Display`, plus
  group/udev rules for device access; no systemd/login work (the OS owns that).
- Lifecycle → handle the Wayland **close request** for clean shutdown.

None of this changes the §6 recommendation — the portable core (propagation,
Doppler, prediction, rig logic) is unaffected; only the platform edges become
better-specified. It does, however, make the **UI/input/packaging** edge of the
PAL concrete enough to prototype against M5's **emulator**
(`M5CardputerZero-Emulator`) before hardware ships.

---

## 8. Design direction for the Zero port

The OS detail in §7 settles *how* CardSat runs on the Zero; this section sets the
*intended shape* of the port itself. Three decisions, each a deliberate divergence
from the ESP32 build, and each something the current architecture already leans
toward. These are direction, not committed implementation — they're written so the
work has a target the day hardware (or the emulator) is in hand.

### 8.1 Radios and rotators exclusively via Hamlib

On the ESP32, CardSat carries its own per-rig CAT backends — `CivRig` (CI-V),
`YaesuRig` (5-byte), `KenwoodRig` (ASCII), `IcomNetRig` (RS-BA1/UDP) — because
there is no room for a 2 MB rig library on the chip. On the Zero that constraint is
gone and **Hamlib is available natively**, so the Zero port should **drop the
hand-rolled backends entirely and talk to radios only through Hamlib**, and
rotators only through Hamlib's rotator side (`rotctld`/`rotctl`).

**Why this is a small change, not a rewrite.** The whole app already speaks to the
radio through the narrow `Rig` interface (`setMainFreq`/`setSubFreq`,
`readSubFreq`/`readMainFreq`, `readPtt`, `enableSatMode`, `selectSubBand`, …), and
a Hamlib client backend **already exists**: `RigctlRig` in `rig.h` is a NET-rigctl
TCP client. The rotator side mirrors this — `RotctlRotator` already speaks Hamlib
NET rotctl. So the Zero radio/rotator layer is mostly *selection and wiring*, not
new protocol code:

- **Collapse the rig factory.** On the Zero build, `makeRig()` returns a Hamlib
  backend unconditionally instead of switching on `PROTO_CIV/YAESU/KENWOOD`. The
  CI-V/Yaesu/Kenwood/IcomNet `.cpp` files simply aren't compiled into the Zero
  target. (They stay in the ESP32 build untouched — this is a per-platform
  selection, like the rest of the PAL.)
- **Own the Hamlib connection locally.** On Linux, CardSat can either link
  `libhamlib` directly or — simpler and more robust — **launch and supervise a
  local `rigctld`** (one per radio) and drive it through the existing `RigctlRig`
  TCP client on `localhost`. The "spawn a helper daemon and talk to it over a
  socket" pattern matches how the Zero OS itself is built, and keeps CardSat's
  process model clean. Rotators get the same treatment via `rotctld` and
  `RotctlRotator`.
- **Make Hamlib configurable inside CardSat.** This is the part that needs new UI,
  not new protocol code. The radio settings screen changes from "pick a CardSat
  model profile" to **"pick a Hamlib rig"**: a Hamlib **model id** (the `rigctld -m`
  number), the **device/port** (`/dev/ttyUSB0`, `/dev/ttyACM0`, a network address,
  or `localhost:4532` for an already-running `rigctld`), **serial parameters**, and
  any **per-rig Hamlib options** (`set_conf` key/values). CardSat stores these in
  its config and uses them to start/connect the daemon. A "list models" helper can
  shell out to `rigctl --list` so the user picks from real Hamlib models rather
  than typing a number blind.

**What this buys the Zero port:** every radio Hamlib supports — including the
USB-CDC rigs that are OUT OF SCOPE on the ESP32 today (FT-991/991A, IC-7100, and
the rest of `HALFDUPLEX_RADIOS_SCOPE.md`) — becomes usable with no CardSat protocol
work, and the per-rig verification burden moves to Hamlib, the most tested rig
library in the hobby. The CI-V band-assignment / sat-mode nuances CardSat hand-codes
(`enableSatMode`, `assignBands`) map onto Hamlib's `set_func`/`set_vfo`/`set_split`
calls; where Hamlib can't express something, the `Rig` method degrades to a no-op
exactly as the abstract base already allows.

**Cost / caveat.** Hamlib's model is one `rigctld` per physical radio, and its
split/sat semantics vary by rig. The mapping from CardSat's "Main = uplink/TX,
Sub = downlink/RX" convention onto a given Hamlib model's VFO/split handling is the
one genuinely new piece of logic and needs per-rig confirmation on the air — but
it's confined behind the `Rig` interface and is far less code than maintaining the
byte tables.

### 8.2 Two radios: separate uplink and downlink

The Zero's 3 USB ports + Ethernet + WiFi make the **SatPC32 "two radios" model**
practical: one rig for the **downlink (RX)** and a *different* rig for the
**uplink (TX)**. CardSat's interface is already half the way there — the `Rig`
methods are split into Main (TX) and Sub (RX) legs precisely because full-duplex
satellite work treats them independently. Today both legs live on **one** `Rig`
instance; the dual-radio model puts them on **two**.

**Proposed shape:**

- **Two `Rig` handles, not one.** Replace the single `rig` pointer with a small
  holder — conceptually `rigDown` (drives Sub/RX) and `rigUp` (drives Main/TX). In
  **single-radio** mode both pointers reference the same object (today's behavior,
  zero functional change). In **dual-radio** mode they're two independent Hamlib
  connections to two `rigctld` instances.
- **Route the Doppler legs to the right handle.** The Doppler loop already calls
  `driveDownlink(...)` → `setSubFreq` and `driveUplinkDeferred(...)` →
  `setMainFreq`. Those calls just need to target `rigDown->setSubFreq(...)` and
  `rigUp->setMainFreq(...)` respectively. With two radios, each is effectively a
  simplex rig: the downlink radio only ever has its RX leg driven, the uplink radio
  only its TX leg — which actually *simplifies* the per-rig VFO logic versus
  cramming both onto one full-duplex rig.
- **PTT and read-back per radio.** `readPtt` is read from the uplink radio (the one
  that transmits); the downlink radio's frequency read-back (`readSubFreq`) drives
  the calibration/knob-follow path. The existing "skip the downlink read while
  transmitting" guard still applies, now keyed on the uplink radio's PTT.
- **Config + UI.** A **Single / Dual** radio mode toggle, and when Dual is
  selected, a **second Hamlib rig configuration** block (model, port, params) for
  the uplink radio. Everything from §8.1 applies twice. Calibration becomes
  per-leg, which the calibration store already supports (separate DL/UL offsets).
- **Mixed transports fall out for free.** Because both radios are just Hamlib
  connections, the pair can be any mix — a USB-CDC downlink rig and a LAN (RS-BA1
  via Hamlib) uplink rig, etc. — without special-casing.

This is the capability that most distinguishes the Zero port from the ESP32 build,
and the existing Main/Sub split is what makes it tractable rather than a rewrite of
the Doppler engine.

### 8.3 An abstracted UI layer (portable beyond the Zero)

The Zero should not be treated as a one-off Linux target. The same Linux build
should run on **other small-form-factor Linux handhelds** — e.g. the **ClockworkPi
uConsole/DevTerm**, GPD/PocketCHIP-class devices, or a plain Pi with a small panel.
What differs between them is the **screen size, the input device, and the display
server** (Wayland here, X11 or DRM/KMS elsewhere), not the application. So the UI
must be abstracted behind a thin surface, and the platform-specific blit/input code
kept on the far side of it.

**Recommended boundary.** Define a minimal **UI platform interface** — the display
half of the PAL from §3.3 — that the portable screen logic draws against:

- **A drawing surface**, not a toolkit. CardSat already renders into an offscreen
  canvas (`M5Canvas`) and pushes it; keep exactly that mental model. The interface
  exposes a framebuffer/canvas of *queryable* dimensions plus the primitives the
  `draw*()` methods use (text, rect/fill, line, pixel, blit) and a `present()`. The
  screen logic stays resolution-aware rather than hard-coded to 240×135 or 320×170.
- **An input event source**, normalized to CardSat's existing key codes, so the
  `key*()` handlers don't change. Each platform maps its native events (Wayland
  `wl_keyboard`, X11, evdev) into that set — and applies platform key reservations
  (on the Zero, `Tab`/`Esc` are taken — §7.5; on the uConsole the key map and
  modifiers differ).
- **A few lifecycle hooks**: `present()`/vsync, a close/quit request (the Zero's
  cooperative "long Esc → close" — §7.5), and backlight/blank if the platform
  exposes it.

**Concrete backends behind that one interface:**

- **SDL2** — the pragmatic default. One SDL2 backend covers the Zero (SDL2 speaks
  Wayland, or runs under Xwayland), the uConsole, and desktop dev, and gives a
  free **development path on a normal PC**. This is almost certainly the right first
  (and possibly only) Linux backend; it makes the "portable to other SFF Linux
  devices" goal mostly automatic.
- **Native Wayland** — optional, for the leanest footprint on the Zero
  specifically (shared-memory buffers, no SDL/Xwayland), if SDL2 overhead ever
  matters.
- **DRM/KMS or fbdev** — optional, for a panel device with no compositor.

**Why this is the same PAL, not a second abstraction.** §3.3 already calls for a
platform abstraction layer splitting display/input/serial/GPIO/storage/power. The
UI interface here *is* the display+input face of that PAL, stated concretely. The
payoff: the resolution-aware screen logic and the whole portable core compile once
and run on the Zero, the uConsole, and a Linux desktop, with only the small
SDL2/Wayland/DRM backend selected per device. The monolithic `CardSat.ino` stays
ESP32-only and does not participate (the dual-apply discipline ends at the platform
boundary, as noted in §3.3).

#### 8.3.1 The concrete problem: resolution, layout, and a different keyboard

Abstracting the *backend* is the easy half. The harder half is that the existing
UI is written for **one specific screen** — 240×135, landscape — and the targets
differ: the **Cardputer ADV** is 240×135, the **Zero** is **320×170**, and the
**uConsole** is different again (and much larger). The porting work is therefore as
much about **layout** as about blitting.

**Where the 240×135 assumption is baked in.** A grep of `app.cpp` finds the screen
width `240` hard-coded in **~50 places** and the height `135` in a handful more —
`fillRect(0, 40, 240, 40, …)`, `drawLine(0, 49, 240, 49, …)`, status bars at fixed
`y` rows, selection highlights of fixed height, and the **world-map vertices**,
which are literal pixel coordinates plotted for a 240-px-wide canvas. Today these
"just work" because there is exactly one display. The moment a second resolution
exists they are bugs: content clips on the left/right, or leaves dead margins, and
the map is mis-scaled. So the first real task is to **delete the magic numbers**,
not to pick a toolkit.

**Recommended approach — a coordinate layer, in three steps:**

1. **Make the canvas size a runtime value, not a literal.** Replace `240`/`135`
   throughout the `draw*()` methods with `canvas.width()` / `canvas.height()` (the
   PAL surface already exposes these). Most fixed rects become width-relative
   (`fillRect(0, y, W, h, …)`); horizontal lines span `0..W`. This alone makes the
   UI *fill* any resolution correctly even before anything is redesigned, and it's
   mechanical, low-risk, and **host-testable** (render to an offscreen buffer at
   240×135 and at 320×170 and diff the layout). This is the bulk of the effort and
   should be done first.

2. **Choose a scaling policy per screen class, not per pixel.** Two clean options,
   selectable in the PAL:
   - **Integer/área scale (letterbox).** Render the existing layout into a 240×135
     logical canvas and **scale the whole surface up** to the device (e.g. ~1.33×
     to 320×170, centered, with a small border). Zero code churn in the screens,
     immediately correct on every device, at the cost of not *using* the extra
     pixels. Best as the **first** Zero milestone — it gets CardSat on-screen and
     usable on day one.
   - **Native reflow.** Let the resolution-relative layout from step 1 actually use
     the larger canvas — more list rows visible, a bigger map, larger fonts on the
     uConsole. More work per screen, but it's what makes the Zero/uConsole feel
     native rather than a magnified Cardputer. Do this **screen by screen** after
     letterboxing proves the port.

   The PAL exposes one knob — *logical size* + *scale mode* — so a new device is a
   table entry (`{name, w, h, scale_mode}`), not a code change.

3. **Handle the world map and any other pixel-art specially.** The map vertex
   tables are normalized to the 240-px canvas; convert them to **fractional
   coordinates (0..1)** scaled at draw time by `canvas.width()/height()`, so the
   coastline is correct at any resolution instead of being re-digitized per screen.
   Same treatment for the compass/polar plots and the signal meters.

**Fonts.** M5GFX bitmap fonts are sized in pixels; at 320×170 the current 8-px
text is legible but small, and on the uConsole it would be tiny. The PAL's text
primitive should take a **logical point size** and let the backend pick the nearest
real font (or scale), so a screen written once reads correctly across devices. The
letterbox policy sidesteps this initially (the whole canvas scales, text included);
native reflow needs the font abstraction.

**The keyboard is a separate, real difference.** CardSat's navigation is already
abstracted at the *semantic* level — `isUp()`, `isDown()`, `isBack()` map the raw
key to an action (today `;` = up, `.` = down, `` ` `` = back, arrows/enter for the
rest), and every `key*()` handler works in those terms. That indirection is exactly
what makes a keyboard port tractable: **only the char-to-action map changes**, not
the handlers. But the maps genuinely differ:
- **Cardputer ADV (56 keys):** the current mapping.
- **Zero (46 keys):** fewer keys, and — critically — **`Tab` and `Esc` are
  reserved by the OS** (§7.5) for task-switch / minimize / close. CardSat must
  **not** bind those, so any in-app use of `Esc`/`Tab` (e.g. back/cancel) has to
  move to another key. This is a behavioral change, not just a remap.
- **uConsole:** a full QWERTY with a trackball/dpad and modifier keys — arrows and
  Enter are available, so navigation can use the *expected* keys rather than the
  Cardputer's `;`/`.` convention.

So the PAL input layer provides a **per-device key map** feeding the existing
`is*()` helpers, plus a small **reserved-keys list** the app honors (so it never
fights the compositor for `Tab`/`Esc` on the Zero). The screen logic above that
line is untouched.

**What this means in practice.** Step 1 (de-magic the coordinates) is the real,
host-testable work and pays off on every target. Letterbox scaling gets a working,
correct CardSat on the Zero quickly; native reflow is an incremental polish pass.
The keyboard is a small per-device table plus the Zero's reserved-key rule. None of
this touches the propagation/Doppler/prediction core — it's confined to the
`draw*()`/`key*()` layer and the PAL, which is the whole point of the abstraction.

### 8.4 How these three fit together

The Zero target becomes: the **portable core** (prediction, Doppler, calibration,
data) unchanged; a **Hamlib-only** radio/rotator layer behind the existing `Rig`/
`Rotator` interfaces (§8.1), extended to **two radios** (§8.2); and a **UI behind
an SDL2-first abstraction** (§8.3) that also reaches other SFF Linux devices. All
three live behind interfaces the codebase already has — the `Rig`/`Rotator`
abstractions and the canvas/key model — which is why this is a *re-platforming
behind stable seams* rather than a fork. Suggested order once hardware/emulator is
available: SDL2 UI backend first (unblocks everything and runs on a PC), then
single-radio Hamlib via local `rigctld`, then the second radio, then the rotator
via `rotctld`.

### 8.5 Keep the Cap LoRa: GPS and messaging on CardSatZero

The M5Stack **Cap LoRa** is advertised alongside the Cardputer Zero, and the
**intent is to keep using it on CardSatZero for both of its CardSat roles** — GNSS
position and CardSat-to-CardSat LoRa text messaging. Both port cleanly to Linux;
neither depends on anything ESP32-specific, and unlike the radio/rotator layer
(§8.1) this is *not* replaced by Hamlib — it stays a first-class CardSat feature.

**GPS (the Cap's AT6668 GNSS).** On the ESP32, CardSat reads the Cap LoRa's onboard
**AT6668** receiver as a serial NMEA stream — the `Cap LoRa868` / `Cap LoRa1262`
GPS sources are a UART at **115200 8N1** (G15/G13). On the Zero that same receiver
appears as an ordinary **Linux serial device** (`/dev/ttyAMA*` / `/dev/ttyUSB*` /
`/dev/serial0`, depending on how the cap is wired to the CM0). The NMEA parser is
already platform-neutral C++; only the *transport* changes — `HardwareSerial` →
Linux `termios` at 115200 — which is the same serial-PAL work §3.2 already lists for
CAT. So GPS continuity is essentially free: the `GpsSource` model becomes
"path + baud" (a `/dev/tty*` device and 115200) instead of a UART-number/pin tuple,
and the parsing, fix handling, and grid/locator math are untouched.

**Messaging (the Cap's SX1262 LoRa).** CardSat's LoRa text messaging talks to the
Cap's **SX1262** through **RadioLib over SPI** (plus a couple of GPIO lines for
NSS/DIO1/RST/BUSY). On the Zero the SX1262 is reached over **Linux SPI (`spidev`)**
and **GPIO** — and conveniently, **RadioLib supports non-Arduino targets through a
HAL**, including a Raspberry Pi HAL (`PiHal`, the upstream `NonArduino/Pi` build
path), so the existing `lora.cpp` logic (framing, the message protocol,
send/poll, the listen/RX path) can largely be reused with its `Module`/SPI binding
re-pointed at the Linux HAL instead of the Arduino SPI. The HAL wires RadioLib's
pin/SPI calls to `spidev` plus a GPIO backend — the modern choice is **lgpio**
(uses `/dev/gpiomem`, so it runs **without root**) rather than the older pigpio the
upstream example ships with. The application-level
messaging code (compose, the broadcast group-chat protocol, the message store and
screen) is platform-neutral and moves as-is.

**One ESP32-specific thing that simply disappears.** A whole class of 0.9.32 bug —
the **shared-SPI-bus contention** between the SX1262 and the microSD card (which
required the `Store::remount()` fix after every LoRa operation) — is an artifact of
the ESP32 sharing one SPI peripheral and the Arduino `SD`/RadioLib drivers fighting
over its clock/mode. On Linux the kernel SPI subsystem arbitrates the bus per
transaction across `spidev` and the SD/MMC driver, so **that fix is not needed on
the Zero** — the remount workaround is ESP32-only and should not be carried into the
Linux port. (If the Cap LoRa and SD end up on genuinely separate SPI controllers on
the CM0, they don't even share a bus.)

**Where it sits in the PAL.** GPS and LoRa are two more **device-access** edges of
the platform layer (§3.2/§3.3): GPS = a serial path, LoRa = `spidev` + `gpiod`.
Like the rest of the Zero's device access (§7.6), the clean approach is **group/
udev permissions** (`dialout` for the GPS tty, `spi`/`gpio` for the SX1262) so
CardSat runs unprivileged. Both are independent of the Hamlib radio/rotator work —
a user can run a Hamlib-controlled satellite station **and** the Cap LoRa for
position + messaging at the same time, exactly as the ESP32 build does today.

**Open question.** How the Cap LoRa physically attaches to the CM0 on the Zero
(which SPI controller / chip-select, which GPIO lines, whether GPS is on a
hardware UART or a USB-serial bridge) is a **hardware-confirmation item** — the
pin/bus specifics come from the Zero + Cap LoRa wiring when in hand, the same
at-your-own-risk caveat the ESP32 LoRa path already carries.

---

> **No code is changed by this document.** Forward-looking scoping only.
