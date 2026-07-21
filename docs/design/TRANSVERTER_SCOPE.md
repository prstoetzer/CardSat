# Scoping: transverter functionality

*Status: proposal / scope only. No code. Written against the 0.9.62 source.*

## 1. What a transverter is, and why CardSat needs the concept

A transverter lets a radio that only tunes a lower "IF" band operate on a much higher
band. The rig tunes an intermediate frequency (commonly 28 MHz, 144 MHz, or 432 MHz); the
transverter mixes that against a fixed local-oscillator (LO) frequency to produce the real
on-air frequency. Example: an FT-817 on 144 MHz through a 144→10368 MHz transverter has an
LO of 10224 MHz (10368 − 144). The operator thinks in the *real* frequency; the rig must be
told the *IF* frequency.

Two facts make this directly relevant to CardSat today:

- **The satellite frequencies CardSat already knows are on the real band.** QO-100's NB
  transponder is 2400.25 MHz up / 10489.75 MHz down. Es'hail-2, the 10 GHz and 24 GHz
  weak-signal/EME segments, and 5.7 GHz all live above what a typical sat rig tunes
  natively. To drive a rig for these, CardSat must convert real → IF before sending CAT,
  and IF → real after reading CAT back.
- **CardSat has no IF concept at all.** Every frequency in the code is the real on-air
  value, sent straight to the rig. There is no place to express "the rig is 10224 MHz low
  on the downlink." So higher-band satellite work is not currently drivable end to end.

This scope also underpins the sibling proposal
[`HIGH_FREQ_SCOPE.md`](HIGH_FREQ_SCOPE.md): a transverter is the *practical* way most hams
reach > 4.2 GHz, and it sidesteps the frequency-type limit described there (the rig only
ever sees a sub-GHz IF).

## 2. What exists to build on

- **Frequencies are `uint32_t` Hz everywhere** (`Transponder.downlink/uplink`, the
  `Predictor` Doppler signatures, the `Rig` CAT interface, the app tune path). uint32 tops
  out at ~4.29 GHz. Doppler math is already promoted to `int64_t` in the hot spots
  (`app.cpp` ~4785, and the manual-tune stepping) to avoid overflow, but *storage and the
  CAT calls* are uint32.
- **Per-radio metadata lives in `RadioProfile`** (`radio_profiles.h`): CI-V address,
  default baud, band-select bytes, sat-mode command, tone sub-command, read-back capability.
  There is **no band-range or IF field** — a transverter is new metadata, not a tweak.
- **The CAT wire can already carry high frequencies.** CI-V uses 5 BCD bytes (10 digits →
  ~10 GHz); the limit is the `uint32_t` argument to `freqToBcd`, not the protocol. Yaesu
  BCD is 4 bytes of hz/10 (~1 GHz max), which is exactly the sub-GHz IF a transverter user
  drives — so Yaesu is well suited as a transverter IF rig.
- **Calibration offsets already exist** (`calDl`/`calUl`, per-satellite, persisted). These
  are small trim offsets added to the computed dial. A transverter LO is conceptually a
  *large* offset, but the plumbing that applies `calDl`/`calUl` at the point of driving the
  rig is the natural place to also apply an LO shift — so part of the mechanism is present.

## 3. Proposed model

Keep all satellite frequencies stored as the **real on-air** value (unchanged). Introduce a
**band → transverter** mapping applied only at the boundary where CardSat drives/reads the
rig:

- **IF sent to rig** = real − LO (for a high-side... actually low-side LO: `IF = real − LO`).
- **Real from rig read-back** = IF + LO.
- A transverter is defined by: an **input (IF) band**, an **LO frequency**, an optional
  **inversion flag** (some transverters invert the spectrum), and a **PTT/enable** note.
  Because sat work is often split-band (e.g. 2.4 GHz up on one transverter, 10 GHz down on
  another), the mapping is **per-leg**: the downlink and uplink can each have their own
  transverter (or none).

