# CardSat companion firmware

Standalone sketches that work *with* CardSat but run on their own hardware.

## CardSatDualRig (`CardSatDualRig/`)

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
