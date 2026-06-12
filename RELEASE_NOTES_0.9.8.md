# CardSat v0.9.8 — Release Notes

All changes since **v0.9.7a**. This release adds networked control surfaces
(CardSat as both a rigctld/rotctld *client* and *server*), a direct-wired Yaesu
rotator backend, a substantially expanded orbital-analysis suite, live AMSAT
OSCAR status on the satellite list, a redrawn world map, a GNSS sky plot, manual
rotator control, a proper per-pass flip decision, a reorganised menu system, and
a set of hardware build guides.

> **Hardware status — please read.** CardSat builds for and runs on the Cardputer
> ADV. **Pass prediction, the plots, GPS, the AOS alarm, deep sleep, and the
> offline caches are confirmed on hardware.** Everything that talks to a **radio,
> rotator, or network** — including all of the backends and servers below — is
> **host-tested only and has not been verified against real equipment**.
> Bench-test against Hamlib's dummy backends (`rigctl -m 2`, `rotctld -m 1`) and
> watch the serial trace before trusting motors or a transmitter. The
> orbital/astro math is likewise first-order analytic and host-verified only.

---

## Highlights

- **Network radio & rotator, both directions** — drive a radio via a Hamlib
  `rigctld` server or Icom RS-BA1 LAN, *and* run CardSat **as** a rigctld/rotctld
  server so a PC (Gpredict, SatPC32, a logger) can drive the gear wired to it.
- **Direct Yaesu rotator** — wire a Yaesu G-5500-class controller straight to
  CardSat (I²C ADC + outputs), no GS-232 box.
- **AMSAT OSCAR status** on the satellite list — see which birds are being heard,
  telemetry-only, or unheard at a glance.
- **Expanded orbital analysis** — footprint diameters, slant range / path delay,
  beacon-frequency Doppler, a J2 nodal page, and uncapped workable-grid coverage.
- **New hardware build guides** for the CI-V, RS-232, and direct-rotor interfaces.

---

## New features

### Networked radio & rotator control

- **rigctl network radio (new CAT type).** CardSat can drive a radio attached to
  a **Hamlib `rigctld` server** elsewhere on the LAN. Choose **Settings → Radio /
  CAT → CAT type → rigctl (net)** and set the **rigctld host/port** (default
  **4532**). CardSat is the TCP client; it carries both legs of the QSO over one
  link using Hamlib **split** semantics — downlink (Sub/RX) on the main VFO
  (`F`/`f`, `M`), uplink (Main/TX) on the split/TX VFO (`I`/`i`, `X`) — and
  enables split on connect. Model-agnostic: the remote rigctld owns the radio.
- **rigctld server.** CardSat can run a **rigctld server** so a PC drives the
  wired/LAN **radio** through CardSat over TCP (default **4532**). VFOA =
  downlink, VFOB = uplink. Supports `f/F`, `m/M`, `v/V`, `t/T`, `q`, plus
  `\dump_state` and `\chk_vfo`.
- **rotctld server.** CardSat can run a **Hamlib NET rotctl (rotctld) server** so
  a networked PC (Gpredict, etc.) can drive the **rotator wired to CardSat**.
  Enable **Settings → Rotator → Rotctld server** (port default **4533**).
  Implements `P` (set_pos), `p` (get_pos), `S` (stop), `q`, and a minimal
  `\dump_state`. A `P` from the PC disengages CardSat's own tracking so the two
  don't fight; commands pass to the rotator verbatim — the external client owns
  calibration.

### Rotator

- **Yaesu (direct) rotator backend (new).** A Yaesu **G-5500**-class az/el
  controller can be wired **straight to CardSat** with no GS-232 box: an I²C
  **ADS1115** reads the position-feedback voltages and a **PCF8574** drives the
  four direction lines (the same I²C bus the GS-232 bridge uses). CardSat closes
  the loop itself — deadband, **soft limits**, and a **stall watchdog** — with a
  live calibration step in **Rotator: manual control** (capture keys **1/2/3/4**
  at the axis endpoints). Choose **Settings → Rotator → Rot type → Yaesu
  (direct)**. See **[ROTOR_INTERFACE.md](ROTOR_INTERFACE.md)** (⚠️ untested —
  build at your own risk).
