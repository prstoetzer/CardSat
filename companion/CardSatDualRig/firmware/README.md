# CardSatDualRig — precompiled firmware (M5StickS3)

Prebuilt binaries for the **M5StickS3** (ESP32-S3-PICO-1-N8R8, 8 MB flash, 8 MB PSRAM).
Flash these if you just want to run the companion without building it yourself. Source
is one directory up; build details are in `../README.md`.

Built with: arduino-cli + `esp32:esp32@3.2.1`, board `esp32:esp32:esp32s3`,
`USBMode=hwcdc, CDCOnBoot=default, FlashSize=8M, PartitionScheme=default_8MB,
PSRAM=enabled`, and `-DESP_USB_HOST_MAX_DEVICES=4 -DCORE_DEBUG_LEVEL=1`.

Checksums (MD5):
- `CardSatDualRig-merged.bin`  575db648439b347b88e773fbaa522a74
- `CardSatDualRig-app.bin`     ac3ad7c0cd39e2982b10da490987f9e5

## Easiest: one file at 0x0 (esptool)

`CardSatDualRig-merged.bin` already contains the bootloader, partition table,
boot_app0, and the app, so it flashes as a single image at offset **0x0**:

```
esptool.py --chip esp32s3 --port <PORT> --baud 921600 write_flash 0x0 CardSatDualRig-merged.bin
```

Replace `<PORT>` with your serial port (e.g. `/dev/ttyACM0`, `/dev/cu.usbmodem*`,
or `COM5`). If the board doesn't enter download mode on its own, hold the reset/G0
sequence for your Stick (or run esptool with `--before default_reset`), then retry.

## Or: the four pieces at their offsets

If you'd rather flash the individual images (identical result), the ESP32-S3
offsets are:

```
esptool.py --chip esp32s3 --port <PORT> --baud 921600 write_flash \
  0x0     CardSatDualRig-bootloader.bin \
  0x8000  CardSatDualRig-partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 CardSatDualRig-app.bin
```

(On the ESP32-S3 the bootloader lives at **0x0**, not 0x1000.)

## Or: M5Burner

M5Burner can flash a custom image: pick the M5StickS3 device, choose "burn" from a
local file, select `CardSatDualRig-merged.bin`, and set the flash address to **0x0**.

## First boot

The Stick comes up in **config mode** (SoftAP `CardSatDualRig-XXXX`, password
`cardsat123`, http://192.168.4.1) so you can set the two radios — or configure it
straight from CardSat over Grove/Wi-Fi (Settings → Radio → *Dual-Rig setup (Stick)*).
See `../README.md` for the full walkthrough, the supported-radio list, and the Grove
power-safety notes.

> Heads-up: the companion is **not yet hardware-tested end to end**. Treat first
> bring-up as verification, not a known-good flash.
