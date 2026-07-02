# CardSat — Setting Up a Fresh Arduino IDE Development Environment

This guide walks through building CardSat from source in the **Arduino IDE**, starting
from nothing. It covers installing the IDE, adding the ESP32 board support, installing
every required library at a known-good version, configuring the board settings, and
compiling and flashing the firmware to an **M5Stack Cardputer ADV**.

If you only want to *install* CardSat (not build it), you don't need any of this — use
one of the prebuilt binaries described in the main `README.md` ("Build & flash").
This document is for building from source.

> **Target hardware:** M5Stack **Cardputer ADV** (StampS3A = ESP32-S3FN8, 8 MB flash,
> **no PSRAM**). The original (non-ADV) Cardputer uses a different MCU module and is
> **not** the build target here.

---

## 0. What you'll end up with

A working toolchain that compiles the single-file sketch `CardSat.ino` and uploads it
to the Cardputer over USB-C. A successful build prints something like
`esp32:esp32:m5stack_cardputer:PartitionScheme=huge_app` and finishes with the sketch
using roughly 2–2.5 MB of a 3 MB app partition.

The reference environment this guide is verified against:

| Component | Known-good version |
|---|---|
| Arduino IDE | 2.x (2.3.x or newer) |
| ESP32 boards package (Espressif) | **3.2.1** |
| M5Cardputer | 1.1.1 |
| M5Unified | 0.2.17 |
| M5GFX | 0.2.23 |
| ArduinoJson | **7.x** (7.4.3) |
| TinyGPSPlus | 1.0.3 |
| Hopperpop SGP4 (`Sgp4.h`) | 1.0.x |
| RadioLib | 7.7.1 |
| ESP_SSLClient (by Mobizt) | 2.x |

Newer point releases generally work. The three that matter most are the **ESP32 core
3.2.x or 3.3.x**, **ArduinoJson v7** (v6 will not compile), and **ESP_SSLClient** —
without the last one the HTTPS code (all downloads and uploads) will not compile.

---

## 1. Install the Arduino IDE

1. Download the **Arduino IDE 2.x** from <https://www.arduino.cc/en/software> for your
   platform (Windows, macOS, or Linux) and install it normally.
2. Launch it once so it creates your sketchbook folder (default:
   `~/Documents/Arduino` on macOS/Windows, `~/Arduino` on Linux). Your libraries will
   live in the `libraries` subfolder of that path.

---

## 2. Add ESP32 board support

CardSat targets the Espressif ESP32 Arduino core (the Cardputer's StampS3A is an
ESP32-S3).

1. Open **Arduino IDE → Settings** (Windows/Linux: *File → Preferences*;
   macOS: *Arduino IDE → Settings*).
2. In **Additional boards manager URLs**, add:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
   If there are already URLs there, separate multiple entries with commas.
3. Click **OK**.
4. Open **Tools → Board → Boards Manager** (or the boards icon in the left sidebar).
5. Search for **esp32** and install **"esp32 by Espressif Systems"**, version
   **3.2.1** (or the latest 3.2.x / 3.3.x). Installation downloads the Xtensa
   toolchain and can take several minutes.

> **Why a specific version?** CardSat builds cleanly against ESP32 core **3.2.x and
> 3.3.x**. Very old cores (2.x) predate API changes CardSat relies on and will not
> compile. If you are on the 3.3.x line, note that Espressif renamed some networking
> headers — CardSat already accounts for this, so 3.3.x is fine.

---

## 3. Install the required libraries

Install these through **Tools → Manage Libraries…** (the Library Manager) unless noted
otherwise. Search by name, pick the matching author, and click **Install**. If the IDE
offers to install dependencies, accept.

### 3.1 From Library Manager

| Library | Search for | Notes |
|---|---|---|
| **M5Cardputer** | `M5Cardputer` | Pulls in **M5Unified** and **M5GFX** as dependencies — accept them. |
| **ArduinoJson** | `ArduinoJson` | **Must be v7.** Do not install v6. |
| **TinyGPSPlus** | `TinyGPSPlus` | By Mikal Hart. Used for the optional external GPS. |
| **ESP_SSLClient** | `ESP_SSLClient` | **By Mobizt. Required.** All HTTPS (downloads + LoTW/Cloudlog uploads) runs on its BearSSL stack; the build will not compile without it. |

When you install M5Cardputer, the IDE should automatically offer **M5Unified** and
**M5GFX**. If it does not, install those two by name as well — CardSat needs all three.

