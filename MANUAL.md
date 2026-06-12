# CardSat User Manual

A complete guide to operating **CardSat**, the amateur-radio satellite tracker and
multi-radio CAT Doppler controller for the M5Stack Cardputer ADV (Icom, Yaesu, Kenwood).

> **Status.** CardSat runs on the Cardputer ADV, and every feature has been
> exercised on hardware **except radio (CAT) control and the antenna rotator** —
> those two are still unverified on real equipment. Their math (the per-protocol
> CAT frequency encoders, and all four rotator backends -- GS-232 framing, the rotctld TCP client, PstRotator UDP and the direct-Yaesu I2C interface) is host-tested, but
> nothing has driven an actual radio or rotator yet. Keep the serial monitor open
> and verify on the air before trusting it for a contact.

---

## Contents

1. [Overview](#1-overview)
2. [What you need](#2-what-you-need)
3. [Connecting your radio](#3-connecting-your-radio)
4. [Connecting a GPS (optional)](#4-connecting-a-gps-optional)
5. [Installing the firmware](#5-installing-the-firmware)
6. [The keyboard: how to navigate](#6-the-keyboard-how-to-navigate)
7. [First-time setup](#7-first-time-setup)
8. [Screen reference](#8-screen-reference)
9. [Doppler tuning and the One True Rule](#9-doppler-tuning-and-the-one-true-rule)
10. [Calibration](#10-calibration)
11. [Working a pass, step by step](#11-working-a-pass-step-by-step)
12. [AOS alarm and deep sleep](#12-aos-alarm-and-deep-sleep)
13. [Sun and eclipse](#13-sun-and-eclipse)
14. [GP age and accuracy](#14-gp-age-and-accuracy)
15. [Working offline](#15-working-offline)
16. [Radio-specific notes](#16-radio-specific-notes)
17. [Antenna rotator (GS-232, rotctl, PstRotator, rotctld server)](#17-antenna-rotator-gs-232-rotctl-pstrotator-rotctld-server)
18. [Managing data and factory reset](#18-managing-data-and-factory-reset)
19. [Troubleshooting](#19-troubleshooting)
20. [Key reference (cheat sheet)](#20-key-reference-cheat-sheet)
21. [Glossary](#21-glossary)
22. [Supporting AMSAT](#22-supporting-amsat)
23. [License](#23-license)

---

## 1. Overview

CardSat turns a pocket-sized Cardputer ADV into a standalone satellite station
controller. It:

- downloads GP (General Perturbations / OMM) orbital elements from AMSAT and transponder frequencies from
  the SatNOGS database, and caches both to flash for **fully offline** use;
- predicts passes with **SGP4** and shows live az/el/range, a polar sky plot, and
  per-pass elevation curves;
- drives an **Icom, Yaesu, or Kenwood** transceiver over **CAT** with continuous **Doppler
  correction**, keeping a constant frequency *at the satellite* (the AMSAT
  "One True Rule") so your signal stays put in a linear transponder's passband;
- lets you tune the passband from the device **or from the radio's own knob**;
- maintains a **favorites** list, a unified **Next Passes** schedule, an **AOS
  alarm**, a **deep-sleep-until-next-pass** power saver, and **sun/eclipse** status;
- points an **az/el antenna rotator** -- a wired **Yaesu GS-232** controller or a
  networked **Hamlib `rotctld`** server;
- **logs QSOs** on the device and **exports ADIF** for LoTW / eQSL or your main logger.

Everything runs on the device. WiFi is needed only to refresh GP/transponders
and to set the clock.

---

## 2. What you need

- **M5Stack Cardputer ADV** (ESP32-S3, 8 MB flash, 240×135 screen, 56-key
  keyboard, speaker, microSD, Grove port, 2×7 header).
- **A supported transceiver** in one of three CAT families:
  - **Icom CI-V** — IC-820, IC-821, IC-910, IC-970, IC-9100, IC-9700;
  - **Yaesu** — FT-847, FT-736R;
  - **Kenwood** — TS-790, TS-2000.
- **A CAT interface suited to that radio.** The ESP32-S3 pins are **not** 5 V
  tolerant — never connect CAT directly. **Icom:** a 3.3 V-safe CI-V interface
  (one-transistor circuit or a ready-made board). **Kenwood:** a **MAX3232** RS-232
  level shifter on the DB-9 COM port. **Yaesu:** a serial CAT interface (verify TTL
  vs RS-232 per the CAT manual).
- *(Optional)* a **GPS**: a NMEA receiver on the Grove port, or an **M5Stack Cap
  LoRa** (868 or 1262) module with onboard GNSS.
- *(Optional)* **WiFi** to fetch GP/transponders and sync the clock.

---

## 3. Connecting your radio

All three CAT families share **UART1**, **RX = G1 / TX = G2** by default, but the
**interface hardware differs** (see [§2](#2-what-you-need) and [§16](#16-radio-specific-notes)).

1. Wire the appropriate CAT interface between the radio's control jack (Icom
   **REMOTE**, Kenwood **COM** DB-9, Yaesu **CAT**) and **G1/G2**. **Never connect
   the radio directly to the GPIO pins** — the ESP32-S3 is not 5 V tolerant.
2. Note the radio's CAT settings: **baud** (all families) and, for **Icom**, the
   **CI-V address** (fixed on older rigs).
3. In CardSat, open **Settings**, choose your **radio model** (this auto-fills the
   defaults), then adjust **CAT baud** — and, for Icom, the **CI-V address** — to match.

CardSat drives two independent VFOs. By default **downlink = Sub/RX, uplink =
Main/TX**, but the **VFO Type** setting can swap the roles (*Main Dn/Sub Up*). On
**Icom** it manages MAIN/SUB and, by default, leaves the rig's satellite mode
**off**; the **Sat mode** setting commands it on/off when you engage radio control
(a no-op on rigs without one, such as the IC-820/821/910/970). On **Yaesu and
Kenwood** the rig's own satellite / full-duplex mode and the band pair are set up
**by you on the radio** — CAT can't switch bands on those rigs — and CardSat
Doppler-tunes within them. (See [§16](#16-radio-specific-notes).)

### Icom over the network (no CI-V wiring)

An Icom with a built-in network port (**IC-9700**, IC-705, IC-7610, IC-785x) can be
controlled over **WiFi/Ethernet** instead of the wired CI-V bus — CardSat speaks the
same RS-BA1 UDP protocol as Icom's own remote software. No level shifter, no UART:
the radio's CI-V/G1/G2 pins stay free.

On the radio (IC-9700: **MENU > SET > Network**): set **Network Control = ON**, set a
**Network User1** id + password, leave the **Control** port at **50001** (Serial =
50002, Audio = 50003 follow it), and turn **CI-V Transceive ON** so the radio reports
changes. Give the radio a stable IP (DHCP reservation or static).

In CardSat **Settings**: set **CAT type → Icom LAN**, **LAN host** to the radio's IP,
**LAN port** to 50001 (unless you changed it), and **LAN user / LAN pass** to the
Network User1 credentials. CardSat connects in the background; once the link is up the
radio behaves exactly like a wired Icom (MAIN/SUB, Doppler, sat mode, CTCSS all work
the same). Only **CAT** is carried — CardSat does not open the audio stream. Keep the
credentials on a trusted LAN. (See [§16](#16-radio-specific-notes).)

---

## 4. Connecting a GPS (optional)

GPS runs on **UART2**, independent of CAT. On the **Location** screen press `s`
to cycle the source:

| Source | Pins | Baud | Notes |
|---|---|---|---|
| **Grove 9600** | G1/G2 | 9600 | ⚠️ same pins as CAT — don't use both at once |
| **Grove 115200** | G1/G2 | 115200 | ⚠️ same pins as CAT — don't use both at once |
| **Cap LoRa868** | G15/G13 | 115200 | M5Stack Cap LoRa (868) onboard AT6668 GNSS |
| **Cap LoRa1262** | G15/G13 | 115200 | M5Stack Cap LoRa (1262) onboard AT6668 GNSS |

Both Cap LoRa modules use identical GPS settings (115200 8N1 on G15/G13). For a
Grove-port receiver, pick the baud that matches it.

Press `p` on the Location screen to enable/disable GPS use. With a fix, your
latitude, longitude, altitude, and grid update automatically, and the clock is
set from GPS time. The Location screen also refreshes on its own the moment a
fix is gained or lost or the satellite count changes.

> If you use the **Grove** GPS option you cannot use CAT on G1/G2 at the same
> time. The two **Cap LoRa** options use G15/G13 and coexist with CAT.

---

## 5. Installing the firmware

### Arduino IDE (single file)

1. Install libraries: **M5Cardputer**, **ArduinoJson** (v7), **TinyGPSPlus** from
   the Library Manager, and the Hopperpop **Sgp4** library via *Sketch → Include
   Library → Add .ZIP Library* (from <https://github.com/Hopperpop/Sgp4-Library>).
2. Open `CardSat.ino`. Under **Tools**, set:
   - **Board:** ESP32S3 Dev Module (or M5StampS3)
   - **Flash Size:** 8MB
   - **Partition Scheme:** **Huge APP (3MB No OTA/1MB SPIFFS)** — *required*
   - **PSRAM:** Disabled · **USB CDC On Boot:** Enabled
3. Upload. Open the Serial Monitor at **115200** baud for diagnostics.

### PlatformIO

`pio run` to build, `pio run -t upload` to flash, `pio device monitor` to watch
the log. The `cardputer_adv` env pins the libraries and the 8 MB partition layout.

---

## 6. The keyboard: how to navigate

CardSat uses the arrow legends printed on the Cardputer keys:

| Key | Action |
|---|---|
| `;` | up |
| `.` | down |
| `,` | left |
| `/` | right |
| **ENTER** | select / confirm |
| `` ` `` or **DEL** | back / cancel |
| `{` `}` | page up / page down (lists) |
| `b` | save a screenshot to the SD card (see §18) |

Other letter keys are screen-specific actions and are shown in the **footer** at
the bottom of each screen. When in doubt, read the footer. The one global
exception is **`b`**, which saves a screenshot on any screen and is deliberately
not shown in any footer.

---

## 7. First-time setup

1. **Settings** (`;`/`.` to move, `,`/`/` to change a value, ENTER to edit text):
   - **Radio** — select your model; CAT baud (and, for Icom, the CI-V address) auto-fill.
   - **CAT baud** — set to match the radio's CAT menu (applies to every CAT family).
   - **CI-V addr** — set to match the radio; **Icom only**.
   - **Min pass el** — passes below this maximum elevation are hidden (default 5°).
   - **WiFi SSID / WiFi pass** — enter your network, then **Save & test WiFi**.
     On the **WiFi SSID** row you can instead press **`s`** to **scan** for nearby
     networks, pick one from the list (strongest first; `*` = secured), and then
     enter its password (open networks skip the password step).
   - **AOS alarm** — on/off for the pre-pass beeps.
   - **VFO Type** — which physical VFO carries each leg: *Main Up/Sub Dn* (default)
     or *Main Dn/Sub Up*.
   - **Sat mode** — command the rig's own satellite mode on/off when you engage CAT
     (a no-op on rigs without one).
   - **CAT rate** — how often Doppler/CAT updates are sent to the radio (default
     **500 ms**, adjustable in 10 ms steps). A soft floor keeps the *effective*
     rate no faster than the CAT baud can service one update; the row shows
     `(min N)` when your setting is being clamped.
   - **CAT delay** — how long to pause *after each CAT command* before sending the
     next one to the radio (default **70 ms**, 0–200 ms in 2 ms steps). Raise it if
     an older or slow radio drops or mis-handles back-to-back commands; lower it
     toward 0 for the fastest tuning on a radio that keeps up. CI-V (Icom) only at
     present.
2. **Location** — set your position one of three ways:
   - `e` latitude, `o` longitude, `a` altitude; or
   - `g` Maidenhead grid (uppercased as you type — hold shift for lowercase
     subsquares — as on the log and mutual-pass grid fields); or
   - `p` enable GPS (and `s` to pick the GPS source).
   - If you have no network or GPS, press `c` to set the **UTC clock** manually
     (`YYYY-MM-DD HH:MM:SS`).
3. **Update** — press `k` (or ENTER) to download GP data; this also syncs the clock
   over NTP if WiFi is configured. Optionally press `a` to cache **all**
   transponders for offline use.
4. You're ready: **Satellites** to pick birds, **Next Passes** to see what's up.

> **At every power-on**, if a WiFi network is configured CardSat connects
> automatically and sets the clock over **NTP** before loading data — so a
> network-connected unit boots with the correct time and no key presses. If WiFi
> is unavailable it carries on with GPS or the cached/manual clock; the attempt is
> best-effort and non-fatal (it can add a few seconds to boot while it tries). If
> the clock is set and even the freshest cached element set is **over a week old**,
> CardSat also refreshes GP automatically at boot. The display blanks its backlight
> after the **Screen sleep** idle time (never while tracking or alarming); any key
> wakes it.

---

## 8. Screen reference

CardSat is a simple state machine. From **Home** you reach every screen; `` ` ``
or **DEL** always steps back.

### Home

A menu: **Satellites · Next Passes (all favs) · Passes (sel) · Track (sel) ·
Location · Update · Settings · About / diagnostics · Log.** The currently
selected satellite is shown at the bottom right. `;`/`.` move, ENTER selects.

### About

Author credit (**Paul Stoetzer, N8HM**), firmware version and build date, storage
backend (microSD or internal flash), GP catalog size and freshest element age,
WiFi/IP, battery level, free heap, and uptime. `` ` `` or ENTER returns home.

### Logging QSOs (Log)

There are two ways to start an entry:

- **From a pass** — press `l` on the **Track** or **Polar** screen to log the
  contact you're working; radio control keeps running while you type. CardSat
  snapshots the UTC time, satellite, mode and the live **Doppler-corrected**
  uplink/downlink frequencies, plus your grid and callsign (from **My callsign**
  in Settings).
- **After the fact** — choose **New QSO entry** from the **Log** menu when you're
  *not* working a pass. The entry opens with the time set to now and everything
  ready to edit, so you can log a contact you made earlier or on another rig.

Every field is editable on the entry screen — `;`/`.` move, ENTER edits the
selected field:

- **Date / Time** — UTC, entered as `YYYY-MM-DD` and `HH:MM:SS`.
- **Sat** — ENTER opens the satellite list; pick a bird and CardSat fills the
  **Sat**, **Mode** and frequency fields with its defaults — the **non-Doppler
  centre** of a linear transponder's passband, or the **nominal** downlink/uplink
  for an FM or single-channel satellite.
- **DL MHz / UL MHz** — downlink and uplink in MHz (e.g. `145.960`).
- **Call** — the station worked (required to save). Letters default to **uppercase**
  (hold shift for lowercase).
- **Mode** — on a **linear** transponder, ENTER toggles **SSB ↔ CW** for this
  contact; on an FM bird the mode is fixed.
- **RST S / RST R** — report sent and received (default `59`).
- **Grid** — the other station's grid, **uppercase** by default (optional, but
  grids are the point of sat ops).
- **Notes** — free text.

`s` saves, `` ` `` cancels. QSOs are appended to **`/CardSat/qso_log.csv`** (one row
each; `notes` is the last column, so commas there are fine).

The **Log** item on the main menu offers **New QSO entry**, **View / edit log**,
and **Export to ADIF** (writes `/CardSat/qso_log.adi`, including `STATION_CALLSIGN`
from My callsign, for upload to LoTW/eQSL or import into your main logger).

LoTW limits the `SAT_NAME` field to six characters and uses its own names, so on
export CardSat translates each satellite via **`/CardSat/lotw_sats.csv`** (rows of
`SAT_NAME,AMSAT_NAME`; a built-in table covers the common cases if the file is
absent). If a logged satellite has no match, CardSat prompts you for its `SAT_NAME`
(entry capped at six characters, uppercased) and saves it back to the CSV, so it's
only asked once. Update that CSV on the card as new satellites appear.

**View / edit log** is a scrollable list of recent contacts (`;`/`.` to move).
Open one with ENTER to correct **any field — including the date, time, satellite
and frequencies** — then `s` to save, or press `x` twice to delete it; changes are
written straight back to the CSV. The most recent **120**
QSOs are available on the device — the complete log always lives in
`/CardSat/qso_log.csv`, which you can also read or edit on a computer.

### Satellites

The catalog (up to 220 sats from the GP data, plus any you add manually).

- `;`/`.` move, `{`/`}` jump 10 rows.
- `f` — toggle **favorite** (favorites are marked with `*`).
- `v` — toggle **favorites-only** filter.
- `n` — add a **manual GP satellite**: enter the name, NORAD ID, epoch, then each
  orbital element in turn (see **Edit** under [§8](#8-screen-reference)).
- **ENTER** — load the satellite's transponders and open its **Passes**.
- `` ` `` — back to Home.

At the right edge each row may show an **AMSAT activity mark**: a **filled dot**
means the satellite has been reported **heard** (active) recently, a **filled
square** means it has only been reported **telemetry only** (a beacon/telemetry
is alive but no transponder or voice contact was logged), a **hollow ring** means
it has only been reported **not heard** recently, and **no mark** means there are
no recent reports. When a satellite has a mix of reports the strongest wins
(heard > telemetry > not heard). The marks come from the [AMSAT OSCAR Status
page](https://www.amsat.org/status/) and are refreshed whenever you run
**Update** (see [§GP source](#14-gp-age-and-accuracy)). Matching is by base
designator, so it works for AMSAT-named birds; satellites loaded from CelesTrak
categories usually won't match and simply show no mark.

### Orbital analysis (`o`)

Opened with `o` from **Satellites**. Six pages, paged with `,`/`/`, `r` to
recompute, `` ` `` to leave:

- **Info** — NORAD, current altitude, **footprint** diameter, period, apogee/
  perigee altitudes, the **footprint diameters at apogee and perigee**,
  inclination/eccentricity, semi-major axis, B\* with a rough drag-decay
  estimate, element age/revolution, and the next ascending-node longitude/time.
  The decay estimate integrates B\* through an exponential-atmosphere model with
  a **King-Hele** treatment that lets an eccentric orbit circularize (drag at
  perigee lowers apogee fastest), down to a ~120 km reentry. Because
  thermospheric density swings about a factor of ten over the solar cycle - the
  single biggest driver of lifetime - a second **"Decay rng"** line shows the
  bracket from **solar maximum (shortest)** to **solar minimum (longest)**, with
  the assumed level (`mean`/`min`/`max`/`auto`, set in *Settings -> Station /
  display -> Decay solar*) in parentheses. In **`auto`** the density scale is
  derived from the live **F10.7 solar flux**, downloaded with the GP elements and
  cached (the setting row shows the current value); without a cached flux it
  falls back to mean. Treat all of this as **order-of-magnitude**: it
  ignores attitude, lift, and short-term space weather, and B\* itself is often
  a fitted fudge term, so the chart is a "should I worry about this object" cue,
  not a reentry prediction.
  The **footprint** is the diameter of the visibility circle on the ground
  (`2·Re·acos(Re/(Re+h))`) — the widest separation between two stations that can
  both see the bird at once, i.e. the **longest theoretically possible QSO**
  through it. It grows with altitude, so for an elliptical orbit the apogee
  figure is the best case and perigee the worst.
- **Live** — az/el, range, range-rate, Doppler at 145.8/435 MHz, sub-point,
  mean anomaly/phase, the sunlit/eclipse flag, and the **eclipse depth** — the
  PREDICT-style angle (degrees) by which the satellite is inside Earth's umbral
  shadow (positive when eclipsed, negative as a sunlight margin), so you can see
  not just *whether* it's in shadow but *how deeply*.
- **Next pass** — AOS countdown, duration, max elevation, AOS/LOS azimuths,
  sunlit fraction, eclipse entry/exit, the **peak eclipse depth** if the bird
  transits shadow during the pass, the optical-visibility flag, the **slant
  range at AOS/TCA/LOS** and the **one-way path delay** at closest approach
  (range ÷ c).
- **Ground track** — the next two orbits projected on an equirectangular map.
- **Doppler** — the Doppler curve across the next pass, computed at a
  **user-settable beacon frequency** (press `f` to change it; default 145.8 MHz).
  Below the plot it shows the peak Doppler shift and the maximum range-rate of
  the pass.
- **Nodal** — orbit-plane dynamics from the J2 secular model: revolutions per
  day; **node drift** (RAAN regression, °/day) and **perigee drift** (apsidal
  precession, °/day); a **sun-synchronous** flag (node drift ≈ +0.986°/day); the
  **LTAN** (local solar time of the ascending node); an approximate **repeat
  ground-track** cycle (revs/days, ≤30 days); and the **longest possible pass**
  (an overhead pass at apogee). These are first-order estimates — handy for
  understanding why pass times march earlier each day, whether a bird is
  sun-synchronous, and how long the very best passes can run — not a precision
  propagator.
- **Pass outlook** — a planning summary over the next 7 days: the total number of
  passes above your mask, how many clear 30°, the longest pass, and the mean gap
  between passes — plus the **best upcoming pass** (its peak elevation, the
  date/time it occurs, the countdown to it, and its duration). This answers "when
  this week is worth operating this bird" at a glance.
- **Orbit position** — where the satellite is in its orbit right now: mean
  anomaly, true anomaly, and **argument of latitude**, the **time to the next
  perigee and apogee**, plus argument of perigee, RAAN, the current revolution
  number, and the element-set age. For eccentric orbits the perigee/apogee timing
  shows when the bird is moving slowest and sitting highest — i.e. when the
  longest-dwell passes occur.

### Next Passes (schedule)

One merged list of the next pass for **every favorite**, soonest first. Each row
shows the countdown to AOS (or **NOW** if the bird is already up), the satellite
name, maximum elevation, and pass length. A red **`!`** flags a satellite whose
elements are stale (see [§14](#14-gp-age-and-accuracy)).

- `;`/`.` move.
- **ENTER** — jump straight to **Track** for that satellite.
- `m` — open the live **World map** (all footprints; see [World map](#world-map)).
- `r` — recompute the schedule.
- `z` — **deep-sleep** until ~60 s before the next AOS (see [§12](#12-aos-alarm-and-deep-sleep)).
- `` ` `` — back.

### Passes

Upcoming passes for the selected satellite, in UTC: **AOS time, duration, max
elevation, LOS time.** The element-set age is shown top-right, color-graded.

- `;`/`.` — select a pass.
- `d` — open the **pass-detail plot** (below).
- `t` or **ENTER** — go to **Track**.
- `n` — add a **manual transponder** for this satellite (see below).
- `r` — recompute the pass list.
- `x` — **mutual window**: enter a remote grid to find co-visibility passes (below).
- `v` — **10-day pass overview** (InstantTrack-style visibility chart; below).
- `i` — **illumination** (60-day Sun/eclipse raster; below).
- `` ` `` — back to Satellites.

### Pass detail

An **elevation-vs-time curve** for the selected pass. The curve is drawn
**yellow where the satellite is sunlit** and **blue where it is in eclipse**. A
cyan vertical line marks "now" if the pass is in progress. Below the plot:
AOS time and azimuth, LOS time and azimuth, maximum elevation, pass duration, and
the percentage of the pass spent in sunlight. Press **`p`** for a **polar view of
this pass** — its ground track across the sky with **A**/**L** (AOS/LOS) markers
and an arrow showing the direction of travel; `p` toggles back to the curve.
`` ` `` or ENTER returns to Passes.

### Mutual windows (co-visibility)

From **Passes**, press **`x`** and enter a remote station's **Maidenhead grid**
(4 or 6 characters, e.g. `IO91` or `JN58td`). CardSat scans the selected
satellite's upcoming passes **over the next 10 days** and lists every window
where **both you and that station have the satellite above your horizons at the
same time** — the periods
you could actually make a contact through the bird. Each row shows the **start
time (UTC)**, the **duration** (m:ss), and the **maximum elevation at each end**
— yours (`me`) and theirs (`dx`) — so you can judge how workable the window is.
`;`/`.` scroll; `` ` `` returns to Passes.

> Co-visibility is computed to the **0° horizon** for both stations; a window
> with very low max elevations may be hard to use in practice. The remote grid
> is assumed at sea level.

---

### 10-day pass overview (`v`)

From **Passes**, press **`v`** for an at-a-glance chart of the selected
satellite's passes over the **next 10 days**, modelled on InstantTrack's
"Multiple Days for Single Satellite" visibility screen. Each row is one day
(today at the top), drawn as a 24-hour timeline (00–24 h UTC, left to right) with
faint gridlines at 06/12/18 h. The window is aligned to UTC midnight and spans
ten **full** days, so every row is filled edge to edge — the chart is not cut off
partway through the last day at the time you opened it. Every pass is a coloured
bar from AOS to LOS, shaded by peak elevation — **dim green** below 15°,
**green** to 40°, **yellow** above — so high passes stand out. A red tick on the
top row marks the current time. **`;`/`.` scroll one day at a time** — the oldest
day falls off the top and a new day appears at the bottom (you can scroll forward
indefinitely, but not before today). `r` recomputes; `` ` `` returns to Passes.

---

### Illumination (`i`)

From **Passes**, press **`i`** for a **60-day solar-illumination raster** in the
style of DK3WN's *illum*. The horizontal axis is date (today at the left edge,
**+60 d** at the right); the vertical axis is one **orbital period** (its length
in minutes is printed at the right). Each cell is shaded **yellow when the
satellite is in sunlight** and left **dark when it is in Earth's shadow
(eclipse)**, so the eclipse band — and the "sunline" at its edge — stands out,
widening and narrowing as the orbit plane precesses relative to the Sun and
vanishing entirely during **full-sun seasons** (handy for judging solar-panel
charging). Below the raster a live readout shows the **current status**
(`SUN`/`SHADOW`), the **eclipse minutes per orbit** and percentage for the
current orbit, and the time to the next Sun↔shadow transition. **`,`/`/` scroll
one day at a time** through the 60-day window (forward indefinitely, not before
today). `r` recomputes; `` ` `` returns to Passes.

> Eclipse uses a cylindrical-shadow model (no penumbra) and the raster is
> sampled, so the band edges and transition times are good to about a minute.

---

### Track

The main operating screen. Top to bottom it shows: **azimuth / elevation** (and
GP age), **range / range-rate** (and an orange **ECL** flag in eclipse), the
selected **transponder**, the **downlink (DN) and Doppler-corrected receive (RX)**
frequencies, the **uplink (UP) and transmit (TX)** frequencies, the **passband**
position line, the **calibration** line, and the **radio status**. On an FM bird
that needs a subaudible **PL/CTCSS tone**, a **`PL nn.n Hz`** line appears (orange
when the radio is on and the rig supports CAT tone, grey otherwise, or
`PL nn.n Hz (rig n/a)` if the selected rig can't set tone over CAT).

Controls:

- `m` — switch between **TUNE** and **CAL** modes.
- `d` — cycle the **Doppler tune mode** (linear birds): FULL One True Rule →
  downlink-only → uplink-only → hold-both. The passband line shows the active mode.
- `l` — **Log QSO** (also on the Polar screen): capture the contact you're working.
  Radio control keeps running while you fill in the entry.
- `t` — cycle to the next transponder.
- `c` — set the **CTCSS/PL tone** for this satellite (numeric entry: a tone in
  Hz, `0` to force it off, or blank to revert to the built-in default).
- `r` — turn radio output **on/off** (sets modes and begins Doppler service).
- `p` — open the **Polar** plot.
- `f` — open **Manual mode** (frequency calculator; see below). Useful when you
  have no CAT-controlled radio and are tuning by hand.
- **ENTER** — save the current calibration **for this satellite**.
- `` ` `` — first press stops the radio output; second press goes back.

TUNE-mode keys: `,`/`/` tune down/up the passband, `s` cycle step
(100/1000/5000 Hz), `x` recenter. CAL-mode keys: `,`/`/` trim downlink, `;`/`.`
trim uplink, `s` cycle step (10/100/1000 Hz), `x` zero. See
[§9](#9-doppler-tuning-and-the-one-true-rule) and [§10](#10-calibration).

### Manual mode (`f` from Track)

**Manual mode** is for operating without a CAT-controlled radio: it shows the
same live data as Track but **never commands a radio or rotator**. Instead, you
fix one leg — the frequency you'll hold on your own rig — and it shows the
**Doppler-corrected frequency to tune the other leg to**, updated live, with your
saved calibration applied.

The two frequency rows are marked **HOLD** (the leg you park on your radio, shown
at its nominal value without Doppler) and **TUNE>** (the leg you must follow,
shown Doppler-corrected). Press **`u`** to toggle which leg is fixed:

- **Linear birds** — fixing the downlink shows the uplink to transmit (and vice
  versa). `,`/`/` move the fixed frequency through the passband, `s` cycles the
  step, `x` recenters. The HOLD leg's parked value moves with the passband; the
  TUNE> leg follows with Doppler.
- **FM birds** — pick which leg is fixed with `u` (typically the **VHF** leg,
  which has little Doppler and is the one you park). The other (UHF) leg shows
  the Doppler-corrected frequency to chase. A hint line notes which leg is fixed
  and whether it's VHF.
- **Downlink-only birds** — just the computed downlink to tune your receiver to.

`m` toggles **CAL** (trim the same per-satellite calibration as Track), `t`
cycles transponder, `l` logs a QSO, `p` opens the polar plot, `g` opens live
workable grids — and **the log, polar, and grid screens all return here** rather
than to Track. **ENTER** saves calibration for this satellite. `` ` `` or `f`
returns to Track.

### Polar

A sky plot centered on the zenith: N/E/S/W cardinal labels, elevation rings at
30°/60°, the horizon as the outer ring. A green dot marks the satellite; a yellow
glyph marks the **Sun** (when it's above the horizon). The satellite's **path
across the sky for the current pass** is drawn as a cyan arc with **A** (AOS) and
**L** (LOS) markers and a white arrowhead showing the **direction of travel**.
When the bird is below the horizon the arc shows the **next** pass instead, and
the readout gives that pass's AOS time. The right-hand readout also shows az/el,
range, range-rate, **Sun az/el**, and whether the satellite is **SUNLIT** or in
**ECLIPSE**. `p`, ENTER, or `` ` `` return to Track.

### Workable grids (`g`)

The 4-character Maidenhead grid squares currently inside the satellite's
footprint — every grid from which a station could work the bird at the same
time you can. Reached two ways:

- `g` from **Passes** — the **union** of grids covered across the selected pass
  (sampled about once a minute from AOS to LOS), computed once on entry.
- `g` from **Track** — the grids under the footprint **right now**, refreshed
  about every 3 s. Radio and rotator tracking keep running while you view it.

Grids are listed six per row in alphabetical order. The **workable count** is
shown on its own cyan line at the top of the list (the header had no room for
it), and when the list spills past one page that line also shows the visible
window, e.g. `1370 workable  (1-48)`. `;`/`.` scroll a row at a time and `{`/`}`
page. Coverage is computed with a per-grid bitset, so there is **no cap** on the
number of grids — it works for any amateur satellite, including high orbits (a
~2500 km bird floods roughly 4500 grids). `` ` `` returns to whichever screen
opened it.

### Workable US states (`w`)

The US states (and DC) currently inside the satellite's footprint — the state
equivalent of the workable-grids screen, reached the same ways and in the same
modes:

- `w` from **Passes** — the **union** of states covered across the selected
  pass, computed once on entry.
- `w` from **Track** (or **Manual**) — the states under the footprint **right
  now**, refreshed about every 3 s, with radio and rotator tracking still
  running.

States are listed by their two-letter USPS code, six per row, alphabetically,
with the same cyan workable-count line and `;`/`.` · `{`/`}` scrolling as the
grids screen. Membership is decided by a point-in-polygon test against bundled
**simplified** state boundaries (about 0.1°/11 km resolution), so a footprint
grazing a state line may briefly claim both neighbours — fine at footprint
scale, where both are in fact workable. AK, HI and DC are included. `` ` ``
returns to whichever screen opened it.

### Workable DXCC (`e`)

The DXCC entities currently inside the footprint — the same idea again, for
**DXCC chasing**, reached the same ways (`e` from Passes for the per-pass union,
or from Track / Manual for live now). Entities are listed by common prefix
(e.g. `DL`, `JA`, `VK`, `9V`), five per row, with the same workable-count line
and scrolling.

**Coverage and accuracy.** All **340 current DXCC entities** are included, via a
hybrid model: the major countries use simplified boundary polygons (so the right
country is picked from the footprint geometry), while the long tail of islands and
micro-entities is represented by each entity's reference coordinate from `cty.dat`
and counted as workable when that point falls within the footprint (plus a small
claim radius). Country borders are coarse, so a footprint near a border may list a
neighbour too, and a single-point entity is claimed as a unit rather than by exact
shape. Treat this as **chasing guidance** — which entities are reachable on the
pass — and confirm the actual entity worked from your own log. `` ` `` returns to
whichever screen opened it.

### Location

- `e` / `o` / `a` — edit latitude / longitude / altitude.
- `g` — set position from a **Maidenhead grid** square.
- `p` — enable/disable **GPS**.
- `s` — cycle the **GPS source** (Grove 9600, Grove 115200, Cap LoRa868, Cap LoRa1262).
- `c` — set the **UTC clock** manually (`YYYY-MM-DD HH:MM:SS`).
- `` ` `` — back (applies the position to the predictor).

### Update

- `k` or **ENTER** — update GP data from your configured source and sync the clock
  (NTP). The same action also refreshes the AMSAT OSCAR **activity marks** shown on
  the Satellites list, the **space-weather** data (solar flux + Kp), **and** the
  terrestrial **weather** for your site, so one press brings everything current. The
  Update screen notes this so it's clear `k` does more than GP.
- `a` — fetch and cache **all** transponders for offline use.
- `w` — connect WiFi only (no download).
- `` ` `` — back. Diagnostics print to the serial monitor at 115200.

### Settings

Settings are grouped into four submenus — **Radio / CAT**, **Rotator**,
**Station / display**, and **Network / data**. `;`/`.` move; ENTER opens a submenu
(or, inside one, edits a text field or runs an action); `,`/`/` change an adjustable
row; `` ` `` backs out to the submenu list, then home. Press `h` anywhere for the
on-screen key reference. The notable rows:

| Row | Action |
|---|---|
| Radio | `,`/`/` select model (auto-sets address + baud) |
| CI-V addr | ENTER → edit (hex); Icom only |
| CAT baud | `,`/`/` cycle 1200…115200 (incl. 57600) — applies to all radio protocols |
| Min pass el | `,`/`/` 0–30° |
| Decay solar | `,`/`/` cycle assumed solar activity **mean → min → max → auto** for the orbital-analysis decay estimate (changes the headline number and the bracket). **auto** uses the live F10.7 flux fetched with GP data |
| Weather units | `,`/`/` cycle the units for the **Weather** screen: **°F, mph → °C, km/h → °C, m/s**. Under *Station / display*. |
| WiFi SSID | ENTER → edit · **`s`** → scan for networks and pick one |
| WiFi pass | ENTER → edit |
| Save & test WiFi | ENTER → connect and report OK/FAIL |
| AOS alarm | `,`/`/` or ENTER toggle on/off |
| Rotator (+ type / host / port / baud / ranges / offsets / deadband / park / pre-point) | `,`/`/` adjust, ENTER edits host/port. **Rot type** cycles **GS-232 → rotctl (net) → PstRotator → Yaesu (direct)**; see [§17](#17-antenna-rotator-gs-232-rotctl-pstrotator-rotctld-server) |
| GP source | ENTER → **source picker**: AMSAT (default), any CelesTrak JSON-PP category (Amateur Radio listed first), or **Custom URL…** — see [§14](#14-gp-age-and-accuracy) |
| VFO Type | `,`/`/` or ENTER toggle *Main Up/Sub Dn* ↔ *Main Dn/Sub Up* |
| Sat mode | `,`/`/` or ENTER toggle the rig's satellite mode on/off |
| CAT rate | `,`/`/` adjust the CAT update period in 10 ms steps (default 500 ms; soft-floored to what the CAT baud can service) |
| CAT delay | `,`/`/` adjust the pause after each command, 0–200 ms in 2 ms steps (default 70 ms; CI-V/Icom only) |
| Screen sleep | `,`/`/` cycle off / 30 s / 1 min / 2 min / 5 min — blanks the backlight after that idle time |
| My callsign | ENTER → enter your station callsign (stored uppercase); used in the log and ADIF `STATION_CALLSIGN` |
| QRZ user / QRZ pass | ENTER → enter your QRZ.com username / password for the **QRZ Lookup** screen (requires a QRZ XML-data subscription). Password shown masked. Under *Network / data*. |
| Backup config+favs → SD | ENTER → copy config + favorites to `config.bak` / `favs.bak` |
| Restore config+favs | ENTER → restore them from the backup files |
| **Reset all data** | ENTER → type **ERASE** to wipe everything (red row) |
| CAT type (+ host / port / user / pass) | `,`/`/` cycle **Wired CI-V** → **Icom LAN** → **rigctl (net)**. Icom LAN drives a network Icom over RS-BA1 (see [§3](#3-connecting-your-radio)); **rigctl** drives a radio attached to a remote Hamlib **rigctld** server over TCP (host/port, default 4532). Under *Radio / CAT*. |
| Rigctld server (+ port) | `,`/`/` enable a **rigctld server** so a PC (Gpredict, WSJT-X, a logger) drives the radio through CardSat over TCP (default 4532); VFOA=downlink, VFOB=uplink. Under *Radio / CAT*. |
| Rotctld server (+ port) | `,`/`/` enable a **rotctld server** so a PC drives the **wired GS-232 rotator** through CardSat over TCP (default 4533). Under *Rotator*. |
| Rotator: manual control | ENTER opens a screen to jog az/el by hand with live read-back. Under *Rotator*. |

### WiFi scan

Reached from **Settings** by pressing **`s`** on the **WiFi SSID** row. CardSat
scans for nearby networks and lists them strongest-signal first, with each
network's RSSI (dBm) and a `*` for secured networks.

- `;`/`.` — select a network.
- **ENTER** — use it: the SSID is saved and you're taken to password entry
  (open networks skip the password and return to Settings).
- `r` — rescan.
- `` ` `` — back to Settings.

### Edit

A simple text entry box (for the fields above and manual GP/transponder/time
entry). Type to append, **DEL** to backspace, **ENTER** to confirm, `` ` `` to
cancel.

**Adding a manual satellite** (from Satellites → `n`): you'll be prompted in
order for the **name**, **NORAD ID**, **epoch** (`YYYY-MM-DD HH:MM:SS`, UTC), then
the GP mean elements — **inclination**, **RAAN**, **eccentricity** (e.g.
`0.0006190`), **argument of perigee**, **mean anomaly**, **mean motion**
(rev/day), and **BSTAR** (drag; `0` is fine if unknown). The satellite is stored
with the downloaded ones and persists across GP refreshes.

**Adding a manual transponder** (from Passes → `n`): you'll be asked, in order,
for **Downlink low (Hz)**, **Uplink low (Hz, 0 = none/beacon)**, **Downlink high
(Hz, 0 = single channel/FM)**. If you gave a downlink high above the low *and* an
uplink, it's treated as **linear** and you'll also be asked **Uplink high (Hz,
0 = same bandwidth)**, **Inverting? (y/n)**, and finally the **Mode**. Single-
channel entries skip straight to Mode. Manual transponders are stored separately
from the SatNOGS cache so a GP/transponder refresh won't erase them. Up to **64
transponders** are held per satellite (SatNOGS plus your manual ones) — enough
for even the busiest birds, such as the ISS (~50 transmitters on SatNOGS).

---

## 9. Doppler tuning and the One True Rule

A satellite's motion shifts both the frequency you **receive** (downlink) and the
frequency the satellite **receives from you** (uplink). CardSat corrects both,
continuously.

CardSat follows the AMSAT **"One True Rule"** (Paul Williamson, KB5MU): *tune both
the transmitter and the receiver to achieve a constant frequency at the
satellite.* In practice the firmware holds your chosen spot in the transponder
fixed **as the satellite sees it**, and applies the Doppler correction to **both**
the receive and transmit frequencies every cycle. The result: you tune somebody
in, let go, and nobody drifts through the passband.

The corrections are:

```
RX = downlink × (1 − β) + calDl      (tune your receiver here)
TX = uplink   ÷ (1 − β) + calUl      (transmit here)
β  = range-rate ÷ c                  (c = speed of light)
```

where `downlink`/`uplink` are the satellite-side frequencies of your chosen
passband spot, and `calDl`/`calUl` are your calibration offsets ([§10](#10-calibration)).

### Choosing your spot in the passband

On a **linear transponder** you can move through the passband two ways:

- **TUNE mode** (device keys): press `m` until the passband line shows `<TUNE>`.
  `,`/`/` move your operating point down/up; `s` cycles the step; `x` recenters.
  The `PB` line shows your offset from band center, the half-width, and `INV` for
  inverting birds.
- **Radio-knob mode** (One True Rule, the natural way): press `d` to cycle the
  tune mode. In **FULL** (passband line shows `<FULL>` in orange) just **turn the
  radio's tuning knob** to move around the passband — CardSat reads your downlink,
  works out where you are, and keeps both legs Doppler-corrected around that fixed
  satellite frequency. Let go and you stay put. Pressing `d` cycles on through
  **downlink-only** (`<DL>` — One True Rule on the downlink, uplink left alone),
  **uplink-only** (`<UL>` — only the transmit leg is corrected; handy when an SDR
  or second receiver handles the downlink, and it needs no frequency read-back so
  it works even on set-only rigs), and back to **hold-both** (`<TUNE>`, device-key
  tuning). FULL and downlink-only need a rig that reports frequency; the cycle
  skips them otherwise.

For an **inverting** transponder the uplink moves opposite to the downlink (tune
the downlink up, the uplink goes down); for a non-inverting one they track
together. CardSat handles the mapping automatically.

### Sidebands

For linear transponders CardSat sets the rig's modes for you: **USB on the
downlink, LSB on the uplink** (because an inverting linear transponder flips the
spectrum, so an LSB uplink is heard as a normal USB downlink). The exception is
any bird with an uplink or downlink **below 30 MHz (HF)**, which uses **USB up and
USB down**. FM and single-channel birds use the transponder's own mode on both
legs.

---

## 10. Calibration

Real radios and satellites have small oscillator offsets. **CAL mode** lets you
null them out per satellite.

1. On **Track**, press `m` until the cal line shows `<CAL>`.
2. Find a known signal (your own downlink, or the satellite's beacon).
3. Trim the **downlink** with `,`/`/` and the **uplink** with `;`/`.`. `s` cycles
   the step (10/100/1000 Hz); `x` zeroes both.
4. Press **ENTER** to save the calibration **for this satellite** — it's stored
   and reloaded automatically next time you track that bird.

The passband position is *not* persisted (it's per-pass); calibration *is*.

---

## 11. Working a pass, step by step

**An FM bird (e.g. a repeater satellite):**

1. **Update → k** to refresh GP data (and the clock) if you have WiFi.
2. **Satellites** → highlight the bird → `f` to favorite it → **ENTER** to open
   Passes.
3. Pick the pass you want; press `d` to preview its elevation curve if you like.
4. At AOS, **ENTER/t** to Track. Press `r` to start sending to the radio.
5. CardSat sets FM on both legs and Dopplers the single channel. Talk.

**A linear (SSB/CW) bird:**

1–4 as above. On Track you'll start in **TUNE** (press `d` to cycle to FULL radio-knob mode).
5. Press `r` to enable radio output. CardSat sets **USB down / LSB up** (or
   USB/USB on HF) and corrects both legs.
6. Tune to a clear spot — with the device `,`/`/` keys, or by turning the rig's
   knob in `<FULL>` mode. Your downlink stays put; the uplink tracks (inverted if
   the bird inverts).
7. If your own signal isn't centered, switch to **CAL** (`m`), trim, and **ENTER**
   to save.

Watch the **ECL** flag and the polar Sun glyph — some birds change behavior in
eclipse.

---

## 12. AOS alarm and deep sleep

**AOS alarm** (toggle in Settings → *AOS alarm*): when enabled and you have
favorites and a set clock, CardSat tracks the soonest upcoming favorite AOS in the
background. It **beeps** at T-60 s, T-30 s, and T-10 s, then sounds a longer
double-beep and shows a blinking **"AOS!"** banner at acquisition. Once a pass is
underway it also chirps at **TCA** (closest approach / peak elevation — a double
mid-tone) and at **LOS** (a descending two-tone), so you can follow a pass by ear
without watching the screen. All of these sounds are governed by this one setting:
turning the AOS alarm off silences the AOS, TCA and LOS cues together. A small
orange countdown banner appears on any screen within the last minute.

**Deep sleep** (Next Passes → `z`): CardSat computes the next favorite AOS and
puts the ESP32 into deep sleep until **~60 s before** it, dramatically extending
battery life between passes. The screen shows how long it will sleep and for which
satellite. On wake, the unit reboots straight to **Next Passes** with the pass
imminent and the alarm ready. (Press the reset button to wake early.) The clock is
preserved across deep sleep, but make sure it was set — via NTP or GPS — before
sleeping.

---

## 13. Sun and eclipse

CardSat computes the Sun's position and whether the satellite is illuminated:

- **Polar screen** — a yellow Sun glyph at its az/el (when above the horizon), the
  Sun's azimuth/elevation in the readout, and **sat SUNLIT** (green) or **sat
  ECLIPSE** (orange).
- **Track screen** — an orange **ECL** flag when the satellite is in Earth's shadow.
- **Pass detail** — the elevation curve is colored yellow (sunlit) / blue
  (eclipse), and a sunlit-percentage is given for the pass.

This uses a low-precision Sun ephemeris and a cylindrical Earth-shadow model — the
sunlit↔eclipse transition is a hard edge rather than the few-second penumbral
fade, which is plenty for knowing whether a bird has power or is optically visible.

### Sun / Moon antenna tracking

**Sun / Moon** on the main menu shows the live position of both bodies from your
location. It opens in a **graphical sky view**: a polar dome (zenith at centre,
North up, horizon at the rim) with the Sun drawn as a rayed yellow disc and the
Moon as a cyan crescent, so you can see at a glance where each one is. A compact
panel on the right lists azimuth, elevation and above/below-horizon for both. A
body below the horizon is shown faintly just outside the rim so its bearing is
still readable. Press **`g`** to toggle between the graphic and a plain
azimuth/elevation data list. `;`/`.` selects which body the rotator follows
(marked with a green ring, not a highlight bar). Press **`o`** to drive the
rotator at the selected body — useful for antenna gain checks against Sun noise,
EME pointing, or rotator calibration against a visible target. **`x`** stops;
**`` ` ``** parks and disengages.

Behavior notes:

- The rotator has **one master at a time**: engaging Sun/Moon tracking disengages
  satellite rotator tracking and vice versa, and opening **Rotator manual**
  control disengages both.
- While the selected body is **below the horizon** the rotator parks and waits;
  tracking resumes automatically when it rises.
- Tracking keeps running if you navigate to other screens — an orange **SUN** /
  **MOON** tag in the header shows it's active. Going back from the Sun/Moon
  screen parks and disengages.
- The Moon ephemeris is a low-precision series (~arc-minutes after topocentric
  parallax correction) — far finer than any amateur antenna beamwidth. The Sun
  is good to ~0.01°.

### Space weather

**Space Wx** on the main menu summarises the indices that matter most for
propagation: the **solar 10.7 cm radio flux** (F10.7, a proxy for solar activity
and ionospheric ionisation), the **planetary Kp index** (geomagnetic disturbance,
0–9), and the **running A index** (the daily-equivalent geomagnetic amplitude,
shown when the feed provides it). They're fetched from NOAA SWPC together with GP
updates, or on demand with **`r`** (needs WiFi). The Kp and A fetch is independent
of the flux fetch, so a hiccup in one never suppresses the other. Each value is
labelled in plain terms — flux low/moderate/good/very-high, Kp and A
quiet/unsettled/storm — and colour-coded, with a short **operating outlook** line
translating the numbers into what to expect on HF and satellite paths, plus a note
of how old the data is.

This is a planning cue, not a forecast: the flux and Kp are observed values, and
the outlook text is a simple heuristic reading of them, not a calibrated
propagation prediction. A high Kp (storm) is the main thing to watch — it warns of
auroral flutter on VHF and disturbed high-latitude HF.

### Weather

**Weather** on the main menu (just below Space Wx) shows current conditions and a
short forecast for your operating site — handy for portable and field operation. It
displays the current temperature and sky condition, wind speed/direction and
humidity, then a row for each of the next few days with the day's condition, high/
low, and chance of precipitation.

The data comes from **Open-Meteo** (open-meteo.com), a free, no-key weather service.
The location is taken from the same site coordinates the prediction engine uses
(your GPS fix or manually set lat/lon), so set your location first. Weather is
fetched when you run **Update**, and also refreshes automatically on entry to the
screen if WiFi is already connected; **`r`** forces a refresh. Like Space Wx, the
last result is cached to flash, so it remains viewable offline with a note of its
age.

Units (°F·mph, °C·km/h, or °C·m/s) are selectable in *Settings → Station / display
→ Weather units*; changing them re-labels the cached values immediately without
needing a re-fetch.

*Weather data by Open-Meteo.com, licensed under CC BY 4.0.*

### Transponder database

From the **Satellites** list, press **`t`** to browse every transponder and
beacon entry the on-device catalog holds for the selected satellite (sourced from
SatNOGS with the transponder cache). Each entry is shown as a short block: its
description, the **downlink** (a range for linear transponders) with mode, and the
**uplink** with any CTCSS tone and inverting/linear flags. `;`/`.` scrolls through
the list. It's a quick offline reference for a bird's frequencies and modes — handy
for checking what a satellite carries without a radio connected. If nothing shows,
the transponders haven't been cached yet; run **Update** with WiFi on.

### QRZ callsign lookup

**QRZ Lookup** on the main menu looks up a callsign in the **QRZ.com** database and
shows the operator's name, mailing address, country, grid square and licence class.
It uses QRZ's XML data service, which **requires a QRZ XML-data subscription**.

To use it, enter your QRZ **username** and **password** in *Settings → Network /
data* (rows **QRZ user** / **QRZ pass**). Then open **QRZ Lookup**, press **ENTER**,
type a callsign, and press ENTER again. CardSat logs in to QRZ, caches the session
key, and displays the result; the key is reused for subsequent lookups until it
expires (then it re-logs in automatically).

The screen handles the obvious cases plainly:

- **No WiFi** — it simply says WiFi isn't connected; connect via Update or Settings
  and try again.
- **No credentials** — it explains that a QRZ XML subscription is required and that
  you need to enter your username and password in Settings.
- **Not found / login error** — the QRZ error message (e.g. "Not found", "password
  incorrect") is shown in the status line.

Your QRZ password is stored on the device the same way the WiFi and radio-LAN
passwords are; it is shown masked in Settings. CardSat talks to QRZ over HTTPS.

---

## 14. GP age and accuracy

SGP4 predictions degrade as the GP mean elements age. CardSat shows the **age of
the element set** (days since the GP epoch) on the Passes and Track screens, color
coded:

- **Green** — under 14 days (fresh).
- **Yellow** — 14–28 days (getting old).
- **Red** — over 28 days (stale; expect timing/pointing error). In the Next Passes
  schedule, stale sats are flagged with a red **`!`**.

Refresh with **Update → k** whenever you have WiFi. For low orbits, weekly (or
fresher) elements are best.

### Choosing the element source

By default CardSat pulls the **AMSAT** daily GP bulletin (all amateur satellites,
JSON, including the friendly `AMSAT_NAME`). To change source, open **Settings → GP
source** and press ENTER for a picker:

- **AMSAT (amateur)** — the default bulletin.
- **CelesTrak categories** — any CelesTrak GP group in JSON-PP format. Amateur
  Radio is listed first, followed by SatNOGS and the Special-Interest, Weather &
  Earth Resources, Communications, Navigation, Scientific and Miscellaneous
  groups. CelesTrak omits `AMSAT_NAME`, so CardSat falls back to `OBJECT_NAME`
  and the data parses correctly.
- **Custom URL…** — type any URL that returns OMM JSON / JSON-PP.

Move with `;`/`.` (or `{`/`}` to page) and press ENTER to select. The choice is
saved immediately and used by the next **Update → k**.

---

## 15. Working offline

CardSat is designed to operate with no network in the field:

1. With WiFi, go to **Update** and press `k` (GP + clock) and then `a` (cache
   **all** transponders). Both are written to flash.
2. After that, everything — pass prediction, transponders, Doppler, the schedule —
   works with WiFi off. The clock keeps running (and survives deep sleep); use GPS
   or manual `c` entry to set it where there's no NTP.

Cached data persists across power cycles until you refresh it or perform a reset.

---

## 16. Radio-specific notes

CardSat speaks three CAT dialects, selected by the **radio model** in Settings.
Defaults are editable; the **CI-V address** field applies to **Icom only**.

| Radio | Family / protocol | Default baud | Interface | Read-back |
|---|---|---|---|---|
| IC-820 | Icom CI-V | 9600 | CI-V 5 V single-wire | yes |
| IC-821 | Icom CI-V | 9600 | CI-V 5 V single-wire | yes |
| IC-910 | Icom CI-V | 19200 | CI-V 5 V single-wire | yes |
| IC-970 | Icom CI-V | 9600 | CI-V 5 V single-wire | yes |
| IC-9100 | Icom CI-V | 19200 | CI-V 5 V single-wire | yes |
| IC-9700 | Icom CI-V | 19200 | CI-V 5 V single-wire | yes |
| FT-847 | Yaesu 5-byte | 57600 | serial (TTL/RS-232) | yes¹ |
| FT-736R | Yaesu 5-byte | 4800 | serial (TTL/RS-232) | no |
| TS-790 | Kenwood ASCII | 4800 | RS-232 (MAX3232) | yes |
| TS-2000 | Kenwood ASCII | 57600 | RS-232 (MAX3232) | yes |

Protocol command sets follow the Hamlib backends (`icom`, `yaesu/ft847`,
`yaesu/ft736`, `kenwood/ts2000`, `kenwood/ts790`). Serial framing is set
automatically: **Icom/Kenwood 8N1, Yaesu 8N2.**

The CAT *interface circuit* differs by family. **Icom CI-V** uses a 3.3 V-safe
single-wire interface (**[CIV_INTERFACE.md](CIV_INTERFACE.md)**); **Yaesu** and
**Kenwood** use RS-232-level serial — build a **MAX3232** level shifter as in
**[RS232_INTERFACE.md](RS232_INTERFACE.md)** (⚠️ untested; build at your own risk).

### Automatic PL / CTCSS tone for FM satellites

Several FM birds require a subaudible **PL (CTCSS) tone** on the uplink. CardSat
applies it automatically: when the active transponder is **FM with an uplink** and
the satellite is in its built-in tone table, it enables the rig's **TX CTCSS
encoder** at the right frequency the moment radio output is on, turns it **off**
again when you switch to a transponder that doesn't need one, and disables it when
you turn radio output off (so your rig isn't left transmitting a tone). The tone in
use is shown on the Track screen as a `PL nn.n Hz` line, and a status flash
confirms it (`PL 67.0 Hz on uplink`).

Built-in tones (operating tone, by NORAD id): **ISS** 67.0, **SO-50** 67.0,
**AO-91** 67.0, **AO-92** 67.0, **PO-101** 141.3. SatNOGS carries no structured
tone field, so this list lives in `SatDb::knownCtcssHz()` — extend it there as new
FM satellites appear. **SO-50 note:** the 67.0 Hz figure is the *operating* tone;
arming its 10-minute timer with a 74.4 Hz burst is a separate manual step on your
radio.

**Setting tones yourself.** Press **`c`** on the Track screen to set a tone for the
current satellite. Enter a frequency in Hz (e.g. `67.0`, `141.3`) and it is snapped
to the nearest standard CTCSS tone; enter `0` to force the tone **off** for that
bird; leave it **blank** to clear your override and fall back to the built-in
default. The override is stored per satellite (by NORAD, in `tones.txt`) and wins
over the table, so it survives reboots and a GP/transponder refresh, and it's how
you add a tone for any FM bird not already in the built-in list.

Tone-over-CAT is supported on **IC-910/9100/9700, FT-847, and TS-2000** (the
`hasTone` flag in `radio_profiles.h`); on other models the `PL` line reads
`(rig n/a)` and no tone command is sent. Encoders by family: **Icom** CI-V `1B 00`
(tone freq, BCD) + `16 42` (encoder on/off); **FT-847** sat-TX opcodes `2B`
(tone, via the CAT code table) + `4A…2A`/`8A…2A` (encoder on/off); **TS-2000**
`TN` (tone number) + `TO` (encoder on/off). All are taken from the Hamlib
backends; like the rest of CAT they're host-verified but not yet exercised on a
real radio — watch the serial trace and confirm the rig shows the tone.

**Icom.** CardSat drives MAIN/SUB directly and forces the rig's built-in satellite
mode **off** at startup (IC-9100/9700; the others have no such mode). Downlink on
SUB, uplink on MAIN. MAIN/SUB select uses CI-V `0x07 D0/D1`, verified against the
IC-821H manual. Read-back uses `0x03` and works on all six (including the
IC-820/821/970, per Hamlib). Wrong-VFO fixes live in `radio_profiles.h`.

**Icom over the network.** Any of these Icoms with a LAN port (IC-9700, plus the
IC-705/7610/785x) can be driven over WiFi/Ethernet instead of the CI-V bus — set
**CAT type → Icom LAN** in Settings. CardSat carries the *same* CI-V frames shown
below inside the RS-BA1 UDP protocol, so MAIN/SUB, read-back, sat mode and CTCSS
behave identically; only the transport changes. Setup is in
[§3](#3-connecting-your-radio); the wire protocol is documented in
`ICOM_LAN_PROTOCOL.md`.

**Yaesu (FT-847, FT-736R).** 5-byte CAT (four data bytes + opcode), big-endian BCD
at 10 Hz. CardSat enables CAT at startup and sets the satellite RX (downlink,
opcode `0x11`) and TX (uplink, `0x21`) VFOs directly.

¹ **FT-847 read-back** uses "read freq & mode" (`0x03`, patched to `0x13` for the
SAT-RX/downlink VFO), which returns 4 big-endian BCD bytes + a mode byte. This works
only on **firmware-updated** FT-847s — early units have no read capability and stay
silent (CardSat times out and holds the passband). In satellite mode the radio can
occasionally return the uplink VFO instead (Hamlib #1286); CardSat rejects any read
that jumps more than 1 MHz from the commanded downlink, so a stray reply holds steady
rather than jerking the passband. So **radio-knob (One True Rule) tuning works on a
firmware-updated FT-847**; on older units, use the device **TUNE** keys.

The **FT-736R** can't report frequency over CAT at all (TUNE keys only), and its
native opcodes differ from the FT-847 — the proven path is an **FT-847-emulating**
interface (KA6BFB / HS-736USB), in which case select **FT-847**. Put either radio in
**VFO** (and its satellite mode in VFO) before enabling CAT.

**Kenwood (TS-790, TS-2000).** ASCII commands terminated by `;`: downlink on
**VFO A** (`FA<11-digit Hz>;`), uplink on **VFO B** (`FB…;`), mode via `MD<n>;`,
read via `FA;`. Connects over **RS-232** (DB-9) — use a **MAX3232** level shifter,
not the CI-V circuit. On the TS-2000, bridge **CTS/RTS** (or use "RTS +12 V") for its
handshake quirk and verify the VFO-A/B ↔ main/sub-band behaviour for your firmware.
The TS-790 supports a subset of the same commands.

> **All Yaesu/Kenwood sat rigs:** CAT **cannot switch the band pair.** You select
> the uplink/downlink bands and engage the rig's own satellite / full-duplex mode
> **on the radio**; CardSat Doppler-tunes within that. (Same as SatPC32.)

### Watching the CAT trace

CardSat prints **every CAT frame it sends** to the serial monitor at **115200
baud**, decoded, with the radio's reply where the protocol provides one. Connect
over USB, open a serial monitor at 115200, and you'll see traffic like:

Icom (CI-V):
```
[CI-V TX] FE FE A2 E0 07 D1 FD  sel-band SUB
[CI-V TX] FE FE A2 E0 05 00 60 58 45 14 FD  set-freq 145456000 Hz
[CI-V RX] radio ACK (FB)
[CI-V TX] FE FE A2 E0 03 FD  read-freq req
[CI-V] SUB freq read: 145456000 Hz
```

Yaesu / Kenwood:
```
[CAT TX] 14 59 00 00 11  set-freq RX 145900000 Hz
[CAT TX] 43 59 00 00 21  set-freq TX 435900000 Hz
[CAT TX] FA00145900000;
[CAT TX] MD2;
[CAT RX] FA00145900000;
[CAT] VFO-A (downlink) read: 145900000 Hz
```

How to read it:

- **`[CI-V TX]` / `[CAT TX]`** — a frame sent to the radio (raw bytes, or the ASCII
  string for Kenwood) plus a decode: which VFO, set-freq `<Hz>`, set-mode, read
  request, or CAT-on.
- **`[CI-V RX] radio ACK (FB)`** — Icom accepted the command; **`NAK (FA)`** means it
  rejected it. (Yaesu set commands aren't acknowledged.)
- **`… freq read: <Hz>`** — the frequency read back (Icom/Kenwood) used by radio-knob
  One True Rule tuning; `no valid reply` if it failed.

This is the fastest way to confirm your **wiring, model, address, and baud**, and to
watch the **Doppler corrections** stream during a pass. The **Icom LAN** backend adds
its own `[NET]` traces for the connect/auth handshake and keepalives (the CI-V frames
it carries are the same ones shown above). Silence a backend by setting `CIV_DEBUG` /
`YAESU_DEBUG` / `KW_DEBUG` / `ICOMNET_DEBUG 0` at the top of
`civ.cpp` / `yaesu.cpp` / `kenwood.cpp` / `icomnet.cpp` and rebuild.

---

## 17. Antenna rotator (GS-232, rotctl, PstRotator, rotctld server)

> **v0.9.8 note.** The network *client* backend is now labelled **rotctl** in
> Settings — it connects to a rotctld server, it is not itself the daemon.
> Separately, CardSat can run its **own rotctld server** (Settings → Rotator →
> *Rotctld server*, default port **4533**) so a networked PC (Gpredict, …) drives
> the **GS-232 rotator wired to CardSat**; a `P` from the PC disengages CardSat's
> own tracking. A **manual control** screen (Settings → Rotator → *Rotator: manual
> control*) jogs az/el by hand with live read-back. At **el range 180°** the
> *flip* decision is now made once per pass and held to LOS (it previously never
> triggered). `rotctld`/`rigctld` servers have no authentication — trusted LAN only.

CardSat can drive an az/el antenna rotator through one of three interchangeable
backends, chosen in Settings with **Rot type**:

- **GS-232** -- a directly-attached controller speaking the **Yaesu GS-232A/B**
  protocol (a Yaesu G-5500 with a GS-232B, a SPID controller, or any GS-232
  emulator: K3NG / RadioArtisan / ERC). Because all three ESP32-S3 UARTs are
  already in use (USB, CAT, GPS), this link is created by an **I2C->UART bridge**
  (SC16IS750/752) on a second I2C bus, so it coexists with the radio and GPS.
- **rotctld (net)** -- a **Hamlib `rotctld` server** anywhere on the same WiFi
  network. CardSat is the TCP client (the same role Gpredict plays), so any
  rotator Hamlib supports can be driven over the LAN with no extra CardSat wiring.
- **PstRotator (net)** -- a **PstRotator** (YO3DMU) instance on the LAN, driven
  over **UDP** (the same datagrams Gpredict/SatPC32 send it). CardSat sends
  `<PST>...</PST>` control messages to PstRotator's UDP Control port; PstRotator
  then drives whatever controller it is configured for.
- **Yaesu (direct)** -- a Yaesu **G-5500**-class az/el controller wired **straight to
  CardSat**, with no GS-232 box: an I2C **ADS1115** reads the position-feedback
  voltages and an I2C **PCF8574** drives the four direction lines, both on the same
  I2C bus the GS-232 bridge would use. CardSat closes the pointing loop itself. This
  is a hardware build with its own calibration step -- see
  **[ROTOR_INTERFACE.md](ROTOR_INTERFACE.md)** (⚠️ untested; build at your own risk).
  Calibrate from **Settings → Rotator → Rotator: manual control** (capture keys
  **1/2/3/4** at the axis endpoints).

Only one rotator is active at a time. The pointing logic -- alignment offsets,
deadband, flip mode, park-on-LOS -- is identical for all of them; **Rot type** only
changes how the final azimuth/elevation reaches the hardware.

### Wiring

Chain: **Cardputer I2C (Wire1) -> SC16IS750 -> MAX3232 -> DB-9 -> GS-232 controller.**

- The bridge runs on the Cardputer-ADV expansion I2C bus, **G8 = SDA / G9 = SCL**
  (`ROT_I2C_SDA` / `ROT_I2C_SCL` in `config.h`), on a second I2C controller
  (Wire1) so it never touches the keyboard/IMU bus. These are confirmed from the
  M5Stack Cap LoRa-1262 pinmap and don't collide with CAT (G1/G2), the GPS UART
  (G13/G15), the LoRa SPI (G3/G4/G5/G6/G14/G39/G40), or the SD card (CS G12).
- GS-232 uses RS-232 voltage levels, so a **MAX3232** sits between the bridge's
  TTL TXD/RXD and the controller's DB-9 (three wires: TXD, RXD, GND).
- The SC16IS750 needs a 14.7456 MHz crystal (the common breakouts have one); set
  `ROT_XTAL_HZ` if yours differs, and its I2C address (A0/A1 strap) in
  `ROT_I2C_ADDR` (default 0x4D).
- On the Cap LoRa-1262, G8/G9 is exactly the I2C bus broken out to the cap's
  **HY2.0-4P Grove Port.A**, so a Grove SC16IS750 bridge plugs straight in. That
  bus also carries the cap's PI4IOE5V6408 IO expander (~0x43/0x44, used only for
  the LoRa RF switch, which CardSat never drives), so keep `ROT_I2C_ADDR`
  (default `0x4D`) clear of those — or add a TCA9548A mux if anything clashes.

### Settings

Enable and tune the rotator in **Settings** (scroll past the radio rows):

| Setting | Meaning |
|---|---|
| Rotator | off / on (builds the selected backend) |
| Rot type | **GS-232** (I2C bridge), **rotctld (net)** (TCP), or **PstRotator (net)** (UDP) |
| Net host | server IP / hostname (rotctl or PstRotator backend) |
| Net port | server port -- rotctld TCP **4533**, PstRotator UDP **12000** |
| Rot baud | GS-232 serial speed (1200 / 4800 / 9600); GS-232 backend only |
| Rot deadband | degrees; suppress moves smaller than this (anti-chatter) |
| Rot park az | azimuth the rotator parks at on LOS / when disabled |
| Rot pre-point | lead before AOS to pre-aim at the rise bearing (off / 30 s / 1-5 min) |
| Rot Az offset | added to commanded azimuth (mount alignment) |
| Rot El offset | added to commanded elevation |
| Rot az range | azimuth travel: **0..360** (default), **-180..+180** (centred on N), or **0..450** (90 deg overlap) |
| Rot el range | **90 deg**, or **180 deg** = flip over the top for high passes |

`Rot type` and `Net port` adjust in place with `,`/`/`; `Net host` and `Net port`
also open a text editor with ENTER.

### Using it

On the **Track** screen press **`o`** to start/stop pointing. The status line
shows **Rot ON / off / n/c** (*n/c* = no link: the I2C bridge wasn't found, or
the rotctld server isn't reachable). Pressing `o` re-attempts the link on the
spot, so you can engage as soon as the controller or server comes up. While on,
CardSat sends the satellite's azimuth and elevation about once a second -- the
GS-232 `W aaa eee` command, rotctld's `P <az> <el>`, or PstRotator's `<PST><AZIMUTH>..</AZIMUTH><ELEVATION>..</ELEVATION></PST>` datagram -- but only when the
position has moved past the deadband, since rotators are slow and mechanical.
When the satellite sets, or you press `o` again or leave the screen, the rotator
**parks** at the configured azimuth/elevation.

**Pre-positioning before AOS.** A slow rotator can take most of a minute to slew
across the sky, so by the time it reaches the rise bearing the pass has already
begun. With **Rot pre-point** set to a lead time (default **2 min**; `off`
disables it), CardSat predicts the next pass for the tracked satellite and, once
AOS is within that window, aims the rotator at the **rise azimuth on the horizon**
instead of the idle park position -- so it is already pointed when the satellite
appears and live tracking takes over smoothly. Set the lead a little longer than
your rotator's worst-case slew time. Outside the window it still rests at **Rot
park az**.

**Azimuth range (0-360 / +/-180 / 0-450).** By default CardSat assumes the
azimuth axis runs **0 to 360 deg**, 0 deg = North, 180 deg = South. **Rot az
range** offers two alternatives for rotators built differently:

- **-180..+180** -- the axis is centred on North and swings +/-180 deg. CardSat
  re-expresses each bearing into that range (e.g. 270 deg -> -90 deg), the same
  option Gpredict offers.
- **0..450** -- the rotator has **90 deg of overlap** past North (a common
  Yaesu/SatPC32 arrangement). A bearing of 0-90 deg is reachable either directly
  or as 360-450 deg, so CardSat commands whichever is **nearer its current
  position**: a pass crossing North continues up into the 360-450 region instead
  of unwinding a full turn -- no dead-zone spin at culmination.

Both affect what is sent on the **rotctld** path (e.g. `P -90.0 12.0` or
`P 372.0 8.0`). GS-232 controllers natively accept 0-450 and re-wrap negatives, so
the GS-232 backend follows the overlap but the +/-180 framing has no visible
effect there.

For overhead passes, setting **Rot el range** to **180 deg** enables a **flip**:
near culmination CardSat commands elevation past 90 deg (over the top) together
with a 180 deg azimuth flip, so an antenna on a 0-180 deg elevation rotator rides
through zenith without the fast 180 deg azimuth swing a 0-90 deg mount would need.
Leave it at **90 deg** for a conventional elevation rotator. CardSat points
**open-loop** from its own SGP4 prediction (it does not poll the controller's
heading); the GS-232 `C2` read-back exists in the backend for diagnostics but is
not used in the tracking loop.

### Network rotators (rotctld and PstRotator)

To use the network backend, run `rotctld` on any machine that can reach your
rotator (or use a controller that speaks rotctld natively), set **Rot type** to
**rotctld (net)**, and enter the server's address in **Net host** (an IP is
simplest) and **Net port** (Hamlib default **4533**). A dummy server for a bench test:

```
rotctld -m 1 -t 4533 -vvvvv
```

`-m 1` is Hamlib's dummy rotator model and `-vvvvv` makes rotctld log every
command it receives, so you can watch CardSat's `P`/`S` commands arrive without
moving real motors (without the verbose flag the server is silent, which can look
like nothing is being sent); swap in your rotator's model and
serial port for the real thing. CardSat opens the socket when you enable the
rotator and reconnects on its own (throttled to a few seconds) if it drops, so a
server that is briefly down or rebooted recovers without intervention. Pointing
is open-loop from CardSat's SGP4 prediction -- it does not read the heading back.

Hardware that speaks rotctld natively works the same way, with no PC in between.
For example, the **MuseLab AntRunner** (BG5DIW) portable az/el rotator (360 deg
azimuth / 180 deg elevation, controlled over WiFi by its onboard ESP32) and the
**AntRunner-Pro** (fixed-install, 360 deg / 90 deg, controlled over Ethernet) both
present a Hamlib `rotctld`-compatible network service. Set **Rot type** to
**rotctld (net)** and point **Net host** / **Net port** at the AntRunner's rotctld
service (or at a `rotctld` instance bridging to it over USB).

> rotctld has **no authentication** -- keep it on a trusted LAN and never expose
> the port to the internet. Several clients may connect to one rotctld and can
> contend for the rotator.

**PstRotator (UDP).** If you already run **PstRotator** on a shack PC, CardSat can
drive it directly instead of a rotctld server. In PstRotator, open
**Communication > UDP Control Port**, set the port (default **12000**), and tick
**UDP Control** in Setup. On CardSat set **Rot type** to **PstRotator (net)**,
**Net host** to the PC's IP, and **Net port** to that UDP port -- *remember to
change it from rotctld's 4533 to 12000*. CardSat then sends
`<PST><AZIMUTH>az</AZIMUTH><ELEVATION>el</ELEVATION></PST>` to point and
`<PST><STOP>1</STOP></PST>` to stop. PstRotator does its own azimuth range,
offsets and flip, so CardSat sends a plain 0-360 bearing here; leave CardSat's
**Rot az range** / **Rot el range** at default and let PstRotator manage overlap
and flip (or use CardSat's and keep PstRotator neutral -- just don't double up).

This backend also drives the **WA4MCM PSR-100** (Don Friend, WA4MCM) -- a popular
portable, lightweight az/el satellite-rotor kit for field/portable work. The
PSR-100 accepts the same PstRotator-style `<PST>` UDP az/el datagrams over its
WiFi interface, so CardSat points it directly: set **Rot type** to **PstRotator
(net)** and aim **Net host** / **Net port** at the PSR-100's UDP interface -- no
PC running PstRotator in between.

> The PstRotator UDP control is **unauthenticated and connectionless** -- keep it
> on a trusted LAN. Because UDP has no connection to lose, CardSat reports
> **Rot ON** whenever WiFi is up and a host is set; it cannot tell whether
> PstRotator is actually listening, so confirm the antenna follows on the first
> pass.

> The rotator only points while you are on the Track or Polar screen. It only
> moves the antenna -- it does not change the radio's bands, and CAT on the
> Yaesu/Kenwood sat rigs can't switch the band pair either, so set those by hand.

The GS-232 backend is bench-reasoned against the GS-232A/B manuals and Hamlib's
gs232a/gs232b backends, and host-tested for the bridge baud math and command
formatting -- it has not been run against a physical SC16IS750 or a real rotator.
The I2C pins (G8/G9) are confirmed from the Cap LoRa-1262 pinmap; still confirm
the SC16IS750's strapped address and the controller's baud before keying real
motors. The rotctld backend follows the published Hamlib `rotctld` protocol and
is the one rotator path you can fully bench-test without trusting hardware
encoders -- point it at `rotctld -m 1` and watch the commands -- but it has not
driven a physical rotator either. The **PstRotator** UDP backend is host-verified
for message formatting against the PstRotator manual (Rev. 7.5); it has not been
run against a live PstRotator instance.

---

## 18. Managing data and factory reset

CardSat keeps all of its data in a **`/CardSat`** folder:

- cached GP data and per-satellite transponder caches,
- your manual GP satellites and manual transponders (kept separate, so a refresh won't
  erase them),
- favorites, per-satellite calibration, and all settings.

CardSat stores this on the **microSD card** by default (in the `/CardSat` folder), so
your configuration travels with the card and is easy to back up. If no card is present
at boot it **falls back to the internal LittleFS** flash (same `/CardSat` folder) so
the unit still works standalone. The serial monitor reports which it mounted
(`[fs] using microSD card for storage (/CardSat)` or `[fs] no SD card - falling back
to internal LittleFS`). Insert the card **before powering on** — the filesystem is
chosen once at startup. For the LittleFS fallback you must flash CardSat with a
partition scheme that includes a data region, e.g. **Huge APP (3MB No OTA/1MB SPIFFS)**.

### Screenshots

Press **`b`** on any screen to save a screenshot of exactly what's displayed. Images
are written to **`/CardSat/Screenshots/`** on the SD card as 24-bit BMP files named
`shot_0001.bmp`, `shot_0002.bmp`, … (never overwritten). A short high beep confirms
each capture; a low beep means there's no SD card to write to — screenshots require
the card, as the LittleFS fallback is too small for images. The key works on every
screen **except** the text-entry screen (where `b` types normally), and it is
intentionally hidden — no footer hint — so it never appears in the captured image.
BMPs open anywhere and convert easily to PNG for documentation.

**Factory reset:** Settings → **Reset all data** → type **ERASE** to confirm. On the
SD card this removes CardSat's own files in `/CardSat` (it never formats your card);
on internal LittleFS it formats the data partition. Either way it reboots to a clean
first-run state. Use it to start over or before handing the unit to someone else.

---

## 19. Troubleshooting

**No passes / "Clock not set."** Set the clock first: Update → `k` (NTP), enable
GPS, or Location → `c` to enter UTC manually. Also confirm your location is set.

**No transponders on Track.** The satellite's transponders weren't cached. With
WiFi, open the bird from Satellites (it fetches from SatNOGS), or use Update → `a`
to cache everything. You can also add one manually (Passes → `n`).

**Radio doesn't respond / wrong VFO moves.** Check the **CAT baud** (and, for
Icom, the **CI-V address**) in Settings match the radio. Open the serial monitor at
115200 and watch the **CAT trace** ([§16](#16-radio-specific-notes)): each command is shown
decoded, and the radio should reply **ACK (FB)** rather than **NAK (FA)**. If the
wrong VFO tunes, the MAIN/SUB select bytes for that model may need adjusting in
`radio_profiles.h`.

**Radio-knob tuning feels jumpy or unresponsive.** Read-back happens about twice a
second, so there's up to ~0.5 s of latency, and a 20 Hz threshold ignores tiny
readback jitter. On a slower CAT link (9600 baud) it's less snappy. If your rig
quantizes frequency coarsely, that threshold may need tuning.

**Predictions seem off.** Check the **GP age** indicator — refresh GP data if it's
yellow/red. Confirm your location and that the clock is correct (UTC).

**Fewer satellites than expected after Update.** The GP file streams straight to
flash and is parsed incrementally, so it isn't limited by RAM; the catalog holds
up to 220 sats. If the count looks short, open the serial monitor — the line
`[net] streamed N bytes ... (declared M)` should show **N == M** (a complete
download). All-null placeholder entries in the AMSAT feed (sats with no current
elements) are skipped on purpose.

**No alarm beeps.** Confirm **AOS alarm** is on in Settings, you have **favorites**,
and the **clock is set** — the alarm tracks only the soonest upcoming favorite AOS,
so with no favorites or an unset clock there's nothing to count down to.

**GPS not fixing.** Make sure the right **GPS source** is selected (Location → `s`)
and that GPS is enabled (`p`). The Grove source shares pins with CAT — don't use
both at once.

**"fs open" / "No filesystem" when downloading GP (often under a launcher).**
CardSat needs somewhere to store the GP file. Run directly from flash it uses the
internal SPIFFS/LittleFS partition; under a launcher that didn't attach a SPIFFS
region it falls back to the **microSD card**. If you see `fs open` / `no filesystem`,
either insert a microSD card (the launcher usually boots from one anyway), have the
launcher allocate a SPIFFS partition for CardSat, or flash CardSat directly with the
**Huge APP (3MB No OTA/1MB SPIFFS)** partition scheme. The serial monitor's
`[fs] …` line at boot tells you what mounted.

**Rotator shows "n/c" or won't move.** *n/c* means the I2C->UART bridge didn't
answer. Check `ROT_I2C_SDA`/`ROT_I2C_SCL` in `config.h` match your wiring and the
SC16IS750's address (`ROT_I2C_ADDR`) doesn't clash with another I2C device. If it
reaches the bridge but the rotator is silent, confirm the MAX3232 level shifter is
in line and the controller's baud matches **Rot baud** in Settings.

---

## 20. Key reference (cheat sheet)

**Global:** `;` up · `.` down · `,` left · `/` right · ENTER select · `` ` ``/DEL back · `{`/`}` page · `b` screenshot · `h` help.

| Screen | Keys |
|---|---|
| **Satellites** | `f` favorite · `v` favorites-only · `n` new GP sat · `o` orbital analysis · `s` simulation · `t` transponder database · `d` 10-day overview · `i` illumination · ENTER passes · right-edge AMSAT mark: filled dot = heard, filled square = telemetry only, ring = not heard, none = no reports |
| **Orbital analysis** | `,`/`/` page (Info / Live / Next pass / Ground track / Doppler / Nodal / Sun-Beta / Pass outlook / Orbit position) · Info: footprint diameter now/apogee/perigee (= longest possible QSO) + decay estimate & solar-bracket range · Live: az/el/range/Doppler, mean anomaly/phase, sunlit/eclipse + **eclipse depth** (deg; >0 = in shadow) · Next pass: slant ranges + path delay + peak eclipse depth · Doppler: `f` set beacon freq, peak shift + max range-rate · Nodal: J2 node/perigee drift, sun-sync, LTAN, repeat track, longest pass · Sun/Beta: solar beta angle, full-sun vs eclipsed, eclipse %/orbit, next transition · Pass outlook: 7-day pass count/>30° count/longest/avg gap + the best upcoming pass (elevation, when, duration) · Orbit position: mean/true anomaly, argument of latitude, time to perigee/apogee, RAAN, rev number · `r` recompute · `` ` `` back |
| **Simulation** | `,`/`/` step time · `;`/`.` step size · `m` world-map view (sub-point + footprint at the simulated time) · `x` reset to now · `` ` `` back |
| **Next Passes** | ENTER track · `m` world map · `r` refresh · `z` deep-sleep until AOS |
| **Passes** | `;`/`.` select · `d` detail · `t`/ENTER track · `n` add TX · `r` recompute · `x` mutual · `v` 10-day · `i` illum · `g` workable grids (this pass) · `w` workable US states (this pass) · `e` workable DXCC (this pass) |
| **Pass detail** | `p` polar of this pass · `` ` ``/ENTER back |
| **Pass polar** | `p` back to curve · `` ` ``/ENTER passes |
| **Track** | `m` TUNE/CAL · `d` cycle tune mode (FULL/DL/UL/hold) · `t` next TX · `c` CTCSS tone · `r` radio on/off · `o` rotator on/off · `p` polar · `f` Manual mode · `l` log QSO · `g` workable grids now (radio/rotator keep running) · ENTER save cal |
| **Manual mode** | no-radio frequency calculator · `u` toggle which leg is fixed (HOLD vs TUNE>) · `,`/`/` move fixed freq in passband (linear) · `s` step · `x` center · `m` CAL · `t` next TX · `l` log · `p` polar · `g` grids (all return here) · ENTER save cal · `` ` ``/`f` back to Track |
| **Workable grids** | 4-char Maidenhead grids under the footprint (per-pass union or live, refreshing ~3 s; uncapped, works to high orbits) · count shown on a cyan line above the list · `;`/`.` and `{`/`}` scroll · `` ` `` back |
| **Track · TUNE** | `,`/`/` tune ∓ · `s` step (100/1k/5k) · `x` recenter |
| **Track · CAL** | `,`/`/` downlink ∓ · `;`/`.` uplink ∓ · `s` step (10/100/1k) · `x` zero |
| **Polar** | `l` log QSO · `p`/ENTER/`` ` `` back to track |
| **Log (menu)** | `;`/`.` select · ENTER → new QSO / browse / export ADIF |
| **Log · list** | `;`/`.` scroll · ENTER edit entry · `` ` `` back |
| **Log · entry** | `;`/`.` field · ENTER edit · `s` save · `x`×2 delete · `` ` `` back |
| **Mutual** | `;`/`.` scroll · `` ` ``/ENTER back to passes |
| **10-day** | `;`/`.` scroll ∓1 day (forward indefinitely, oldest day off the top; not before today) · `r` recompute · `` ` ``/ENTER back |
| **Illum** | `,`/`/` scroll ∓1 day (forward indefinitely; not before today) · `r` recompute · `` ` ``/ENTER back |
| **Location** | `e`/`o`/`a` lat/lon/alt · `g` grid · `p` GPS on/off · `s` GPS source · `c` set clock · ENTER GPS sky plot |
| **GPS sky plot** | live GNSS az/el coloured by signal · `` ` `` back |
| **World map** | `f` highlight a favorite · `y` sun · `c` eclipse · `` ` `` back |
| **Rotator (manual)** | `,`/`/` az · `;`/`.` el · `s` step · `x` stop · `` ` `` back |
| **Help** | `;`/`.` scroll · `` ` `` back |
| **Update** | `k`/ENTER GP · `a` cache all TX · `w` WiFi only |
| **Settings** | `,`/`/` change · ENTER edit/toggle · `s` scan WiFi (on SSID row) · (Reset = type ERASE) |
| **GP source** | pick **AMSAT** / any **CelesTrak** JSON-PP category (Amateur Radio first) / **Custom URL** · `;`/`.` move · `{`/`}` page · ENTER select |
| **Sun / Moon** | graphical sky-dome view (Sun/Moon glyphs on a polar dome) · `g` toggle graphic/data list · `;`/`.` pick Sun/Moon · `o` rotor track on/off (takes the rotator from sat tracking) · auto-parks while the body is below the horizon · header shows SUN/MOON tag on other screens · `x` stop · `` ` `` back |
| **Space Wx** (main menu) | solar 10.7 cm flux + planetary Kp + running A index, each labelled & colour-coded, with a plain-language HF/satellite operating outlook and a data-freshness note · `r` refresh over WiFi · `` ` `` back |
| **Weather** (main menu) | terrestrial current conditions + multi-day forecast for the operating site from Open-Meteo · current temp/sky/wind/humidity then per-day hi/lo & precip chance · refreshes on entry (if on WiFi) and with Update · `r` refresh · cached offline · `` ` `` back |
| **QRZ Lookup** (main menu) | callsign lookup via QRZ.com XML (needs a QRZ XML subscription + credentials in Settings → Network) · ENTER type a callsign · shows name/address/country/grid/class · WiFi required · `` ` `` back |
| **Transponder DB** (Satellites → `t`) | scrollable list of the selected satellite's transponder/beacon entries (description; **D** downlink + mode; **U** uplink + tone/inv/lin flags) · `;`/`.` scroll · `` ` `` back |
| **Edit** | type · DEL backspace · ENTER ok · `` ` `` cancel |
| **About** | build/version, IP, free heap and diagnostics (read-only) |

---

## 21. Glossary

- **AOS / LOS** — Acquisition / Loss of Signal: when the satellite rises above and
  sets below your horizon.
- **TCA** — Time of Closest Approach (maximum elevation).
- **Azimuth / Elevation** — compass bearing (0°=N, clockwise) and angle above the
  horizon to the satellite.
- **Range / range-rate** — slant distance to the satellite and how fast it's
  changing (drives Doppler; +ve = receding).
- **Doppler shift** — the frequency change caused by relative motion.
- **Downlink / uplink** — the satellite's transmit frequency (you receive) and
  receive frequency (you transmit).
- **Linear transponder** — relays a band of frequencies (SSB/CW), as opposed to a
  single FM channel. May be **inverting** (flips the spectrum) or non-inverting.
- **Passband** — the range of frequencies a linear transponder relays.
- **GP / OMM** — General Perturbations data / Orbit Mean-elements Message: the
  orbital element set (distributed as JSON) that SGP4 propagates. Replaces the
  legacy **TLE** (Two-Line Element) text encoding, which CardSat still rebuilds
  internally to feed the propagator.
- **SGP4** — the standard model that turns an element set into position/velocity over time.
- **Maidenhead grid** — the locator system (e.g. `FM18`) for your station position.
- **Eclipse / sunlit** — whether the satellite is in Earth's shadow or illuminated.
- **CAT** — Computer Aided Transceiver control: the generic term for computer
  control of a radio. CardSat speaks three CAT dialects — Icom **CI-V**, Yaesu, and
  Kenwood — behind one abstract rig interface, over a wired serial bus or (for
  network-capable Icoms) the **Icom LAN** RS-BA1 protocol.
- **CI-V** — Icom's Communications Interface-V serial control bus (Icom's CAT dialect).
- **MAIN / SUB** — the two VFOs/bands of a satellite-capable radio; by default
  CardSat uses MAIN for uplink and SUB for downlink (the **VFO Type** setting swaps
  them).
- **One True Rule** — KB5MU's principle: tune so the frequency *at the satellite*
  stays constant; the computer corrects both legs for Doppler.

---

## 22. Supporting AMSAT

CardSat exists because of the work **AMSAT** and its volunteers do — building and
keeping amateur satellites flying, publishing the orbital data this app depends on,
and advocating for amateur radio in space. **If you find CardSat useful, please
consider joining and/or donating to AMSAT at [www.amsat.org](https://www.amsat.org/).**
Membership and donations directly fund the next generation of satellites you'll
track and work with this very tool.

---

## 23. License

CardSat is released under the **MIT License**.

> Copyright (c) 2026 Paul Stoetzer (N8HM)
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of
> this software and associated documentation files (the "Software"), to deal in the
> Software without restriction, including without limitation the rights to use, copy,
> modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
> and to permit persons to whom the Software is furnished to do so, subject to the
> following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
> INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
> PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
> HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
> CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
> OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Third-party components keep their own licenses: SGP4 propagation
([Hopperpop/Sgp4-Library](https://github.com/Hopperpop/Sgp4-Library)), GP data from
AMSAT, and transponder data from SatNOGS.

---

*CardSat is amateur-radio software. Operate within your license privileges and
local band plans. Built on SGP4 (Hopperpop), GP data from AMSAT, and transponder data
from SatNOGS.*
