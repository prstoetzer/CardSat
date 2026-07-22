# CardSat — Build & Flash Guide

How to get CardSat onto a Cardputer ADV: prebuilt binaries (no toolchain),
upgrading an existing install, and building from source. For a fully detailed
from-scratch Arduino setup, see **[guides/ARDUINO_SETUP.md](guides/ARDUINO_SETUP.md)**.

### Prebuilt binaries (no toolchain)

Two binaries ship with each release:

| File | Use it with | Notes |
|---|---|---|
| `CardSat-app.bin` | **[Launcher](https://github.com/bmorcelli/Launcher)** (bmorcelli) | App-only image; **only** works through Launcher, which writes the bootloader/partition table itself. Cannot be flashed standalone. |
| `CardSat-merged.bin` | **M5Burner**, or **direct flash** (esptool / web flasher) | Complete standalone image (bootloader + partition table + app + empty LittleFS) at `0x0`. |

**Launcher:** copy `CardSat-app.bin` to Launcher's bin folder on the microSD (or use
its WebUI/OTA), then start Launcher and pick **CardSat** — it builds the right
partition layout and installs the app. The file has no standalone bootloader, so it
runs only through Launcher.

Both binaries are built with **USB CAT included** — *USB serial* is in the CAT
type row out of the box (it is on by default in source too; see below).