- **Manual rotator control screen.** Off **Settings → Rotator**, jog
  azimuth/elevation by hand with the arrow keys (immediate send), with live
  position read-back, a step toggle, and stop. For the Yaesu-direct backend this
  screen also shows live ADC counts and hosts the calibration capture.
- **Per-pass flip decision.** The `Rot el range = 180° (flip)` option now decides
  **once per pass** whether to track flipped (moving a high pass's fast azimuth
  swing onto the elevation axis), mirroring Gpredict's `is_flipped_pass`, and
  holds the decision to LOS. This replaces a trigger that never fired. No-op for
  the 450° overlap range and when flip is disabled.

### Orbital analysis

The orbital-analysis screen gains pages and readouts:

- **Info** lists the footprint **circle diameter** — current, at apogee, and at
  perigee — i.e. the longest theoretically possible QSO through the satellite
  (`2·Re·acos(Re/(Re+h))`). Layout was tightened (Incl/Ecc and Age/Rev share a
  line each) to fit.
- **Next pass** adds the **slant range at AOS / TCA / LOS** and the **one-way
  path delay** at closest approach.
- **Doppler** is computed at a **user-settable beacon frequency** (`f` to edit;
  persisted) and shows the **peak Doppler shift** and **maximum range-rate** of
  the pass.
- **Nodal** (new page, J2 secular model): revs/day, **node drift** and **perigee
  drift** (°/day), a **sun-synchronous** flag, the **LTAN** (local time of the
  ascending node), an approximate **repeat ground-track** cycle, and the
  **longest possible pass** (overhead at apogee).
- **Workable grids — uncapped.** Footprint coverage is tracked in a per-grid
  bitset (one bit per 4-char Maidenhead grid, 32 400 total) instead of a
  300-entry string list. The old cap is gone (a ~2 500 km bird covers ~4 500
  grids), the O(n²) dedup is removed, and the hot loop compares
  `cos(distance)` against `Re/(Re+h)` directly, dropping `acos`. The live grid
  view rebuilds every ~3 s (not every redraw) and preserves scroll position.

All of these are first-order analytic estimates and, like the rest of the
orbital math, are host-verified only.

### AMSAT OSCAR status integration

The Satellites list now shows whether each bird has been **reported active
recently**, from the AMSAT OSCAR Status API (`/status/api/v1/summary.php`):

- **Filled dot** — heard recently (Heard / Crew Active)
- **Filled square** — **Telemetry Only** reports (beacon alive, no contact)
- **Hollow ring** — only reported *not heard* recently
- **No mark** — no recent reports

When a satellite has mixed reports the strongest wins: **heard > telemetry > not
heard**. One request covers the whole catalog; the summary is fetched whenever
elements are updated and cached to flash, so the marks survive a reboot and are
reapplied at boot. Matching is by base designator with leading-zero
normalisation (bulletin `AO-07` ↔ status `AO-7_[U/v]`). The "recently" window is
72 hours (`AMSAT_STATUS_HOURS`).

### Display

- **Realistic world map.** The world map draws a coarse public-domain
  **coastline** (recognisable continents) under the graticule, instead of just a
  lat/lon grid. It is a deliberately low-resolution outline sized for the 240-px
  display — for orientation, not navigation.
- **All-favourites footprints + highlight.** Ground footprints for **every
  favourite** are shown at once, and **`f`** now **cycles a highlight** through
  your favourites (and back to "none"): the highlighted bird is drawn bright and
  labelled while the others dim. (`f` previously toggled footprints on/off.)
- **GPS data + GNSS sky plot.** A dedicated GPS screen (ENTER on the Location
  screen) shows fix state, satellites used/in-view, and position, alongside a
  **polar sky plot of the GNSS satellites in view** (az/el, coloured by C/No).
  Adds manual **NMEA GSV** parsing, which TinyGPS++ does not expose.

### Navigation / help

- **Two-level Settings menu.** Settings is organised into **Radio / CAT**,
  **Rotator**, **Station / display**, and **Network / data** submenus, each in a
  logical order, instead of one long flat list.
- **Help screen.** Press **`h`** from (almost) any screen for a scrollable key
  reference covering global keys and every screen, including the new ones.
- **Home menu tidy.** **Log** now appears before **About**; "About / diagnostics"
  is renamed **About** and credits **Paul Stoetzer, N8HM**.

