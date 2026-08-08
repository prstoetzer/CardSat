# CardSat companion firmware

Standalone sketches that run on their own hardware and work *with* CardSat.

## CardSatUsbHelper (`CardSatUsbHelper/`)

A **second USB host** on the end of a Grove cable, so a USB radio or rotator can be
attached without spending any of the Cardputer's own USB host channels.

The ESP32-S3 has eight host channels for the whole bus, one per open pipe. A hub
costs 2, a CDC radio 3, and the IC-705 — which contains its own internal hub —
costs 5 by itself. So `hub + TH-D75 + IC-705` needs ten, and no arrangement of
eight fits it. A second microcontroller brings its own eight.

CardSat still owns everything: every CAT dialect, all rotator grammar, all the
Doppler logic. The helper is a **byte pipe** that moves bytes, manages the CDC line
state, and reports what is plugged in. It stores nothing across reboots.

Select it in CardSat as CAT type **USB helper (Grove)**, as a dual-rig leg bus
**Helper**, or as the rotator wire **USB helper**. One device at a time — it is a
single exclusive resource and CardSat refuses a second claimant.

See [`CardSatUsbHelper/README.md`](CardSatUsbHelper/README.md) for wiring, power and
build, and [`../docs/interfaces/CSUH_PROTOCOL.md`](../docs/interfaces/CSUH_PROTOCOL.md)
for the wire protocol.

### Status

The firmware compiles clean and its link layer is verified host-side — the codec
against reference vectors, and the *shipped* CardSat client run against a mock
helper covering handshake, enumeration, flow control under load, and reboot
recovery. **It has not yet been run on a real M5StickS3 with a radio attached.**
Bench findings are very welcome; they are worth more than any amount of review.

`tools/helper_probe.py` speaks CSUH from a Mac over a USB-serial adapter, so the
Stick can be exercised with CardSat out of the loop — which is the fastest way to
tell a firmware problem from a wiring problem.

## CardSatDualRig — retired in 0.9.73

The previous companion was a rigctld server that owned CAT state for two radios and
carried its own radio catalogue. Once `CAT_DUAL` landed natively in CardSat (0.9.68)
that duplication bought nothing: every radio fix had to be made twice and kept
honest across two release cycles. It was never run on hardware by anyone.

It is gone, along with the `\csdr_*` configuration escape it needed. **The generic
rigctl transports are unaffected** — CAT type `rigctl (net)` and `rigctl (Grove)`
still drive any Hamlib rigctld, which is what they were always for.

## USB host library — use the patched copy

Both CardSat and the helper drive **EspUsbHost** and inherit the same defects. Build
against the patched copy vendored at `third_party/EspUsbHost/`, not the stock
library from the Library Manager. `third_party/EspUsbHost/PATCHES.md` lists every
patch and how to re-apply it after an upstream bump.

The two that matter most here:

* **CDC serial OUT drain.** `sendSerial()` submits its transfer without recording it
  in the library's endpoint table, so a radio that stops reading its port leaves the
  transfer enqueued forever. `usb_host_interface_release()` then refuses the
  interface, which blocks the device close, the client deregistration and the host
  uninstall in turn — and the USB stack is stranded until a reboot.
* **`ESPUSBHOST_CLAIM_AUDIO` defaults to 0.** Not claiming a composite radio's audio
  interface is what keeps it from spending the very channels the helper exists to
  conserve.