**M5Burner / direct flash:** use `CardSat-merged.bin`. In M5Burner, add it as a
custom firmware and burn. To flash directly:

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0 CardSat-merged.bin
```

or the web flasher at <https://espressif.github.io/esptool-js/> (chip **ESP32-S3**,
file at **`0x0`**). The filesystem starts empty — run **Update** once on first boot
to fetch GP elements.

### Upgrading an existing install

Upgrading to a new CardSat version is the same as installing, with one thing to know:
**how each method treats your saved data** (settings, calibration, per-satellite notes,
CTCSS overrides, favorites, and the cached GP/AMSAT data).

**Where your data lives — and why it usually survives.** CardSat prefers a **microSD
card** for all storage: if a FAT32 card is inserted, everything is saved to the card
(under `/CardSat`), and the internal flash is used only as a fallback when no card is
present. Flashing firmware only touches the device's internal flash, **never the SD
card** — so **if you run CardSat with an SD card in, your data persists across any
upgrade, including a full merged flash.** The table below applies to the **no-SD-card**
case, where data lives in the internal LittleFS partition.

| Method (no SD card) | File | Internal saved data |
|---|---|---|
| **Launcher** (recommended for upgrades) | `CardSat-app.bin` | **Preserved.** Launcher replaces only the app partition, so settings/notes/favorites and cached elements in LittleFS survive the update. |
| **M5Burner / direct flash at `0x0`** | `CardSat-merged.bin` | **Erased.** The merged image includes a fresh empty LittleFS, so a full flash wipes internally-stored data — you'll re-enter station settings and run **Update** again. |

So for a routine version bump, the easiest path is either: keep an **SD card** in (your
config persists no matter how you flash), or use **Launcher** with `CardSat-app.bin` (which
preserves the internal LittleFS data too). A full `CardSat-merged.bin` flash only erases
data when it was stored **internally** (no SD card).

If you do end up on a clean slate (merged flash with no SD card), reconfigure: set your
**station location/grid**, re-enter **radio/CAT and rotator** settings, and run
**Update** once to refetch GP elements.

After any upgrade, confirm the new version on the device: it's shown on the **About**
screen (Main menu → About). To verify nothing carried stale state, a power cycle after
flashing is good practice.

### Build from source — Arduino IDE (single-file `CardSat.ino`)

> 📖 **New to this?** See **[ARDUINO_SETUP.md](guides/ARDUINO_SETUP.md)** for complete
> step-by-step instructions on setting up a fresh Arduino IDE environment from
> scratch — installing the IDE, the ESP32 core, every library at a known-good
> version, the board settings, and flashing. The summary below is the short version.

Install **M5Cardputer**, **ArduinoJson** (v7), **TinyGPSPlus**, **RadioLib**
(by Jan Gromes), and **ESP_SSLClient** (by Mobizt) from Library Manager, and the
Hopperpop **Sgp4** library via *Add .ZIP Library*
(<https://github.com/Hopperpop/Sgp4-Library>). **Or**, more easily, install **SparkFun SGP4
Arduino Library** from the Library Manager — it is a straight clone of Hopperpop's (its
`library.properties` still reads `author=Hopperpop`), cloned only so it could be listed in the
Library Manager. Same header, same API; either works. **ESP_SSLClient is required** — all
HTTPS (GP/space-weather/hams.at downloads, LoTW and Cloudlog uploads) runs on its
BearSSL stack, and the build will not compile without it. RadioLib is also required:
LoRa is built into the standard binaries (`CARDSAT_HAS_LORA` defaults to `1`). Then
under **Tools**:

| Setting | Value |
|---|---|
| Board | **ESP32S3 Dev Module** (full Tools menu) or **M5StampS3** |
| Flash Size | **8MB (64Mb)** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** — **required** |
| PSRAM | **Disabled** |
| USB CDC On Boot | **Enabled** |

The default ~1.25 MB app partition is too small and the build fails with *"Sketch
too big"*; the 3 MB "Huge APP" layout fits (the app is ~92.9% of it as of 0.9.64,
so headroom is limited but sufficient — see the size note below) and provides the
1 MB SPIFFS region that LittleFS uses for cached data.

### USB CAT (`CAT_USB`) — on by default since 0.9.59

USB CAT drives a radio through a **USB↔serial adapter** (FTDI / CP210x / CH34x) on the
USB-C port instead of the G1/G2 UART and its level shifter. It shipped opt-in in
0.9.58, was **bench-proven on an IC-821 + FTDI adapter** (engage, disengage,
re-engage, and Doppler tracking over many cycles), and is **part of the default build
since 0.9.59**. That makes **EspUsbHost** (TANAKA Masayuki) a required library —
**v2.4.1 or later**. v0.9.64's USB teardown depends on 2.4.1's `end()` completing the
full drain / deregister / free-all / uninstall handshake so a disengaged host actually
releases its memory; 2.3.x compiles clean (the `peripheral_map` fix landed upstream in
v2.3.1) but keeps the host resident on `end()`, which would reproduce the retained-RAM
and re-engage failures this release fixes. Verified end-to-end: CardSat 0.9.64 compiles
and runs its USB teardown against v2.4.1 on arduino-esp32 3.2.1 (~92% of the 3 MB app
partition), and the v2.3.0 → v2.4.1 diff was audited — the entire API surface CardSat
uses is unchanged (classes, config structs, enum values, task names), and the
behavioral change that matters is `end()` now actually releasing the host, plus
improvements on CardSat's paths: transfer-error recovery on unplug is now deferred
to the client task so a disconnected adapter is released instead of re-submitted
to, and serial-endpoint binding is pinned to the recorded CDC-data / vendor
interface number (better behavior on composite devices like an IC-9700's own USB
port). To build **without** USB CAT (no EspUsbHost needed, no *USB serial* CAT
type, otherwise identical firmware), see *Building without USB CAT* at the end of
this section.

> ✅ **Fixed upstream in v2.3.1** — the assignment now sits inside
> `#if defined(CONFIG_IDF_TARGET_ESP32P4)`, a *target* guard (even better than the
> version guard this note originally asked for). **Everything below applies only to
> stale copies ≤ 2.3.0**, kept for anyone with an old Library Manager cache.
>
> ⚠️ **EspUsbHost ≤ 2.3.0 does not compile against arduino-esp32 3.2.1 without a one-line edit.**
> ```
> EspUsbHost.cpp:4796:14: error: 'struct usb_host_config_t' has no member named 'peripheral_map'
> ```
> `peripheral_map` selects among *multiple* USB peripherals — an **ESP32-P4** feature — and is not
> in the IDF v5.4 snapshot arduino-esp32 3.2.1 ships. EspUsbHost sets it unconditionally, with no
> `ESP_IDF_VERSION` guard. **Every 2.x release through 2.3.0 has this** (checked 2.0.0 → 2.3.0; fixed in 2.3.1), and 1.0.1 —
> the last version without it — is the original HID-only library with no USB-serial support at
> all. So downgrading does not help.
>
> **The fix is one line, and it is a no-op on this hardware.** In
> `Documents/Arduino/libraries/EspUsbHost/src/EspUsbHost.cpp`, comment out line 4796:
> ```cpp
> void EspUsbHost::taskLoop() {
>   usb_host_config_t hostConfig = {};
>   hostConfig.skip_phy_setup = false;
>   hostConfig.intr_flags = ESP_INTR_FLAG_LOWMED;
>   // hostConfig.peripheral_map = hostPeripheralMap(config_.port);   // ESP32-P4 only
> ```
> Why this is safe: `hostPeripheralMap()` is `#if defined(CONFIG_IDF_TARGET_ESP32P4)` — on an
> ESP32-S3 it **always returns 0**, and IDF documents `peripheral_map = 0` as "use the default
> peripheral". `hostConfig` is already zero-initialized by `= {}`. The line assigns zero to a field
> that would be zero anyway; on an S3 it does nothing.
>
> This is a **local patch to a third-party library** — it will be overwritten by a Library
> Manager update, which since v2.3.1 is exactly the fix: updating **replaces the patch with
> the upstream guard**, and a previously-patched copy updates cleanly.
> Without it the default build fails; either apply the patch or build with
> `CARDSAT_HAS_USBCAT` set to `0` (see *Building without USB CAT* below).

