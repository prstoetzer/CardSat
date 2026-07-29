# 8 MB partition map — leaving room for bmorcelli/Launcher

Investigation only. No build performed. Purpose: decide how CardSat's flash layout
should change so it can coexist with the bmorcelli/Launcher firmware launcher on the
Cardputer ADV's 8 MB flash, and stop wasting half the chip.

## 1. Current reality (measured)

- Build FQBN: `esp32:esp32:esp32s3:PartitionScheme=huge_app,CDCOnBoot=cdc`. No custom
  CSV in the tree; `huge_app` is the stock core scheme.
- `huge_app.csv` (ESP32 core 3.2.1) is a **4 MB** layout:

  ```
  nvs       data nvs      0x9000   0x5000
  otadata   data ota      0xe000   0x2000
  app0      app  ota_0    0x10000  0x300000   (3 MB)
  spiffs    data spiffs   0x310000 0xE0000    (~896 KB)
  coredump  data coredump 0x3F0000 0x10000
  ```

  It ends at `0x400000`. The FQBN sets no `FlashSize`, so the board default
  `esp32s3.build.flash_size=4MB` applies.

- **Consequence: the upper 4 MB of the 8 MB chip is unaddressed and unused.** CardSat is
  at 2,988,822 bytes = **95.0 % of the 3 MB app partition** while 4 MB sits idle.

- CardSat's filesystem is **LittleFS mounted on the `spiffs`-subtype partition**
  (`LittleFS.begin(true)` — mounts, formats if dirty). It stores only small files
  (`gp.json`, the AO-7 observation cache, `dbg.json`); large logs go to SD. So the data
  partition can stay modest. **Any custom CSV must keep a `data, spiffs` partition** or
  LittleFS won't find its filesystem, and CardSat prints "No filesystem! Allocate SPIFFS
  or insert SD."

- CardSat has **no self-OTA / boot-partition manipulation** (no `esp_ota_set_boot_partition`,
  no `Update.begin`), so it is a single-app-slot program — it does not need two app slots
  of its own. It does use `RTC_NOINIT` for cross-reboot state, already guarded by magic
  numbers against the power-cycle garbage case (see `usbserial.cpp`), so Launcher's
  power-cycle-to-switch behavior introduces nothing new.

## 2. How bmorcelli/Launcher actually works (from the repo)

- Flash map on an 8 MB device (from `support_files/custom_8Mb.csv`):

  ```
  nvs       data nvs      0x9000   0x4000
  otadata   data ota      0xD000   0x2000
  phy_init  data phy      0xf000   0x1000
  app0      app  test     0x10000  0x150000   (1.375 MB — Launcher itself)
  coredump  data coredump 0x160000 0x10000
  ```

  It defines partitions only up to `0x170000` (~1.44 MB). **The upper ~6.5 MB is left
  unallocated on purpose.**

- Boot flow: bootloader `0x0`, partition table `0x8000`, Launcher app `0x10000`. On power-on
  Launcher runs first; if no key is pressed it chain-boots the last-installed app.

- Newer Launcher ships **"PMan" (Partition Manager)**: it rewrites the partition table at
  runtime, carving an app partition sized to the firmware being installed (plus a
  SPIFFS/FAT data partition) out of that free upper region, writes the firmware there, and
  boots it. Apps come from SD `.bin` files or the M5Burner OTA list. So Launcher owns the
  low ~1.44 MB and manages the rest dynamically.

This means "leave room for Launcher" is not one design — there are three genuinely
different integration models, below.

## 3. Options

### Proposal A — Standalone CardSat, full 8 MB, **no Launcher**

Just stop wasting the upper 4 MB: switch to a custom 8 MB CSV with a bigger app and a
bigger LittleFS. This does **not** make room for Launcher; it's the baseline "use the
whole chip" move, listed for comparison.

```
# CardSat-8MB-standalone.csv
nvs       data nvs      0x9000   0x5000
otadata   data ota      0xe000   0x2000
app0      app  ota_0    0x10000  0x500000    # 5 MB app (was 3 MB) — huge headroom
spiffs    data spiffs   0x510000 0x2E0000    # ~2.9 MB LittleFS
coredump  data coredump 0x7F0000 0x10000
```

- Pros: trivial change (one CSV + FQBN `FlashSize=8M` + `PartitionScheme=custom`),
  massive app headroom, no coexistence complexity.
- Cons: **no Launcher.** Doesn't answer the actual request. CardSat owns the whole chip.

### Proposal B — Launcher + CardSat **both resident** (recommended for a fixed dual-boot)

Launcher keeps its low region; CardSat gets a dedicated second app partition; they share
one LittleFS. Launcher boots by default and switches to CardSat by setting the boot
partition to `app1` (no re-flash — CardSat is already there).

```
# CardSat-8MB-with-launcher.csv   (flash to BOTH: Launcher at app0, CardSat at app1)
nvs       data nvs      0x9000   0x4000
otadata   data ota      0xd000   0x2000
phy_init  data phy      0xf000   0x1000
app0      app  factory  0x10000  0x160000    # 1.375 MB — Launcher (subtype: see note)
app1      app  ota_0    0x170000 0x3C0000    # 3.75 MB — CardSat
spiffs    data spiffs   0x530000 0x2A0000    # ~2.6 MB LittleFS (shared)
coredump  data coredump 0x7D0000 0x10000
```

- Ends at `0x7E0000`; fits with room. All app partitions are 64 KB-aligned (required).
- **CardSat gets 3.75 MB** — the current 2.99 MB build is **76 %** of it, so this recovers
  real headroom (from 95 % to 76 %) and leaves growth room, while still fitting Launcher.
