# CardSat — Build & Flash Guide

How to get CardSat onto a Cardputer ADV: prebuilt binaries (no toolchain),
upgrading an existing install, and building from source. For a fully detailed
from-scratch Arduino setup, see **[guides/ARDUINO_SETUP.md](guides/ARDUINO_SETUP.md)**.

### Prebuilt binaries (no toolchain)

Two binaries ship with each release:

| File | Use it with | Notes |
|---|---|---|
| `CardSat.bin` | **[Launcher](https://github.com/bmorcelli/Launcher)** (bmorcelli) | App-only image; **only** works through Launcher, which writes the bootloader/partition table itself. Cannot be flashed standalone. |
| `CardSat_Merged.bin` | **M5Burner**, or **direct flash** (esptool / web flasher) | Complete standalone image (bootloader + partition table + app + empty LittleFS) at `0x0`. |

**Launcher:** copy `CardSat.bin` to Launcher's bin folder on the microSD (or use
its WebUI/OTA), then start Launcher and pick **CardSat** — it builds the right
partition layout and installs the app. The file has no standalone bootloader, so it
runs only through Launcher.

**M5Burner / direct flash:** use `CardSat_Merged.bin`. In M5Burner, add it as a
custom firmware and burn. To flash directly:

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0 CardSat_Merged.bin
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
| **Launcher** (recommended for upgrades) | `CardSat.bin` | **Preserved.** Launcher replaces only the app partition, so settings/notes/favorites and cached elements in LittleFS survive the update. |
| **M5Burner / direct flash at `0x0`** | `CardSat_Merged.bin` | **Erased.** The merged image includes a fresh empty LittleFS, so a full flash wipes internally-stored data — you'll re-enter station settings and run **Update** again. |

So for a routine version bump, the easiest path is either: keep an **SD card** in (your
config persists no matter how you flash), or use **Launcher** with `CardSat.bin` (which
preserves the internal LittleFS data too). A full `CardSat_Merged.bin` flash only erases
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

Install **M5Cardputer**, **ArduinoJson** (v7), **TinyGPSPlus**, and **RadioLib**
(by Jan Gromes) from Library Manager, and the Hopperpop **Sgp4** library via *Add
.ZIP Library* (<https://github.com/Hopperpop/Sgp4-Library>). RadioLib is required:
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
too big"*; the 3 MB "Huge APP" layout fits with room to spare and provides the
1 MB SPIFFS region that LittleFS uses for cached data.

### Build from source — PlatformIO

```
pio run                 # build
pio run -t upload       # flash
pio device monitor      # 115200 baud log
```

The `cardputer_adv` env (`board = m5stack-stamps3`) pins M5Cardputer,
ArduinoJson, TinyGPSPlus, RadioLib, and the Hopperpop SGP4 library. If the
keyboard or display misbehave on the ADV (a very recent variant), switch to the
git `master` of M5Cardputer/M5Unified in `lib_deps`.
