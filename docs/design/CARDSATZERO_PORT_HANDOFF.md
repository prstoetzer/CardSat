# CardSatZero — Port Kickoff Handoff

**Audience: a future Claude instance (and Paul) starting the CardSatZero port in a
fresh project.** This is the "read me first" for that work. It assumes the ESP32
CardSat repo (this repo) has been dropped into the new project as the reference
source. It does not repeat the design scope — it tells you where everything is,
what's already decided, what will bite you, and how to start.

> Written at CardSat **v0.9.32** (ESP32 line). If the ESP32 repo has moved on,
> trust the code over this memo, but the architecture below changes slowly.

---

## 0. The one-paragraph orientation

CardSatZero is a **Linux re-platforming** of CardSat for the M5Stack **Cardputer
Zero** (Raspberry Pi CM0, Debian/Pi OS, labwc Wayland), written so the same build
also runs on **other small-form-factor Linux devices** (e.g. ClockworkPi uConsole)
and on a **plain Linux PC** for development. The portable core (orbit propagation,
the Doppler engine, prediction, calibration, data handling) moves nearly as-is; the
**edges** (display, input, CAT/rotator transport, GPIO/SPI/serial, storage, power)
get rebuilt behind a platform abstraction layer (PAL). Three deliberate decisions
distinguish it from the ESP32 build: **radios/rotators default to Hamlib, with
CardSat's native drivers kept as a selectable per-radio fallback** (see the
Hamlib-vs-native trade-off analysis in `docs/guides/PORTING.md` §5a), **two radios
(separate uplink/downlink)**, and an **SDL2-first abstracted UI**.

---

## 1. Read these first, in this order

All paths are in the ESP32 reference repo unless noted.

1. **`docs/design/CARDPUTER_ZERO_PORT_SCOPE.md`** — the master design doc. §7 is how
   the Zero OS actually works (Wayland/labwc, login, the `.desktop` app contract,
   reserved keys); §8 is the design direction (8.1 Hamlib-default + native fallback,
   8.2 dual-radio, 8.3 + 8.3.1 the UI/resolution/keyboard plan, 8.5 keeping the Cap
   LoRa for GPS + messaging). **This memo is the companion to that doc — read the
   scope first**, and see `docs/guides/PORTING.md` §5a for the Hamlib-vs-native
   decision analysis.
2. **`docs/guides/PORTING.md`** — especially **§2 Portability tiers** (the
   module-by-module A/B/C/D table) and **§8 "Going off-Arduino"** (the
   `arduino_shim.h` desktop approach). The tier table tells you exactly which files
   lift cleanly and which are rewrites. Do not re-derive this; it's already done.
3. **`docs/guides/CODE_REFERENCE.md`** — the function/type map of the codebase.
4. **`docs/guides/HANDOFF.md`** — the ESP32 project's working invariants and
   hard-won lessons. Most apply to any CardSat work; the dual-apply rule (below)
   does not carry to the Linux port.
5. **`docs/interfaces/*`** (CIV/ROTOR/RS232/LoRa protocol docs) and
   **`docs/WIRING.md`** — only when you touch those specific subsystems.

---

## 2. Decisions already made (don't relitigate without reason)

These were made deliberately with Paul; treat them as the project's direction.