**Arduino IDE** — one step: install **EspUsbHost** (TANAKA Masayuki, **v2.4.1 or
later** — required for v0.9.64's USB teardown) from **Library Manager**. No
patch. `CARDSAT_HAS_USBCAT` is already `1` in `CardSat.ino`, and the repo already
ships the **`build_opt.h`** the `.ino` build needs (it carries
`-mtext-section-literals` and `-DESP_USB_HOST_MAX_DEVICES=4`).

> **Why `-mtext-section-literals` is needed.** Without it the link fails:
> ```
> dangerous relocation: l32r: literal target out of range (try using text-section-literals)
>   in function `EspUsbHostCdcSerial::~EspUsbHostCdcSerial()'
> ```
> Xtensa's `l32r` instruction loads constants from a literal pool via a **signed 16-bit offset** —
> it can only reach about 256 KB backwards. `CardSat.ino` is a **~2.1 MB single translation
> unit**, and adding `EspUsbHostCdcSerial` (a header-defined class with six virtual overrides and
> an implicit virtual destructor, so its vtable and `D0Ev` are emitted into whichever TU
> instantiates it — here, the `.ino`) pushes a literal reference past that reach.
> `-mtext-section-literals` places literal pools next to the code that uses them instead of in one
> distant pool. It is the fix the linker itself suggests.
>
> This is **not** an EspUsbHost problem and not really a USB CAT problem — it is the monolithic
> `.ino` approaching an Xtensa architectural limit. USB CAT was simply the addition that crossed
> it. **PlatformIO builds are unaffected** (`src/*.cpp` are separate translation units, each far
> below the limit). Worth knowing for the future: the single-file `.ino` has finite headroom, and
> this is the first thing to hit it.

**PlatformIO** — nothing to enable: `platformio.ini` ships with
`tanakamasayuki/EspUsbHost @ ^2.3.1` under `lib_deps` and
`-DESP_USB_HOST_MAX_DEVICES=4` active (`CARDSAT_HAS_USBCAT` defaults to `1` in
`config.h`, so no flag is needed). The PlatformIO registry serves ≥ 2.3.1, so the
downloaded copy compiles clean with no patch.

**Building without USB CAT** — drops the EspUsbHost dependency and the *USB serial*
CAT type; everything else is byte-for-byte identical. Arduino IDE: change
`#define CARDSAT_HAS_USBCAT 1` to `0` in `CardSat.ino`. PlatformIO: add
`-DCARDSAT_HAS_USBCAT=0` under `build_flags` and comment out the EspUsbHost
`lib_dep`.

> **You do not need to set `ESP_USB_HOST_MAX_DEVICES` — and do not pin it per-file.** An
> earlier revision had `usbserial.cpp` define it to `1` before including `EspUsbHost.h`.
> That froze the firmware the moment USB CAT was enabled: the slot array is a **member of
> the `EspUsbHost` object**, so the library's own translation unit (compiled with the
> default of 8) and `usbserial.cpp` (compiled with 1) disagreed about the object's size and
> layout — a one-definition-rule violation, and the first out-of-line library call wrote
> past the end of the object. The safe form is a **global** define that reaches the library's
> translation unit too — and the repo now ships one: `build_opt.h` in the sketch folder carries
> `-DESP_USB_HOST_MAX_DEVICES=4` (root hub + adapter, headroom for the USB rotator that shipped in 0.9.58;
> ~1.7 KB per slot removed from the transient host object). The Arduino IDE passes
> `build_opt.h` to **every** c/cpp compile — sketch, core and libraries alike (arduino-esp32
> 3.2.1 `platform.txt`, `recipe.c.o.pattern` / `recipe.cpp.o.pattern`). One caveat: the IDE's
> build cache does not watch this file — after adding or editing `build_opt.h`, force a full
> rebuild (touch any source file, or flip a Tools option back and forth). PlatformIO does not
> read `build_opt.h`; put the same `-DESP_USB_HOST_MAX_DEVICES=4` under `build_flags` there.
>
## Console capture to file

**Settings → Station / logging → `Console to file`** (default **off**) mirrors the entire serial console to
`/CardSat/Logs/console.log`, retrievable through the web files portal.

Why it exists: engaging USB CAT or a USB rotator claims the S3's one USB PHY, which closes the
serial console **for the rest of the session** (the host is resident until reboot). Every
`Serial.print` after that goes nowhere — exactly when a field problem needs a trace.

How it works: arduino-esp32 defines `Serial` as a macro aliasing the real device
(`#define Serial HWCDCSerial`). CardSat redefines that macro to a `Print`-derived tee which
forwards to the hardware **and** captures the text. All ~181 `Serial.print` call sites are
covered with none of them edited.

**Cost, measured and modeled:**

| | |
|---|---|
| RAM | ~512 B buffer. Negligible. |
| Time (capture ON, tracking) | ~0.5% of `loopTask` |
| Time (capture ON, unbuffered — *not* what ships) | ~5% |
| Time (capture OFF) | a flag test |
| Flash wear | ~2.2 years of *continuous* tracking on a 320 KB partition; decades at realistic duty. SD: non-issue. |

Writes are **buffered** (512 B, flushed on fill, after 1 s idle, before every `ESP.restart()`
and before a USB engage). That is ~10× fewer flash operations than per-line: CardSat ships
`CIV_DEBUG=1`, so Doppler tracking emits ~8 console lines/sec, and unbuffered that would be
~48 ms/s of blocking I/O on the task that also runs Doppler, the UI, WiFi and LoRa.

The honest tradeoff: **a hard crash loses whatever is still buffered** — often the tail you
wanted. Mitigated by the flush points above, and by the fact that the USB engage trace does
*not* go through here: `Logstore` writes it unbuffered, so it survives a freeze on its own.

Size is capped: ~32 KB on flash (2 × 16 KB with rotation), ~512 KB on SD. Delete
`/CardSat/Logs` to reclaim it all.

## Turn OFF "Optimize for Debugging"

**Check this before every release build.** Arduino IDE → *Sketch* → **Optimize for Debugging**
must be **unchecked**.

When it is checked, arduino-cli swaps `compiler.optimization_flags` from the platform's
release value (`-Os`) to the debug one (`-Og -g3`) for **every** translation unit — sketch,
core and libraries alike (arduino-cli `builder.go`: `if optimizeForDebug { ... .debug }`).
You can confirm which one you got by reading any compile line in the verbose build log:

    xtensa-esp32s3-elf-g++ -MMD -c @.../cpp_flags -w -Og -g3 ...    <- DEBUG build
    xtensa-esp32s3-elf-g++ -MMD -c @.../cpp_flags -w -Os ...        <- release build

It is not the same as *Core Debug Level*. `DebugLevel=none` in the FQBN only sets
`CORE_DEBUG_LEVEL=0` (log verbosity); it does not touch the optimizer.

`-Og` deliberately declines to inline small functions so that every one stays separately
steppable in a debugger. CardSat's hot paths are exactly the shape that costs: struct scans
over the satellite DB with small accessors (`visible()`, `score()`, per-record formatting).
Measured on a host proxy of that shape, `-Os` is **~19% faster** than `-Og` — and *smaller*.
There is no tradeoff to weigh here: `-Os` wins on both axes. (Host x86 proxy, so treat the
ratio as indicative, not a promise about Xtensa.)

`-g3` costs nothing on-device: DWARF lands in the `.elf`, not the `.bin` (2.9 MB `.bin`
against a 40 MB `.elf` on the 0.9.58 build). Keep the `.elf` — `tools/` backtrace decoding
needs it.

### Do NOT use `-O2`

`-O2` was ~39% larger than `-Os` on the same sample. CardSat's `.bin` is already
**2,921,408 of the 3,145,728-byte `huge_app` app partition — 92.9%**. Even a 10% text
growth would not fit. `-Os` is the only correct setting for this project.

> `build_opt.h` also carries **`-mtext-section-literals`**, and this one is not optional: as
> `CardSat.ino` grew, the assembler could no longer place each function's literal pool within the
> Xtensa `L32R` displacement of the code referencing it, and the build fails outright. The flag
> emits literals into `.text` next to their use instead of in per-section pools. Symptom without
> it: assembler errors about literal placement / out-of-range `L32R` on a full rebuild. PlatformIO
> users put it in `build_flags` alongside the define.
> The full story is the comment block at the top of `src/usbserial.cpp`.

Then select **Settings → Radio / CAT → CAT type → USB serial**.

**Note:** engaging USB CAT takes over the USB port, so the **serial console disappears**
for as long as the radio is engaged. That is inherent — the ESP32-S3 has **one internal
USB PHY**, shared by the host stack and the CDC console. Whether the console re-attaches
on disengage without a reboot is an open bench question (the PHY mux may stay with the
host stack until power-cycle — cosmetic if so). The on-device performance monitor
(**About → `m`**) exists partly to cover this.

### Build from source — PlatformIO

```
pio run                 # build
pio run -t upload       # flash
pio device monitor      # 115200 baud log
```

The `cardputer_adv` env (`board = m5stack-stamps3`) pins M5Cardputer,
ArduinoJson, TinyGPSPlus, RadioLib, ESP_SSLClient, and the Hopperpop SGP4 library. If the
keyboard or display misbehave on the ADV (a very recent variant), switch to the
git `master` of M5Cardputer/M5Unified in `lib_deps`.
