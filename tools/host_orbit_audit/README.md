# Host orbital / Doppler audit harness

Compiles the **real** `src/predict.cpp` and the **real** `SatDb::gpToTle`
(extracted verbatim from `src/satdb.cpp` at build time) against the Hopperpop
Sgp4 library on the host, runs them on a fresh ISS TLE, and cross-checks every
output against **skyfield** (IAU-grade frames, JPL DE421 Sun):

| Checked                          | Result (0.9.59 audit, near element epoch)      |
|----------------------------------|-----------------------------------------------|
| `gpToTle` round trip             | line 2 byte-identical; line 1 differs only in the `+0`/`-0` zero-exponent style (own checksum consistent) |
| Az / El / slant range            | ≤ 0.002° / 0.001° / 0.03 km                    |
| Range rate (Doppler source)      | ≤ 0.16 m/s ≈ **0.23 Hz @ 435.5 MHz**           |
| Passes (AOS/LOS/TCA, max el)     | 7/7 within **±1 s**, ≤ 0.01°                   |
| One True Rule + both hold modes  | round-trip error **< 0.5 Hz** (integer rounding) over a whole pass, verified against a simulated inverting transponder |
| Eclipse (cylindrical shadow)     | 31/31 transitions agree with skyfield `is_sunlit` (±90 s brackets) |
| Solar beta angle                 | 0.002° vs an of-date-frame reference           |
| `lookFor` (BASIC `SATSEL` path)  | vs `look()`: az/el ≤ 0.0003°, range ≤ 10 m, rr identical, sub-point ≤ 0.0001°, 0 sunlit mismatches |
| High-orbit passes (HIORBIT)      | Molniya-class: every firmware pass up at mid-pass / down outside, crossings ≤ 0.04° of zero, skyfield up-samples covered 194/194; geosynchronous-in-view: one horizon-long pass, matching skyfield's continuous visibility |

A year-from-epoch run (the propagation the 60-day Illumination screen leans on)
stays within 0.024° az / 4.1 m/s rr of skyfield propagating the same elements —
the residual is TEME-vs-full-frame fidelity, smooth and TCA-peaked, not error.

Run: `tools/host_orbit_audit/build.sh [path-to-Sgp4-lib/src]`
Needs g++, python3 + `pip install skyfield sgp4`, and network (TLE via
CelesTrak with an AMSAT `nasabare.txt` fallback; DE421 via curl, cached beside
the script). `sample_run.txt` is a captured harness output.
