# CardSat — precompiled firmware (M5Cardputer ADV)

Prebuilt binaries for the **M5Stack Cardputer ADV** (ESP32-S3FN8, 8 MB flash, no PSRAM),
firmware **v0.9.72**. Flash these if you want to run this exact build without
compiling. Source is the rest of this repo; `CardSat.ino` is the monolithic sketch.

Built with: arduino-cli + `esp32:esp32@3.2.1`, FQBN
`esp32:esp32:esp32s3:PartitionScheme=custom,CDCOnBoot=cdc` with the repo's
`partitions.csv` (4 MB app / 1.5 MB LittleFS), M5Cardputer library.
No `build.extra_flags` (that would break the HWCDC serial console). Flash usage at build time: 3,067,552 bytes of a **4 MB** app partition (73.1%);
static RAM 162,376 bytes (49%).

The flash percentage is only meaningful beside the library versions it was built
with: M5Cardputer 1.1.1, M5GFX 0.2.26, M5Unified 0.2.19, ESP_SSLClient 3.1.3,
ArduinoJson 7.4.2, TinyGPSPlus 1.0.3, RadioLib 7.7.1, Sgp4 1.0.3, EspUsbHost **2.7.0, PATCHED** -- see `third_party/EspUsbHost/PATCHES.md`.

> **Building this yourself? Two libraries must be prepared, not one.**
>
> 1. **EspUsbHost must be patched.** A stock copy compiles and appears to work, then
>    strands the USB stack the first time a radio stops answering. The patched copy is
>    vendored at `third_party/EspUsbHost/`; copy it over your installed library.
> 2. **The ESP-IDF USB host stack must be vendored.** Run
>    `./tools/vendor_usb_host.sh`. Without it the build silently links Arduino's
>    prebuilt `libusb.a`, and the reset timings, enumeration retry and USB diagnostics
>    in this release are all absent — with no error to tell you. Read
>    `docs/design/USB_HOST_VENDORING.md` first; the version pin must match your core.
>
> After either step, **delete the sketch build cache**. `build_opt.h` is not a
> dependency of any object file, so a changed flag will otherwise produce a
> byte-identical binary that looks exactly like the change having no effect.
>
> Verify the override took, in the map file:
> `grep -c 'libraries/UsbHostSrc' build/*.map` (hundreds) and
> `grep -c 'libusb\.a(' build/*.map` (zero).

Checksums (MD5):
- `CardSat-merged.bin`  12a6b05ebde1a013ba5efff436954ff4
- `CardSat-app.bin`     1db3bc9a69d93827a00293ef63594fe1
- `CardSat-bootloader.bin`  c7f9b41acfaba802c7e74ae639a9a162
- `CardSat-partitions.bin`  a4c137645ca493e8abffae39e6fb5a03

> **v0.9.71** is a **tools release**: Tiny BASIC gains string variables, text
> functions, named arrays and a pre-run input form; the calculators gain satellite and
> antenna functions plus an on-device list (`Fn`+`f`) and a printable card; the MUF tool
> can target a DXCC entity. **Dual CAT works** (tested: TH-D75 on USB + IC-705 over
> Wi-Fi). **Multiple USB devices do not** — see the release notes.
>
> **PARTITION CHANGE.** The app partition is now **4 MB** and LittleFS **1.5 MB**, using
> flash that was previously unused. Upgrading through **Launcher is unaffected**: the app
> still fits an existing 3 MB layout, so `CardSat-app.bin` installs exactly as before and
> your settings survive. The larger filesystem only appears after a full
> `CardSat-merged.bin` flash, which erases internal data — back it up first if you keep
> notes or logs there.
>
> See `docs/releases/RELEASE_NOTES_0.9.71.md`.

## Easiest: one file at 0x0 (esptool)

`CardSat-merged.bin` already contains the bootloader, partition table, boot_app0, and
the app, so it flashes as a single image at offset **0x0**:

```
esptool.py --chip esp32s3 --port <PORT> --baud 921600 write_flash 0x0 CardSat-merged.bin
```

Replace `<PORT>` with your serial port (e.g. `/dev/ttyACM0`, `/dev/cu.usbmodem*`, or
`COM5`). A power cycle after flashing is good practice to clear any stale state.

## Or: the four pieces at their offsets

Identical result, flashing the individual images. These are the **huge_app** partition
scheme offsets on the ESP32-S3 (bootloader at **0x0**, not 0x1000):

```
esptool.py --chip esp32s3 --port <PORT> --baud 921600 write_flash \
  0x0     CardSat-bootloader.bin \
  0x8000  CardSat-partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 CardSat-app.bin
```

## Or: M5Burner

Pick the Cardputer / ESP32-S3 device, choose "burn" from a local file, select
`CardSat-merged.bin`, and set the flash address to **0x0**.

## Notes

- **CDC on boot** is enabled (`CDCOnBoot=cdc`): the USB-C port is the serial console
  and the USB-CAT / USB-host transport, matching how this firmware expects it.
- The companion (Stick) firmware is separate — see
  `../companion/CardSatDualRig/firmware/`.
- Settings, GP/TLE data, logs and calibration live in the LittleFS partition and
  survive a reflash of the app; use **Settings → Reset all data** for a clean slate.
