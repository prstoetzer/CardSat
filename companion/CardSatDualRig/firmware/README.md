# CardSatDualRig — precompiled firmware (M5StickS3)

> ⚠️ **EXPERIMENTAL — never tested on hardware.** This firmware compiles clean and has
> been through several CardSat audits, but nobody has run it with two radios on a real
> M5StickS3. Treat it as a starting point if you have the hardware, not as a finished
> product. Bench findings are very welcome.

Prebuilt binaries for the **M5StickS3** (ESP32-S3-PICO-1-N8R8, 8 MB flash, 8 MB PSRAM),
built for CardSat **v0.9.70**.
Flash these if you just want to run the companion without building it yourself. Source
is one directory up; build details are in `../README.md`.

Built with: arduino-cli + `esp32:esp32@3.2.1`, board `esp32:esp32:esp32s3`,
`USBMode=hwcdc, CDCOnBoot=default, FlashSize=8M, PartitionScheme=default_8MB,
PSRAM=enabled`, and `-DESP_USB_HOST_MAX_DEVICES=4 -DCORE_DEBUG_LEVEL=1`, against the **patched**
EspUsbHost from CardSat's `third_party/EspUsbHost/` (a stock library will build but
can strand the USB stack — see `third_party/EspUsbHost/PATCHES.md`).
Flash usage: 1,280,738 bytes (38%); static RAM 61,244 bytes (18%).

Checksums (MD5):
- `CardSatDualRig-merged.bin`  cc6ec3f54acfa9fd4e03e3d7b4900dd4
- `CardSatDualRig-app.bin`     3a796887d685f592b007e50665885669
- `CardSatDualRig-bootloader.bin`  120ea00b035393656671e3561eee9eaf
- `CardSatDualRig-partitions.bin`  801ba71678a964614657a6d8fbc6baca

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

## 2026-07 rebuild — TH-D74/D75 fixes

This build corrects two defects that made Kenwood TH-D74/D75 control impossible, both
found when a TH-D75 was bench-tested against CardSat (which had inherited this
firmware's encoder):

- **The Kenwood-handheld CAT dialect was wrong.** There is no `FQ` command on this
  family: the frequency lives inside the `FO <band>` record and a set is a
  read-modify-write of that whole record. `MD` also requires a space before its
  parameters, and AM/DV were transposed in the mode map. Verified against Hamlib
  `rigs/kenwood/thd74.c`.
- **The CDC control lines were never asserted.** Only the baud rate was set, so any
  radio that gates traffic on DTR — the D74/D75 among them — stayed silent whatever we
  sent. DTR and RTS are now asserted on every bind.

The second one is not Kenwood-specific: it could have affected any CDC radio that
waits for DTR before accepting host traffic.

## Same rebuild — Yaesu dialect audit

Following the Kenwood finding above, the Yaesu dialects were audited against Hamlib
too, and two radios were in the wrong family:

- **FT-100** was filed under the FT-817 5-byte family. It shares only the frame
  length: frequency opcode 0x0A (not 0x01), **little-endian** BCD (not big), the mode
  byte in `data[3]` with opcode 0x0C (not `data[0]` with 0x07), its own mode values
  (FM = 0x06), read opcode 0x10, and a reply whose frequency starts at offset 1
  behind a band number. It now has its own dialect.
- **VR-5000** shares the FT-817 framing but its FM is **0x88** (Hamlib maps
  RIG_MODE_FM to MODE_FMN for this receiver; plain 0x08 is not in its table), and it
  has **no frequency read command at all**, so read-back is no longer attempted.

The FT-817/818/857/897 dialect was verified correct and unchanged (Hamlib's ft817.c,
ft857.c and ft897.c are byte-identical in this area). The FT-991/991A ASCII dialect
was also verified correct.

## Same rebuild — CI-V mode-command filter byte

The CI-V dialect was audited too (it was otherwise clean: framing, 5-byte frequency,
addresses and mode bytes all verified against Hamlib). One issue: the set-mode command
was always sent as `06 <mode> <filter>`, but a few Icoms reject cmd 06 when it carries
passband data — Hamlib names the IC-475 and IC-7000 among them, and both are in this
catalog. They now get the two-byte `06 <mode>` form. Since nothing checks the CI-V ACK,
the old behavior would have shown up only as "mode changes do nothing".

## Same rebuild — IC-905 six-byte frequency

Above 5.85 GHz the IC-905 takes a **six-byte** CI-V frequency field. Five bytes is ten
BCD digits, which tops out just under 10 GHz and cannot express that band at all. Below
the threshold the radio uses the ordinary five-byte form, so the choice is made per
frequency, not per radio.

## Same rebuild — eight more SSB-capable VHF/UHF radios

Scoped deliberately to radios that do **SSB on VHF/UHF** (the FM-only mobiles and
D-STAR handhelds were considered and rejected — they cannot work a linear satellite):

- **IC-271** (0x20), **IC-471** (0x22), **IC-575** (0x16), **IC-1275** (0x18) — the
  classic all-mode base stations, direct siblings of the IC-275/475 already supported.
- **IC-706MKII** (0x4E) and **IC-706** (0x48) — 2 m SSB; note neither has 70 cm, which
  arrived with the MKIIG.
- **TS-711** and **TS-811** — a new dialect, `CAT_KENWOOD_TS`: generic Kenwood ASCII
  (`FA` + eleven digits + `;`, `MD<digit>;`, 4800 baud). A TS-711 + TS-811 pair on 2 m
  and 70 cm is the classic two-radio all-mode satellite station.

All addresses and the 5-byte frequency form verified against Hamlib. None is
bench-tested.
