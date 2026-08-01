# CardSat — precompiled firmware (M5Cardputer ADV)

Prebuilt binaries for the **M5Stack Cardputer ADV** (ESP32-S3FN8, 8 MB flash, no PSRAM),
firmware **v0.9.70**. Flash these if you want to run this exact build without
compiling. Source is the rest of this repo; `CardSat.ino` is the monolithic sketch.

Built with: arduino-cli + `esp32:esp32@3.2.1`, FQBN
`esp32:esp32:esp32s3:PartitionScheme=huge_app,CDCOnBoot=cdc`, M5Cardputer library.
No `build.extra_flags` (that would break the HWCDC serial console). Flash usage at
build time: 3,045,402 bytes (96.8%); static RAM 162,112 bytes (49%).

The flash percentage is only meaningful beside the library versions it was built
with: M5Cardputer 1.1.1, M5GFX 0.2.26, M5Unified 0.2.19, ESP_SSLClient 3.1.3,
ArduinoJson 7.4.2, TinyGPSPlus 1.0.3, RadioLib 7.7.1, Sgp4 1.0.3, EspUsbHost **2.7.0, PATCHED** -- see `third_party/EspUsbHost/PATCHES.md`.

> **Building this yourself? The USB host library must be patched.** A stock EspUsbHost
> compiles and appears to work, then strands the USB stack the first time a radio stops
> answering ("USB busy" on every later engage, until you reboot). The patched copy is
> vendored at `third_party/EspUsbHost/`; copy it over your installed library. Three of
> the patches are drafted as upstream bug reports, so a future release may drop the
> vendored copy.

Checksums (MD5):
- `CardSat-merged.bin`  1e19b517a7254c85dda7d7626dc05fec
- `CardSat-app.bin`     ab151902df865a7c8c77097727a76eb7
- `CardSat-bootloader.bin`  c7f9b41acfaba802c7e74ae639a9a162
- `CardSat-partitions.bin`  70007348574201233bc0cb17155e9d12

> **v0.9.70** is a **USB release**. USB CAT can now be engaged and disengaged as often
> as you like — switch satellites, switch the radio off and on — without rebooting.
> Four defects were stacked here: an undrained CDC write that stranded the USB host
> stack until reboot (fixed in the vendored library, reported upstream), a port that
> was never closed because DTR was never de-asserted, and a radio whose CAT firmware
> never returns after re-enumeration — so the host now **stays resident between
> engagements**, and **`Fn`+`u`** releases it explicitly (which is when the serial
> console returns). The **Kenwood TH-D74/D75** CAT path was rebuilt from measurement on
> real hardware. The charge indicator is **removed** (this board cannot report charge
> state; the old one said "on battery" while plugged in). See
> `docs/releases/RELEASE_NOTES_0.9.70.md`.

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
