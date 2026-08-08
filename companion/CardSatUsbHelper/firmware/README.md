# CardSatUsbHelper — precompiled firmware (M5StickS3)

Prebuilt binaries for the **M5StickS3**, matching CardSat **v0.9.73**. Flash these
to run this exact build without compiling.

Built with arduino-cli + `esp32:esp32@3.2.1`, FQBN
`esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled,DebugLevel=error`
and `compiler.cpp.extra_flags=-DCORE_DEBUG_LEVEL=1`, with the USB host tunables in
this sketch's `build_opt.h`. **Built against the vendored ESP-IDF USB host stack**
(`tools/vendor_usb_host.sh`) — verified in the map: 1,269 `UsbHostSrc` references,
`libusb.a(` at zero.
Libraries: M5Unified 0.2.19, M5GFX 0.2.26, EspUsbHost **2.7.0, PATCHED** (see
`third_party/EspUsbHost/PATCHES.md`).

Flash usage 717,346 bytes of a 3,342,336-byte app partition (21%); static RAM
39,408 bytes (12%).

Checksums (MD5):

- `CardSatUsbHelper-merged.bin`      1326feb42209d8f3ab337ecebf8810ed
- `CardSatUsbHelper-app.bin`         9a0878d98bdfafcc61cd142d0524b613
- `CardSatUsbHelper-bootloader.bin`  120ea00b035393656671e3561eee9eaf
- `CardSatUsbHelper-partitions.bin`  801ba71678a964614657a6d8fbc6baca
- `boot_app0.bin`                    e6327541e2dc394ca2c3b3280ac0f39f

## Easiest: one file at 0x0

`CardSatUsbHelper-merged.bin` contains the bootloader, partition table, boot_app0
and the app, so it flashes as a single image:

```
esptool.py --chip esp32s3 --port <PORT> --baud 921600 write_flash 0x0 CardSatUsbHelper-merged.bin
```

## Or: the four pieces at their offsets

```
esptool.py --chip esp32s3 --port <PORT> --baud 921600 write_flash \
  0x0     CardSatUsbHelper-bootloader.bin \
  0x8000  CardSatUsbHelper-partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 CardSatUsbHelper-app.bin
```

Power-cycle after flashing.

## First run

The screen stays **dark** — that is correct, not a failed flash. Press **Button A**
to wake it for 12 seconds; it should show `link: scanning` until CardSat starts
talking, then `link up @230400`.

If it never links, check the Grove pair first: TX and RX are easy to swap, and a
reversed pair is silent rather than noisy, so it presents exactly like a dead
cable. `tools/helper_probe.py` will confirm the Stick is alive independently of
CardSat.

## Status

Compiles clean; the link layer is verified host-side. **Not yet bench-tested on a
real M5StickS3 with a radio attached.**
