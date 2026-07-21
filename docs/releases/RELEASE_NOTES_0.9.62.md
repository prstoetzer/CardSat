# CardSat v0.9.62 — release notes

This release adds **microwave-band** support (frequencies and Doppler now correct all the
way up, past the old 4.29 GHz ceiling) and the **transverter LO** offsets that make a 1.2 GHz
rig usable on 2.4/10 GHz; a **dual-radio** path — the **CardSatDualRig** companion for the
M5StickS3 that turns two half-duplex/RX radios into one full-duplex station, a **rigctl
(Grove)** transport and an on-device **Dual-Rig setup** screen to drive and configure it, and
a mirror **`<FULLu>`** uplink-knob tune mode; plus **calendar (.ics) export**, a documented
**`/api/status`** JSON contract, and a gyro/accelerometer **hand-pointing aid**.

> **Two feature areas ship untested on hardware — see the field-testing notes below.**
> The **transverter/microwave** path and the entire **dual-radio** path (companion, Grove
> rigctl, Dual-Rig setup screen, `<FULLu>`) compile clean and are verified against the code
> and the relevant specs, but neither has been exercised against the real hardware they need
> (a microwave transverter; two live radios on a Grove-tethered Stick). They are included so
> operators who have that hardware can try them and report back — treat first use as
> verification, not a known-good path. Everything else in this release runs on the Cardputer
> as before.

# New

## New features

### Calendar export (.ics)

CardSat can now write standard **iCalendar (`.ics`)** files for import into any phone or
desktop calendar. From the About screen press **`k`** for **Calendar export** and pick a
source:

- **Favorite passes** — the all-favorites schedule.
- **Selected pass** — the active satellite's next pass.
- **Activations + sked reminders** — the hams.at activation feed plus your manual entries.
- **EME good days** — the next 90 days' best moonbounce days (high northern Moon
  declination with low distance degradation).
- **Visible passes + solar transits** — the active satellite's upcoming *visible* passes
  (sunlit satellite, dark sky) and any solar-transit events the transit scanner has found.

Each event's description carries the satellite, frequencies, mode, maximum elevation, rise
direction, and the station grid and coordinates, so it's self-explanatory in any calendar.
Files are written to `/CardSat/Calendars/` with a timestamped name and downloaded over WiFi
from the web **Files** page; all times are UTC (every calendar app converts to local).

### Stable machine-readable status API

The web server's `/api/status` endpoint gained a **documented, stable** set of fields for
external tooling — a dashboard, a rotator bridge, a logger, anything that wants CardSat's
live state as JSON. Alongside the existing panel keys it now reports: firmware version and
**UTC**; observer **latitude/longitude/grid** and its source; **range** and **range rate**;
the next/in-progress pass **AOS / TCA / LOS**, **maximum elevation**, and **rise direction**;
the **commanded vs. rig read-back** downlink/uplink frequencies; the rotator's **actual and
commanded** azimuth/elevation; and a `sys` block with **radio, CAT protocol, GPS, SD, Wi-Fi
(IP + RSSI), battery (+ charging), and heap** status. The contract is documented field by
field in `docs/interfaces/WEB_API.md`; the older panel keys remain for backward
compatibility. Still plain HTTP on the LAN with no authentication.

### Hand-pointing aids (Cardputer ADV)

The **point-here arrow** screen (Track → `a`) gains an optional **pointing aid** on boards
with the IMU. Press **`g`**, aim the device north, and press **`n`** to set the reference.
From then on a **cyan needle** on the compass rose shows where you are physically pointing
— tracked by integrating the BMI270 **gyroscope** — and a **cyan tick** on the elevation
bar shows the device's tilt, read from the **accelerometer**. Rotate until the needle meets
the green target arrow and tilt until the tick meets the target; the bottom line gives live
"Turn L/R, Tilt up/dn" guidance and reads **ON!** within a few degrees of both. It lets you
aim a handheld antenna without a rotator. The heading is gyro-only (no magnetometer), so it
**drifts** over a minute or two — press **`n`** any time to re-zero north. It's a rough
aiming aid, deliberately labeled as approximate, not a calibrated rotator.

### Microwave-band frequencies and Doppler (above 4.29 GHz)

CardSat now stores and computes frequencies as a **64-bit** quantity, lifting the old
**4.294 GHz** ceiling that silently truncated anything higher. Satellites with C-, X-, or
Ku-band transponders and downlinks — a 10.489 GHz QO-100-style downlink, or a C/X-band LEO
like CatSat — now track, Doppler-correct, and **display** at their true on-air frequency
instead of wrapping around to a garbage sub-GHz number. The Doppler math was already
double-precision internally, so shift accuracy is unchanged; what changed is that the stored
and displayed frequency is now correct all the way up. Transponder ingestion from SatNOGS no
longer clips high-band entries either.

On its own this is a **display and tracking** improvement: the supported radios still only
tune up to the 1.2 GHz band, so working a 10 GHz satellite over the air needs a transverter
— which the next feature drives.

### Transverter LO offset (drive a microwave transverter from a supported rig)

