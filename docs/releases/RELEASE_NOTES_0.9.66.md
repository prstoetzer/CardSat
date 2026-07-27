# CardSat v0.9.66 — release notes

An HF-and-space-physics release, and a thorough shakedown of the charge/sleep and live-feed
paths. Two new tools land on top of the existing space-weather and orbital-analysis screens:
an **HF MUF-to-regions** predictor built on a verified NOSC model, and an **orbital-zones
transit** tool that tells you when a satellite passes through the South Atlantic Anomaly, into
eclipse, over the poles, or through the Van Allen belts. Alongside those, a run of bench-driven
fixes: the charge/sleep screen now reads the battery correctly and wakes without flashing, the
live APRS/DX/ADS-B feeds fetch and refresh the way you'd expect, WiFi comes back after charge
mode, and Tiny BASIC's `IF … THEN` handles every statement.

Everything here is built and gate-checked (18 gates: 13 static plus 5 host harnesses, including
two new physics harnesses that pin the MUF model and the orbital-zone math to known values).
The two new screens and the battery/charge behavior were exercised on the device during this
round; the belt behavior on a high-orbit satellite still benefits from a hardware check with a
HEO/GEO bird loaded.

# New

### HF MUF to world regions (MINIMUF-3.5)

From the **Space Wx** screen, press **`m`** for a maximum-usable-frequency prediction from your
QTH to two dozen world DX regions, computed for the current UT from the observed solar flux /
sunspot number. Each row shows the region, its bearing and great-circle distance, the path MUF,
and the best workable band (rule-of-thumb 0.85 × MUF), color-coded by frequency. Press **`k`**
for a world map that plots every region as a MUF-colored dot with your QTH marked; step the
selection with `;`/`.` to read each region's exact MUF in place. The table and the map both
print (**`x`**).

The engine is **MINIMUF-3.5** (NOSC Technical Document 201, Rose & Martin, 1978), transcribed
faithfully and cross-checked against DXSpider's implementation to resolve three points the
archival scan rendered ambiguously. It reproduces TD-201's own 24-hour verification table to
**0.16 MHz RMS**, and a host harness pins it to that table so it can't silently regress. It is
an F-region model — best on ~800–8000 km one- and two-hop paths, ~4 MHz RMS against the real
ionosphere, and it ignores sporadic-E. The sunspot number comes from the observed SSN when
available, else it's derived from the 10.7 cm flux.

### Orbital-zone transit tool

Select a satellite → **`o`** (orbital analysis) → **`z`**. The tool propagates the selected bird
forward over the next few orbits and lists when it enters and leaves distinctive orbital
regions, with times and durations, plus a live in/out status and the current magnetic L-shell.
Cycle the zone with **`z`**:

- **South Atlantic Anomaly** — where the inner radiation belt dips down to low-Earth-orbit
  altitudes, a real source of single-event upsets and detector background. Modeled as the
  commonly-drawn geographic outline, drift-corrected westward.
- **Eclipse** — the satellite in Earth's shadow; useful for power and thermal planning.
- **Polar region** — passes above 60° latitude.
- **Inner** and **outer Van Allen belts** — for higher satellites. These are computed by
  magnetic L-shell (centered-dipole McIlwain L) with an altitude floor, so a low-Earth-orbit
  bird correctly reports no belt transits (its only belt exposure is the SAA, handled
  separately), while a GTO, Molniya, or GEO bird shows the belt crossings as it climbs. QO-100
  at GEO reads as sitting in the outer belt continuously.

The zone math is a documented approximation — there is no sharp edge to any of these regions,
and the belts use a centered dipole rather than the full geomagnetic field — but the membership
logic is pinned by a host harness against known points and orbits. Report prints with **`x`**.

# Fixes

### Charge / sleep: battery reads correctly, and no flashing

The charge/sleep screen previously showed **0%** and a permanent **"Charging"** on the Cardputer
ADV. The cause was that the M5 library's `getBatteryVoltage()` returns 0 on this board (its
public ADC path fails there), and it has no way to read a charge line the ADV doesn't expose.
The battery percentage now comes from the same working reading the About screen uses, and the
voltage is read directly from the battery ADC pin (GPIO10) the way the M5 Launcher does. Charge
state, which the hardware provides no status line for, is **inferred from the voltage trend** —
a steady rise reads as charging, a fall as on-battery.

The screen also no longer flashes. The wake now happens behind the backlight: the panel is
slept fully (for the power saving) but woken with the backlight held off while the fresh frame
is drawn, so the panel's re-initialization is never visible — it lights up already showing the
readout. The woken window is **10 seconds** and the status stays on screen for the whole time.

### WiFi comes back after charge/sleep

Leaving charge mode with WiFi previously on now reconnects. Two things were wrong: the "was
WiFi up?" flag was being captured *after* the radio had already been powered down (so it always
read "no"), and the reconnect needed the full radio re-initialization the firmware uses
elsewhere rather than a bare reconnect, because charge mode powers the WiFi PHY all the way
down.

### Live feeds fetch on entry and refresh in place

The **APRS**, **DX cluster**, and **ADS-B** screens now fetch immediately when you open them,
instead of waiting. The ADS-B scatter-target overlay updates the moment you set a target grid,
rather than only after leaving and re-entering the screen. And when you're viewing an APRS
station's bearing, a new station arriving over the socket no longer bumps you to a different
station — the view stays locked to the callsign you're looking at even as the list re-sorts.

### Tiny BASIC: `IF … THEN` handles every statement

`IF condition THEN <statement>` used to accept only a handful of statement types after `THEN`
(PRINT, LET, GOTO, and a few more); a graphics or subroutine statement like
`IF C=3 THEN TEXT …` fell through and reported "unknown name." `THEN` now dispatches the full
statement set, so any statement works as the consequent. A separate latent recursion-depth gap
in the boolean-`NOT` parser was also closed.

# Under the hood

### Two new physics verification harnesses

The gate suite grew from 16 to 18. `host_muf` extracts the live MINIMUF-3.5 model from the
source and asserts it against TD-201's verification table (0.16 MHz RMS, every hour within
0.8 MHz). `host_zones` extracts the live orbital-zone membership math and pins the SAA geographic
test, the centered-dipole L-shell, and the belt altitude gates to known points — including that
the ISS is correctly excluded from the belts by the altitude floor and QO-100 is correctly
placed in the outer belt. Both run on every gate pass, so neither model can regress unnoticed.

### 8 MB partition map investigated (not yet applied)

The current build uses a 4 MB partition layout on the ADV's 8 MB flash, leaving half the chip
unused. A design study (`docs/design/PARTITION_8MB_LAUNCHER.md`) works out three ways to use
the full chip and coexist with the M5 Launcher, with validated candidate partition tables in
`partitions/`. This is documentation only in 0.9.66 — the build still ships the standard
layout; the repartition is a deliberate future step.

---

*This release adds two space-physics tools on top of the existing analysis screens and resolves
the charge/sleep, live-feed, WiFi-resume, and BASIC issues found on the bench. The MUF model and
the orbital-zone math are verified against known references; the Van Allen belt behavior on a
high-orbit satellite is the main item worth confirming on hardware.*
