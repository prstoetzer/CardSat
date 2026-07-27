# CardSat — precompiled firmware (M5Cardputer ADV)

Prebuilt binaries for the **M5Stack Cardputer ADV** (ESP32-S3FN8, 8 MB flash, no PSRAM),
firmware **v0.9.66**. Flash these if you just want to run this build without
compiling. Source is the rest of this repo; `CardSat.ino` is the monolithic sketch.

Built with: arduino-cli + `esp32:esp32@3.2.1`, FQBN
`esp32:esp32:esp32s3:PartitionScheme=huge_app,CDCOnBoot=cdc`, M5Cardputer library.
No `build.extra_flags` (that would break the HWCDC serial console). Flash usage at
build time: 2,990,734 bytes (95.07%); static RAM 161,344 bytes (49%).

The flash percentage is only meaningful beside the library versions it was built
with: M5Cardputer 1.1.1, M5GFX 0.2.26, M5Unified 0.2.19, ESP_SSLClient 3.1.3,
ArduinoJson 7.4.3, TinyGPSPlus 1.0.3, RadioLib 7.7.1, Sgp4 1.0.3, EspUsbHost 2.4.1.

Checksums (MD5):
- `CardSat-merged.bin`  9222c2136858727f59f5bbaa55d48ef4
- `CardSat-app.bin`     a00199cd27b7535e80647f7bb300609f
- `CardSat-bootloader.bin`  c7f9b41acfaba802c7e74ae639a9a162
- `CardSat-partitions.bin`  70007348574201233bc0cb17155e9d12

> **v0.9.66** adds an **HF MUF-to-regions** predictor (verified MINIMUF-3.5 model) and an
> **orbital-zone transit** tool (South Atlantic Anomaly, eclipse, polar, and the inner/outer
> Van Allen belts by magnetic L-shell), and fixes the charge/sleep battery read and wake
> flashing, WiFi resume after charge mode, live-feed fetch/refresh, and Tiny BASIC `IF … THEN`.
> Gate-checked (18 gates incl. two new physics harnesses). The **Van Allen belt behavior on a
> high-orbit satellite** is the main item to confirm on hardware. Earlier-release items that
> remain first-bring-up (two-adapter USB, the dual-radio companion over TCP and Grove rigctl,
> and the transverter/microwave LO-offset paths) are unchanged and still benefit from hardware
> testing.

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
