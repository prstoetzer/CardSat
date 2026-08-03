# Planning: a native ESP-IDF build for CardSat

**Status: assessed, not started. No code or build files have been changed.**

Written after the 0.9.71 USB investigation, where several promising leads ended at the
same wall: the behaviour we wanted to change is compiled into the prebuilt ESP-IDF that
arduino-esp32 ships, and cannot be reached from an Arduino build at all. This document
records what was measured, what a native build would buy, what it would cost, and a
staged way to find out cheaply — so the question does not have to be re-researched from
scratch next time it comes up.

The owner chose Arduino originally for ease of development while compiling by hand.
Nothing here argues that was wrong; it argues only that the USB work has now found the
edge of it.

---

## 1. Why this came up

Three separate USB findings all terminate in the same place:

* **FIFO partitioning — now quantified, and the leading suspect.** Arduino's prebuilt
  IDF sets `CONFIG_USB_HOST_HW_BUFFER_BIAS_PERIODIC_OUT=y`. Read from `hcd_dwc.c`, on a
  full-speed PHY (`otg_dfifo_depth` = 256, 200 usable 4-byte lines) that yields:

  | bias | RX | **non-periodic TX** | periodic TX |
  | --- | --- | --- | --- |
  | `PERIODIC_OUT` (Arduino) | 136 B | **64 B** | 600 B |
  | `BALANCED` (IDF default) | 416 B | **256 B** | 128 B |

  The non-periodic TX FIFO carries **control transfers and bulk OUT** — enumeration
  itself, and every CDC write. Arduino leaves it at **64 bytes: exactly one full-speed
  packet.** This matches the bench exactly: one CDC device works, a second fails to
  enumerate, a hub (control traffic to the hub *and* each downstream port) fails, and a
  single adapter plugged direct is always fine. Arduino presumably chose this for USB
  audio and MIDI, which is the opposite of what CAT over CDC needs.

  **We cannot change it**: the split is computed inside the prebuilt library at
  `usb_host_install()` time from a compile-time Kconfig, and our `usb_host_config_t` has
  no `fifo_settings_custom` field (newer than the bundled component). No runtime
  override exists, in EspUsbHost or anywhere else.
* **Control transfer size.** `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256` is likewise
  fixed. It was ruled out as the IC-705's problem (its descriptor is 141 bytes), but it
  is not adjustable if a future device needs more.
* **The USB host stack version.** Mini-FT8 vendors `espressif__usb` as a managed
  component rather than using the IDF-bundled one. That is where the newer API lives.

None of these is reachable by patching EspUsbHost, which is what we have been doing all
cycle. They are below it.

