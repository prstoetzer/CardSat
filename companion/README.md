# CardSat companion firmware

Standalone sketches that work *with* CardSat but run on their own hardware.

## CardSatDualRig (`CardSatDualRig/`)

> ⚠️ **EXPERIMENTAL — never tested on hardware.** This firmware compiles clean and its
> logic has been reviewed and carried through several CardSat audits, but **no one has
> yet run it with two radios on a real M5StickS3.** Treat it as a starting point for
> someone who has the hardware, not as a finished product. If you try it, findings are
> very welcome — bugs found on a bench are worth more than any amount of code review.
>
> The 0.9.70 USB work below was applied here by inspection, from defects proven on
> CardSat's own hardware. That makes it well-founded, but it is still unverified on
> this board.

A rigctld server for the **M5StickS3** that bridges CardSat to **two half-duplex or
receive-only radios** over USB, so you can work a linear-transponder satellite pass
with one radio on the downlink and another on the uplink — the pair acting as one
"full-duplex" rig from CardSat's point of view.

CardSat drives it either over Wi-Fi (CAT type **rigctl (net)**) or over a Grove cable
(CAT type **rigctl (Grove)**, added in 0.9.62 — no Wi-Fi needed). The Stick can also
be configured from CardSat over the same link via the `\csdr_*` escape.

See `CardSatDualRig/README.md` for the full build, wiring, supported-radio list, and
the honest not-yet-hardware-tested status. It is a separate Arduino sketch: open the
`CardSatDualRig/` folder in the Arduino IDE, not the CardSat root.

## USB host library — use the patched copy (0.9.70)

This firmware uses **EspUsbHost**, the same library CardSat drives, and inherits the
same defects. Build against the patched copy vendored at
`third_party/EspUsbHost/` in the CardSat repository rather than the stock library from
the Library Manager. `third_party/EspUsbHost/PATCHES.md` lists every patch, why it
exists, and how to re-apply it after an upstream version bump.

The one that matters most for a dual-rig companion:

* **CDC serial OUT drain.** `sendSerial()` allocates a transfer, submits it and forgets
  it — the transfer is never recorded in the library's endpoint table. If a radio stops
  reading its port (switched off, or its CAT application not running), that transfer
  stays enqueued in the pipe indefinitely. `usb_host_interface_release()` then refuses
  the interface that owns the endpoint, which blocks the device close, the client
  deregistration and the host uninstall in turn — and the USB stack is stranded until
  a reboot. The library already drains audio-out and vendor-out endpoints this way;
  serial-out was simply missed.

Also applied to this firmware directly in 0.9.70: **DTR is now de-asserted when a leg
releases a radio.** On a CDC-ACM device DTR is what tells the device the host has the
port open, and there is no other close notification. This firmware asserted DTR at bind
and never dropped it, so a radio keying its CAT session off DTR believed the session was
still open after the leg unbound — on a TH-D75 that meant CAT could not be
re-established without power-cycling the radio.
