# CardSat User Manual

A complete guide to operating **CardSat**, the amateur-radio satellite tracker and
multi-radio CAT Doppler controller for the M5Stack Cardputer ADV (Icom, Yaesu, Kenwood).

> **Status.** CardSat runs on the Cardputer ADV, and every feature has been
> exercised on hardware **except radio (CAT) control and the antenna rotator** —
> those two are still unverified on real equipment. Their math (the per-protocol
> CAT frequency encoders and the GS-232 command formatting) is host-tested, but
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
17. [Antenna rotator (GS-232)](#17-antenna-rotator-gs-232)
18. [Managing data and factory reset](#18-managing-data-and-factory-reset)
19. [Troubleshooting](#19-troubleshooting)
20. [Key reference (cheat sheet)](#20-key-reference-cheat-sheet)
21. [Glossary](#21-glossary)

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
  alarm**, a **deep-sleep-until-next-pass** power saver, and **sun/eclipse** status.

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
Location · Update GP/Freq · Settings · About / diagnostics · Log.** The currently
selected satellite is shown at the bottom right. `;`/`.` move, ENTER selects.

### About / diagnostics

Firmware version and build date, storage backend (microSD or internal flash),
GP catalog size and freshest element age, WiFi/IP, battery level, free heap, and
uptime. `` ` `` or ENTER returns home.

### Logging QSOs (Log)

Press `l` on the **Track** or **Polar** screen to log the contact you're working —
radio control keeps running while you type. CardSat snapshots the UTC time,
satellite, mode, and the current uplink/downlink frequencies plus your own grid
and callsign (from **My callsign** in Settings), then lets you fill in:

- **Call** — the station worked (required to save). Letters default to **uppercase**
  (hold shift for lowercase).
- **Mode** — on a **linear** transponder, ENTER toggles **SSB ↔ CW** for this
  contact; on an FM bird the mode is fixed.
- **RST S / RST R** — report sent and received (default `59`).
- **Grid** — the other station's grid, **uppercase** by default (optional, but
  grids are the point of sat ops).
- **Notes** — free text.

`;`/`.` move between fields, ENTER edits the selected one, `s` saves, `` ` ``
cancels. QSOs are appended to **`/CardSat/qso_log.csv`** (one row each; `notes` is
the last column, so commas there are fine).

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
Open one with ENTER to correct any field and `s` to save, or press `x` twice to
delete it; changes are written straight back to the CSV. The most recent **120**
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

### Next Passes (schedule)

One merged list of the next pass for **every favorite**, soonest first. Each row
shows the countdown to AOS (or **NOW** if the bird is already up), the satellite
name, maximum elevation, and pass length. A red **`!`** flags a satellite whose
elements are stale (see [§14](#14-gp-age-and-accuracy)).

- `;`/`.` move.
- **ENTER** — jump straight to **Track** for that satellite.
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
satellite's upcoming passes and lists every window where **both you and that
station have the satellite above your horizons at the same time** — the periods
you could actually make a contact through the bird. Each row shows the **start
time (UTC)**, the **duration** (m:ss), and the **maximum elevation at each end**
— yours (`me`) and theirs (`dx`) — so you can judge how workable the window is.
`;`/`.` scroll; `` ` `` returns to Passes.

> Co-visibility is computed to the **0° horizon** for both stations; a window
> with very low max elevations may be hard to use in practice. The remote grid
> is assumed at sea level.

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
- **ENTER** — save the current calibration **for this satellite**.
- `` ` `` — first press stops the radio output; second press goes back.

TUNE-mode keys: `,`/`/` tune down/up the passband, `s` cycle step
(100/1000/5000 Hz), `x` recenter. CAL-mode keys: `,`/`/` trim downlink, `;`/`.`
trim uplink, `s` cycle step (10/100/1000 Hz), `x` zero. See
[§9](#9-doppler-tuning-and-the-one-true-rule) and [§10](#10-calibration).

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

### Location

- `e` / `o` / `a` — edit latitude / longitude / altitude.
- `g` — set position from a **Maidenhead grid** square.
- `p` — enable/disable **GPS**.
- `s` — cycle the **GPS source** (Grove 9600, Grove 115200, Cap LoRa868, Cap LoRa1262).
- `c` — set the **UTC clock** manually (`YYYY-MM-DD HH:MM:SS`).
- `` ` `` — back (applies the position to the predictor).

### Update

- `k` or **ENTER** — download GP data from AMSAT and sync the clock (NTP).
- `a` — fetch and cache **all** transponders for offline use.
- `w` — connect WiFi only (no download).
- `` ` `` — back. Diagnostics print to the serial monitor at 115200.

### Settings

`;`/`.` move; `,`/`/` change adjustable rows; ENTER edits text fields or runs
actions.

| Row | Action |
|---|---|
| Radio | `,`/`/` select model (auto-sets address + baud) |
| CI-V addr | ENTER → edit (hex); Icom only |
| CAT baud | `,`/`/` cycle 1200…115200 (incl. 57600) — applies to all radio protocols |
| Min pass el | `,`/`/` 0–30° |
| WiFi SSID | ENTER → edit · **`s`** → scan for networks and pick one |
| WiFi pass | ENTER → edit |
| Save & test WiFi | ENTER → connect and report OK/FAIL |
| AOS alarm | `,`/`/` or ENTER toggle on/off |
| Rotator (+ baud / deadband / park / offsets) | `,`/`/` adjust; see [§17](#17-antenna-rotator-gs-232) |
| GP source URL | ENTER → edit the GP/OMM download URL |
| VFO Type | `,`/`/` or ENTER toggle *Main Up/Sub Dn* ↔ *Main Dn/Sub Up* |
| Sat mode | `,`/`/` or ENTER toggle the rig's satellite mode on/off |
| CAT rate | `,`/`/` adjust the CAT update period in 10 ms steps (default 500 ms; soft-floored to what the CAT baud can service) |
| CAT delay | `,`/`/` adjust the pause after each command, 0–200 ms in 2 ms steps (default 70 ms; CI-V/Icom only) |
| Screen sleep | `,`/`/` cycle off / 30 s / 1 min / 2 min / 5 min — blanks the backlight after that idle time |
| My callsign | ENTER → enter your station callsign (stored uppercase); used in the log and ADIF `STATION_CALLSIGN` |
| Backup config+favs → SD | ENTER → copy config + favorites to `config.bak` / `favs.bak` |
| Restore config+favs | ENTER → restore them from the backup files |
| **Reset all data** | ENTER → type **ERASE** to wipe everything (red row) |

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
from the SatNOGS cache so a GP/transponder refresh won't erase them. Up to **32
transponders** are held per satellite (SatNOGS plus your manual ones) — enough
for even the busiest birds, such as the ISS.

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
double-beep and shows a blinking **"AOS!"** banner at acquisition. A small orange
countdown banner appears on any screen within the last minute.

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
watch the **Doppler corrections** stream during a pass. Silence a backend by setting
`CIV_DEBUG` / `YAESU_DEBUG` / `KW_DEBUG 0` at the top of
`civ.cpp` / `yaesu.cpp` / `kenwood.cpp` and rebuild.

---

## 17. Antenna rotator (GS-232)

CardSat can drive an az/el antenna rotator that speaks the **Yaesu GS-232A/B**
protocol -- a Yaesu G-5500 with a GS-232B, a SPID controller, or any GS-232
emulator (K3NG / RadioArtisan / ERC). Because all three ESP32-S3 UARTs are
already in use (USB, CAT, GPS), the rotator's serial link is created by an
**I2C->UART bridge** (SC16IS750/752) on a second I2C bus, so it coexists with
the radio and GPS without giving either of them up.

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
| Rotator | off / on (builds the bridge backend) |
| Rot baud | GS-232 serial speed (1200 / 4800 / 9600) |
| Rot deadband | degrees; suppress moves smaller than this (anti-chatter) |
| Rot park az | azimuth the rotator parks at on LOS / when disabled |
| Rot Az offset | added to commanded azimuth (mount alignment) |
| Rot El offset | added to commanded elevation |

### Using it

On the **Track** screen press **`o`** to start/stop pointing. The status line
shows **Rot ON / off / n/c** (*n/c* = the I2C bridge wasn't found). While on,
CardSat sends the satellite's azimuth and elevation about once a second with the
GS-232 `W aaa eee` command, but only when the position has moved past the
deadband -- rotators are slow and mechanical, so this avoids chattering the
relays. When the satellite sets, or you press `o` again or leave the screen, the
rotator **parks** at the configured azimuth/elevation.

For overhead passes, a persisted **flip mode** (`rotFlip`) commands 0-180 deg
elevation with a 180 deg azimuth flip, for rotators with 450 deg azimuth / 180 deg
elevation travel. CardSat points **open-loop** from its own SGP4 prediction (it
does not poll the controller's heading); the GS-232 `C2` read-back exists in the
backend for diagnostics but is not used in the tracking loop.

> The rotator only points while you are on the Track or Polar screen. It only
> moves the antenna -- it does not change the radio's bands, and CAT on the
> Yaesu/Kenwood sat rigs can't switch the band pair either, so set those by hand.

This backend is bench-reasoned against the GS-232A/B manuals and Hamlib's gs232a/
gs232b backends, and host-tested for the bridge baud math and command formatting
-- it has not been run against a physical SC16IS750 or a real rotator. The I2C
pins (G8/G9) are confirmed from the Cap LoRa-1262 pinmap; still confirm the
SC16IS750's strapped address and the controller's baud before keying real motors.

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

**Global:** `;` up · `.` down · `,` left · `/` right · ENTER select · `` ` ``/DEL back · `{`/`}` page · `b` screenshot.

| Screen | Keys |
|---|---|
| **Satellites** | `f` favorite · `v` favorites-only · `n` new GP sat · ENTER passes |
| **Next Passes** | ENTER track · `r` refresh · `z` deep-sleep until AOS |
| **Passes** | `;`/`.` select · `d` detail plot · `t`/ENTER track · `n` add TX · `r` recompute · `x` mutual window |
| **Pass detail** | `p` polar of this pass · `` ` ``/ENTER back |
| **Pass polar** | `p` back to curve · `` ` ``/ENTER passes |
| **Track** | `m` TUNE/CAL · `d` cycle tune mode (FULL/DL/UL/hold) · `t` next TX · `c` CTCSS tone · `r` radio on/off · `o` rotator on/off · `p` polar · `l` log QSO · ENTER save cal |
| **Track · TUNE** | `,`/`/` tune ∓ · `s` step (100/1k/5k) · `x` recenter |
| **Track · CAL** | `,`/`/` downlink ∓ · `;`/`.` uplink ∓ · `s` step (10/100/1k) · `x` zero |
| **Polar** | `p`/ENTER/`` ` `` back to track |
| **Mutual** | `;`/`.` scroll · `` ` ``/ENTER back to passes |
| **Location** | `e`/`o`/`a` lat/lon/alt · `g` grid · `p` GPS on/off · `s` GPS source · `c` set clock |
| **Update** | `k`/ENTER GP · `a` cache all TX · `w` WiFi only |
| **Settings** | `,`/`/` change · ENTER edit/toggle · `s` scan WiFi (on SSID row) · (Reset = type ERASE) |
| **Edit** | type · DEL backspace · ENTER ok · `` ` `` cancel |

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
  Kenwood — behind one abstract rig interface.
- **CI-V** — Icom's Communications Interface-V serial control bus (Icom's CAT dialect).
- **MAIN / SUB** — the two VFOs/bands of a satellite-capable radio; CardSat uses
  MAIN for uplink and SUB for downlink.
- **One True Rule** — KB5MU's principle: tune so the frequency *at the satellite*
  stays constant; the computer corrects both legs for Doppler.

---

*CardSat is amateur-radio software. Operate within your license privileges and
local band plans. Built on SGP4 (Hopperpop), GP data from AMSAT, and transponder data
from SatNOGS.*
