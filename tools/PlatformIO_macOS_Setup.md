# Building CardSat with PlatformIO on macOS

A step-by-step guide to moving CardSat development from the Arduino IDE to
PlatformIO (PIO) on a Mac. CardSat already ships with a working
`platformio.ini`, so this is mostly *install the tools and open the existing
project* — not *configure from scratch*.

The single most important thing to get right is the **ESP32 core version**,
because CardSat calls `WiFiClientSecure::setBufferSizes()` to shrink the TLS
buffers on the no-PSRAM ESP32-S3. That method exists in arduino-esp32 **2.0.x**
and was removed in **3.x**. This guide pins PlatformIO to a platform version that
gives you the 2.0.x core, so the build matches the firmware that works.

---

## 1. Install Visual Studio Code

PlatformIO runs as an extension inside VS Code.

1. Download VS Code from <https://code.visualstudio.com/> (the "Mac Universal"
   build runs natively on both Apple Silicon and Intel).
2. Open the downloaded `.zip`, then drag **Visual Studio Code.app** into your
   `/Applications` folder.
3. Launch it once from Applications so macOS clears the "downloaded from the
   internet" warning.

## 2. Check the prerequisites

PlatformIO bundles its own Python, but it still wants a system Python 3 and Git
available. Open **Terminal** (Applications → Utilities → Terminal) and run:

```bash
python3 --version     # expect 3.6 or newer; macOS ships with one
git --version         # if missing, macOS will prompt to install the dev tools
```

If `git` triggers a "command line developer tools" popup, accept it and let it
install — that's the easiest way to get Git on a Mac.

## 3. Install the PlatformIO IDE extension

1. In VS Code, click the **Extensions** icon in the left toolbar (the four-squares
   icon) or press `Cmd+Shift+X`.
2. Search for **PlatformIO IDE**.
3. Click **Install**. The first install downloads PlatformIO Core (the `pio`
   command-line tool) in the background — this takes a few minutes. Watch the
   status bar at the bottom; when the house/alien icons appear, it's ready.
4. Reload VS Code if it prompts you to.

You do **not** need to install PlatformIO Core separately — it's built into the
extension and is available in the PlatformIO terminal as `pio`.

## 4. Open the CardSat project

PlatformIO treats any folder containing a `platformio.ini` as a project, so you
just open the CardSat folder:

1. **File → Open Folder…**
2. Select the `CardSat` repository folder (the one containing `platformio.ini`,
   `src/`, and `CardSat.ino`).
3. If VS Code asks whether you trust the authors, choose **Yes**.

PlatformIO will detect `platformio.ini` and, on first open, download the
toolchain, the ESP32 platform, and the libraries listed under `lib_deps`. This
first run is slow (hundreds of MB); subsequent builds are fast and cached.

### What PlatformIO builds (and the .ino)

CardSat keeps two parallel copies of the same program:

- `src/*.cpp` / `src/*.h` — the modular sources, **including `src/main.cpp`**,
  which holds the `setup()` / `loop()` entry points. **This is what PlatformIO
  compiles.**
- `CardSat.ino` — the single-file Arduino IDE version, kept byte-for-byte in
  sync with `src/` for people who build in the Arduino IDE.

PlatformIO only compiles the `src/` directory, so the `.ino` in the project root
is ignored and there's no conflict. **Do not move `CardSat.ino` into `src/`** —
it also defines `setup()`/`loop()`, and having both in `src/` would cause
duplicate-symbol link errors. Leave it where it is.

## 5. Pin the ESP32 core version (the critical step)

Open `platformio.ini`. The stock file starts its environment like this:

```ini
[env:cardputer_adv]
platform        = espressif32
board           = m5stack-stamps3
framework       = arduino
```

With `platform = espressif32` (unpinned), PlatformIO pulls the **latest**
platform, which now installs an arduino-esp32 **3.x** core — the one that dropped
`setBufferSizes()`, so the build fails to compile, or (worse) compiles against a
3.x package and reproduces the heap-fragmentation TLS failures.

Pin the platform to the **6.x** line, whose last release (6.9.0) ships the
**2.0.17** core that still has `setBufferSizes()`:

```ini
[env:cardputer_adv]
platform        = espressif32 @ 6.9.0
board           = m5stack-stamps3
framework       = arduino
```

That one change is the whole point of the move: it gives you a reproducible,
known-good core where the firmware's TLS buffer trimming compiles and works.

> **Why 6.x?** PlatformIO's `espressif32` platform version is *not* the same
> number as the arduino-esp32 core it bundles. Platform **6.x → core 2.0.x**
> (has `setBufferSizes`); platform **7.x / develop → core 3.x** (does not). If
> you ever want to confirm exactly which core a platform version ships, the
> PlatformIO platform release notes list it.