**Not a reason to migrate:** the hub failures themselves. External hub support IS
compiled into Arduino's library (`ext_hub.c.obj` and `ext_port.c.obj` are present, 37
symbols), and the root port is powered by default (`root_port_unpowered` defaults false,
matching the owner's measured VBUS). Whatever is wrong with hubs, "the feature is
missing from the build" is not it.

---

## 2. Measured environment facts

Checked in the working container, not assumed:

| Item | State |
| --- | --- |
| `xtensa-esp32s3-elf-gcc` / `g++` | **present**, shipped with the Arduino core |
| `cmake`, `ninja` | **missing**, but pip wheels download successfully |
| `git`, `python3` | present |
| ESP-IDF | **not installed** |
| `github.com`, `dl.espressif.com` | reachable (HTTP 200) |
| Root filesystem | 252 G total, **1.8 G free (91 % used)** |
| `/root/.arduino15` | 6.2 G |
| arduino-esp32 3.2.1 | ships `CMakeLists.txt`, `Kconfig.projbuild`, `idf_component.yml` |
| Bundled IDF | `idf-release_v5.4-858a988d-v1` |

**The Arduino core is designed to be consumed as an IDF component.** That is the
realistic path: an IDF project with arduino-esp32 as a component, which keeps every
Arduino library CardSat depends on (M5Unified, M5GFX, RadioLib, Sgp4, ArduinoJson,
TinyGPSPlus, ESP_SSLClient, the vendored EspUsbHost) working unchanged.

### Disk, which looked like the blocker and is not

1.8 G free is not enough for an IDF install. However, 2.3 G of what is already on disk
is unused by this project:

| Unused | Size |
| --- | --- |
| `esp-rv32` (RISC-V toolchain — CardSat is Xtensa only) | 2.1 G |
| `xtensa-esp-elf-gdb` | 91 M |
| `riscv32-esp-elf-gdb` | 89 M |
| `openocd-esp32` | 11 M |

Pruning those yields roughly **4.1 G**, which is workable for a shallow IDF v5.4 clone
plus a Python environment. Note the Xtensa GDB is only unused because nothing here
debugs on hardware; if that ever changes, the figure changes with it.

---

## 3. The recurring cost, which is the real argument against

**The container filesystem resets between sessions.** An IDF install is therefore not a
one-time setup — it is **10–20 minutes of re-installation at the start of every future
session**, before a single build runs, on top of build times already around five
minutes each.

That is a permanent tax on iteration speed, and it is the factor to weigh most heavily.
The USB debugging in this cycle needed many short build/inspect cycles; a native build
would have made each of those slower, not faster.

Mitigations worth testing if this proceeds: a shallow single-branch clone
(`--depth 1 -b v5.4`), installing only the `esp32s3` target's tools
(`install.sh esp32s3`), and reusing the Xtensa toolchain already on disk instead of
letting IDF fetch its own.

---

## 4. What it would buy

* Full `sdkconfig` control — the FIFO bias, control-transfer size, hub options, and
  anything else currently frozen in the prebuilt library.
* The option to vendor the standalone `espressif/usb_host` component, which is where
  `fifo_settings_custom` and later fixes live.
* Partition, PSRAM and log-level control without the `build_opt.h` and custom-partition
  workarounds now in use.
* IDF's own logging reaching us properly, rather than the `CORE_DEBUG_LEVEL` guard that
  compiles most of the USB library's diagnostics out — which cost real time this cycle.

---

## 5. What it would cost beyond setup

* **The 18 static gates** assume the Arduino layout. `check_parity`, `check_body_parity`
  and `check_ino_dupes` exist to keep `src/*.{h,cpp}` and the monolithic `CardSat.ino`
  byte-identical; a native build removes the reason for the `.ino` but not the reason
  for the gates, so this needs deciding rather than dropping.
* **The dual-representation discipline** would need re-thinking. If the `.ino` stops
  being the shipped artifact, keeping it costs effort with no consumer; removing it
  changes how every future edit is applied.
* **Release artifacts must stay Launcher-compatible.** Most users install
  `CardSat-app.bin` through Launcher, which writes its own partition table sized to the
  binary. Whatever the build system, that file must keep the same shape.
* **The build scripts** (`run_build.sh`, the diag build, `check_app_fits`) all assume
  `arduino-cli` and would need rewriting.

---

## 6. Staged plan, with a go/no-go at each step

Deliberately front-loads the cheap failure modes.

1. **Throwaway proof, no CardSat involvement.** Prune the unused toolchains, shallow-clone
   IDF v5.4, install `esp32s3` tools only, and build the stock `hello_world` example with
   arduino-esp32 as a component.
   *Go/no-go: does it fit on disk, and how long does a cold session setup actually take?*
   This answers the two biggest unknowns without touching the project.
2. **Measure the tax honestly.** Time a full cold setup and one incremental build. If a
   session's fixed overhead exceeds roughly 20 minutes, the iteration cost probably
   outweighs the `sdkconfig` access for day-to-day work, and the right answer may be to
   use a native build only for targeted USB experiments rather than as the main build.
3. **Confirm the computed figure before migrating anything.** Build a minimal IDF
   project with `BALANCED` (or an explicit custom split) and try **two** devices behind
   a powered hub — two Prolific adapters are enough, no radios needed. This is now a
   check on an arithmetic prediction (64 B → 256 B of non-periodic TX), not a hunch. If
   two devices enumerate, the cause is settled and migration is justified. If they do
   not, the FIFO theory dies for the cost of one experiment and the migration is
   unnecessary — which is the entire point of doing this step first.
4. **Only then** consider porting CardSat, and only if step 3 succeeded.

---

## 7. What does not change

**I cannot flash or run anything, under either build system.** A native build would let
me compile, inspect symbols and sizes, and verify `sdkconfig` values — but every
behavioural claim would still need the owner's bench, exactly as now. Nothing in this
document reduces the amount of hardware verification required; it only widens what can
be changed before that verification happens.

---

## 8. Alternatives considered

* **Stay on Arduino, keep patching EspUsbHost.** Free, and it has carried the project a
  long way — but the three findings in §1 are below that library and cannot be reached
  from it.
* **`esp32-arduino-lib-builder`.** Rebuilds the prebuilt Arduino libraries with a custom
  `sdkconfig`, keeping the Arduino workflow. Conceptually the smallest change that
  unlocks the FIFO bias. In practice it is a heavier build than IDF itself and has the
  same disk and per-session costs, so it is not obviously cheaper — but it deserves a
  look before step 4, because it would preserve the entire existing gate and release
  flow.
* **Do nothing, and document the limit.** If step 3 shows the FIFO bias is not the
  cause, this is the correct outcome: record that hub support on this hardware is
  unreliable for reasons outside the firmware, and stop spending on it.
