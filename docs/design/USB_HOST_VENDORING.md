# Vendoring the ESP-IDF USB Host component

**Read this before touching anything USB, and before bumping the Arduino core.**

CardSat compiles the ESP-IDF USB Host stack **from source**, as an Arduino library, so
that Arduino's prebuilt `libusb.a` is never linked. Without this, three settings that
0.9.72 depends on are unreachable.

---

## 1. Why

Arduino ships ESP-IDF as prebuilt static libraries. Their build configuration is fixed,
and three of its values block work CardSat needs:

| Setting | Arduino's value | Why it matters |
| --- | --- | --- |
| `CONFIG_LOG_MAXIMUM_LEVEL` | 1 (ERROR) | Every `ESP_LOGD` in the USB stack is compiled out. Enumeration failures are silent, which is why the USB problems went undiagnosed for so long. |
| `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK` | not set | `enum_filter_cb` exists in the struct but the feature is compiled out, so a callback is ignored. |
| Root-port reset timings | fixed at 30 ms | `CONFIG_USB_HOST_RESET_HOLD_MS` / `_RECOVERY_MS` cannot be raised for a slow hub. |

The USB Host Library is distributed by Espressif as a standalone component
(`espressif/usb`), removed from ESP-IDF core in 6.0, and explicitly supports overriding
the copy bundled with 5.x. Compiling it with the sketch puts all of it under our control.

**The alternative is `esp32-arduino-lib-builder`** — rebuilding Arduino's libraries with
a custom `sdkconfig`. It reaches more settings and needs no version pin, but costs hours
of build time and ~12 GB, on every core bump. `rebuild_arduino_libs.sh` in the tools
directory does it if ever needed.

## 2. How

```bash
./tools/vendor_usb_host.sh            # install
./tools/vendor_usb_host.sh --verify   # check an existing install
./tools/vendor_usb_host.sh --remove   # revert to the prebuilt libusb.a
```

It downloads the ten component sources plus headers, applies `usb-host-cardsat.patch`,
installs `cardsat_usb_log.h`, and writes an Arduino library into the sketchbook.

### The version pin, which is not optional

```
IDF_COMMIT=858a988d6e
```

This must match the esp-idf commit the **installed core's** libraries were built from.
Read it from the core:

```bash
grep esp-idf ~/Library/Arduino15/packages/esp32/tools/esp32-arduino-libs/*/versions.txt
```

`usb_dwc_hal.c` lives in `libhal.a`, **not** in the usb component, so it cannot be
overridden. A component newer than the core's HAL fails to compile with
`implicit declaration of usb_dwc_hal_set_fifo_config` — which is exactly how the
registry's current release behaves against this core. **Bumping the Arduino core means
re-reading `versions.txt` and re-running the script.**

## 3. The CardSat edits

Applied by patch, so they stay reviewable and a re-install is reproducible.

* **`enum.c` — `CARDSAT_ENUM_STAGE_RETRIES` (3).** IDF abandons a device the first time
  a control transfer fails. Each `CHECK_*` stage is preceded by its own `GET_*` stage, so
  stepping back one stage re-issues the request. Restricted to the six descriptor and
  address check stages; other stages have no request to repeat.
* **`enum.c` — `CARDSAT_ENUM_INITIAL_MPS_FS` (64).** Initial EP0 packet size. Left at the
  stock value: 8 was tried and disproved.
* **`hcd_dwc.c` — `CARDSAT_USB_RESET_HOLD_MS` / `_RECOVERY_MS`.** The stock values come
  from `sdkconfig.h`, so a plain `-D` clashes; hence the indirection. Shipping at 60 and
  400 ms against stock 30 and 30.
* **`cardsat_usb_log.h`.** Re-points `ESP_LOGD`/`W`/`I`/`V` in these files at
  `ESP_LOG_ERROR`, keeping the severity as a text marker. Necessary because
  `esp_log_level_set()` lives in the prebuilt `liblog.a` and will not raise a tag above
  the level that library was built for — the narration is generated and then dropped.
  Opt in with `-DCARDSAT_USB_VERBOSE=1`.

## 4. Traps that cost real time

**arduino-cli will silently not compile the library.** It matches `#include` directives
against library headers, and `usb/usb_host.h` is already satisfied by the platform
include path, so the resolver never attributes it here. The build succeeds, the binary
barely changes, and nothing has been overridden. `UsbHostSrc.h` exists solely to force
discovery, and the sketch must `#include <UsbHostSrc.h>`.

**`build_opt.h` does not invalidate cached objects.** It is not a dependency of any
`.o`, so changing a flag and rebuilding produces a byte-identical binary that looks
exactly like the flag having no effect. **Delete the sketch build cache after any
`build_opt.h` change.**

**Verify the override happened. Do not assume it.**

```bash
grep -c 'libraries/UsbHostSrc' build/*.map    # expect hundreds
grep -c 'libusb\.a('          build/*.map     # expect 0
```

## 5. Build flags

In `build_opt.h`:

```
-DESP_USB_HOST_MAX_DEVICES=4
-mtext-section-literals
-DCARDSAT_ENUM_STAGE_RETRIES=3
-DCARDSAT_USB_RESET_HOLD_MS=60
-DCARDSAT_USB_RESET_RECOVERY_MS=400
-DCONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=1
```

Add for a diagnostic build:

```
-DCARDSAT_USB_DIAG=1
-DCARDSAT_USB_VERBOSE=1
```

`CARDSAT_USB_VERBOSE` produces a great deal of text. Raise `USBDIAG_CAP` (16384 in
0.9.72) or expect the ring to truncate; the capture reports dropped bytes, so check that
count before trusting a quiet log.
