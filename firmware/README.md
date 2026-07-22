# CardSat — precompiled firmware (M5Cardputer ADV)

Prebuilt binaries for the **M5Stack Cardputer ADV** (ESP32-S3FN8, 8 MB flash, no PSRAM),
firmware **v0.9.64**. Flash these if you just want to run this build without
compiling. Source is the rest of this repo; `CardSat.ino` is the monolithic sketch.

Built with: arduino-cli + `esp32:esp32@3.2.1`, FQBN
`esp32:esp32:esp32s3:PartitionScheme=huge_app,CDCOnBoot=cdc`, M5Cardputer library.
No `build.extra_flags` (that would break the HWCDC serial console). Flash usage at
build time: 2,911,728 bytes (92%); static RAM 156,936 bytes (47%).

Checksums (MD5):
- `CardSat-merged.bin`  8e03605b0886757bea53429e37b5e53f
- `CardSat-app.bin`     ae4ea8928ed17bacf6d6ba14a78594cf
- `CardSat-bootloader.bin`  c7f9b41acfaba802c7e74ae639a9a162
- `CardSat-partitions.bin`  70007348574201233bc0cb17155e9d12

> This is a **testing release**. v0.9.64 overhauled the USB-control lifecycle (USB CAT and
> USB rotators release their memory when turned off), added two-adapter USB radio+rotator with
> a **None** option, and went through three rounds of security/lifecycle auditing plus a
> dedicated Dual-Rig audit: storage writes are now transactional (config/notes/caches survive
> an interrupted write), downloads reject truncation, GPS/voice-memo/rig lifecycles were
> tightened, and the CardSatDualRig Grove path had three release-blocking bugs fixed (UART
> pins, 115200 baud, model parsing). The **two-adapter USB** path, the **dual-radio companion**
> (both TCP and Grove rigctl), and the transverter/microwave (LO-offset) paths are implemented
> but remain first-bring-up items that benefit most from hardware testing.

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