- **Separate GitHub repo for CardSatZero.** Recommended (and Paul's lean): keep the
  Linux port in its **own repo**, not a branch of the ESP32 repo. Rationale: the
  two targets share *portable source files* but nothing else — no shared build
  system (Arduino/PlatformIO vs CMake), no shared UI/transport layer, and the
  ESP32 repo's monolithic `CardSat.ino` is meaningless on Linux. A monorepo would
  mostly create confusion and a constant temptation to break the portable core with
  platform `#ifdef`s. **See §3 for how to share the core across two repos.**
- **Radios and rotators default to Hamlib, native drivers kept as a per-radio
  fallback** (scope §8.1; full trade-off analysis in `PORTING.md` §5a). Hamlib is the
  default and unlocks the USB-CDC class — link `libhamlib`, or (simpler) spawn/supervise
  a local `rigctld`/`rotctld` and use the existing `RigctlRig`/`RotctlRotator` TCP
  clients. The `civ/yaesu/kenwood/icomnet` backends are kept compiled in and selectable,
  chiefly to preserve the hardware-proven IC-821 single-pin path and as an
  always-working baseline while each rig's Hamlib sat-mode mapping is confirmed on the
  air. (If you instead decide on Hamlib-only per §5a, those backends just aren't built
  into the Linux target.)
- **Two radios** (scope §8.2): independent uplink (Main/TX) and downlink (Sub/RX)
  rigs, with single-radio mode pointing both at one rig (today's behavior).
- **SDL2-first UI** (scope §8.3): one SDL2 backend covers the Zero, the uConsole,
  and PC dev. Wayland-native and DRM/fbdev are optional later backends.
- **Keep the Cap LoRa** (scope §8.5) for GPS (AT6668 serial NMEA) and LoRa
  messaging (SX1262 via RadioLib's Linux `PiHal`). This stays a CardSat feature; it
  is *not* subsumed by Hamlib.

---

## 3. How to structure the port (the seam that makes this tractable)

The whole bet is that CardSat already separates *logic* from *platform* at clean
interfaces. Your job is to keep the logic and replace the platform, not to fork the
logic.

**Portable core (lift, per PORTING.md §2 Tier A/B):** `predict`, `location`
(NMEA/geo math), `satdb` (swap the filesystem calls), the `Rig`/`Rotator`
*interfaces*, the Doppler engine and calibration logic living in `app.cpp`, the
transponder/SatNOGS handling. These are C++ + `<Arduino.h>`; shim or remove the
Arduino include and they compile on Linux. PORTING.md §8 shows the `arduino_shim.h`
trick (provide `millis()`, `String`, etc.) — for a from-scratch Linux app you may
prefer to replace `String` with `std::string` and `millis()` with a
`std::chrono` helper rather than shim, but the shim is the fastest first compile.

**Sharing the core across two repos.** Options, roughly in order of preference:
1. **Git submodule / subtree** of the portable modules from the ESP32 repo into the
   CardSatZero repo. Keeps one source of truth for the crown-jewel math.
2. **Extract the portable core into a third small repo** (`libcardsat-core`) both
   targets depend on. Cleanest long-term; more upfront ceremony.
3. **Vendor (copy) the portable files** into CardSatZero and re-sync occasionally.
   Simplest to start; risks drift. Fine for an initial spike, not for the long run.
Discuss with Paul before committing; (1) is the usual sweet spot.

**The PAL (the new code).** Define thin interfaces and one Linux implementation:
- **Display surface** — a canvas of queryable size with the primitives the
  `draw*()` methods use (text, fill/rect, line, pixel, blit, `present()`). Back it
  with SDL2 first. **Critical:** the ESP32 UI hard-codes 240×135 in ~50 places
  (see §4); de-magic those to `canvas.width()/height()` as step one.
- **Input** — normalize platform key events into CardSat's existing semantic keys
  (`isUp`/`isDown`/`isBack` etc.), with a per-device key map and a reserved-keys
  list (the Zero takes `Tab`/`Esc`).
- **Radio/rotator** — a Hamlib-backed implementation of the `Rig`/`Rotator`
  interfaces (§8.1/§8.2).
- **Serial / SPI / GPIO** — Linux `termios`, `spidev`, GPIO (lgpio) for GPS and the
  Cap LoRa (§8.5).
- **Storage / power** — normal files; Linux power-supply sysfs.

---

## 4. Traps and specifics that will save you a day each

- **The UI is hard-coded to 240×135.** Literally: ~**51** `240` literals and **7**
  `135` literals in `app.cpp` (fixed-width `fillRect`/`drawLine`, status rows,
  selection highlights, **world-map vertices** as raw pixels). The Zero is 320×170;
  the uConsole differs again. Plan: (1) replace literals with runtime canvas
  dimensions; (2) pick letterbox-scale first (works day one), native reflow later;
  (3) convert map/compass/meter pixel-art to fractional 0..1 coords. (Scope §8.3.1.)
- **Reserved keys on the Zero.** The OS key policy owns `Tab` (task panel),
  **short Esc** (minimize), **long Esc** (close). CardSat must not bind those; move
  any in-app Esc/Tab use elsewhere, and handle the Wayland **close request** for a
  clean shutdown (flush config, stop radios). (Scope §7.5.)
- **App discovery is non-standard.** ZeroShell scans **only**
  `/usr/share/APPLaunch/applications/*.desktop` (not `/usr/share/applications`), and
  the entry **must** set `X-Zero-Display=wayland|xwayland` or it won't launch. A
  "task" is a compositor window, not a PID — CardSat must create a real
  Wayland/Xwayland toplevel. (Scope §7.4.)
- **Do NOT carry the `Store::remount()` LoRa/SD fix to Linux.** On the ESP32 the
  SX1262 and microSD share one SPI peripheral and the Arduino drivers fight over
  clock/mode, which is why v0.9.32 added `Store::remount()` after every LoRa op.
  The Linux kernel SPI subsystem arbitrates per transaction, so that workaround is
  **ESP32-only** — porting it would be cargo-culting a fix for a problem that no
  longer exists. (Scope §8.5.)
- **The `@`-suffix LoRa echo bug is still open** on the ESP32 line (own messages
  echo with a trailing `@`, receive/display path, RadioLib/hardware-dependent). If
  you reuse `lora.cpp` messaging logic, the round-trip framing/parse is clean in
  host sim — the bug is below it. Don't assume it's fixed; it isn't, as of 0.9.32.
- **VFO convention.** Everywhere in the code: **"Main" = uplink/TX, "Sub" =
  downlink/RX**, regardless of how a given radio labels its VFOs. The dual-radio
  split maps `rigUp->setMainFreq` / `rigDown->setSubFreq`. Calibration already has
  separate DL/UL offsets (`calDlHz`/`calUlHz`).
- **Hamlib VFO/split semantics vary per rig.** The one genuinely new piece of logic
  is mapping CardSat's Main/Sub model onto a given Hamlib model's split/sat
  handling. Confirm per rig on the air; it's confined behind the `Rig` interface.
- **Cap LoRa GPS = serial NMEA at 115200.** The AT6668 GNSS on the Cap LoRa is a
  UART (G15/G13 on the ESP32) at **115200 8N1**; on Linux it's a `/dev/tty*`
  device. The NMEA parser in `location.cpp` is portable — only the read changes.

---

## 5. Verification discipline to carry over (and what changes)

CardSat's quality came from host-side verification before hardware. Keep the
spirit; the specifics change for Linux.

- **Carry over:** host-test algorithms before trusting them (the Doppler/predict
  core is fully host-testable — propagate known TLEs, check against known passes);
  treat the bench radio / oscilloscope as ground truth for anything timing- or
  RF-related; ship a diagnostic rather than a theory when hardware is the only
  arbiter (this is how the v0.9.32 SD-bus bug was actually found).
- **Drop:** the **dual-apply** rule (mirror every edit into `CardSat.ino`) is
  ESP32-only — there is no monolithic `.ino` on Linux. Likewise `balance.py` /
  `parity.py` are about the dual-representation invariant and don't apply to a
  normal CMake project. Use ordinary tooling: a compiler with `-Wall -Wextra`, unit
  tests on the core, and `clang-format`.
- **New leverage on Linux:** you can now run the **actual CardSat core in CI** on an
  x86 runner, render the UI to an offscreen SDL surface and snapshot-diff layouts at
  240×135 and 320×170, and talk to a **dummy Hamlib rig** to exercise the radio path
  with no hardware. Note the Hamlib model split: run the **server** as
  `rigctld -m 1` (model 1 = "Hamlib Dummy", accepts all commands, does nothing), and
  CardSat's `RigctlRig` connects as a **client** on `localhost:4532` — Hamlib clients
  use model 2 ("NET rigctl") for that. Lean into this; it's a big step up from the
  ESP32's host sims. (The same pattern with `rotctld` and a dummy rotator covers the
  rotator path.)

---

## 6. A concrete first-session plan (when hardware/emulator is in hand)

Ordered to front-load the host-testable wins and de-risk early. (Scope §8.4 has the
same order; this is the operational version.)

1. **Stand up the build.** New repo, CMake, pull in (submodule) the portable core,
   get `predict`/`location`/`satdb` compiling on Linux (shim or replace
   `<Arduino.h>`). Prove the core with a CLI that prints the next pass for a known
   TLE — no UI yet. Fully host-testable.
2. **SDL2 UI backend + de-magic the coordinates.** Bring up a window, implement the
   display-surface PAL on SDL2, and replace the 240×135 literals with runtime
   dimensions. Letterbox-scale to start. Now CardSat *renders* on a PC.
3. **Input PAL.** Map SDL/Wayland keys to the semantic key set; wire the reserved
   keys for the Zero. The existing `key*()` handlers should now work unchanged.
4. **Single-radio Hamlib.** Implement the `Rig` interface over a local `rigctld`
   (start with the **Dummy** model, then a real rig). Get Doppler driving frequency.
   Hamlib is the default; once it works, add the **per-radio driver selector** so a
   radio can instead route to the native `civ/yaesu/kenwood/icomnet` backend ported to
   Linux serial (the IC-821 single-pin path is the prime reason to keep this). See
   `PORTING.md` §5a for why both paths ship.
5. **Second radio.** Split Main/Sub onto two `Rig` handles; add the Single/Dual
   toggle and the second rig config.
6. **Rotator via `rotctld`**, then **Cap LoRa** GPS (serial) and messaging
   (RadioLib `PiHal` + lgpio).
7. **Package** an APPLaunch `.desktop` entry with `X-Zero-Display`, and group/udev
   rules for device access; confirm on the real panel (orientation/offset, keymap).

Steps 1–4 need no Zero hardware — they run on any Linux PC. That's the point.

---

## 7. Open questions to resolve early (mostly need the hardware)

- Does the community OS profile + ZeroShell ship by default, or is it an opt-in
  install? (Affects packaging assumptions — scope §5.)
- Physical Cap LoRa attachment on the CM0: which SPI controller/CS, which GPIO
  lines, GPS on a hardware UART vs a USB-serial bridge? (Scope §8.5.)
- Panel specifics: exact orientation/offset and the real keyboard matrix/keymap on
  the Zero (and separately on the uConsole).
- USB-host current limits for bus-powered USB-CDC rigs.
- Hamlib model coverage for the specific rigs Paul runs, and their split/sat quirks.

---

## 8. Working norms with Paul (so the future Claude fits the established rhythm)

- **Paul drives hardware.** He commits, flashes/installs, and runs the device;
  Claude does host-side reasoning, code, and verification. The bench (and now a
  Linux box) is ground truth.
- **Ground every technical claim in the actual source**, not in summaries or
  memory. Read the header before describing a function. This caught real errors in
  the ESP32 docs and should continue.
- **Scope docs are forward-looking and say so** ("no code changed by this
  document"). Keep that honesty — don't describe intended behavior as if shipped.
- **Prefer a diagnostic over a theory** when only the hardware can decide. The
  v0.9.32 SD-bus saga is the cautionary tale: two plausible theories (chip-select,
  then bus-contention CS hygiene) were wrong before a serial diagnostic + a real
  remount fixed it. Confidence is not evidence.

---

> **No code is changed by this document.** It is a handoff for future work.
