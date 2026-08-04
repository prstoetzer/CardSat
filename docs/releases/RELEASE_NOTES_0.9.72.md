# CardSat 0.9.72 release notes

**A USB release.** The USB host stack is now compiled from source instead of taken
from Arduino's prebuilt library, which made three long-standing problems fixable and a
fourth one finally diagnosable. The headline: **the IC-705 works over USB CAT**, for
the first time.

---

## What changed for the operator

### The IC-705 works over USB

Plugged into a **self-powered hub**, the IC-705 enumerates and takes CAT. Confirmed on
the bench with real CI-V traffic: set-mode and set-frequency both acknowledged, tracking
an AO-7 Mode A downlink at 29.44958 MHz.

**It must go through a powered hub.** Connected directly to the Cardputer the radio does
not even show `USB COM` on its own display, so it never sees a valid host — this is a
power-delivery limit of the Cardputer's port, not a firmware problem, and turning off the
radio's USB power input does not change it. Nothing in CardSat can work around it.

### Scanning no longer gives up too early

A cold scan that found nothing used to report **"no adapters found"** while the USB stack
was still settling — and then enumerate successfully several seconds later, with nobody
watching. That is the entire explanation for the "unplug the hub, scan, plug it back in"
folklore: re-plugging simply took longer than the settling did.

A scan that comes up empty now watches the live host for a further 12 seconds and picks
up anything that arrives late:

```
scan: nothing yet - watching for late enumeration
scan: late enumeration after 8400 ms
```

**If you have been power-cycling your hub to make it appear, stop. It was never
necessary.**

### Scanning works after a disengage

`usb_host_uninstall()` was returning `ESP_ERR_INVALID_STATE` because a queued library
event was never consumed, leaving the host installed. Every scan after a CAT disengage
then reported "host would not start" until the Cardputer was rebooted. The event queue is
now drained before uninstalling, with one retry.

### The adapter list tells the truth

An unplugged adapter kept appearing in the list, the "N seen" row kept counting it, and a
stored key still resolved to its label as though it were present. Slot count and live
count were the same number before the registry started tombstoning unplugged entries;
they are not the same number now, and every emptiness test used the wrong one.

### One more USB device fits

CDC-ACM control interfaces are detected but no longer claimed. They carry only the
notification endpoint, which CardSat never reads, and claiming one costs a host channel.
See the channel budget below for why that mattered.

---

## The channel budget (read this before planning a setup)

The ESP32-S3 has **8 DWC host channels** for the entire bus. One is consumed per open
pipe, including each device's default control pipe:

| | Channels |
| --- | --- |
| Hub | 2 (EP0 + status interrupt) |
| CDC radio (TH-D75, IC-705) | 3 (EP0 + bulk pair) |
| Vendor-serial adapter (Prolific) | 4 (EP0 + interrupt + bulk pair) |
| FTDI adapter | 3 (EP0 + bulk pair) |

**The IC-705 contains its own internal hub**, fanning out its CDC device and a
Burr-Brown audio CODEC. It therefore costs **5**, not 3.

Practical consequences:

* **Powered hub + IC-705** = 7 of 8. Works.
* **Powered hub + TH-D75 + a 3-channel adapter** = 8 exactly. Fits, with no headroom.
* **Powered hub + TH-D75 + IC-705** = 10. **Does not fit, and no software change can
  make it fit.** Use the IC-705's own Wi-Fi CAT for that pairing.

The IC-705's audio CODEC declares a 1191-byte configuration descriptor against IDF's
256-byte fetch limit, so it can never enumerate. This is harmless — it fails on its own
hub port and leaves the radio alone.

---

## Known issues

* **An FTDI FT232R that works perfectly on the Cardputer's own port can still fail behind
  a hub**, at the first control transfer (`CHECK_SHORT_DEV_DESC`, transfer status 1).
  Enumeration now retries the request three times before giving up; on the bench adapter
  this failure was deterministic and the retries did not rescue it. A Prolific adapter in
  the same hub port works. Worth re-testing with this release, since late-enumeration
  watching may cover some cases that previously looked like hard failures.
* **The IC-705 cannot be used directly**, only through a powered hub. See above.
* Heap headroom is tight with a hub plus two devices — the largest free block drops to
  around 11 KB. Watch it if you add features.

---

## Under the hood

The ESP-IDF USB Host component is now vendored as an Arduino library and compiled with
the sketch, so `CONFIG_USB_HOST_*` options and the stack's own `ESP_LOGD` narration can be
chosen at build time. Arduino's prebuilt `libusb.a` contributes **zero** symbols to the
link. See `docs/design/USB_HOST_VENDORING.md` for why, how, and the traps.

This replaces what would otherwise have required rebuilding Arduino's ESP-IDF libraries
with `esp32-arduino-lib-builder` — hours of build time and about 12 GB of toolchain, on
every core bump.
