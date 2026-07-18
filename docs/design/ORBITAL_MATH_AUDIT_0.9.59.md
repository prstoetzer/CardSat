# Orbital & Doppler math audit — 0.9.59

**Verdict up front: zero mathematical defects found.** Every formula in
`src/predict.cpp` and the orbital-analysis code was either verified numerically
against an independent reference (skyfield, IAU-grade frames + JPL DE421) or
checked analytically against the textbook form — and the numeric checks run the
**actual firmware translation units on the host**, not re-implementations. The
harness is permanent: `tools/host_orbit_audit/` (the same pattern as the
`ppm2pwg` print-raster validation).

## What was verified numerically (real `predict.cpp` + real `gpToTle` vs skyfield)

- **GP→TLE rendering** (`SatDb::gpToTle`): renders a parsed ISS element set back
  to TLE text **byte-identical on line 2**; line 1 differs only in the
  `+0` vs `-0` zero-exponent style for a zero n̈ (checksum self-consistent, and
  SGP4 parses both identically).
- **Look angles**: az ≤ 0.002°, el ≤ 0.001°, slant range ≤ 0.03 km near the
  element epoch. A deliberate **one-year-from-epoch** run stays ≤ 0.024° — the
  residual is the documented TEME/GMST-only frame fidelity, not error.
- **Range rate** (the Doppler source, `rangeRateAt`): ≤ 0.16 m/s near epoch
  (**0.23 Hz at 435.5 MHz**); ≤ 4.1 m/s a year out, a smooth TCA-peaked profile
  that is exactly the TEME-vs-full-frame signature. The implementation is the
  Gpredict method — SGP4 velocity vector dotted with the observer-relative
  geometry, observer velocity ω⊕×r included, WGS72 constants matching the TLE
  convention throughout.
- **Pass prediction** (`predictPasses` through the Sgp4 library): 7/7 passes in
  24 h with AOS/LOS/TCA all within **±1 s** of skyfield `find_events`, max
  elevation within 0.01°.
- **Eclipse** (`sunlitAt`, cylindrical shadow, low-precision solar almanac):
  **31/31** umbra transitions over 24 h agree with skyfield's full-precision
  `is_sunlit` within the 60 s sampling grid.
- **Solar beta angle** (`betaAngleDeg`): agrees to **0.002°** with an of-date
  reference. (A first comparison showed −0.34° — that was the *reference*
  mixing an ICRF Sun with the TEME orbit normal, i.e. 26 years of accumulated
  precession. The firmware's mean-of-date solar almanac is the frame-consistent
  choice against TLE elements. The shipped `compare.py` documents this trap.)

## The Doppler chain, proved end-to-end

`dopplerFreqs` (One True Rule: rx = dl·(1−β), tx = ul/(1−β)) and **both hold
modes** — `uplinkForFixedDownlink` and `downlinkForFixedUplink` — were exercised
against a **simulated inverting transponder** (heard = tx·(1−β), emit = K−heard,
ground = emit·(1−β)) at every 15 s sample of a real pass: worst round-trip error
**< 0.5 Hz**, i.e. pure integer-Hz rounding. The hold-mode algebra was also
derived by hand and closes exactly; the inverting `passbandFreqs` keeps
dl+ul constant (bottom-of-downlink ↔ top-of-uplink), matching the hold modes'
inversion sense. Calibration offsets are applied in the dial domain
consistently between the tracking loop's knob-read inversion
(`(rx − calDl)/(1−β)`) and `dopplerFreqs`; the one site ordering it the other
way (`uplinkForFixedDownlink`'s parked point) differs by calDl·β ≈ 0.06 Hz —
verified immaterial.

## Verified analytically (with independent numeric confirmation)

Semi-major from mean motion, apo/perigee altitudes, vis-viva at r/apo/peri,
equation of center to e³, footprint arc `2·RE·acos(RE/(RE+h))`, **J2 nodal and
apsidal rates** (ISS: −4.95 and +3.69 °/day against published ≈ −5.0/+3.6), the
sun-synchronous target 0.98565 °/day, the Explore page's max-pass
`2λ/ω_apo` with the true apogee angular rate `h/r²`, `geocentricAltKm`'s
ellipsoidal handling (so "altitude now" compares apples-to-apples with
geocentric apo/peri), and the ascending-node bisection.

## Informational notes (design choices, not defects)

1. **TEME/GMST-only frames** cost ≲ 0.03° pointing and ≲ 6 Hz of 70 cm Doppler
   worst case — invisible against amateur antenna beamwidths and 3 kHz
   passbands, and the right trade for this MCU.
2. The Explore page's **max-pass ignores Earth rotation** (a few percent short
   for prograde LEO) — a teaching figure, labeled as such.
3. The **decay estimator** is a King-Hele-style integrator whose Cd·A/m uses a
   38·B* calibration (≈3× textbook 12.74) fitted to observed ISS-class
   reentries; the code documents the discrepancy and brackets the answer with
   the solar-activity low/high bounds. Appropriately labeled a coarse cue.