A **transverter** converts a radio's lower-band **IF** up to a microwave band (and back on
receive), which is how a station whose rig tops out at 1.2 GHz gets on 2.4 GHz, 10 GHz, or
higher. CardSat can now drive one. Under **Settings → Radio** there are two new rows,
**Downlink LO** and **Uplink LO**: enter each transverter's local-oscillator frequency in
MHz (or `0` for none). CardSat then tracks, Doppler-corrects, logs, and displays the **real
on-air frequency**, but the value it actually sends to the rig — and reads back — is the
**IF = real − LO**. The Doppler correction is applied to the real frequency *before* the LO
is subtracted, which is the only correct order (Dopplering the IF would under-correct by the
real-to-IF ratio — about 70× on a 10 GHz downlink with a 144 MHz IF).

The two legs are independent, so split-band microwave work is supported directly — for a
QO-100-style bird you'd set the uplink LO for the 2.4 GHz up-converter and the downlink LO
for the 10 GHz down-converter, and the rig stays on its comfortable 70 cm / 2 m IF while
CardSat shows the microwave dial. On the **Track** screen the **DN**/**UP** labels grow a
trailing **`x`** on any leg that has a transverter engaged, as a reminder that the rig is on
an IF and the number shown is the real frequency. Because the supported rigs never tune above
1.2 GHz, this is what makes them usable on the microwave satellite bands at all.

> **Field-testing note.** The high-band math and the LO transform are exercised by the
> in-repo orbit-audit harness, but neither of the project's bench radios operates above
> 1.2 GHz, so the end-to-end microwave path has not been hardware-verified against a real
> transverter. Operators with a transverter are encouraged to report results. The previous
> truncation was silent, so there is no older known-good baseline to regress against.

### Two half-duplex radios as one full-duplex station (CardSatDualRig companion)

A linear-transponder pass wants full duplex — hear the downlink while you transmit the
uplink. A single half-duplex or receive-only radio (IC-705, FT-817, an R8600, a TH-D74)
can't do that, so a proper station needs *two*. The new **CardSatDualRig** companion
firmware — for the **M5StickS3**, in `companion/CardSatDualRig/` — hosts two radios on its
own USB port, speaks each one's native CAT (CI-V, the FT-817 binary/text Yaesu dialects, or
Kenwood-HT), and presents CardSat a single Hamlib **`rigctld`** server. CardSat steers two
VFOs (VFOA = downlink, VFOB = uplink) and the Stick fans them out to the two radios. It
supports **27 radios** including a range of Icom/Yaesu/Kenwood transceivers and wideband
receivers; PTT is **manual** (you key your transmit radio by hand). Prebuilt Stick binaries
ship in `companion/CardSatDualRig/firmware/`.

### rigctl (Grove) — drive the companion (or any rigctld) over a Grove cable

Alongside **rigctl (net)** over Wi-Fi, CAT type now offers **rigctl (Grove)**: the identical
Hamlib VFO-mode protocol over the Cardputer's Grove UART (G1/G2), no Wi-Fi needed — a single
cable to the Stick carries all radio control. The **port** field becomes the Grove baud
(default 115200). Like wired CI-V it claims the Grove UART, so it shares the Grove
mutual-exclusion rules (a Grove rotator or Grove GPS must yield). The refactor keeps **one**
copy of the VFO-mode protocol behind a transport boundary, so **rigctl (net) against a
generic `rigctld` is unchanged** and still speaks only standard `V`/`F`/`M`/`\chk_vfo`.

### Configure the companion from CardSat — Dual-Rig setup screen

**Settings → Radio → Dual-Rig setup (Stick)** configures the companion over whichever rigctl
transport is active — no phone or captive portal. It shows the Stick's **live USB
enumeration** (product + VID:PID) so you can bind the right physical adapter to each leg —
the tricky part when two identical adapters are plugged in — pick each radio's model and CI-V
address, and push it all with one save. The Radio settings row carries a **red/green link
dot** so bring-up is a glance, not a query. The device/model tables are heap-allocated only
while the screen is open.

### One True Rule on the uplink knob (`<FULLu>`)

The **`d`** tune-mode cycle gains a fifth stop: **`<FULLu>`**, One True Rule driven from the
**uplink** VFO instead of the downlink. For two-radio setups where the uplink rig is the one
with the good knob, turn *it* and CardSat reads that VFO, recovers your passband spot, and
keeps both legs Doppler-corrected around it — the exact mirror of `<FULL>`, inverting-
transponder sense included. The existing downlink `<FULL>`/`<DL>`/`<UL>` paths are unchanged.

> **Field-testing note — the whole dual-rig path is untested on hardware.** The
> CardSatDualRig companion, the Grove rigctl link end to end, the Dual-Rig setup screen, and
> the `<FULLu>` uplink-knob mode all compile clean and are verified against the code and the
> Hamlib/CAT specs, but have **not** been exercised against real radios or a real Grove
> tether yet. The per-radio CAT details (e.g. the TH-D74 all-mode receiver being on Band B,
> and the Yaesu VR-5000 opcodes, which are flagged in the companion README) especially need
> on-air confirmation. Treat first bring-up as verification, not a known-good path. The
> Dual-Rig setup screen is meant for the companion only — pointed at a generic `rigctld` its
> link dot will read red, which is correct (there is no Stick to answer).
