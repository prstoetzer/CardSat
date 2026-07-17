# CardSat vs PREDICT 3.0.0 — audit

*PREDICT 3.0.0, John A. Magliacane KD2BD, released **13-Jul-2026** (two days ago). Source fetched
from qsl.net and read; 9,948 lines of `predict.c`. Everything below is from that source or from
CardSat's, not from recollection.*

PREDICT is the reference implementation for amateur satellite tracking on Unix, and has been for
thirty years. It is the right thing to measure against.

## Headline

**CardSat covers all 13 of PREDICT's menu items**, and exceeds it in several areas PREDICT has
never attempted (Doppler CAT control, printing, LoTW/Cloudlog upload, on-device tooling).

**Correction: I originally reported that PREDICT has SDP4 and CardSat does not. That was wrong.**
CardSat delegates propagation to the Hopperpop Sgp4-Library, which is the Vallado unified SGP4 —
deep-space model included, selected automatically at the same 225-minute boundary PREDICT uses. I
grepped CardSat's source, found no "SDP4", and never looked inside the dependency. Verified by
compiling the library and driving a HEO through CardSat's exact code path. See below.

## Feature coverage

| PREDICT 3.0.0 main menu | CardSat |
|---|---|
| `[P]` Predict Satellite Passes | Passes, 10-day overview, All-passes report |
| `[V]` Predict Visible Passes | Visible-pass list (`V` from Passes) |
| `[S]` Solar Illumination Predictions | Illumination screen + ASCII-raster report |
| `[L]` Lunar Predictions | Sun/Moon + EME screen (self-echo Doppler, degradation, mutual window) |
| `[O]` Solar Predictions | Sun/Moon screen |
| `[T]` Single Satellite Tracking | Track screen — **plus Doppler CAT, rotator, logging** |
| `[M]` Multi-Satellite Tracking | Overhead-now, Next Passes (favs), sat-to-sat windows |
| `[I]` Program Information | About |
| `[G]` Edit Ground Station Information | Location screen (+ GPS, Maidenhead, 8 formats) |
| `[D]` Display Satellite Orbital Data | Orbital analysis — **11 pages** |
| `[U]` Update Sat Elements From File | Update screen, GP import, 12+ CelesTrak groups |
| `[E]` Manually Edit Orbital Elements | manual satellite entry (`n` on Satellites) |
| `[B]` Edit Transponder Database | Transponder DB screen |

## SDP4 / deep space — **I got this wrong**

*Corrected after actually testing the library. The original audit said "PREDICT has one capability
CardSat does not: SDP4 deep-space propagation." That is false.*

### What I did wrong