If you'd rather track the core explicitly instead of relying on the platform
version, you can also force it:

```ini
platform         = espressif32 @ 6.9.0
platform_packages =
    platformio/framework-arduinoespressif32 @ https://github.com/espressif/arduino-esp32.git#2.0.17
```

Either form is fine; the simple `@ 6.9.0` pin is enough for CardSat.

## 6. Build

Two equivalent ways to compile:

- **Toolbar:** click the checkmark (✓) icon in the blue status bar at the bottom
  of VS Code.
- **Terminal:** open the PlatformIO terminal (the terminal icon in the status
  bar, or **Terminal → New Terminal**) and run:

  ```bash
  pio run
  ```

The first build is slow while it compiles the whole core and libraries; later
builds only recompile what changed. A successful build ends with a `SUCCESS`
banner and a flash/RAM usage summary.

If `setBufferSizes` throws a *"no member named setBufferSizes"* compile error,
the platform pin didn't take effect and you're still on a 3.x core — re-check the
`@ 6.9.0` in `platformio.ini` and rebuild.

## 7. Connect the Cardputer and find its port

1. Plug the Cardputer ADV into the Mac with a USB-C cable. Use a **data** cable,
   not a charge-only one.
2. The StampS3 uses the ESP32-S3's built-in USB, so recent macOS usually needs no
   driver. To confirm the port is visible:

   ```bash
   pio device list
   ```

   Look for something like `/dev/cu.usbmodem*` (native USB-S3) or
   `/dev/cu.usbserial*` / `/dev/cu.wchusbserial*` (if it enumerates as a UART
   bridge). PlatformIO auto-detects the port, so you normally don't need to set
   it — but if detection ever picks the wrong one, add
   `upload_port = /dev/cu.usbmodemXXXX` to `platformio.ini`.

   If nothing shows up and the board uses a CH34x UART bridge, install the WCH
   `CH34xSER` macOS driver, then re-check.

## 8. Flash (upload) the firmware

- **Toolbar:** click the right-arrow (→) icon in the status bar.
- **Terminal:**

  ```bash
  pio run -t upload
  ```

The StampS3 normally enters the bootloader automatically. If an upload ever fails
to start, hold the **G0/BOOT** button while it begins, then release — but with the
S3's native USB this is rarely needed.

## 9. Open the serial monitor

CardSat logs at 115200 baud (the `[net] …` lines you've been reading).

- **Toolbar:** click the plug icon in the status bar.
- **Terminal:**

  ```bash
  pio device monitor -b 115200
  ```

`platformio.ini` already sets `monitor_speed = 115200`, so `pio device monitor`
alone works too. Press `Ctrl+C` to exit the monitor. To build, upload, and
monitor in one shot:

```bash
pio run -t upload -t monitor
```

## 10. Quick command reference

| Action            | Toolbar icon | Command                         |
|-------------------|--------------|---------------------------------|
| Build             | ✓ checkmark  | `pio run`                       |
| Upload (flash)    | → arrow      | `pio run -t upload`             |
| Serial monitor    | 🔌 plug       | `pio device monitor -b 115200`  |
| Upload + monitor  | —            | `pio run -t upload -t monitor`  |
| Clean build       | 🗑 trash       | `pio run -t clean`              |
| List USB ports    | —            | `pio device list`               |

---

## The LittleFS data partition

`platformio.ini` sets `board_build.filesystem = littlefs`. CardSat stores its
caches (TLEs, transponders, config) on flash. If you keep any seed files in a
`data/` folder and want them on the device, build and upload the filesystem image
separately:

```bash
pio run -t buildfs
pio run -t uploadfs
```

CardSat creates its own files at runtime, so you usually don't need this unless
you're pre-loading data.

## Looking further ahead: staying on core 3.x

If you later want to move to the 3.x core for its newer features, the heap
problem `setBufferSizes()` solved comes back — and the sketch can't fix it,
because 3.x removed the method. The proper fix there is the ESP-IDF mbedTLS
option **`CONFIG_MBEDTLS_DYNAMIC_BUFFER`**, which frees the large TLS buffers
between connections so they don't fragment the heap.

The catch: under the **Arduino** framework, mbedTLS ships as a *precompiled*
library, so this option can't be flipped from `build_flags` or a project
`sdkconfig` — it requires either the ESP-IDF framework or rebuilding the core.
That's a much bigger project than pinning to 2.0.x. For now, the 6.9.0 / 2.0.17
pin in step 5 is the simple, reliable path, and it matches the firmware that
already works on your hardware.