Selection can be automatic (choose the transverter whose real-band range contains the
transponder's frequency) with a manual override. Given CardSat's audience, a small fixed set
of user-defined transverter slots (say 2–4) covering the common bands (2.3/2.4 GHz, 3.4 GHz,
5.7 GHz, 10 GHz, 24 GHz) is enough; a full editable table is not required for v1.

The display should show the **real** frequency (what the operator cares about) with a marker
that a transverter is in use, while the value actually sent to the rig is the IF.

## 4. Interaction with existing subsystems

- **Doppler.** Doppler shift is a property of the *real* RF path and must be computed on the
  real frequency, then the LO subtracted to get the IF dial. Applying Doppler to the IF is
  wrong (it would under-correct by the ratio real/IF, which at 10 GHz vs 144 MHz is ~70×).
  This is the single most important correctness point: **Doppler first on real, LO last.**
- **Calibration.** `calDl`/`calUl` are trims on the real dial and should continue to apply
  on the real side before LO subtraction. An LO also has its own error (a 10 GHz LO off by
  a few kHz is common); whether to fold LO trim into the existing per-sat cal or give the
  transverter its own trim is a design choice — a per-transverter LO trim is cleaner.
- **Band/VFO assignment.** On a duplex rig (IC-9700) the two legs are MAIN/SUB. With
  transverters the IF bands may both be, say, 144 MHz — so the rig sees two 144 MHz VFOs and
  the transverters separate them in RF. The existing MAIN/SUB assignment logic is unaffected
  (it works on IF), but the reference/band-plan screens that assume the dial *is* the band
  need to be aware of the mapping.
- **CAT read-back and the status API.** `rxRead`/`txRead` (0.9.62) read the rig, i.e. the
  IF. To report the real frequency they must add the LO back. The web/status layer needs to
  know whether a value is IF or real.
- **The `> 4.2 GHz` limit.** With a transverter the rig only ever sees the IF (sub-GHz), so
  **the uint32 limit is not hit on the CAT path** even for 24 GHz work. But the *real*
  frequency CardSat stores and displays (10489.75 MHz = 1.049e10 Hz) still overflows uint32.
  So a transverter feature that displays the real frequency **still needs the wider
  frequency type** from `HIGH_FREQ_SCOPE.md` for storage/display, even though the rig side
  stays 32-bit. The two proposals are complementary: high-freq type = store/display;
  transverter = drive the rig.

## 5. Memory cost

Small. The additions are configuration and a boundary transform, not bulk data.

- **Config (`Settings`, in `.bss` via the global `App`):** each transverter slot needs an LO
  frequency (8 bytes if real-Hz is widened to 64-bit, else 4), an IF-band tag, an
  inversion + enable bool, and an optional LO trim (4 bytes). Call it ~16 bytes per slot;
  2–4 slots → **~32–64 bytes**. Two `uint8_t` per-leg "which transverter" selectors add
  ~2 bytes. Persisted in the existing config JSON — a handful of new keys, no new file.
- **Code (flash):** the boundary transform (apply/remove LO on drive and read-back), the
  auto-select-by-band logic, a small settings UI (a few rows), and display markers. Estimate
  **~3–6 KB flash**. No new task, no buffers, no per-satellite storage.
- **No `activeTx[]` growth.** Transverter state is global, not per-transponder, so the
  64-entry `activeTx[]` array (the big RAM consumer at ~85 B × 64 ≈ 5.4 KB) is untouched.

Net: negligible RAM (tens of bytes), a few KB of flash. Flash is the tighter budget (91%
used), but a few KB fits.

## 6. Risks

- **Doppler-on-real correctness (high impact, low complexity).** Getting the order wrong
  (LO before Doppler) is a silent, large error. Mitigated by making the transform a single
  well-tested function with a clear contract, and by the host orbit-audit harness being able
  to check computed dials.
- **Split-band mental model (medium).** Two transverters, two IFs that may be equal, plus
  MAIN/SUB — this is confusing to implement and to document. Risk is UI/UX and doc clarity,
  not computation. Mitigated by shipping with clear per-leg labeling and the display always
  showing the real frequency.
- **Read-back ambiguity (medium).** If read-back returns the IF and code somewhere treats it
  as real (or vice-versa), the knob-tracking deadband and the status API go wrong. Mitigated
  by converting to real at exactly one choke point immediately after every read.
- **Interaction with the > 4.2 GHz type change (dependency).** Displaying the real frequency
  needs the wider type; if transverter ships first with real frequencies still uint32, the
  display wraps at 4.29 GHz. Options: do `HIGH_FREQ_SCOPE.md` first, or ship transverter
  with the *IF* shown (less friendly) as an interim. Recommend sequencing high-freq type
  first, then transverter.
- **No hardware to validate (medium).** Neither bench radio (IC-821H, IC-9700) is a
  transverter-IF setup for 10 GHz. The transform is verifiable in the host harness, but
  end-to-end (real rig + real transverter + real bird) is owner-tested. This matches how
  dual-radio and several rig features are handled — ship gate-clean, flag for a hardware
  owner.
- **Inversion handling (low–medium).** Inverting transverters flip the spectrum, which
  interacts with the *inverting-transponder* logic already in the passband math. Two
  inversions can cancel; getting the sign bookkeeping right needs care but is bounded.

## 7. Recommendation

Feasible and cheap in memory. The correctness surface (Doppler ordering, IF/real
choke-points) is the real work, not the byte cost. **Sequence after** the wider frequency
type, because a transverter that shows the real band needs frequencies that can exceed
4.29 GHz. A minimal v1 — 2 user-defined transverter slots, per-leg auto-select with manual
override, real frequency shown with an "XVTR" marker, Doppler-on-real — would cover QO-100
and the common microwave bands with a modest flash cost and negligible RAM.