- LittleFS: still a `spiffs`-subtype partition, so CardSat mounts it unchanged. Note it is
  now shared with whatever Launcher stores there; ~2.6 MB is plenty for CardSat's KB-scale
  files.
- Subtype note: Launcher ships as subtype `test` at app0; a hand-built dual-boot CSV can
  use `factory` for app0 (Launcher) and `ota_0` for app1 (CardSat), then `otadata` selects
  which OTA app runs and CardSat is reached by `esp_ota_set_boot_partition(app1)`. Whether
  Launcher's binary tolerates being at `factory` vs `test` needs a bench check — Launcher's
  changelog mentions an "app offset parameter to allow firmware placed at different
  factory/app0 addresses," so it is designed to be relocatable, but this is the one
  assumption to verify on hardware.
- Return-to-Launcher: CardSat would need a small "reboot to Launcher" action that calls
  `esp_ota_set_boot_partition()` on the Launcher partition and `esp_restart()`. This is a
  **new ~15-line feature** in CardSat (a menu item), not a partition concern, but it's the
  natural companion so the two can round-trip without a manual reset. Optional: Launcher can
  also just be re-entered by the boot-time key press.
- Pros: both firmwares always present; instant switch; no SD needed; CardSat keeps a fixed,
  known partition (simplest for our build/flash/verify pipeline — the `.bin` at a fixed
  offset, §9 packaging parity unchanged in spirit).
- Cons: CardSat capped at 3.75 MB (fine now, but it is a ceiling); requires flashing two
  images at defined offsets; the app0-subtype question needs bench confirmation.

### Proposal C — Launcher **PMan-managed**, CardSat lives on SD

Flash Launcher's stock `custom_8Mb.csv` unchanged. Put `CardSat.bin` on the SD card (or
install via M5Burner OTA). Launcher's PMan carves an app partition to fit CardSat (~3 MB)
plus a data partition at install time and boots it.

- Pros: zero custom CSV to maintain on our side; Launcher owns partitioning; can host
  CardSat *and* other firmware; matches how Launcher is designed to be used; CardSat can be
  as large as the free region allows (~6 MB) since PMan sizes to fit.
- Cons: CardSat is not permanently in flash (re-loaded on selection, ~seconds); its
  LittleFS lives in a PMan-created data partition whose subtype/size we don't fully control
  — **needs verification that PMan creates a `spiffs`-subtype partition CardSat can mount**,
  or CardSat may need to fall back to SD-only. Our build pipeline changes: we'd ship a plain
  `CardSat.ino.bin` for a Launcher-managed slot (Launcher wants app binaries that may be
  cropped/re-headered — see repo note about "binaries not merged, allowing attaching SPIFFS
  partition"), which is a different artifact than the merged image we package today.

## 4. Recommendation

**Proposal B** for a fixed, dependable CardSat-plus-Launcher dual-boot, because it keeps
CardSat at a fixed, known partition (least disruption to our fixed-offset build / MD5 /
§9-packaging pipeline), recovers headroom (95 % → 76 %), and needs no SD card. Its one
open question — whether Launcher runs happily from the `factory`/`app0` slot in a
hand-built table — is bench-checkable and the changelog suggests it's supported.

**Proposal C** is the "more Launcher-native" path and is better if the goal is a general
multi-firmware device (CardSat as one of several apps) rather than a two-app appliance. It
offloads all partition management to Launcher at the cost of our build artifact and a
verification that PMan gives CardSat a mountable `spiffs` partition.

**Proposal A** only if Launcher turns out not to be wanted after all — it's the trivial
"use the whole 8 MB" win but ignores the request.

## 5. What changes in our build (when we do proceed — NOT in this step)

Common to A and B (custom fixed CSV):
1. Add the chosen CSV to the tree (e.g. `partitions/CardSat-8MB.csv`).
2. FQBN gains `PartitionScheme=custom` and `FlashSize=8M`; point the custom scheme at the
   CSV (Arduino CLI: a `partitions.csv` beside the sketch, or a `boards.local.txt` custom
   menu entry). The `upload.maximum_size` must be raised to the new app size or the size
   gate in the build will reject a >3 MB image.
3. `firmware/README.md` flash instructions and the §9 packaging checks update to the new
   offsets and the new merged-image size; the app-size assertion in the release checklist
   changes from 3,145,728 to the new app-partition size.
4. Bench: confirm LittleFS mounts on the resized `spiffs` partition; confirm coredump still
   present (CardSat references it in reset reporting); confirm the RTC_NOINIT guards behave
   across a Launcher-initiated reboot.

Proposal B additionally:
5. Flashing becomes two images (Launcher @ app0, CardSat @ app1) at defined offsets.
6. Optional CardSat "reboot to Launcher" menu action (`esp_ota_set_boot_partition` on the
   Launcher partition + `esp_restart`) — small, new, and the natural round-trip companion.
7. Verify Launcher tolerates its `factory`/`app0` slot in the hand-built table.

Proposal C additionally:
5. Ship a Launcher-compatible `CardSat.ino.bin` artifact (not the merged image); confirm
   Launcher's crop/re-header step accepts it.
6. Verify PMan creates a `spiffs`-subtype data partition CardSat can mount, else document
   SD-only fallback.

## 6. Open questions for the bench (none blocking this investigation)

- Does Launcher run from `factory`/`app0` in a hand-built dual-boot table (Proposal B)?
- Does PMan produce a CardSat-mountable `spiffs` partition (Proposal C)?
- Preferred CardSat app-partition size: 3.75 MB (Proposal B, leaves Launcher + shared FS)
  vs larger — depends on expected growth. Current build is 2.99 MB.
