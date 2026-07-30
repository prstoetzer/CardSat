# CardSat — precompiled firmware (M5Cardputer ADV)

Prebuilt binaries for the **M5Stack Cardputer ADV** (ESP32-S3FN8, 8 MB flash, no PSRAM),
firmware **v0.9.69**. Flash these if you just want to run this build without
compiling. Source is the rest of this repo; `CardSat.ino` is the monolithic sketch.

Built with: arduino-cli + `esp32:esp32@3.2.1`, FQBN
`esp32:esp32:esp32s3:PartitionScheme=huge_app,CDCOnBoot=cdc`, M5Cardputer library.
No `build.extra_flags` (that would break the HWCDC serial console). Flash usage at
build time: 3,031,306 bytes (96.4%); static RAM 162,080 bytes (49%).

The flash percentage is only meaningful beside the library versions it was built
with: M5Cardputer 1.1.1, M5GFX 0.2.26, M5Unified 0.2.19, ESP_SSLClient 3.1.3,
ArduinoJson 7.4.2, TinyGPSPlus 1.0.3, RadioLib 7.7.1, Sgp4 1.0.3, EspUsbHost 2.5.2.

Checksums (MD5):
- `CardSat-merged.bin`  004056d04bb65d477fec09e05a5f9945
- `CardSat-app.bin`     fb8b066f355c55d296d6fec8cb1e832d
- `CardSat-bootloader.bin`  c7f9b41acfaba802c7e74ae639a9a162
- `CardSat-partitions.bin`  70007348574201233bc0cb17155e9d12

> **v0.9.69** is a correctness release for the v0.9.68 dual-radio feature, most of it
> found by an external audit and bench review. Two release-blocking bugs are fixed: the
> new **Dual** CAT type was silently discarded on every reboot (a saved dual config came
> back as wired CI-V, which could also seize the Grove UART from a GPS or rotator), and
> changing settings while dual-USB was engaged stranded the second CAT port holding the
> USB host, so the serial console never returned. **Single-wire CI-V now works on a
> dual-rig leg** — it never had, and most half-duplex Icoms present CI-V on one wire.
> Either leg may now be set to **None**. Also: the web API can no longer contradict the
> device's battery reading, the Telnet client negotiates properly, receive-only radios
> are refused as uplinks, and USB enumeration waits for both adapters. Gate-checked (17
> static gates, 8 host harnesses). **Still first-bring-up: native dual-radio has never
> driven a real radio** — see docs/THINGS_TO_VERIFY.md.

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