### 3.2 The SGP4 orbital library (manual ZIP install)

CardSat uses the **Hopperpop SGP4** library (it provides `Sgp4.h` and the
`sgp4(wgs72, …)` propagator). It is **not** in Library Manager, so install it from ZIP:

1. Go to <https://github.com/Hopperpop/Sgp4-Library>.
2. Click **Code → Download ZIP**.
3. In the Arduino IDE: **Sketch → Include Library → Add .ZIP Library…** and select the
   downloaded ZIP.

> The header is `Sgp4.h`. Depending on how a given distribution names the folder, the
> Library Manager may also list an SGP4 library that satisfies `#include <Sgp4.h>`; if
> you already have one that exposes the `sgp4(wgs72, satrec, tsince, r, v)` API and the
> sketch compiles, you're fine. The Hopperpop source above is the canonical one.

### 3.3 RadioLib — only if you want LoRa features (optional)

The LoRa messaging/beacon features are **disabled by default** and the entire module is
compiled out unless you opt in. You only need RadioLib if you intend to build with LoRa
enabled.

- Install **RadioLib** (by Jan Gromeš) from Library Manager — version **7.7.1** is the
  reference. Then see §7 for how to turn the feature on with a build flag.
- If you do **not** want LoRa, you can skip RadioLib entirely; CardSat builds and runs
  without it.

### 3.4 Libraries you do NOT install

