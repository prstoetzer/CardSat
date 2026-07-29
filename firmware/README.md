# CardSat — precompiled firmware (M5Cardputer ADV)

Prebuilt binaries for the **M5Stack Cardputer ADV** (ESP32-S3FN8, 8 MB flash, no PSRAM),
firmware **v0.9.68**. Flash these if you just want to run this build without
compiling. Source is the rest of this repo; `CardSat.ino` is the monolithic sketch.

Built with: arduino-cli + `esp32:esp32@3.2.1`, FQBN
`esp32:esp32:esp32s3:PartitionScheme=huge_app,CDCOnBoot=cdc`, M5Cardputer library.
No `build.extra_flags` (that would break the HWCDC serial console). Flash usage at
build time: 3,029,150 bytes (96.3%); static RAM 162,080 bytes (49%).

The flash percentage is only meaningful beside the library versions it was built
with: M5Cardputer 1.1.1, M5GFX 0.2.26, M5Unified 0.2.19, ESP_SSLClient 3.1.3,
ArduinoJson 7.4.2, TinyGPSPlus 1.0.3, RadioLib 7.7.1, Sgp4 1.0.3, EspUsbHost 2.5.2.

Checksums (MD5):
- `CardSat-merged.bin`  4c70777f9facf634d4b25779d63b67ad
- `CardSat-app.bin`     983e589f540a396ecb040ea8ee2f3285
- `CardSat-bootloader.bin`  c7f9b41acfaba802c7e74ae639a9a162
- `CardSat-partitions.bin`  70007348574201233bc0cb17155e9d12

> **v0.9.68** adds **native dual-radio support**: CAT type **Dual (2 radios)** drives a
> downlink and an uplink radio directly — any two of **27 radios**, each on its own bus (Grove
> serial, a USB adapter with **both legs allowed on USB** through a hub, or Icom LAN including
> the **IC-705 over its own Wi-Fi**) — with the CardSatDualRig companion still supported. Two
> pieces of orbital physics were rebuilt after bench reports: the **Van Allen belt** zones now
> use McIlwain (L, B/B0) traced through the real **IGRF-14** field instead of a centered dipole
> plus an altitude floor, and the **orbital-decay** estimate is re-anchored on each element
> set's measured decay rate — it had been predicting roughly a fifth of an object's true
> remaining life, and now lands within ±30% for 89% of real re-entries, validated against 244
> objects that actually re-entered. A new gate also found five pre-existing cases where
> canceling an edit field dropped the operator into an unrelated editor. Gate-checked (15
> static gates, 8 host harnesses). First-bring-up items this cycle: the native dual-rig paths
> (all bus combinations), the IC-705 LAN leg, and dual-USB CAT.

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
