# CardSat v0.9.22 — Release Notes

> **v0.9.22 supersedes v0.9.21, which was broken.** If you flashed 0.9.21, please
> update. 0.9.22 fixes two regressions that made 0.9.21 effectively unusable on the
> Cardputer ADV (see "Critical fixes" below). The feature content is otherwise the
> same as 0.9.21 (the IR pass beacon, described further down).

## Critical fixes (v0.9.22)

- **Voice memo now records on the Cardputer ADV.** The ADV's ES8311 mic codec is
  driven through M5Unified's `M5.Mic` with `cfg.internal_mic` enabled, built against
  ESP-IDF 5.4.x (Espressif esp32 Arduino core 3.2.x). See the note at the end and
  MANUAL.md §"Voice memo" for the toolchain requirement.

- **Downloads no longer freeze the screen.** On the ESP-IDF 5.4.x toolchain (required
  for the mic), the display canvas was being freed to make heap room for the TLS
  handshake and then could not be reallocated — the 64 KB block never returns on this
  toolchain — leaving the whole device frozen after any update. The display canvas is
  now an **8 bpp palette sprite** (~32 KB instead of ~64 KB), which leaves enough free
  heap (~85 KB) for the handshake with the sprite kept allocated the whole time. The
  sprite is never freed/recreated, so the screen can't freeze, and a real RGB565
  palette keeps colors correct. Net result: updates download and the UI stays live and
  full-color throughout.

---

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

- **Cardputer ADV voice memo works when built against ESP-IDF 5.4.x.** The ADV's
  ES8311 mic codec records correctly on the **Espressif esp32 Arduino core 3.2.x**
  (IDF 5.4.2); on the 3.3.x core (IDF 5.5.x) the codec's record clock isn't driven
  and recordings are silent — an upstream regression
  ([espressif/esp-idf#18621](https://github.com/espressif/esp-idf/issues/18621)),
  not a CardSat bug. Voice memo captures via M5Unified's `M5.Mic` with
  `cfg.internal_mic` enabled. Only voice memo is affected by the toolchain; every
  other feature builds and runs on either core. The boot log's IDF version (5.4.x)
  confirms a correct build. See MANUAL.md §"Voice memo".

- All host-verified only. The IR carrier generation (LEDC PWM at 38 kHz, gated per
  burst) and the flash state machine were checked off-device (balance + g++
  semantic compile + a mock-clock run of every flash count), but on-device
  confirmation — that a real IR receiver sees and counts the bursts — is still
  needed.