I grepped **CardSat's own source** for `SDP4` / `deep_space` / `dpinit`, found nothing, and
concluded CardSat was SGP4-only. But CardSat does not implement SGP4 — it **delegates** to
`#include <Sgp4.h>`, the **[Hopperpop Sgp4-Library](https://github.com/Hopperpop/Sgp4-Library)**
(a `lib_deps` entry in `platformio.ini`). I never looked inside the dependency.

### What is actually true

Hopperpop's library is the **Vallado unified SGP4** implementation, which includes the deep-space
model. It carries the full deep-space state in `elsetrec` (`irez`, `d2201`…`d5433`, `xlamo`,
`atime`, the lunisolar terms), implements `dpper` / `dscom` / `dsinit` / `dspace`, and selects the
model automatically at **`sgp4unit.cpp:1523`**:

```c
/* --------------- deep space initialization ------------- */
if ((2*pi / satrec.no) >= 225.0)
  {
    satrec.method = 'd';
    ...
```

**The identical 225-minute criterion PREDICT uses.** Same model, same boundary, same lineage.

### Verified by building it, not by reading it

The library was fetched and compiled on the host, and driven through **CardSat's exact code path**
(`Predictor::setSat()` → `gpToTle()` → `Sgp4::init()` → `twoline2rv()` → `sgp4init()`):

| test satellite | period | `method` |
|---|---|---|
| ISS (15.5 rev/day) | 92.9 min | `'n'` — near-Earth |
| IO-86 (14.79) | 97.5 min | `'n'` |
| AO-7 (12.53) | 114.9 min | `'n'` |
| **boundary (6.40)** | **225.1 min** | **`'d'` — deep space** |
| **AO-40-class HEO (2.05, ecc 0.72)** | **702.6 min** | **`'d'`, `irez` 2, error 0, propagates** |
| **QO-100 (1.0027)** | **1436.2 min** | **`'d'`** |

The AO-40-class case was run end-to-end through `twoline2rv` with physically valid elements
(perigee 951 km, apogee 38,644 km — checked independently): it selects SDP4, initialises resonance
handling, and propagates with no error.

Also verified: `gpToTle()` encodes eccentricity as a 7-digit field clamped to `0.9999999`, so a
HEO's ~0.72 round-trips fine.

**So a future amateur HEO needs no propagator work.** See `docs/design/SDP4_SCOPE.md` for what
*would* actually need doing.

## Where CardSat was already ahead of 3.0.0's headline changes

Several 3.0.0 items are things CardSat already had:

| PREDICT 3.0.0 change | CardSat |
|---|---|
| *"In response to the deprecation of the TLE format, PREDICT now accepts GP data in CSV, KVN, JSON, XML as well as TLE"* | **already GP-JSON native** — AMSAT `daily-bulletin.json` by default |
| *"automatically update through celestrak.org on start-up if not modified in more than 12 hours"*; `groups.list` selects groups | **already** — CelesTrak `gp.php?GROUP=…`, with a **12-entry source picker** (amateur, satnogs, stations, visual, active, analyst, debris groups…) plus a custom-URL option |
| *"Transponder database information ... now stored in separate files under ~/.predict/xponder"* | **already** — per-satellite transponder cache + Transponder DB screen |
| *"PREDICT now handles rotator control through Hamlib"* | **already** — rotctld client, plus GS-232, Easycomm, SPID, PstRotator, direct Yaesu |
| *"Rotator movement now begins 2 minutes before AOS"* | CardSat has rotator pre-positioning; **worth checking the lead time against 2 min** |
| *"fix an RCE vulnerability on PREDICT's network server component"* | CardSat's LAN control is **default-off** — see roadmap §1.3 |

The GP-format convergence is the interesting one: PREDICT 3.0.0's flagship change is moving off
TLE onto GP, which is the design CardSat started from.

## Where CardSat exceeds PREDICT

Not a competition — different tools — but worth stating for scope:

- **Doppler CAT control** — PREDICT has none. CardSat: CI-V/Yaesu/Kenwood, 10 radios, full/one-side
  tuning, calibration, satellite mode, transponder inversion.
- **Printing** — 29 reports, 9 page-description formats, three sinks.
- **Logging + upload** — QSO log, ADIF, LoTW (signed), Cloudlog/Wavelog.
- **On-device tooling** — 35 tools including Tiny BASIC, a graphing calculator, location
  converters, link budget, and a state-vector→GP fitter.
- **AMSAT integration** — status roster, one-key "I heard it" reports, activation feed.
- **EME** — self-echo Doppler per band, path degradation, sky noise, mutual-Moon windows.
- **Decay estimation** — F10.7-informed, vs PREDICT's simple `Decayed()` boolean.

## Two guards worth borrowing

PREDICT has three orbital sanity checks. CardSat has richer versions of two and **lacks the third**:

| PREDICT | CardSat |
|---|---|
| `Decayed()` — boolean | **richer** — F10.7-informed decay-point estimate |
| `Geostationary()` — `fabs(meanmo-omega_E)<0.0002 && incl<=0.15` | partially — now covered by `isDeepSpace()` for the model question |
| **`AosHappens()`** — *"returns 1 if the satellite can ever rise above the horizon of the ground station"* | **absent** |

`AosHappens()` is a cheap apogee-vs-latitude test. Without it, asking CardSat for passes of a
satellite that can never rise from your site means `predictPasses()` iterates its 200-step search
and quietly returns nothing — indistinguishable from "no passes soon".

**Now ported (0.9.58)** as `aosPossible()`, and the Passes screen distinguishes the two cases.

The motivating example, and a lesson in checking: **IO-86** (6.0° inclination) from FM18LU. I
assumed Paul flew it and that a "never rises" verdict would therefore be a false negative — so I
brute-forced real SGP4 against IO-86's live AMSAT elements over ten days. **Max elevation: −7.4°.**
PREDICT was right, my assumption was wrong, and IO-86 turns out to be the perfect *positive* case
for the guard rather than a counter-example.

Also verified: the 331.25 constant is Kepler's third law with the period in minutes —
`(mu·3600/4π²)^(1/3) = 331.2533` for `mu = 398600.4418`. Derived, not copied. And the shipped C++
was extracted from the final source and cross-checked against an independent Python model across
5 satellites × 5 latitudes: **25/25 agreement**.

## Method note

The tarball was fetched and extracted; `predict.c` (9,948 lines), `CHANGES` and the menu code were
read directly. The deep-space criterion is quoted verbatim from `predict.c:1104`. CardSat's side
was checked against its own source. Nothing here is from memory of what PREDICT "does" — which
matters, because PREDICT is exactly the kind of thing I would have been confident about and wrong.