`WiFi`, `WiFiClientSecure`, `HTTPClient`, `Wire`, `SPI`, `SD`, `FS`, `LittleFS`, and the
`esp_*` headers all ship **with the ESP32 core** from step 2. Do not install separate
copies — duplicates cause "multiple libraries found" warnings or the wrong header
winning. (A harmless `Multiple libraries were found for "SD.h"` notice can appear; the
core's copy is the one used.)

---

## 4. Get the CardSat source

1. Download the CardSat repository (from
   <https://github.com/prstoetzer/CardSat>): **Code → Download ZIP**, or `git clone`.
2. Arduino requires the main sketch and its folder to share a name. Ensure you have a
   folder named **`CardSat`** containing **`CardSat.ino`** plus the `src/` directory and
   the other project files. If you downloaded the ZIP, you may get a folder like
   `CardSat-main/` — rename it to `CardSat` (or place `CardSat.ino` in a folder named
   `CardSat`).
3. Open **`CardSat.ino`** in the Arduino IDE (File → Open).

> **Single-file vs modular source.** The repository ships the firmware **two ways**:
> the all-in-one `CardSat.ino`, and a modular version under `src/` (`app.cpp`, `app.h`,
> etc.). The Arduino IDE builds the **single-file `CardSat.ino`** — that is the file you
> open and compile. The two are kept byte-for-byte equivalent; for an Arduino build you
> do not need to do anything with `src/`.

---

## 5. Configure the board settings

Connect the Cardputer ADV by USB-C, then set **Tools → Board → esp32 →** your board, and
the options below.

**Board:** choose **M5Cardputer** if your core lists it. If not, **ESP32S3 Dev Module**
works and exposes the full Tools menu (the reference build uses the
`m5stack_cardputer` board definition).

Then set:

| Tools setting | Value | Why |
|---|---|---|
| **Partition Scheme** | **Huge APP (3 MB No OTA / 1 MB SPIFFS)** | **Required.** The default ~1.25 MB app partition is too small and the build fails with *"Sketch too big."* |
| **Flash Size** | **8 MB (64 Mb)** | The ADV's StampS3A has 8 MB. |
| **PSRAM** | **Disabled** | The StampS3A has **no PSRAM**. Leaving PSRAM enabled can crash at boot. |
| **CPU Frequency** | 240 MHz | Default; fine. |
| **Flash Mode** | QIO | Default; fine. |
| **USB CDC On Boot** | **Enabled** | So the Serial Monitor works over USB-C. |
| **Upload Speed** | 921600 | Drop to 115200 if uploads fail. |
| **Port** | your Cardputer's serial port | See §6. |

Leave the other options at their defaults.

> If you pick **ESP32S3 Dev Module** instead of an M5 board, the same settings apply —
> just make sure **Partition Scheme = Huge APP**, **Flash Size = 8 MB**, and
> **PSRAM = Disabled**.

---

## 6. Select the port and flash

1. Plug the Cardputer ADV into your computer with a **data-capable USB-C cable** (some
   cables are charge-only — if no port appears, try another cable first).
2. **Tools → Port** and select the Cardputer's port:
   - **macOS:** `/dev/cu.usbmodem…`
   - **Linux:** `/dev/ttyACM0` (you may need to be in the `dialout` group:
     `sudo usermod -aG dialout $USER`, then log out and back in)
   - **Windows:** a `COM` port (install the USB driver if it doesn't enumerate)
3. Click **Verify** (✓) to compile. The first build is slow (the core compiles a lot);
   later builds are cached and much faster.
4. Click **Upload** (→) to flash. If the upload doesn't start, hold the Cardputer's
   **G0/BtnA** while plugging in to force the bootloader, then upload.
5. Open **Tools → Serial Monitor** at **115200 baud** to watch boot logs.

A successful compile ends with the sketch size and the FQBN line
`esp32:esp32:m5stack_cardputer:PartitionScheme=huge_app,…`.

---

## 7. LoRa is built in — install RadioLib

The LoRa text-messaging and beacon code is built into the standard binaries
(`CARDSAT_HAS_LORA` defaults to `1`), so **RadioLib is a required dependency**:

1. Install **RadioLib** (§3.3) before compiling, or the build fails on the missing
   `RadioLib.h`.
2. That's it — nothing to toggle. The `#if CARDSAT_HAS_LORA` guards remain in the
   source only so the tree still compiles if you deliberately set the flag to `0` in
   your own build (you shouldn't need to).

> LoRa text messaging is **hardware-verified** (two-way against a LilyGo T-LoRa unit
> running the companion CardSat Pager firmware); see the LoRa notes in `README.md`.

---

## 8. Troubleshooting

**"Sketch too big" / text section exceeds available space.**
Partition Scheme is not set to **Huge APP**. Fix it in Tools (§5) and recompile.

**`ArduinoJson` compile errors about `StaticJsonDocument` / `DynamicJsonDocument`.**
You have ArduinoJson **v6** installed. CardSat needs **v7**. In Library Manager, select
ArduinoJson and install a 7.x version.

**`Sgp4.h: No such file or directory`.**
The Hopperpop SGP4 library isn't installed. Add it via **Sketch → Include Library →
Add .ZIP Library…** (§3.2).

**`RadioLib.h: No such file or directory`.**
Either install RadioLib (§3.3) or set `CARDSAT_HAS_LORA` back to `0` (§7).

**`'…' was not declared in this scope` after editing the sketch.**
In C++, file-scope objects must be defined before first use. If you reorder functions
in `CardSat.ino`, keep definitions ahead of the code that references them.

**Board not detected / no port appears.**
Try a different USB-C cable (charge-only cables won't enumerate), a different port, and
on Linux confirm `dialout` group membership. As a last resort, hold **G0** while
plugging in to enter the ROM bootloader, then select the port and upload.

**Upload starts but fails partway.**
Lower **Upload Speed** to 115200 and retry.

**Crashes/reboots right after flashing.**
Confirm **PSRAM = Disabled** and **Flash Size = 8 MB** — the StampS3A has no PSRAM and
mis-set flash size can fault at boot.

**`Multiple libraries were found for "SD.h"` (or similar).**
Harmless. The ESP32 core's bundled copy is used. Don't install a separate SD library.

---

## 9. Quick checklist

- [ ] Arduino IDE 2.x installed
- [ ] ESP32 boards URL added; **esp32 core 3.2.x/3.3.x** installed
- [ ] **M5Cardputer** (+ M5Unified, M5GFX) installed
- [ ] **ArduinoJson v7** installed
- [ ] **TinyGPSPlus** installed
- [ ] **Hopperpop SGP4** added via ZIP
- [ ] *(optional)* **RadioLib** installed for LoRa
- [ ] Source opened as `CardSat/CardSat.ino`
- [ ] Board = M5Cardputer (or ESP32S3 Dev Module)
- [ ] **Partition Scheme = Huge APP**, **Flash = 8 MB**, **PSRAM = Disabled**, **USB CDC On Boot = Enabled**
- [ ] Correct **Port** selected
- [ ] Verify compiles, Upload flashes, Serial Monitor at 115200 shows boot

---

*For prebuilt-binary installation (Launcher, M5Burner, web flasher) and the PlatformIO
build, see `README.md`. This document covers the Arduino IDE source build only.*
