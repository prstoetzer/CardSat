# CardSat v0.9.17 — Release Notes

Three operator-facing additions: a large-font operating readout, an adjustable
screen brightness, and opt-in accelerometer (tilt) tuning.

> **Hardware status.** The readout and brightness controls are host-verified
> (tokenizer-balanced, logic-checked) but have not been exercised on a device this
> release. Tilt tuning is **opt-in and ADV-only**; its logic is host-tested with a
> mocked IMU (the passband moves and clamps correctly), but the live sensor path
> and the feel of the rate curve have **not** been confirmed on hardware — expect
> to fine-tune the dead-zone and rate to taste.

---

## Large-font operating readout (press `z` on Track)

A stripped-down, glanceable view for working a pass without squinting at the small
type. It shows the **RX** and **TX** frequencies in large digits, **Az/El** below
them, and a big **LOS** countdown — or an **AOS** countdown when the bird is below
the horizon, or **\*\* WORKABLE \*\*** when it's up. Small badges show **RAD** /
**ROT** (radio and rotator state), **TILT** if tilt tuning is armed, and the
transponder index.

The radio, rotator and Doppler tracking keep running exactly as on Track — this is
purely an alternate view of the same live session (the tracking service is gated on
whether radio/rotator output is on, not on which screen is showing). `t` (next
transponder), `r` (radio), `o` (rotator) and `l` (log a QSO) all work without
leaving the readout. Press `z` or `` ` `` to return to Track.

(Note: `z` was chosen because `b` and `h` are global hotkeys — screenshot and
help — and never reach the per-screen handlers.)

## Screen brightness setting

The active backlight level is now adjustable under *Settings → Station / display →
Brightness* (`,`/`/` in ~6% steps, with a live preview). It's saved with the rest
of the configuration and re-applied at boot and whenever the display wakes from the
sleep timeout. Previously the brightness was a fixed compile-time constant.

## Accelerometer (tilt) tuning — opt-in, ADV only

The Cardputer **ADV** carries a motion sensor (the original Cardputer does not).
With **Tilt tuning** switched on under *Settings → Station / display*, you can roll
the device left/right to move through a linear transponder's passband instead of —
or alongside — the `,`/`/` keys.

It's deliberately a **rate** control rather than an absolute mapping, which is far
steadier to hold by hand: a gentle tilt nudges slowly for fine work, a firmer tilt
slews faster, and holding the device level holds the frequency. A few-degree
dead-zone around level keeps a hand-held device from drifting, the reading is
low-pass filtered to tame sensor noise, and the rate saturates past roughly 35°.

The feature is **off by default** and only acts on the **Track** and **large-font**
screens, in **TUNE** mode, on a **linear** bird; everywhere else it does nothing.
A **TLT**/**TILT** marker shows when it's armed, and you can flip it on/off mid-pass
with **`y`** on either screen without opening Settings. On a board without the
sensor the setting reads **n/a (no IMU)** and can't be enabled, so nothing changes
for original-Cardputer users. It's offered as an option, not a default — tilting
the device also moves your antenna and your eyes, so many operators will still
prefer the keys.

---

## Notes

- All three are host-verified only; confirm on hardware before relying on them
  during a pass. In particular the tilt rate curve (≈8 kHz/s at full tilt, ~7°
  dead-zone) is a first guess and will likely want adjustment once you feel it on
  a real ADV.
