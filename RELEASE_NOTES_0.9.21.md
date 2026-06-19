# CardSat v0.9.21 — Release Notes

An IR pass-alert beacon: the built-in IR LED now flashes on each pass-alert event,
with a distinct number of flashes per event, so you can build your own
IR-triggered hardware.

> **Hardware status.** Host-verified only (tokenizer-balanced; the carrier timing
> and flash-count state machine compiled with mocks and checked off-device). The
> actual IR output and a receiver decoding it have not been confirmed on a device —
> verify before relying on it.

---

## IR pass beacon (build-your-own trigger)

Enable **Settings → Station → IR pass beacon** (off by default) and CardSat will
flash the Cardputer's built-in IR LED (GPIO 44) on every existing pass-alert event,
*in addition to* the usual beeps. Each event sends a distinct number of flashes:

| Event | Flashes |
|---|---|
| T-60 s to AOS | 1 |
| T-30 s to AOS | 2 |
| T-10 s to AOS | 3 |
| AOS (pass start) | 4 |
| TCA (peak elevation) | 5 |
| LOS (pass end) | 6 |

Each flash is a ~60 ms burst of standard **38 kHz IR carrier** with ~140 ms gaps —
the kind any common IR receiver/demodulator (TSOP38238, TSOP4838, etc.) detects.
CardSat only transmits the counts; **the receiving side is yours to design.** Point
a 38 kHz receiver at the Cardputer, count the pulses, and trigger whatever you want:
power up a rotator/preamp at T-10, flash a shack light at AOS, start an SDR
recording, drive a louder external alert, or log events on a second microcontroller.

The flashing is fully **non-blocking** — one burst at a time between the normal
tracking updates — so radio, rotator, and web control keep running throughout. It's
gated by the AOS-alarm system, so it only fires when the AOS alarm is on.

> Host-verified only. The 38 kHz carrier, duty cycle, and burst/gap timing may need
> tuning for your particular receiver; treat the flash *counts* as the stable
> contract and the exact waveform as adjustable. The feature uses only GPIO 44 (the
> built-in IR LED) and is off until you enable it.

---

## Notes

- All host-verified only. The IR carrier generation (LEDC PWM at 38 kHz, gated per
  burst) and the flash state machine were checked off-device (balance + g++
  semantic compile + a mock-clock run of every flash count), but on-device
  confirmation — that a real IR receiver sees and counts the bursts — is still
  needed.