### Documentation

- **[CIV_INTERFACE.md](CIV_INTERFACE.md)** now presents **multiple interfacing
  options** — the discrete level-shifter, a ready-made logic-level CI-V board,
  and using an **RS-232 → CI-V cable** (e.g. an Icom CT-17) via a MAX3232 stage.
- **[RS232_INTERFACE.md](RS232_INTERFACE.md)** (new) — build a **MAX3232** RS-232
  CAT interface for Yaesu / Kenwood radios.
- **[ROTOR_INTERFACE.md](ROTOR_INTERFACE.md)** (new) — wire a Yaesu G-5500-class
  controller directly to CardSat.
- All hardware guides note that the Cardputer's **Grove port supplies 5 V (not
  3.3 V)** and carry an explicit **untested / at-your-own-risk** disclaimer.

---

## Renames / behaviour changes

- **`rotctld (net)` rotator → `rotctl (net)`.** The network rotator backend is a
  *client* that connects to a rotctld server, so it is now labelled **rotctl** to
  avoid implying it is the daemon. (The new **rotctld server** is the server
  side.) Setting and behaviour are otherwise unchanged.
- **World-map `f`** changed from footprint on/off to highlight-cycle (above).

---

## Fixes

- **Rotator single-master rule.** Satellite rotator tracking (Track `o`) and
  Sun/Moon tracking could be engaged at once and fight, slewing the rotator
  between targets every second. Engaging either now disengages the other, and
  opening Rotator manual control disengages both.
- **Sun/Moon below the horizon.** Tracking previously clamped elevation to 0 and
  followed the body's azimuth along the horizon all night. It now parks once when
  the body sets and resumes when it rises; the Sun/Moon screen shows "set
  (parked)".
- **Background-tracking visibility.** Sun/Moon tracking continues if you leave the
  screen; an orange SUN/MOON tag now appears in the header on other screens so it
  never runs invisibly.
- **Live workable-grids scroll.** The live grid view rebuilt its list on every
  redraw and reset the scroll with it. Scroll now survives rebuilds (see the
  uncapped-grids change above).

---

## Notes & caveats

- All network backends are **host-tested only**. The rigctld/rotctld
  `\dump_state` responses are minimal; library clients needing full capabilities
  may require testing. Bench-test the rigctl client against `rigctld -m 2 -r
  <ip>:4532` and the servers against `rigctl`/`rotctl` or Gpredict first.
- `rigctld`/`rotctld` have **no authentication** — keep them on a trusted LAN.
- The **Yaesu-direct** backend drives motors from an untested circuit; validate
  with the feedback simulated (pots) and the motors disconnected before trusting
  it. Soft limits and the stall watchdog are safety nets, not a substitute.
- Both servers drive the same hardware CardSat itself uses; don't run a server
  and CardSat's own tracking against the same radio/rotator at once.

---

## Internals

- New rig backend `RigctlRig` (rigctld client) in `rig.{h,cpp}`; `CAT_RIGCTL`
  CAT type.
- New `YaesuRotator` backend (`ROT_YAESU`) in `rotator.{h,cpp}`: ADS1115 +
  PCF8574 on Wire1, closed-loop `service()` driven from the main loop; base
  `Rotator` gains optional `service()` / `rawPos()` hooks.
- New `serviceRigctld` / `serviceRotctld` TCP servers in `app.cpp`, pumped from
  the main loop. `RotctldRotator` class renamed `RotctlRotator`.
- World-map coastline embedded as a compact `int16` polyline array.
- GSV parsing added to `location.{h,cpp}` (`GpsSat`, in-view list).
- AMSAT status: `SatEntry.amsatStatus`, base-designator matching + precedence in
  `satdb.{h,cpp}`, fetch/cache in `app.cpp`.
- New persisted config (`settings.{h,cpp}`): `rigdEnable/rigdPort`,
  `rotdEnable/rotdPort`, `CAT_RIGCTL`, and the Yaesu calibration counts
  `rotAzCnt0/rotAzCntF/rotElCnt0/rotElCntF`.
- `FW_VERSION` → **0.9.8**.

---

*CardSat is open source — issue reports, on-air test logs, and pull requests are
welcome at <https://github.com/prstoetzer/CardSat>.*
