# CardSat v0.9.16 — Release Notes

A focused bug-fix release for **Manual mode** (the no-radio frequency calculator),
correcting the uplink/downlink math when working a **linear transponder full
duplex** and holding one leg fixed.

> **Hardware status.** The fix is host-verified: the new frequency math is checked
> against an independent round-trip Doppler model across the full range-rate span,
> for both inverting and non-inverting transponders, holding the operator on their
> own signal to within a few Hz. It has not yet been confirmed on the air. A quick
> on-air check (park one leg, work yourself across a pass) would close the loop —
> see the note below.

---

## Manual mode: round-trip Doppler when holding one leg fixed (bug fix)

On the Manual screen you fix one leg — the frequency you park on your own radio —
and CardSat shows the frequency to tune the **other** leg to. For a **linear**
transponder worked full duplex, the goal is to keep hearing **yourself** on a
stationary frequency while the satellite moves. The previous math handled each leg
independently (the same satellite-frame convention CardSat uses when it *drives* a
real radio, where you let the downlink drift and chase it). That is the wrong
convention for the Manual calculator's "park one leg, hear yourself" use: it does
not account for the **round trip**.

The physics: on a linear bird, where your own signal lands on the downlink depends
on where the satellite *heard* your uplink. So holding one leg stationary requires
the other leg to cancel **both** Doppler shifts, not just its own. With the old
per-leg math, the operator's own signal drifted off the fixed leg by:

- up to **~3 kHz** at ±7 km/s when **holding the downlink** (tuning the uplink), and
- up to **~10 kHz** at ±7 km/s when **holding the uplink** (tuning the downlink) —
  larger because, on an inverting transponder, the fixed-uplink Doppler and the
  downlink Doppler add rather than partly cancel.

Either was enough to walk you out of an SSB passband mid-pass.

**Both directions are now corrected.** Two new predictor helpers,
`uplinkForFixedDownlink()` and `downlinkForFixedUplink()`, compute the derived leg
from the round trip and hold the operator on their own signal to within a few Hz
across the pass, for both inverting and non-inverting transponders, with
per-satellite calibration applied. Toggle which leg is fixed with **`u`** as
before.

Unaffected on purpose:

- **FM birds** — the uplink and downlink are independent channels (no shared
  passband), so the plain per-leg Doppler is correct; no round-trip term is
  applied.
- **The Track screen** (driving a real radio) — it intentionally fixes a point in
  the *satellite* passband and Doppler-corrects both legs around it, letting the
  downlink drift on the ground while the operator's knob (or CardSat) follows it.
  That convention is correct for live radio control and is unchanged.

---

## Notes

- This release changes only Manual-mode display math and the firmware version
  string; nothing else from 0.9.15 is altered.
- Still worth an on-air confirmation: park one leg on a linear bird (e.g. an
  inverting SSB transponder) and check that you keep hearing yourself on the fixed
  frequency as the pass develops. The math is verified against a round-trip model
  but has not been flown.
