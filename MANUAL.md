# CardSat User Manual

A complete guide to operating **CardSat**, the amateur-radio satellite tracker and
multi-radio CAT Doppler controller for the M5Stack Cardputer ADV (Icom, Yaesu, Kenwood).

> **Status.** CardSat runs on the Cardputer ADV, and every feature has been
> exercised on hardware. **Single-pin CI-V is confirmed working on an IC-821** (full
> bidirectional exchange over one wire). The remaining CAT paths — separate-pin CI-V,
> Yaesu and Kenwood encoders, the Icom LAN (RS-BA1) backend — and all of the rotator
> backends (GS-232, Easycomm I/II/III, and SPID over the I²C→UART bridge; the rotctld
> TCP client; PstRotator UDP; and the direct-Yaesu I²C interface) are host-tested but
> have not yet driven that specific hardware. Keep the serial monitor open and verify
> on the air before trusting CAT for
> a contact.

---

## Contents

1. [Overview](#1-overview)
2. [What you need](#2-what-you-need)
3. [Connecting your radio](#3-connecting-your-radio)
4. [Connecting a GPS (optional)](#4-connecting-a-gps-optional)
5. [Installing the firmware](#5-installing-the-firmware)
6. [The keyboard: how to navigate](#6-the-keyboard-how-to-navigate)
7. [First-time setup](#7-first-time-setup)
8. [Screen map and navigation](#8-screen-map-and-navigation)
9. [Doppler tuning and the One True Rule](#9-doppler-tuning-and-the-one-true-rule)
10. [Calibration](#10-calibration)
11. [Working a pass, step by step](#11-working-a-pass-step-by-step)
12. [AOS alarm and deep sleep](#12-aos-alarm-and-deep-sleep)
13. [Sun and eclipse](#13-sun-and-eclipse)
14. [GP age and accuracy](#14-gp-age-and-accuracy)
15. [Working offline](#15-working-offline)
16. [Radio-specific notes](#16-radio-specific-notes)
17. [Antenna rotator (GS-232, rotctl, PstRotator, Yaesu direct, rotctld server)](#17-antenna-rotator-gs-232-rotctl-pstrotator-yaesu-direct-rotctld-server)
18. [Mobile web control](#18-mobile-web-control)
19. [Managing data and factory reset](#19-managing-data-and-factory-reset)
20. [Troubleshooting](#20-troubleshooting)
21. [Screen-by-screen reference](#21-screen-by-screen-reference)
22. [Key reference (cheat sheet)](#22-key-reference-cheat-sheet)
23. [Glossary](#23-glossary)
24. [Supporting AMSAT](#24-supporting-amsat)
25. [License](#25-license)

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
- helps you **observe** as well as work the birds — it flags **visually observable
  passes** (sunlit satellite, dark sky), predicts **Sun/Moon transits** (when a bird
  crosses the solar or lunar disc from your location), shows **decay/reentry** watch
  flags on declining orbits, and keeps a **per-satellite operating note** for each bird;
- points an **az/el antenna rotator** -- a wired **GS-232**, **Easycomm**, or **SPID**
  controller, a **Yaesu rotator driven directly** (no GS-232 box, via an I²C
  ADC + output expander), or a networked **Hamlib `rotctld`** / **PstRotator** server;
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

### Icom CI-V: separate-pin or single-wire

Icom's CI-V bus is electrically a **single half-duplex wire**, so CardSat offers two
ways to connect it, chosen with **CI-V wiring** in *Settings → Radio / CAT*:

- **Separate TX/RX (G2/G1)** — the default and most reliable path: G2 carries transmit,
  G1 carries receive, through your interface to the radio's single CI-V line. This is
  the recommended option.
- **Single-pin (1-pin G2** or **1-pin G1)** — the entire CI-V bus runs on **one shared
  GPIO**, matching CI-V's native single-wire wiring. This is **confirmed working on an
  IC-821** (full bidirectional exchange — frequency reads and ACKs — over a single
  wire), but because both directions share one pin it needs a proper **open-drain,
  level-correct** interface between the 3.3 V GPIO and the radio's CI-V line; get the
  5 V/3.3 V interfacing right before using it. See
  **[docs/interfaces/CIV_SINGLE_PIN.md](docs/interfaces/CIV_SINGLE_PIN.md)** for the
  interface detail and cautions. If in doubt, use separate TX/RX.

This choice applies to **wired Icom CI-V only**; Yaesu and Kenwood use their own serial
wiring, and Icom-over-LAN (below) needs no CI-V wire at all.

### Icom over the network (no CI-V wiring)

The **IC-9700** can be controlled over **WiFi/Ethernet** instead of the wired CI-V
bus — CardSat speaks the same RS-BA1 UDP protocol as Icom's own remote software. No
level shifter, no UART: the radio's CI-V/G1/G2 pins stay free.

> **Scope.** Icom LAN in CardSat is **intended for the IC-9700** — the one
> network-capable Icom in CardSat's radio list that is a full-duplex satellite rig.
> The LAN transport itself is **confirmed working**: CardSat has successfully
> controlled an **IC-705** over the network (with the correct CI-V address set),
> which proves the RS-BA1 connect/auth/CI-V path drives a real Icom radio. The IC-705
> isn't usable for live satellite work, though — it's a single-receiver radio, so its
> two VFOs just swap back and forth rather than offering the simultaneous MAIN/SUB
> downlink+uplink the IC-9700 provides. Other networked Icoms (IC-7610, IC-785x) speak
> the same protocol but likewise lack the satellite architecture. **The IC-9700 itself
> has not yet been tested** over LAN — the path should work given the IC-705 result,
> but confirm it before relying on it. Select **Icom LAN** with the **IC-9700** as the
> radio model.

On the radio (IC-9700: **MENU > SET > Network**): set **Network Control = ON**, set a
**Network User1** id + password, leave the **Control** port at **50001** (Serial =
50002, Audio = 50003 follow it), and turn **CI-V Transceive ON** so the radio reports
changes. Give the radio a stable IP (DHCP reservation or static).

In CardSat **Settings**: set **CAT type → Icom LAN**, **LAN host** to the radio's IP,
**LAN port** to 50001 (unless you changed it), and **LAN user / LAN pass** to the
Network User1 credentials. CardSat connects in the background; once the link is up the
radio behaves exactly like a wired IC-9700 (MAIN/SUB, Doppler, sat mode, CTCSS all
work the same). Only **CAT** is carried — CardSat does not open the audio stream. Keep
the credentials on a trusted LAN. (See [§16](#16-radio-specific-notes).)

### Driving a radio through Hamlib (rigctl over the network)

As a third option, CardSat can control **any radio Hamlib supports** by talking to a
**`rigctld`** server over the network instead of wiring the radio to CardSat. Set
**CAT type → rigctl (net)** and point **LAN host / LAN port** at the machine running
`rigctld` — set the port to the one your `rigctld` listens on (Hamlib's conventional
default is **4532**). CardSat is the client — the PC's `rigctld` holds the actual
serial/USB connection to the rig and CardSat sends it set/read commands, mapping VFO-A
to the downlink and VFO-B to the uplink. This is handy when the radio is already cabled
to a shack computer, or for a rig CardSat has no native backend for. It is
**host-tested only** — verify against your `rigctld` before a pass.

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

There are two ways to get CardSat onto the device: flash a **prebuilt binary**
(no toolchain needed — easiest), or **build from source** with the Arduino IDE or
PlatformIO. Two binaries are published with each release:

| File | Use it with | Notes |
|---|---|---|
| `CardSat.bin` | **Launcher** (bmorcelli/Launcher) | App-only image; Launcher writes the partition table and bootloader, so this file is **only** usable through Launcher — it cannot be flashed on its own. |
| `CardSat_Merged.bin` | **M5Burner**, or a **direct flash** (esptool / web flasher) | Complete standalone image (bootloader + partition table + app + empty filesystem), written at offset `0x0`. |

### Install with Launcher (`CardSat.bin`)

[Launcher](https://github.com/bmorcelli/Launcher) by bmorcelli is a firmware
launcher for the Cardputer (and many other ESP32 boards) that installs and runs
binaries from a microSD card, over-the-air, or through its WebUI — and builds the
right partition layout for each app automatically. It must already be installed on
the device (see its repository for how to flash Launcher itself).

1. Copy **`CardSat.bin`** to the Launcher binaries folder on the device's microSD
   card (or push it via Launcher's WebUI / OTA favorites).
2. On the device, start **Launcher**, browse to **CardSat**, and install/run it.
   Launcher writes the appropriate partition scheme and the app for you.
3. `CardSat.bin` is an app-only image with no standalone bootloader or partition
   table, so it works **only** through Launcher. To flash without Launcher, use the
   merged image below.

### Install with M5Burner, or direct flash (`CardSat_Merged.bin`)

`CardSat_Merged.bin` is a **complete image** — bootloader, partition table,
application, and the empty LittleFS filesystem combined, written at offset `0x0`.
Use it with **M5Burner** (add it as a custom firmware and burn), for a fresh
device, to recover one in an unknown state, or whenever you are not using Launcher.

To flash it directly with **esptool** (replace the port with yours):

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0 CardSat_Merged.bin
```

Or with the web **ESP Tool** (<https://espressif.github.io/esptool-js/>): connect
the Cardputer over USB, set chip to **ESP32-S3**, add `CardSat_Merged.bin` at
address **`0x0`**, and flash.

Hold the device's reset/boot as your tool instructs if it does not enter download
mode automatically. After flashing, the device reboots into CardSat. (The merged
image starts with an **empty** filesystem; run **Update** once on first boot to
download GP elements — see [§7](#7-first-time-setup).)

### Build from source — Arduino IDE (single file)

1. Install libraries: **M5Cardputer**, **ArduinoJson** (v7), **TinyGPSPlus** from
   the Library Manager, and the Hopperpop **Sgp4** library via *Sketch → Include
   Library → Add .ZIP Library* (from <https://github.com/Hopperpop/Sgp4-Library>).
2. Open `CardSat.ino`. Under **Tools**, set:
   - **Board:** ESP32S3 Dev Module (or M5StampS3)
   - **Flash Size:** 8MB
   - **Partition Scheme:** **Huge APP (3MB No OTA/1MB SPIFFS)** — *required*
   - **PSRAM:** Disabled · **USB CDC On Boot:** Enabled
3. Upload. Open the Serial Monitor at **115200** baud for diagnostics.

### Build from source — PlatformIO

`pio run` to build, `pio run -t upload` to flash, `pio device monitor` to watch
the log. The `cardputer_adv` env pins the libraries and the 8 MB partition layout.

### Upgrading an existing install

Updating to a newer CardSat version uses the same tools as a first install. The one
thing to understand is **what each method does to your saved data** — your station
settings, radio/rotator calibration, per-satellite operating notes and CTCSS
overrides, favorites, and the cached GP/AMSAT data.

**Where your data lives.** CardSat **prefers a microSD card** for all storage. When a
FAT32 card is inserted, everything is saved to the card (under `/CardSat`), and the
device's internal flash (LittleFS) is used only as a fallback when no card is present.
Flashing firmware only writes the **internal flash** and never touches the SD card — so
**if you keep an SD card in the device, your configuration survives any upgrade,
including a full merged flash.** A factory reset likewise leaves the SD card's contents
in place. The table below describes the **no-SD-card** case, where data is stored
internally in LittleFS.

| Method (no SD card) | File | Effect on internal saved data |
|---|---|---|
| **Launcher** (recommended for upgrades) | `CardSat.bin` | **Preserved.** Launcher replaces only the application partition, leaving the LittleFS filesystem intact, so settings, notes, favorites, and cached elements survive the update. |
| **M5Burner / direct flash at `0x0`** | `CardSat_Merged.bin` | **Erased.** The merged image carries a fresh empty filesystem, so flashing it at `0x0` overwrites internally-stored data. You'll reconfigure and run **Update** again. |

In short, your settings carry over if **either** an **SD card** is present (regardless
of how you flash) **or** you upgrade with **Launcher** (`CardSat.bin`). Data is only
lost when it was stored **internally** *and* you do a full `CardSat_Merged.bin` flash.

If you do land on a clean slate, plan to set up again: re-enter your **location/grid**,
your **radio (CAT) and rotator** settings, any **calibration**, then run **Update** once
to refetch GP elements (see [§7](#7-first-time-setup)). Advanced users without an SD
card who still want to keep internal data across a binary update can flash only the
application partition (point esptool at the app offset instead of `0x0`, leaving the
filesystem partition untouched), but Launcher does this automatically and is less
error-prone.

After upgrading, confirm the running version on the **About** screen (Main menu →
**About**). A power cycle after flashing is good practice to clear any stale state.


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
| **Fn** + key | a modifier used by the Notes editor for cursor movement and save, so the `;` `.` `,` `/` keys stay typeable as punctuation (see [§8 → Notes](#notes-free-form-text-editor)) |
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
   - **CI-V wiring** — *(Icom wired CI-V only)* choose **TX/RX (G2/G1)** — the normal,
     recommended separate-wire path — or a **single-pin** mode (**1-pin G2** or
     **1-pin G1**) that drives the whole CI-V bus over one shared open-drain GPIO.
     Single-pin is confirmed working on hardware (verified on an IC-821) but still
     needs correct 5 V/3.3 V level interfacing; the separate TX/RX path is simplest.
     See `docs/interfaces/CIV_SINGLE_PIN.md` and mind the level cautions before wiring.
   - **CAT serial monitor** — opens a live diagnostic that shows raw CAT traffic
     (TX in cyan, RX in green) as hex, for any wired protocol (CI-V, Yaesu, Kenwood).
     While open it **actively polls the rig** (~once per 700 ms) by reading the
     downlink frequency, so you always see live traffic — your read request as TX and
     the radio's reply as RX — making it a real bench check of the CAT link with no
     satellite pass required. Press **`p`** to toggle polling off for a purely passive
     view; **`s`** to type a raw frame in hex (e.g. `FE FE 4C E0 03 FD`) and transmit
     it; `;`/`.` scroll back through the log; `` ` `` exits. Polling pauses
     automatically while Doppler tracking is engaged so the two don't collide. If you
     see "(no traffic yet)" with polling on, check the CAT wiring, baud, and (for
     Icom) the CI-V address. **Caution:** sending raw frames transmits arbitrary bytes
     to the radio — a malformed or wrong-address frame can mis-set the rig.
   - **Min pass el** — passes whose **peak elevation** never reaches this value are
     hidden from the pass lists and schedule (default 5°).
   - **Visible passes** — when on, the schedule flags passes that are **visually
     observable** (satellite sunlit, sky dark at your location, and the bird high
     enough) with a yellow `*`, and the pass-detail screen shows a verdict
     ("Visible: YES", or the reason it isn't — daylight, satellite in shadow, or too
     low). **Sky-dark gate** chooses how dark the sky must be — civil (Sun −6°),
     nautical (−12°), or astronomical (−18°) — and **Visible min el** is the minimum
     peak elevation to count (default 10°). This turns the schedule into a "what can
     I go outside and watch" tool; the ISS and other bright objects light up `*` on a
     good evening pass.
   - **WiFi SSID / WiFi pass** — enter your network, then **Save & test WiFi**.
     On the **WiFi SSID** row you can instead press **`s`** to **scan** for nearby
     networks, pick one from the list (strongest first; `*` = secured), and then
     enter its password (open networks skip the password step).
   - **WiFi 2 SSID / WiFi 2 pass** — an **optional second network**. If the primary
     network can't be joined, CardSat automatically falls back to this one. Handy
     in the field: keep your home router as the primary and a **phone hotspot** (or
     a portable travel router) as the second, and the device connects to whichever
     is present. Leave it blank to disable.
   - **AOS alarm** — on/off for the pre-pass beeps.
   - **VFO Type** — which physical VFO carries each leg: *Main Up/Sub Dn* (default)
     or *Main Dn/Sub Up*.
   - **Beacon/RX-only DL** — which VFO carries the downlink for receive-only entries
     (beacons, telemetry, SSTV, CW — anything with a downlink but no uplink). *Follow
     VFO* (default) puts it on the same VFO as a normal transponder downlink, per VFO
     Type; *Main* or *Sub* force it to that VFO. Use *Follow VFO* if swapping to a
     beacon mid-pass moves your receive to the wrong VFO; force *Main* only if your
     rig reads that VFO back more reliably for receive-only.
   - **Sat mode** — command the rig's own satellite mode on/off when you engage CAT
     (a no-op on rigs without CAT sat mode, including the IC-820/821/970 — engage
     SAT on those from the front panel).
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

## 8. Screen map and navigation

CardSat is a simple state machine. From **Home** you reach every screen; `` ` ``
or **DEL** always steps back. Backing out **returns you to the screen you came
from** — so opening **Passes** from the Home menu backs to Home, while opening it
from the **Satellites** list backs to the list; the same applies to **Track**,
which returns to Passes, Next Passes, or Home depending on where you launched it.

One deliberate exception: leaving the **Track** screen does **not** stop tracking.
If the radio and/or rotator are engaged they keep running in the background (CAT
Doppler and rotator pointing are serviced regardless of the current screen), and a
green **RAD** / **ROT** / **R+R** tag appears in the header so you know the rig may
still be transmitting. Stop with `r` (radio) or `o` (rotator) — see
[§8 → Track](#track).

### Home

A menu: **Satellites · Next Passes (all favs) · Passes (sel) · Track (sel) ·
World Map · Sun / Moon · Space Wx · Weather · Activations · QRZ Lookup · Location ·
Update · Settings · Log · Messages · About / diagnostics · Charge / Sleep.** The
currently selected satellite is shown at the bottom right. `;`/`.` move, ENTER selects.

### About

Author credit (**Paul Stoetzer, N8HM**), firmware version and build date, storage
backend (microSD or internal flash), GP catalog size and freshest element age,
WiFi/IP, battery level, free heap, and uptime. `` ` `` or ENTER returns home.

### Charge / Sleep

The last item on the home menu is a minimal low-power mode for charging or
parking the device. Selecting it **blanks the backlight and stops radio/rotator
output** so almost nothing runs. **Any key wakes the screen** to show battery
status — a large gauge, charging state, and the cell voltage — then it auto-blanks
again after about 5 seconds. **ESC/back** returns to the home menu.

The battery percentage here is derived from the cell **voltage** against a LiPo
discharge curve (3.30 V ≈ 0 %, 4.20 V ≈ 100 %), which is more accurate in the flat
middle of the discharge than the PMIC's raw level; it falls back to the raw level
if voltage isn't available. Charging state comes from the Cardputer ADV's PMIC.

Pressing **H** in this mode triggers an **on-demand heap reset**: on a long
session, repeated HTTPS/TLS handshakes can fragment the (no-PSRAM) heap until the
largest free block is too small for a new TLS context and fetches start failing,
even though total free heap still looks healthy. The reset drops and reconnects
WiFi/TLS to free and coalesce those transient allocations, and reports the
before/after largest contiguous block.

### Logging QSOs (Log)

There are two ways to start an entry:

- **From a pass** — press `l` on the **Track** or **Polar** screen to log the
  contact you're working; radio control keeps running while you type. CardSat
  snapshots the UTC time, satellite, mode and the live **Doppler-corrected**
  uplink/downlink frequencies, plus your grid and callsign (from **My callsign**
  in Settings). The form opens with the **Call** field already selected, so you can
  press ENTER and type the callsign straight away.
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
**Export to ADIF** (writes `/CardSat/qso_log.adi`, including `STATION_CALLSIGN`
from My callsign, for upload to LoTW/eQSL or import into your main logger),
**Sign & upload to LoTW** (see [LoTW upload](#logbook-of-the-world-lotw-direct-upload) below),
**Upload to Cloudlog** (see [Cloudlog upload](#cloudlog--wavelog-upload) below),
**Voice Memos** (the on-device voice-memo browser described above),
and **Notes** (a free-form text editor, see [Notes](#notes-free-form-text-editor) below).
The two upload options sit together: uploading to Cloudlog can itself forward your QSOs
to LoTW (if your Cloudlog instance is configured for that), so for most operators they're
alternatives rather than things to do separately. The core logging functions are listed
first and the menu scrolls; the non-core **Voice Memos** and **Notes** tools are grouped
together at the bottom.

LoTW limits the `SAT_NAME` field to six characters and uses its own names, so on
export CardSat translates each satellite via **`/CardSat/lotw_sats.csv`** (rows of
`SAT_NAME,AMSAT_NAME`; a built-in table covers the common cases if the file is
absent). If a logged satellite has no match, CardSat prompts you for its `SAT_NAME`
(entry capped at six characters, uppercased) and saves it back to the CSV, so it's
only asked once. Update that CSV on the card as new satellites appear.

### Logbook of the World (LoTW) direct upload

**Sign & upload to LoTW** (on the **Log** menu) signs your un-uploaded satellite
QSOs into a `.tq8` file and sends them straight to ARRL's Logbook of the World over
WiFi — no PC, no TQSL, no separate upload step. CardSat builds the same
cryptographically-signed file TQSL would, signs each QSO with your callsign
certificate's private key, and posts it to LoTW's self-authenticating upload
service (the signed payload authenticates itself, so no login is needed).

> **!  This feature requires a microSD card, and your LoTW private key lives on
> it.** Read this whole section before using it. The key is your amateur-radio
> identity credential. Treat the card like you'd treat that key: keep it in your
> possession, and don't use a shared or disposable card. CardSat loads the key only
> when you run an upload, keeps it in RAM only, and never copies or transmits it
> anywhere except as the signature inside the `.tq8`. **Use this at your own risk.**

**This is an upload feature, not enrollment.** ARRL issues first-time certificates
only through TQSL plus a postcard mailed to your FCC address — that can't happen on
a handheld. You enroll once on a computer the normal way, then copy your existing
credential to the card.

**One-time setup (on your computer):**

1. In TQSL, you already have your callsign certificate. Export it to a `.p12`
   file (TQSL: *Callsign Certificates → right-click your call → Save Callsign
   Certificate*, choosing the `.p12`/PKCS#12 format with a password).
2. Convert the `.p12` into the two PEM files CardSat reads. The easy way is the
   **bundled converter**: open **`tools/lotw_cert_converter.html`** in any web
   browser, choose your `.p12` file, enter the export password, and click
   **Convert** — it produces **`lotw_key.pem`** and **`lotw_cert.pem`** for you. It
   runs entirely in the browser (it even works with your network disconnected), and
   your private key and password never leave your computer.

   If you'd rather use the command line, OpenSSL does the same thing:

   ```
   openssl pkcs12 -in CALL.p12 -nocerts -nodes | openssl rsa -out lotw_key.pem
   openssl pkcs12 -in CALL.p12 -clcerts -nokeys -out lotw_cert.pem
   ```

   (The converter writes an unencrypted key, which is what CardSat expects. With the
   OpenSSL route you can instead keep the key encrypted on the card by dropping
   `-nodes` and setting a password; CardSat will ask for it at upload time.)
3. Copy **`lotw_key.pem`** and **`lotw_cert.pem`** into the **`/CardSat/`** folder
   on the microSD card.
4. In **Settings → Station / display**, set your LoTW station location. The fields
   form a chain — each one narrows the next:
   - **LoTW DXCC** — press ENTER to pick your entity from the full DXCC list. The
     entities that have a subdivision (United States, Canada, Japan, the Russias,
     China, Australia, Finland/Åland/Market Reef) are grouped at the **top** of the
     list, marked with a `>`; everything else follows alphabetically. Start typing to
     filter the list (e.g. "japan" or "unit"). The list is long, so the typeahead is
     the fast way in.
   - **LoTW CQ zone** and **LoTW ITU zone** — your zones.
   - **LoTW (primary)** — the row is labelled with your entity's actual subdivision
     name: **state** (US), **province** (Canada/China), **oblast** (Russia),
     **prefecture** (Japan), **state** (Australia), or **kunta** (Finland). Press
     ENTER to pick from the valid list for your DXCC; type to filter. If your entity
     has no subdivision the row shows **(n/a)** and you can skip it. (US/AK/HI **must**
     set a state — LoTW rejects uploads from those entities without one.)
   - **LoTW (secondary)** — only two entities have a second level: the **United
     States** (county, which lets your contacts earn county awards) and **Japan**
     (city/gun/ku). The row is gated by your primary choice — pick the state first and
     the county list follows; pick the prefecture first and the city list follows.
     Other entities show **(n/a)**.
   - **LoTW IOTA** — optional, any entity: enter your island reference (e.g. `NA-005`)
     if you're operating from a qualifying island.

   This is the same flow for everyone — the US is just the DXCC "United States" with
   "state" and "county" as its two levels, exactly like Japan's "prefecture" and
   "city". Your callsign and grid come from **My callsign** and your location, which
   CardSat already has. (Inside any picker: `;`/`.` move, ENTER selects, typing
   filters, and `` ` `` clears the filter or backs out.)

**Uploading:** open **Log → Sign & upload to LoTW**. The screen shows whether the
card, the key/cert, and the station fields are present, and how many QSOs haven't
been uploaded yet. Press **`u`** to sign and send; if your key is password-protected
you'll be asked for the password (leave it blank if you exported with `-nodes`).
CardSat signs each pending QSO, posts the `.tq8`, shows how many were accepted, and
flags those QSOs so they're never sent twice. (LoTW rejects duplicate uploads, and
CardSat tracks what it has sent in a new `uploaded` column in `qso_log.csv`.) Large
backlogs are sent in batches of up to 50 per upload; just run it again for the rest.

As with the Cloudlog upload, if the connection itself is refused (the rare
post-long-session memory-fragmentation case), CardSat offers to **reboot and upload** —
**ENTER** to reboot and finish from a clean start, **`` ` ``** to cancel. Because the
LoTW key password is never stored on the device, CardSat re-prompts for it after the
reboot before signing.

The satellite `SAT_NAME` translation described above applies here too — the same
`/CardSat/lotw_sats.csv` map is used, so make sure each bird you've worked has a
LoTW name before uploading.

**View / edit log** is a scrollable list of recent contacts (`;`/`.` to move),
**ordered newest-first** — the most recent QSO is at the top, and the list opens
there. Open one with ENTER to correct **any field — including the date, time, satellite
and frequencies** — then `s` to save, or press `x` twice to delete it; changes are
written straight back to the CSV. The most recent **60**
QSOs are available on the device — the complete log always lives in
`/CardSat/qso_log.csv`, which you can also read or edit on a computer.

### Cloudlog / Wavelog upload

CardSat can upload your satellite QSOs to a self-hosted **Cloudlog** (or the compatible
**Wavelog**) online logbook over WiFi. Because a Cloudlog instance can itself be set up to
forward QSOs on to LoTW, eQSL, and others, for most operators this is an **alternative** to
the on-device LoTW upload, not something to do in addition. CardSat tracks the two
independently, so a QSO uploaded to Cloudlog is not marked as sent to LoTW, and vice-versa.

**Setup** (all under *Settings → Station / display*):

- **Cloudlog URL** — the base address of your Cloudlog instance, e.g.
  `https://log.example.com`. Both `https://` and `http://` (for a LAN instance) are
  accepted. A trailing slash is trimmed automatically; CardSat appends the API path for
  you.
- **Cloudlog API key** — generate a **read-write** key in your Cloudlog account's API page
  and enter it here. (A read-only key is rejected for uploads.)
- **Cloudlog station ID** — the numeric *station profile id* the QSOs should be filed
  under, shown in your Cloudlog station-profile settings. Cloudlog verifies that this
  profile belongs to your key and that its callsign matches each QSO's station callsign, so
  make sure your **My callsign** setting matches the profile.

**Uploading:** open **Log → Upload to Cloudlog**. The screen shows your server, whether the
key and station ID are set, and the count of QSOs not yet sent to Cloudlog. Press `u` to
upload (CardSat connects to WiFi if needed). To re-send contacts that are already marked
uploaded — for instance after fixing a setting — press `a` to toggle re-send mode, then
`u`. On success the screen reports how many QSOs Cloudlog imported. If the **server**
rejects the upload it shows the reason (a wrong key, insufficient key rights, or a
station-ID/callsign mismatch are the usual causes). Your API key is treated as a secret and
is never written to the serial log.

If instead the upload can't even **connect** (a rare "connection refused" that can occur
after a long operating session has fragmented memory), CardSat offers a failsafe: it asks
whether to **reboot and upload** — press **ENTER** to reboot and complete the upload from a
clean start, or **`` ` ``** to cancel and leave the QSOs pending. A fresh boot reliably
clears the condition. (This only appears for a true connection failure; a server rejection,
which a reboot wouldn't fix, is just reported.)

### Notes (free-form text editor)

**Notes** on the **Log** menu is a simple multi-page text editor for jotting things
down on the device — a sked frequency, a grid you still need, antenna settings, a
reminder for the next pass. Each note is a plain `.txt` file stored under
**`/CardSat/notes/`** (on the microSD card if one is fitted, otherwise in internal
flash, so notes work with or without a card), which means you can also read or edit
them on a computer.

The **browser** lists your notes newest-first, each with the date and time it was
last saved (UTC, like every other time on the device). Press **`n`** to start a new
note, **ENTER** to open the highlighted one, and **`d`** then **ENTER** to delete it
(a confirmation step guards against an accidental press). **`` ` ``** returns to the
Log menu.

The **editor** is a full multi-line editor: just type, with **ENTER** for a new line
and the **delete** key for backspace. Because the Cardputer's `;` `.` `,` `/` keys
are needed as ordinary punctuation in your text, the editor's commands all use the
**Fn** modifier, so every plain key types literally:

- **Fn + `,`** and **Fn + `/`** move the cursor **left** and **right**.
- **Fn + `;`** and **Fn + `.`** move the cursor **up** and **down**.
- **Fn + `s`** **saves** (the first time, you're asked for a name; allowed
  characters are letters, numbers, spaces, `-` and `_`, capped at 31 characters).
- **`` ` ``** **exits** to the browser. If you have unsaved changes, a named note is
  saved automatically; an unnamed note prompts you for a name first (or press
  **`` ` ``** again at that prompt to discard it).

Notes are limited to 4 KB each, which is several pages of text — ample for operating
notes.

### Satellites

The catalog (up to 220 sats from the GP data, plus any you add manually).

- `;`/`.` move, `{`/`}` jump 10 rows.
- `f` — toggle **favorite** (favorites are marked with `*`).
- `v` — toggle **favorites-only** filter.
- `n` — add a **manual GP satellite**: enter the name, NORAD ID, epoch, then each
  orbital element in turn (see **Edit** under [§8](#8-screen-map-and-navigation)). If your
  elements are only in TLE form, convert them first with `tools/tle2gp.py` (see
  [§8](#8-screen-map-and-navigation)).
- `x` — **delete** the selected satellite, but *only if it was added manually*
  (one you entered with `n`). Press `x` once to arm, `x` again to confirm. This
  removes it from `/CardSat/mgp.json` and from your favorites; cached GP
  satellites from the network can't be deleted this way (they'd just return on the
  next Update). To **edit** a manual satellite, delete it and re-add it with `n`.
- `e` — open the **EQX table** (equatorial crossings) for the selected satellite,
  for use with an OSCARLOCATOR (see [Transponder/EQX screens in §8](#8-screen-map-and-navigation)).
- `2` — open the **sat-to-sat visibility finder**: the windows when this satellite
  and a second one (chosen from your favorites) are **both above your horizon at
  once** over the next few days (see below).
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

Each row may also show a small **down-arrow** to the left of the activity mark — a
**decay flag** for orbits that are dropping. It's coloured by severity: **yellow**
(watch), **orange** (decaying), or **red** (reentry imminent), derived from the
satellite's perigee altitude and the lifetime estimate (see the **Perigee** line on
the Orbital-analysis screen). It's an at-a-glance "this bird is on its way down" cue
— useful for working a satellite before it's gone, and for spotting objects nearing
reentry. The level is an order-of-magnitude estimate from the elements, **not** a
precise reentry prediction; for that, consult CelesTrak or Space-Track.

### Orbital analysis (`o`)

Opened with `o` from **Satellites**. Nine pages, paged with `,`/`/`, `r` to
recompute, `` ` `` to leave.

#### Theory of operation — how these numbers are produced

A short primer, because the pages below assume some of this:

**The elements.** Each satellite is described by a set of **mean orbital elements**
(the modern GP/OMM form of the classic two-line element set, TLE). The six that fix
the orbit's size, shape and orientation are: **semi-major axis** *a* (orbit size,
here derived from mean motion), **eccentricity** *e* (how elliptical, 0 = circle),
**inclination** *i* (tilt of the orbit plane to the equator), **right ascension of
the ascending node** Ω/RAAN (where the orbit plane cuts the equator going north,
measured around the equator), **argument of perigee** ω (where the low point sits
within the orbit plane), and **mean anomaly** *M* (where the satellite is along the
orbit at the element epoch). **Mean motion** *n* (revolutions/day) stands in for *a*,
and **B\*** carries atmospheric drag. "Mean" means small periodic wobbles have been
averaged out; the propagator puts them back.

**The propagator.** CardSat predicts position with **SGP4** — the same Simplified
General Perturbations model the elements were built for. SGP4 is not a plain Kepler
solver: it folds in the dominant perturbations (Earth's oblateness through the **J2**
harmonic, plus atmospheric drag via B\*) so that a near-Earth satellite's real motion
is reproduced to roughly a kilometre near epoch. Every live read-out — az/el, range,
range-rate, sub-point — comes from propagating the elements to the current instant and
converting the resulting position/velocity vectors. **Accuracy decays with element
age**, which is why the Info and Orbit-position pages show how old the set is and
CardSat nudges you to refresh GP. For the full theory of GP/OMM elements, what SGP4
is and isn't, the coordinate frames involved, and where prediction error comes from,
see [§14](#14-gp-age-and-accuracy).

**Range rate and Doppler.** The radial velocity (range-rate) is taken straight from
the SGP4 velocity vector — the component along the station→satellite line. Doppler
shift is then `Δf = −(range-rate ÷ c) · f`: positive range-rate (receding) lowers the
received frequency, and the shift scales with frequency, so a 70 cm downlink swings
roughly three times as far as a 2 m one. This is the same quantity the Track screen
uses to tune the radio (see [§9](#9-doppler-tuning-and-the-one-true-rule)).

**J2 and why pass times drift.** A spherical Earth would hold the orbit plane fixed in
space; the real Earth's equatorial bulge (the J2 term) makes the plane **precess**.
The two secular rates CardSat reports come directly from J2: the node regresses at
`Ω̇ = −1.5 n J2 (Re/p)² cos i` (°/day) and perigee rotates at
`ω̇ = 0.75 n J2 (Re/p)² (5cos²i − 1)` (°/day), where *p* = *a*(1−*e*²). Node regression
is what walks a Sun-relative orbit's pass times earlier each day, and when it happens
to equal the Earth's ~0.986°/day motion around the Sun the orbit is **sun-synchronous**
(it keeps a fixed local solar time — the **LTAN**). Apsidal precession turns ω, moving
where perigee sits; it goes to zero at the **critical inclination** of 63.4°
(where 5cos²i−1 = 0), the trick Molniya orbits use to keep perigee parked.

**Beta angle and eclipses.** The **solar beta angle** β is the angle between the Sun
and the orbit *plane* (CardSat computes it as 90° minus the angle between the orbit-
normal vector and the Sun vector). It governs lighting: at low β the satellite dips
into Earth's shadow once per revolution; past a threshold **β\*** = arccos(Re/(Re+h))
the orbit rides above the shadow and is in **continuous sunlight**. CardSat tests
shadow with a **cylindrical-umbra** model (Earth casts a parallel-sided shadow tube,
no penumbra) — simple, and good to about a minute for power-budget purposes. As the
node precesses (above), β cycles over weeks, producing **eclipse seasons** and
full-sun seasons; the Sun/Beta and Illumination screens visualise this.

**Footprint geometry.** The ground footprint is the cap of Earth from which the
satellite is above the horizon. Its diameter is `2·Re·acos(Re/(Re+h))`, set purely by
altitude *h* — higher means a wider circle and a longer maximum possible contact. For
an elliptical orbit the footprint breathes between its apogee (largest) and perigee
(smallest) values, both of which the Info page lists.

The pages:

- **Info** — NORAD, current altitude, **footprint** diameter, period, apogee/
  perigee altitudes, the **footprint diameters at apogee and perigee**,
  inclination/eccentricity, semi-major axis, B\* with a rough drag-decay
  estimate, element age/revolution, and the next ascending-node longitude/time.
  The decay estimate integrates B\* through an exponential-atmosphere model with
  a **King-Hele** treatment that lets an eccentric orbit circularize (drag at
  perigee lowers apogee fastest), down to a ~120 km reentry. The physics: drag
  acts hardest where the air is densest, which is at **perigee**, and a velocity
  loss there pulls down the *opposite* side of the orbit — so an elliptical orbit
  rounds off (apogee drops toward perigee) before the whole thing spirals in. B\*
  scales how much atmosphere the satellite "feels" (ballistic coefficient × a
  reference density), and the model decrements energy each revolution until the
  perigee reaches the ~120 km reentry floor. Because
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
  not just *whether* it's in shadow but *how deeply*. The depth is the angular
  gap between the satellite's anti-solar position and the edge of the shadow
  cylinder: it climbs as the bird moves toward the centre of the shadow and
  crosses zero exactly at the **terminator** (entry/exit), which is why a value
  near 0° means a sunrise/sunset is imminent — useful for spin-stabilised or
  solar-powered birds whose behaviour changes at the shadow boundary.
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
  (an overhead pass at apogee). The repeat cycle looks for a small whole number of
  days in which the satellite completes a whole number of revolutions, so its
  track over the ground closes back on itself — when it exists, the same passes
  recur on that period (a 3-day repeat means today's geometry returns in three
  days). These are first-order estimates — handy for understanding why pass times
  march earlier each day, whether a bird is sun-synchronous, and how long the very
  best passes can run — not a precision propagator.
- **Sun / Beta** — the orbit's lighting geometry. The **solar beta angle** (the
  angle between the Sun and the orbit plane) now, the analytic **β\*** threshold
  beyond which the orbit is in continuous sunlight, and whether the bird is
  currently in a **full-sun orbit** or **eclipsed every revolution** — the verdict
  and the **eclipse fraction / minutes per orbit** come from sampling a full orbit
  with the same geometric shadow test the Illumination screen uses, so the two
  never disagree. A final line scans ahead to the **next transition** (the date the
  orbit next crosses into full sun, or back into eclipse, with a day countdown).
  Useful for solar-powered birds and for anticipating eclipse-season power
  behaviour.
- **Pass outlook** — a planning summary over the next 7 days: the total number of
  passes above your mask, how many clear 30°, the longest pass, and the mean gap
  between passes — plus the **best upcoming pass** (its peak elevation, the
  date/time it occurs, the countdown to it, and its duration). This answers "when
  this week is worth operating this bird" at a glance.
- **Orbit position** — where the satellite is in its orbit right now: mean
  anomaly, true anomaly, and **argument of latitude**, the **time to the next
  perigee and apogee**, plus argument of perigee, RAAN, the current revolution
  number, and the element-set age. The three angles describe position three ways:
  **mean anomaly** advances at a constant rate (it's the orbit clock, uniform in
  time); **true anomaly** is the real geometric angle from perigee (it runs ahead
  near perigee where the bird moves fast, behind near apogee — the two are equal
  only for a circular orbit); and **argument of latitude** (ω + true anomaly) is
  the angle up from the equator, which tells you how far through the
  north/south part of the orbit it is. For eccentric orbits the perigee/apogee
  timing shows when the bird is moving slowest and sitting highest — i.e. when the
  longest-dwell passes occur.

### Next Passes (schedule)

One merged list of the next pass for **every favorite**, soonest first. Each row
shows the countdown to AOS (or **NOW** if the bird is already up), the satellite
name, maximum elevation, and pass length. A red **`!`** flags a satellite whose
elements are stale (see [§14](#14-gp-age-and-accuracy)). A yellow **`*`** marks a
**visually observable** pass (satellite sunlit, sky dark, high enough) when visible-pass
prediction is enabled in Settings — see [§7 → Visible passes](#7-first-time-setup).

- `;`/`.` move.
- **ENTER** — jump straight to **Track** for that satellite.
- `m` — open the live **World map** (all footprints; see [World map](#world-map)).
- `r` — recompute the schedule.
- `z` — **deep-sleep** until ~60 s before the next AOS (see [§12](#12-aos-alarm-and-deep-sleep)).
- `` ` `` — back.

### Passes

Upcoming passes for the selected satellite, in UTC: **AOS time, duration, max
elevation, LOS time.** The element-set age is shown top-right, color-graded. A
yellow **`*`** to the right of a pass marks it as **optically visible** — the
satellite is sunlit while your sky is dark and the pass clears the visibility
elevation gate (see Settings → Visible passes).

- `;`/`.` — select a pass.
- `d` — open the **pass-detail plot** (below).
- `t` or **ENTER** — go to **Track**.
- `n` — add a **manual transponder** for this satellite (see below).
- `r` — recompute the pass list.
- `x` — **mutual window**: enter a remote grid to find co-visibility passes (below).
- `v` — **10-day pass overview** (InstantTrack-style visibility chart; below).
- `V` — **visible-pass list**: every optically-visible pass for this satellite over
  the next 10 days, as a scrollable list (below).
- `i` — **illumination** (60-day Sun/eclipse raster; below).
- `` ` `` — back to where you came from (Satellites, or Home if you opened Passes
  from the Home menu).

### Visible-pass list (`V` from Passes)

A scrollable list of every **optically-visible** pass for the selected satellite
over the next 10 days — filtered to passes where the satellite is **sunlit**, your
**sky is dark** (Sun below the configured gate), and the **peak elevation** clears
the visibility minimum. Each row shows AOS time, duration, peak elevation, and LOS,
the same columns as the Passes screen. `;`/`.` scroll; **ENTER** opens the
**pass-detail plot** for the highlighted pass (and returns here when you back out);
`r` recomputes; `` ` `` returns to Passes. This complements the **10-day chart**
(`v`), which shows *all* passes graphically — the `V` list is just the ones you
could actually see. Requires the **UTC clock** and your **location** to be set.

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
`;`/`.` scroll; **`d`** opens the **DX Doppler table** for the highlighted
window (below); `` ` `` returns to Passes.

> Co-visibility is computed to the **0° horizon** for both stations; a window
> with very low max elevations may be hard to use in practice. The remote grid
> is assumed at sea level.

#### DX Doppler table (`d` from a mutual window)

For a short window, the hard part of a coordinated contact is that **the dial
frequencies are different at each end** — your Doppler and the DX station's
Doppler aren't the same, because you're moving relative to the satellite by
different amounts. This table solves that. It lists, **every 30 seconds across the
window**, the predicted **RX (downlink)** and **TX (uplink)** dial frequencies for
**both your station and the DX station**, for the selected transponder. Each
30-second step shows two lines — your dials (green, "me") and the DX station's
(cyan, "DX") — so the four frequencies never run together. Press **`t`** to cycle
which transponder is used (the same selection you can also make with `t` on the
Satellites screen).

Three tracking modes, cycled with **`m`**:

- **True rule** — the operating point is held fixed *in the satellite's
  passband*; every dial Doppler-tracks, each station with its own geometry. Both
  of you stay on the same transponder channel the whole pass.
- **Fixed downlink** — the **anchor** station holds its **receive dial constant**
  in real RF (the knob they don't touch). The passband operating point drifts to
  absorb that station's downlink Doppler, and the other three dials are computed to
  follow.
- **Fixed uplink** — the anchor station holds its **transmit dial constant**; the
  other three dials follow.

Press **`a`** to cycle which dial is the **anchor** (my RX, my TX, DX RX, or DX
TX). For a **linear** transponder you also choose **where in the passband** to
operate: the table **opens at the centre of the passband** (and re-centres when you
switch transponders with **`t`**). The header shows the operating point **relative
to the centre of the passband's downlink** — `ctr` at centre, or a signed offset
like `+7.5k` / `-12.5k` — so you always know how far off-centre you are.

How the **`,`/`/`** keys step depends on the mode:

- In a **fixed** mode (fixed downlink or fixed uplink), `,`/`/` step the **anchored
  dial** to the next **round 1 kHz**, grid-aligned to the centre of the passband.
  This is what lets you park your fixed RX or TX on a clean, easy-to-call number:
  cycle the anchor to the dial you care about (say *my TX*), and each press lands
  that transmit dial on 145.949, 145.950, 145.951 MHz — never 145.9502. CardSat
  nudges the passband so the anchored dial sits exactly on the kHz, then recomputes
  the rest of the table around it.
- In **true rule** mode (nothing anchored), `,`/`/` simply nudge the passband
  operating point by 1 kHz, as before.

RX values are shown in green, TX in yellow. **`;`/`.`** scroll through the
30-second steps; `` ` `` returns to the mutual window list.

> Frequencies include your station calibration offsets. The DX station is assumed
> at sea level with no local calibration. For an SSB contact, treat the numbers as
> the centre of where to listen/transmit and fine-tune by ear.

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
- `n` — **jump to the beacon**: select the satellite's beacon (a downlink-only
  entry, or one whose description names a beacon) so the radio tunes straight to
  it with Doppler correction. Useful for finding a bird by its telemetry/CW beacon
  before working it. Also available on the **Big readout** screen. If the satellite
  has no beacon listed, a status note says so. On an Icom rig a receive-only
  transponder like this turns the rig's **satellite mode off** and tunes the
  downlink on the **MAIN** band (which also reads back more reliably), matching how
  OscarWatch and the SDR-Control apps handle receive-only birds.
- `c` — set the **CTCSS/PL tone** for this satellite (numeric entry: a tone in
  Hz, `0` to force it off, or blank to revert to the built-in default).
- `N` — edit a **per-satellite operating note**: a short free-text reminder that
  travels with the bird (active modes, schedule, "PL 67.0, use high passes," your
  own observations). It's stored by NORAD and shown on the Track screen — a `*`
  appears next to the satellite name in the header and the note text on its own
  line. Leave it blank to clear. Notes persist across reboots and reflashes (in
  `notes.txt`).
- `k` — on a **linear (SSB/CW) transponder**, toggle **CW mode on both legs** so
  you can work CW through the bird instead of SSB. CardSat sets CW on the uplink
  and downlink; on an inverting transponder the sideband flips but CW is CW on both
  ends, so you just zero-beat your downlink. The choice is **per channel** (it
  resets when you change satellite or transponder), shows a `CW` tag on the Track
  and large-font screens, and applies live whether the radio is already engaged or
  you turn it on afterward. On an FM bird the key is a no-op ("CW: linear birds
  only"). Available on both the Track and large-font screens.
- `r` — turn radio output **on/off** (sets modes and begins Doppler service).
- `o` — turn **rotator** pointing **on/off** (if a rotator is configured; sends
  live az/el to the selected backend, parks on stop). See [§17](#17-antenna-rotator-gs-232-rotctl-pstrotator-yaesu-direct-rotctld-server).
- `p` — open the **Polar** plot.
- `z` — open the **large-font readout** (see below). A quick way to read RX/TX,
  az/el and the AOS/LOS countdown at arm's length; radio and rotator keep running.
- `y` — toggle **tilt tuning** on/off on the fly (only if the board has the sensor
  and the feature is otherwise available; see Settings). Lets you flip accelerometer
  tuning on for a tricky stretch and back off without opening Settings.
- `v` — start/stop a **voice memo** (recorded to the microSD card via the ADV's
  built-in mic); radio and rotator tracking keep running while you record.
- `g` / `w` / `e` — show the **workable grids / US states / DXCC** reachable under
  the footprint **right now**, refreshing live; radio and rotator control keep
  running while you look. (The same three lists are also available per-pass from
  the Passes screen.)
- `f` — open **Manual mode** (frequency calculator; see below). Useful when you
  have no CAT-controlled radio and are tuning by hand.
- **ENTER** — save the current calibration **for this satellite**.
- `` ` `` — leave the Track screen, **returning to the screen you came from**
  (Passes, Next Passes, or Home). If the radio and/or rotator are engaged, they
  **keep tracking in the background** — the loop's Doppler and rotator service runs
  regardless of which screen you're on, so you can browse passes, log a QSO, check
  the map, etc. while the rig stays Doppler-corrected and the antenna stays pointed.
  A green **RAD** / **ROT** / **R+R** tag in the header on those other screens shows
  tracking is still live (and that the rig may be transmitting). Use **`r`** to stop
  the radio and **`o`** to stop (and park) the rotator. When neither is engaged, `` ` ``
  is just a plain exit. Background tracking stays locked to the satellite it was
  started on: if you **select a different satellite** while it's running (for example
  by opening another bird from the Satellites list), tracking **stops and the rotator
  parks** rather than silently retargeting the rig — re-engage with `r`/`o` once
  you've opened the new satellite's Track screen.

TUNE-mode keys: `,`/`/` tune down/up the passband, `s` cycle step
(100/1000/5000 Hz), `x` recenter. CAL-mode keys: `,`/`/` trim downlink, `;`/`.`
trim uplink, `s` cycle step (10/100/1000 Hz), `x` zero. See
[§9](#9-doppler-tuning-and-the-one-true-rule) and [§10](#10-calibration). When
**Tilt tuning** is enabled (see Settings), in TUNE mode on a linear bird you can
also roll the Cardputer left/right to move through the passband — a small **TLT**
marker on the passband line shows it's armed.

### Large-font readout (`z` from Track)

A stripped-down, glanceable view for operating a pass without squinting: the
**RX** and **TX** frequencies in the largest digits that fit, with **Az/El** and
the active **Doppler tune mode** below them. Small badges show **RAD**/**ROT**
(radio and rotator on/off), **TILT** if tilt tuning is armed, and the transponder
index. The bottom line echoes the tune mode (**FULL / DL / UL / TUNE / CAL**) and,
on a linear bird, the passband position — so the big view follows whatever Doppler
tuning option you selected on the Track screen.

The radio, rotator and Doppler tracking keep running exactly as on Track — this is
just an alternate view of the same live session. All the in-place Track controls
work here too: `,`/`/` tune through the passband, `s`/`x` step/recenter, `m`
TUNE/CAL, `d` cycle tune mode, `t` next transponder, `r` radio, `o` rotator, `y`
tilt, and `l` to log a QSO. Press `z` or `` ` `` to return to Track.

(The earlier AOS/LOS countdown was removed from this view to give the frequencies
more room; the countdowns remain on the regular Track and Passes screens.)

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
  step, `x` recenters. The HOLD leg's parked value stays put; the TUNE> leg
  follows with Doppler. Because the goal is to keep hearing *yourself* on the
  fixed leg, the TUNE> leg is corrected for the **round-trip** Doppler — it
  cancels both the uplink and downlink shift, in **either** direction (hold the
  downlink and tune the uplink, or hold the uplink and tune the downlink), since
  where your own signal lands depends on where the bird heard your transmission.
  (Fixing a single satellite-passband point instead — the convention used when
  CardSat drives a real radio on the Track screen — lets the downlink drift on the
  ground; here the goal is the opposite, a stationary fixed leg you can park on.)
- **FM birds** — pick which leg is fixed with `u` (typically the **VHF** leg,
  which has little Doppler and is the one you park). The other (UHF) leg shows
  the Doppler-corrected frequency to chase. FM legs are independent channels, so
  no round-trip correction applies. A hint line notes which leg is fixed and
  whether it's VHF.
- **Downlink-only birds** — just the computed downlink to tune your receiver to.

`m` toggles **CAL** (trim the same per-satellite calibration as Track), `t`
cycles transponder, `l` logs a QSO, `p` opens the polar plot, `g` opens live
workable grids — and **the log, polar, and grid screens all return here** rather
than to Track. **ENTER** saves calibration for this satellite. `` ` `` or `f`
returns to Track.

Press **`z`** for a **large-font version of the Manual calculator** — the HOLD and
TUNE legs in big digits, with the fixed/derived leg labelled, for reading at arm's
length in the field. The in-place keys (`u` swap leg, `m` CAL, `,`/`/` tune, `s`/`x`,
`t`) work the same there; `z` or `` ` `` returns to the normal Manual view. If the
board has the motion sensor and **Tilt tuning** is on, you can roll the device to
move through the passband in Manual mode too, exactly as on Track.

### Polar

A sky plot centered on the zenith: N/E/S/W cardinal labels, elevation rings at
30°/60°, the horizon as the outer ring. A green dot marks the satellite; a yellow
glyph marks the **Sun** (when it's above the horizon). The satellite's **path
across the sky for the current pass** is drawn as a cyan arc with **A** (AOS) and
**L** (LOS) markers and a white arrowhead showing the **direction of travel**.
When the bird is below the horizon the arc shows the **next** pass instead, and
the readout gives that pass's AOS time. The right-hand readout also shows az/el,
range, range-rate, **Sun az/el**, and whether the satellite is **SUNLIT** or in
**ECLIPSE**. `l` logs a QSO and `v` starts/stops a **voice memo** (microSD, ADV)
without leaving the plot. `p`, ENTER, or `` ` `` return to Track.

### OSCARLOCATOR (`k` from Satellites)

A live **azimuthal-equidistant** plotting board — the classic OSCARLOCATOR view —
reached with `k` on the **Satellites** screen. Unlike the **Polar** screen (which
plots a single pass in az/el over your sky), this plots the satellite's
**sub-point on the Earth** in real time, the way a paper OSCARLOCATOR dial does.
It complements the tabular **EQX table** (`e`): the EQX table lists the
equator-crossing times and longitudes, and this view shows the resulting geometry.

Two modes, toggled with **`m`** (the view **opens in polar mode** by default):

- **Polar** (default) — a pole sits at the centre and the satellite is plotted by
  latitude (distance from the pole) and longitude (angle). CardSat **automatically
  chooses the North or South polar sheet** from the satellite's current hemisphere
  and **flips between them live** as the bird crosses the equator, so the satellite
  always stays on the visible chart.
- **QTH-centred** — your station sits at the centre, with range/elevation rings
  around it. The satellite is drawn at its true bearing and great-circle distance
  from you, so you can see at a glance where it is relative to your horizon and
  how far away it is.

Both modes draw a coarse coastline, a lat/lon graticule, the satellite marker
(yellow when sunlit, cyan in eclipse) and its **ground footprint** circle. They
also draw a **QTH range ring** as a dashed amber circle — the satellite's footprint
radius at the bird's mean altitude, centred on your station, so anything the
satellite's sub-point reaches inside that ring is a workable pass (dashed so it
stays distinct from the same-size instantaneous footprint) — and the **ground-track
arc** in blue: the satellite's full sub-point track across the disc — a complete
orbit's worth of ground track centred on the current time, so it sweeps the whole
projection like a real OSCARLOCATOR (portions outside the plotted radius are
clipped). Green and orange markers show where the current pass's **AOS** and **LOS**
fall along that track, and a white arrowhead at the satellite shows the direction
of travel. The right-hand readout
shows the sub-satellite lat/lon, az/el, slant range, altitude, and sunlit/eclipse
state; an **(off chart)** note appears when the satellite is outside the plotted
area (e.g. below the horizon in QTH mode, or in the other hemisphere in polar mode
just before the view flips). `m` toggles the mode; `` ` `` returns to Satellites.

### 3D Globe (`3` from Satellites)

A three-dimensional wireframe Earth, reached with `3` on the **Satellites** screen.
Where the world map is a flat projection and the OSCARLOCATOR is a flat azimuthal
disc, this is an **orthographic globe** — the Earth as a sphere you're looking at
from space. Only the near hemisphere is drawn; anything on the far side of the
Earth is hidden behind the curve, just as it would be in real life.

The globe **auto-follows the selected satellite**: it rotates continuously so that
satellite's sub-point stays at the centre of the disc. Drawn on it:

- a **graticule** (meridians and parallels every 30°) and a coarse **coastline**,
  both clipped at the limb so only the visible hemisphere shows;
- a **day/night terminator** in yellow — the line dividing the sunlit and dark
  halves of the Earth, computed from the Sun's current sub-point;
- your **QTH** as a white cross;
- **all your favourites** as small dim-green dots (those on the near hemisphere),
  with the **selected satellite** drawn larger and centred (yellow when sunlit,
  cyan in eclipse);
- the selected satellite's **ground footprint** as a green circle, which wraps
  around the limb of the globe naturally as the bird nears the horizon;
- the selected satellite's **ground-track trail** in blue — a full orbit's worth of
  sub-points projected onto the sphere, so you can see where the bird came from and
  where it's heading;
- optionally a **second (DX) location**: press **`g`** and enter a Maidenhead grid
  square (e.g. `IO91`). The DX station is drawn as an orange box with its own
  footprint ring at the selected satellite's altitude; where that ring overlaps the
  satellite's own footprint is the **live mutual-visibility region** for that bird —
  the area from which both you and the DX station could work it at once. Press
  **`G`** (shift-g) to clear the DX location.

The right-hand readout shows the selected satellite's sub-lat/lon, az/el, altitude,
sunlit/eclipse state, the view mode (**follow** or **free**), and either the DX grid
(when set) or the favourite count. **Arrow keys** turn the globe by hand — this
drops into **free-look** so you can inspect any part of the Earth; **ENTER**
re-snaps to auto-follow. `` ` `` returns to Satellites.

### Sat-to-sat visibility (`2` from Satellites)

Press **`2`** to find the windows when the selected satellite **and a second
satellite** are **both above your horizon at the same time** over the next five
days — for cross-satellite relay experiments, or simply to plan back-to-back
working of two birds on one outing. The second satellite is taken from your
**favorites** list.

You first land on a **pick screen** that shows the currently-selected second
satellite and its position in your favorites (e.g. "3 of 7"). Press **`n`** /
**`p`** to step forward/back through favorites — this is **instant**, because no
search runs while you are choosing. When you have the satellite you want, press
**ENTER** (or **`r`**) to run the window search. After results appear, `n`/`p`
return you to the pick screen to choose a different second satellite, so you never
sit through a calculation just to scroll past a bird you didn't want.

Each row is one overlap window, showing its **start time (UTC)**, **duration**,
and the **peak elevation of each satellite** during the window (the first in
green, the second in cyan) so you can judge how usable it is. `;`/`.` scroll;
`` ` `` returns to Satellites.

The search runs in the background with a **progress bar** while it scans, so the
screen stays responsive (the back key works throughout) and always finishes.

> Both satellites are evaluated to your **0° horizon**. A window with very low
> peak elevations on either bird may be hard to use. The search looks three days
> ahead and lists up to sixteen windows.

### Voice memo (`v`, SD card required)

Press `v` on the **Track**, **Manual**, **large-font readout** (Track or Manual),
or **Polar** screen to record a short spoken note — for example "worked W1ABC, good
signal" during a pass — without leaving the screen. A red **REC** badge with a
countdown appears top-right while recording, and **radio, rotator, and web control
keep running** throughout (the memo is captured one small block at a time between
the normal tracking updates). Press `v` again to stop, or it stops automatically at
the 30-second cap.

Memos are saved as 16 kHz mono WAV files under **`/CardSat/audio/`** on the SD card,
named by UTC timestamp and (when a satellite is selected) the bird being tracked,
e.g. `memo_20260617_203145_AO-91.wav`. **An SD card is required** — with no card,
`v` reports "Memo: SD card required" and does nothing.

**Voice Memos browser (Log → Voice Memos).** Memos can be reviewed on-device from
the **Log** menu's **Voice Memos** entry. The browser lists every memo on the card
**newest first**, showing each one's date, time, the **satellite** it was recorded
on (or `-` for a standalone memo), and its length. Keys:

- **ENTER** — play the selected memo through the speaker. Playback streams from the
  SD card a block at a time; **any key stops** it early.
- **`n`** — record a **new standalone memo** right from the browser, not attached to
  any satellite. A `REC` timer shows while it records; **any key stops and saves**.
- **`d`** — delete the selected memo (with a confirm prompt).
- **`r`** — refresh the list; **`` ` ``** returns to the Log menu.

You can still copy the WAV files off the card on a computer as well.

> **Cardputer ADV — voice memo requires building against ESP-IDF 5.4.x.** The ADV
> captures through an ES8311 codec (the original Cardputer used a PDM mic), and the
> codec's record clock is only driven correctly when CardSat is built against the
> **ESP-IDF 5.4.x** toolchain. In practice that means the **Espressif "esp32" Arduino
> core 3.2.x** (which bundles IDF 5.4.2). On the newer 3.3.x core / IDF 5.5.x the
> mic path produces a constant (silent) signal — an upstream regression, not a
> CardSat bug (see
> [espressif/esp-idf#18621](https://github.com/espressif/esp-idf/issues/18621)).
> Everything else in CardSat builds and runs on either toolchain; only voice memo is
> affected. To confirm your build is correct, the boot log should report an IDF
> 5.4.x SDK version. Recording uses M5Unified's `M5.Mic`, so an up-to-date M5Unified
> is recommended.

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
- `v` — open the **live GPS position** screen (below).
- **ENTER** — open the **GPS sky plot** (GNSS satellites in view).
- `` ` `` — back (applies the position to the predictor).

#### Live GPS position (`v`)

A full-precision readout of where you are, for rovers, portable ops, and grid-line
activations. It shows your **latitude and longitude in degrees-minutes-seconds**
to 0.001″ (about 3 cm — finer than the fix itself), the same coordinates in
**decimal degrees** to six places, your **altitude**, your **Maidenhead grid**,
and — from a live fix — your **ground speed** (km/h and knots) and **course over
ground** (degrees true with a cardinal label). The top line shows the fix status,
the number of satellites used, and the HDOP. The grid recomputes live as you move,
so you can watch it flip as you cross a grid boundary. `` ` `` returns to Location.

### LoRa Messages (Home → Messages)

CardSat-to-CardSat **text messaging** over LoRa, using the M5Stack **Cap LoRa
(SX1262)** module. It is a **broadcast group chat**: every CardSat tuned to the
same frequency, spreading factor and bandwidth sees every message — there is no
addressing or routing, which keeps it simple and reliable for a group on an
outing, a club net, or a SOTA/portable activation.

> **Hardware-verified.** Two-way LoRa text messaging is confirmed working on the M5Stack
> Cap LoRa (SX1262), tested against a LilyGo T-LoRa unit running the companion CardSat
> Pager firmware. It's built into the standard binaries (RadioLib is a required build
> dependency). **Know your band's rules** — pick the **LoRa region** preset that matches
> where you operate (see Settings below):
>
> - **US (default)** — the **33 cm amateur band (902–928 MHz)**. The US 70 cm band
>   is held to ~100 kHz occupied bandwidth, which is tight for 125 kHz LoRa, so
>   33 cm is the home for amateur LoRa here. Default **906.875 MHz**, clear of the
>   busy 915 MHz ISM centre.
> - **EU** — the **70 cm amateur band (430–440 MHz)**. Default **433.775 MHz**, the
>   LoRa-APRS standard, at 125 kHz.
> - **Japan** — the **430 MHz amateur band (430–440 MHz)**. Default **431.000 MHz**.
>   (Japan's 920 MHz band is ISM, with certification rules — not amateur — so
>   amateur LoRa belongs on 430 MHz.)
>
> These are starting points within each band, not the only legal frequencies; you
> can still set any carrier 150–960 MHz by hand, and you remain responsible for
> operating within your licence and local regulations.

**Using it.** Set your callsign (Settings → Station, or the QRZ screen), enable
**LoRa msg** in Settings → Network/data, pick your **LoRa region**, then fine-tune
the frequency/SF/bandwidth to match the other stations. Open **Messages** from the
Home menu. Press `n` to write a message on the full keyboard and **ENTER** to send;
`;`/`.` scroll back through history. Your own messages show in cyan as `>me`;
received messages show in green by the sender's callsign. The top line shows the
current frequency, SF, bandwidth and your callsign. History holds the most recent
24 messages (a fixed ring — no SD card needed, lost on reboot).

**Settings.** In Settings → Network/data: **LoRa msg** on/off (turning it on
brings the radio up); **LoRa region** (US 33cm / EU 70cm / JP 430 — selecting one
seeds a legal default frequency + 125 kHz bandwidth for that band); **LoRa freq**
(arrow-adjust in 100 kHz steps, or ENTER to type an exact MHz value, 150–960 MHz);
**LoRa SF** (7–12; 12 = maximum range and sensitivity, the default; lower = faster
but shorter range); **LoRa BW** (62.5 / 125 / 250 kHz); **LoRa TX pwr** (0–22 dBm).
Both ends must use the same frequency, SF and BW to hear each other.

**Notifications.** CardSat listens for messages continuously in the background (the
receiver is left on whenever LoRa is enabled), so a message can arrive while you're
on any screen. When one does, an **envelope badge with an unread count** appears in
the header (top, just left of the battery) on every screen, and — depending on the
**Msg notify** setting (Settings → Network/data) — a brief banner shows the
sender's callsign, optionally with a short beep. Opening **Messages** clears the
badge. **Msg notify** options: *off* (silent badge only), *banner* (the default,
a cross-screen banner on arrival), or *banner+beep* (adds an audible chirp — handy
when the screen is parked, but off by default so it can't surprise you mid-pass).
In the Charge / Sleep screen the badge updates silently and no banner or beep
fires, keeping that mode minimal.

**The build.** LoRa is a **standard, always-built feature** — official binaries ship
with it compiled in (`CARDSAT_HAS_LORA` defaults to `1`), so **RadioLib is a required
build dependency**: install it via the Arduino Library Manager (search "RadioLib" by
Jan Gromes) before compiling. The `#if CARDSAT_HAS_LORA` guards remain in the source
only so the tree still compiles if you deliberately override the define to `0` in your
own build flags; you should not need to.

> **If the single-file `CardSat.ino` fails to link** showing `dangerous relocation:
> l32r: literal target out of range`, that is an Xtensa toolchain limit: the very large
> single translation unit pushes RadioLib's code out of the reach of the `l32r`
> instruction's literal pool. CardSat mitigates this by constructing the SX1262 on the
> heap (a pointer, not a static global) so its destructor/vtable stay out of the
> sketch's static-init code. If the single-file build still fails to link, **build from
> the modular `src/` folder instead** (use the PlatformIO build): there `lora.cpp` is
> its own small translation unit and the literal-range problem does not arise.

### Update

- `k` or **ENTER** — update GP data from your configured source and sync the clock
  (NTP). The same action also refreshes the AMSAT OSCAR **activity marks** shown on
  the Satellites list, the **space-weather** data (solar flux + Kp), **and** the
  terrestrial **weather** for your site, so one press brings everything current. The
  Update screen notes this so it's clear `k` does more than GP.
- `f` — **fast update**: refresh the orbital elements (GP), the AMSAT activity
  marks (a single bulk fetch, so it's included), and the transponder data for your
  **favorites only** — skipping the space-weather and terrestrial-weather fetches
  that `k` also pulls. This is the quick way to bring your regularly-worked birds
  current without the longer full refresh — handy in the field. (If you haven't
  marked any favorites, it refreshes the currently active satellite instead.)
- `a` — fetch and cache **all** transponders for offline use. This runs in small
  batches across **automatic reboots**: CardSat caches a handful of satellites,
  reboots to get a fresh network connection, and continues where it left off,
  repeating until every satellite is done. Each reboot returns to the **Update**
  screen showing the running count (e.g. "Caching 24/90"), and the run finishes
  on "Cached all 90 transponders". The whole pass takes a few minutes and several
  reboots — this is normal and lets the unit cache the full catalog reliably even
  on a weak Wi-Fi link. A run resumes automatically if interrupted (e.g. by a
  power cycle); to cancel a pending run, delete `/CardSat/tx_resume.txt` from the
  card. Satellites with no transmitters in the SatNOGS database are cached as an
  empty list, which is expected.
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
| CI-V wiring | `,`/`/` cycle **TX/RX (G2/G1)** / **1-pin G2** / **1-pin G1** — Icom wired CI-V only; single-pin is **confirmed on an IC-821** but still needs correct 5 V/3.3 V level interfacing, and TX/RX is the simplest path (see docs/interfaces/CIV_SINGLE_PIN.md) |
| CAT baud | `,`/`/` cycle 1200…115200 (incl. 57600) — applies to all radio protocols |
| Min pass el | `,`/`/` 0–30° |
| Decay solar | `,`/`/` cycle assumed solar activity **mean → min → max → auto** for the orbital-analysis decay estimate (changes the headline number and the bracket). **auto** uses the live F10.7 flux fetched with GP data |
| Weather units | `,`/`/` cycle the units for the **Weather** screen: **°F, mph → °C, km/h → °C, m/s**. Under *Station / display*. |
| WiFi SSID | ENTER → edit · **`s`** → scan for networks and pick one |
| WiFi pass | ENTER → edit |
| WiFi 2 SSID | ENTER → edit an **optional second network** tried if the first fails (field use: a second router, or a phone hotspot). Leave blank to disable |
| WiFi 2 pass | ENTER → edit |
| Save & test WiFi | ENTER → connect and report OK/FAIL (tries the primary network, then the second if set) |
| AOS alarm | `,`/`/` or ENTER toggle on/off |
| IR pass beacon | `,`/`/` or ENTER toggle on/off — also flash the built-in IR LED on each pass alert, with a distinct flash count per event (see [§12](#12-aos-alarm-and-deep-sleep)). SD not required; off by default |
| Rotator (+ type / host / port / baud / ranges / offsets / deadband / park / pre-point) | `,`/`/` adjust, ENTER edits host/port. **Rot type** cycles **GS-232 → rotctl (net) → PstRotator → Yaesu (direct) → Easycomm I → Easycomm II → Easycomm III → SPID Rot2Prog**; see [§17](#17-antenna-rotator-gs-232-rotctl-pstrotator-yaesu-direct-rotctld-server) |
| GP source | ENTER → **source picker**: AMSAT (default), any CelesTrak JSON-PP category (Amateur Radio listed first), or **Custom URL…** — see [§14](#14-gp-age-and-accuracy) |
| VFO Type | `,`/`/` or ENTER toggle *Main Up/Sub Dn* ↔ *Main Dn/Sub Up* |
| Sat mode | `,`/`/` or ENTER toggle the rig's satellite mode on/off |
| CAT rate | `,`/`/` adjust the CAT update period in 10 ms steps (default 500 ms; soft-floored to what the CAT baud can service) |
| CAT delay | `,`/`/` adjust the pause after each command, 0–200 ms in 2 ms steps (default 70 ms; CI-V/Icom only) |
| Dopp FM band | `,`/`/` the FM-leg write deadband, 0–2000 Hz in 25 Hz steps (default 300 Hz). CardSat only re-sends an FM frequency once Doppler has moved it more than this — FM's wide passband absorbs the rest, so a loose value avoids needless CI-V chatter |
| Dopp linear band | `,`/`/` the SSB/CW-leg write deadband, 0–1000 Hz in 10 Hz steps (default 50 Hz). Tighter than FM because linear modes need close tracking; near closest approach CardSat tightens this automatically |
| Dopp lead | `,`/`/` the predictive-lead cap, 0–100 ms in 5 ms steps (default 50 ms; `0` = off). On fast overhead passes CardSat can compute Doppler slightly ahead to mask CAT latency, tapering the lead to zero near closest approach. Raise it if your rig's CI-V is slow; set `0` to disable |
| Screen sleep | `,`/`/` cycle off / 30 s / 1 min / 2 min / 5 min — blanks the backlight after that idle time |
| Brightness | `,`/`/` adjust the active screen brightness in ~6% steps; previews live. Under *Station / display* |
| Tilt tuning | `,`/`/` or ENTER toggle **accelerometer passband tuning** on/off. Shown as **n/a (no IMU)** on boards without one (only the Cardputer **ADV** has the sensor). When on, roll the device left/right in TUNE mode on a linear bird to move through the passband. Under *Station / display* |
| My callsign | ENTER → enter your station callsign (stored uppercase); used in the log and ADIF `STATION_CALLSIGN` |
| LoTW DXCC / CQ zone / ITU zone / primary / secondary / IOTA | A chained set of pickers for the **LoTW upload** station location. **DXCC** opens a full entity list (subdivision-bearing entities grouped at the top, marked `>`; type to filter). **CQ/ITU zone** are typed. **Primary** (labelled state/province/oblast/prefecture/kunta per your DXCC) opens a gated picker — required for US/AK/HI, **(n/a)** for entities without one. **Secondary** is county (US) or city/gun/ku (Japan), gated by the primary; **(n/a)** elsewhere. **IOTA** (e.g. `NA-005`) is optional for any entity. Inside a picker: `;`/`.` move, ENTER selects, typing filters, `` ` `` clears/back. Under *Station / display*. See [§8 → LoTW upload](#logbook-of-the-world-lotw-direct-upload). |
| Cloudlog URL / key / station ID | ENTER → enter your self-hosted **Cloudlog** (or Wavelog) base URL (`https://…` or `http://…`), a **read-write API key**, and the numeric **station profile id** to file QSOs under, for **Log → Upload to Cloudlog**. Under *Station / display*. See [§8 → Cloudlog upload](#cloudlog--wavelog-upload). |
| QRZ user / QRZ pass | ENTER → enter your QRZ.com username / password for the **QRZ Lookup** screen (requires a QRZ XML-data subscription). Password shown masked. Under *Network / data*. |
| Backup config+favs → SD | ENTER → copy config + favorites to `config.bak` / `favs.bak` |
| Restore config+favs | ENTER → restore them from the backup files |
| **Reset all data** | ENTER → type **ERASE** to wipe everything (red row) |
| CAT type (+ host / port / user / pass) | `,`/`/` cycle **Wired CI-V** → **Icom LAN** → **rigctl (net)**. Icom LAN drives the **IC-9700** over RS-BA1 (see [§3](#3-connecting-your-radio)); **rigctl** drives a radio attached to a remote Hamlib **rigctld** server over TCP (set host/port to your rigctld; Hamlib's conventional port is 4532). Under *Radio / CAT*. |
| Rigctld server (+ port) | `,`/`/` enable a **rigctld server** so a PC (Gpredict, WSJT-X, a logger) drives the radio through CardSat over TCP (default 4532); VFOA=downlink, VFOB=uplink. Under *Radio / CAT*. |
| Rotctld server (+ port) | `,`/`/` enable a **rotctld server** so a PC (Gpredict, …) drives **whichever rotator CardSat is configured for** through CardSat over TCP (default 4533). Under *Rotator*. |
| Web control (+ port) | `,`/`/` or ENTER enable the **mobile web control page** served over WiFi (default port 80). When on and connected, the row shows the device's IP address to browse to (e.g. `192.168.1.42` — open it as `http://192.168.1.42`). Under *Network / data*. Plain HTTP on the LAN, **no authentication** — see [§18](#18-mobile-web-control). |
| Rotator: manual control | ENTER opens a screen to jog az/el by hand with live read-back. For a **Yaesu (direct)** rotator this is also where you **calibrate the ADC**: it shows live ADC counts and you capture the axis endpoints with `1`/`2`/`3`/`4` (az 0 / az full / el 0 / el full) — see [§17](#17-antenna-rotator-gs-232-rotctl-pstrotator-yaesu-direct-rotctld-server) and [ROTOR_INTERFACE.md](docs/interfaces/ROTOR_INTERFACE.md). Under *Rotator*. |

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

> **Only have a TLE? Convert it with `tools/tle2gp.py`.** Manual entry asks for GP
> (OMM) mean elements, but some objects are still published only as a classic
> **two-line element set (TLE)**. The repo includes a small, dependency-free
> Python helper that decodes a TLE into exactly the fields above — handling the
> conversions that are easy to get wrong by hand: the TLE epoch (`YYDDD.dddddddd`
> → ISO date/time), the implied-decimal eccentricity, BSTAR's exponent notation,
> and the derivative scaling (TLE stores *n*-dot/2 and *n*-ddot/6; GP reports the
> full values). Run it on a file of one or more TLEs, or paste a 2–3 line set on
> standard input:
>
> ```
> python3 tools/tle2gp.py mysat.txt        # file with one or more TLEs
> python3 tools/tle2gp.py                   # then paste 2–3 lines, Ctrl-D
> python3 tools/tle2gp.py --json mysat.txt  # AMSAT-style GP/OMM JSON instead
> ```
>
> The default output lists each element with its label and units, ready to type
> straight into the `n` prompts in order (epoch, inclination, RAAN, eccentricity,
> argument of perigee, mean anomaly, mean motion, BSTAR). The `--json` form emits
> an AMSAT-style GP/OMM record, handy if you'd rather host the set at a **Custom
> URL** GP source (see [§7](#7-first-time-setup)) than type it in. Either way the
> numbers are identical to what CardSat would have downloaded — the script only
> *re-packages* the same SGP4 mean elements a TLE already contains, so accuracy
> still decays with the element set's age and you'll want a fresh TLE periodically.

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

### Tuning with the radio's knob

When you turn the radio's knob in FULL or downlink-only mode, CardSat distinguishes a
deliberate dial move from the rig's own tuning-step rounding and read-back jitter using
a **mode-aware threshold** (about 30 Hz on SSB/CW, 250 Hz on FM, never below the rig's
tuning step). The moment it detects a real move it adopts your new spot and then **holds
off its own Doppler writes to the downlink for a short grace window (~400 ms)** so it
never tugs against the knob while you're turning — it resumes downlink correction once
you let go. The uplink isn't connected to your knob, so it **keeps following** your new
passband point immediately and tracks the move without lag. If tracking ever feels
slightly sticky or slightly loose for your operating style, the `KNOB_MOVE_SSB_HZ`,
`KNOB_MOVE_FM_HZ`, and `TUNE_GRACE_MS` constants in the firmware are what to adjust.

### Sidebands

For linear transponders CardSat sets the rig's modes for you: **USB on the
downlink, LSB on the uplink** (because an inverting linear transponder flips the
spectrum, so an LSB uplink is heard as a normal USB downlink). The exception is
any bird with an uplink or downlink **below 30 MHz (HF)**, which uses **USB up and
USB down**. FM and single-channel birds use the transponder's own mode on both
legs.

**Working CW through a linear bird.** Press **`k`** on Track to toggle **CW mode on
both legs** of a linear transponder, so you can work CW through the passband instead of
SSB. CardSat sets CW on the uplink and downlink; on an inverting transponder the
sideband still flips but CW is CW on both ends, so you simply zero-beat your downlink.
The choice is **per channel** — it resets when you change satellite or transponder — and
a `CW` tag appears on the Track and large-font readouts while it's active. On an FM bird
the key does nothing ("CW: linear birds only").

> **Frequency representation and display.** CardSat stores every frequency as a
> 32-bit count of hertz, so the highest frequency it can represent is about
> **4294 MHz** — comfortably above the amateur-satellite bands in use (2 m, 70 cm,
> 23 cm, 13 cm). Frequencies above that can't be entered or tracked. On screen,
> readouts **shed decimal places as the integer part grows** so they always fit the
> panel: sub-GHz birds keep five decimals (e.g. `145.99000`), while higher bands
> show fewer (`1296.500`, and so on). The large-font views trim one more decimal
> than the normal views to keep the big digits inside the screen. This only affects
> the *displayed* precision — the underlying tuning is always full-resolution.

### Tilt tuning (accelerometer, opt-in, ADV only)

A third way to move through a linear transponder's passband, alongside the device keys
and the radio knob. The Cardputer **ADV** has a motion sensor (the original Cardputer
does not). When **Tilt tuning** is switched on under *Settings → Station / display*, you
can roll the device left and right to move through the passband instead of (or alongside)
the `,`/`/` keys. It's deliberately a **rate** control, not an absolute one: a gentle
tilt nudges slowly for fine work, a firmer tilt slews faster, and holding the device
level holds the frequency. There's a dead-zone of a few degrees around level so a
hand-held device doesn't drift, and the rate saturates past roughly 35°.

It only acts on the **Track**, **large-font**, and **Manual** screens, in **TUNE**
mode, on a **linear** bird — everywhere else it does nothing. A **TLT** (Track /
Manual) or **TILT** (large-font) marker shows when it's armed. On a board without
the sensor the setting reads **n/a (no IMU)** and can't be turned on. Tilt tuning
is off by default; many operators will prefer the keys, since tilting the device
also moves your antenna and your eyes — it's offered as an option, not the default.
Once the board has the sensor you can flip it on and off mid-pass with **`y`** on
the Track or large-font screen, without opening Settings; the change is saved
either way.

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

### Editing calibrations on the SD card

Per-satellite calibrations are stored as a plain-text file, so you can author or
bulk-edit them on a computer instead of nudging each bird by hand. With the
microSD removed (or over USB mass storage), open **`/CardSat/calib.txt`**. Each
line is one satellite:

```
# norad  downlink_Hz  uplink_Hz      (lines starting with # or ; are comments)
43017   -250   300
25544    120     0
```

- The first field is the **NORAD catalog number**; the next two are the
  **downlink** and **uplink** offsets in **Hz** (signed — negative lowers the
  frequency). These are the same `calDl`/`calUl` values you trim in **CAL** mode.
- Whitespace-separated, one satellite per line. Blank lines and lines beginning
  with `#` or `;` are ignored, so you can annotate the file freely.
- The file is read each time you select or track a satellite, so edits take
  effect the next time you open that bird — **no reflash needed**. Saving a
  calibration on the device (CAL → **ENTER**) rewrites this file but preserves
  your comment lines.

CTCSS tone overrides work the same way in **`/CardSat/tones.txt`**, one line per
satellite as `norad tone_tenths` (tenths of a Hz, so `670` = 67.0 Hz; `0` forces
the tone off):

```
# norad  tone_tenths
25544   670
```

Both files live on the microSD card if one is present, otherwise in the device's
internal flash. If a satellite has no line in `calib.txt`, the global calibration
from **Settings** is used.

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

**IR pass beacon** (toggle in Settings → Station → *IR pass beacon*, off by
default): when enabled, every pass-alert event *also* emits a burst of flashes from
the Cardputer's **built-in IR LED** (GPIO 44), with a **distinct flash count per
event** so external hardware can tell them apart. The flashes accompany the existing
beeps — they don't replace them — and are gated by the same AOS-alarm machinery, so
they only fire when the AOS alarm is on. Each flash is a ~60 ms burst of standard
**38 kHz IR carrier** (with ~140 ms gaps), the kind any common IR receiver/
demodulator detects:

| Event | Flashes |
|---|---|
| T-60 s to AOS | 1 |
| T-30 s to AOS | 2 |
| T-10 s to AOS | 3 |
| AOS (pass start) | 4 |
| TCA (peak elevation) | 5 |
| LOS (pass end) | 6 |

CardSat only **transmits** these counts — what you do with them is up to you. Point
a 38 kHz IR receiver module (e.g. a TSOP38238 or a Vishay TSOP4838) at the
Cardputer, count the pulses in each burst, and trigger whatever you like: key a
relay to power up a rotator or preamp at T-10, flash a shack light at AOS, start an
SDR recording, drive a bigger external alert, or log events on a second
microcontroller. The flashing is fully non-blocking — it runs one burst at a time
between the normal tracking updates, so radio, rotator, and web control keep running
throughout. Build whatever receiver and logic you want around the counts above.

> **The IR pass beacon is host-verified only.** The carrier timing and flash-count
> logic were checked off-device, but the actual IR output and a receiver decoding it
> have not been confirmed on hardware. The 38 kHz carrier, duty cycle, and burst/gap
> timing may need tuning for your particular receiver; treat the counts as the stable
> contract and the exact waveform as adjustable.

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

#### Sky sources (`s`)

Press **`s`** on the Sun/Moon screen for a secondary **sky-sources** plot: the
classical planets and the strongest cosmic radio sources, on the same sky dome
(zenith centre, North up, elevation = radius). It's an antenna-pointing aid and a
reference for the brightest RF sources crossing the sky — handy for a Sun-noise-style
gain check against a strong source, or simply to know what's overhead.

**Radio sources** are drawn as orange crosses and **planets** as cyan dots; an
object below the horizon sits just outside the rim in grey so its bearing is still
readable. `;`/`.` step through the objects (a green ring marks the selection); the
right-hand panel shows the selected object's azimuth, elevation, above/below-horizon
status, and whether it's a radio source or a planet. `` ` `` returns to Sun/Moon.

The catalogue covers the planets **Mercury, Venus, Mars, Jupiter, Saturn**
(computed live) and the fixed radio sources **Cassiopeia A** (the brightest
galactic source), **Cygnus A**, the **galactic centre (Sgr A\*)**, the **Crab
nebula (Tau A)**, and **Virgo A (M87)**, plus a few bright stars (Polaris, Vega,
Antares) for orientation. Positions use the same low-precision ephemeris as the
Moon — far finer than any amateur beamwidth.

#### Sun / Moon transits (`t`)

Press **`t`** on the Sun/Moon screen to open the **transit finder** for the active
satellite. It scans the next **48 hours** for times the satellite passes in front of
(or close to) the **Sun or Moon** as seen from your location — the dramatic
astrophotography event where the ISS is silhouetted on the solar or lunar disc. The
scan runs incrementally with a progress bar (it never blocks the device), then lists
each close approach: the body (Sun/Moon), a countdown to the event, the minimum
angular separation, the body's elevation, and whether it's a true **TRANSIT** (the
satellite crosses the disc) or a near **conjunction**. Press `r` to rescan, `;`/`.`
to move the selection.

Because the ground path of a transit is only a few kilometres wide, this is a
**point prediction for your exact location** — moving a few km changes it, and fresh
elements matter for the centre-line. **Never observe a solar transit without proper
solar filtering on your eyes and optics.**

### Space weather

**Space Wx** on the main menu summarises the indices that matter most for
propagation: the **solar 10.7 cm radio flux** (F10.7, a proxy for solar activity
and ionospheric ionisation), the **planetary Kp index** (geomagnetic disturbance,
0–9), and the **running A index** (the daily-equivalent geomagnetic amplitude,
shown when the feed provides it). Each value is
labelled in plain terms — flux low/moderate/good/very-high, Kp
quiet/unsettled/minor-storm/moderate-storm/major-storm, and A
quiet/unsettled/active/storm — and colour-coded, with a short **operating outlook**
line translating the numbers into what to expect on HF and satellite paths, plus a
note of how old the data is.

Opening **Space Wx** shows the last cached values immediately, then — if WiFi is up —
fetches an update in the background, showing an **"Updating Space Wx"** banner on the
bottom status bar and then a brief result (**Space Wx updated/unchanged**, **update
failed**, or **WiFi failed**) before the banner clears. The indices are also
refreshed whenever you run **Update**, and **`r`** forces a refresh on demand. The Kp and
A fetch is independent of the flux fetch, so a hiccup in one never suppresses the other.

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
(your GPS fix or manually set lat/lon), so set your location first. Opening **Weather**
shows the cached forecast immediately, then — if WiFi is up — fetches an update in the
background with an **"Updating Weather"** banner on the bottom status bar, followed by a
brief result (**Weather updated**, **update failed**, **WiFi failed**, or **Set a
location first**) before the banner clears. It also refreshes when you run **Update**, and
**`r`** forces a refresh. Like Space Wx, the last result is cached to flash, so it remains
viewable offline with a note of its age.

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

### Upcoming activations (Activations)

**Activations** on the main menu shows the **upcoming satellite activations** that
operators have scheduled on **[hams.at](https://hams.at)** — roves, grid
activations, and special operations announced ahead of time. It's a quick way to see
who's planning to be on which bird, from where, and when, so you can be listening for
a rare grid or a wanted station.

Opening **Activations** shows the last-known list immediately from the on-device cache —
even with no WiFi — then, if WiFi is up, fetches an update in the background with an
**"Updating Activations"** banner on the bottom status bar, followed by a brief result
(**Activations updated/unchanged**, **update failed**, or **WiFi failed**) before
the banner clears. Each row shows the **date, callsign, satellite and grid**. Move with
`;`/`.`, press **ENTER** on a row for the full detail — **start and end times (UTC), mode,
frequency, and the activator's comment** — and `` ` `` to go back to the list. Press
**`r`** at any time to refresh.

The feed is fetched over HTTPS from `https://hams.at/feeds/upcoming_alerts`, parsed (up to
30 activations), and **cached to flash** so the last-known roster survives a reboot and
shows instantly offline. The **Update** screen's `k` also refreshes it alongside the GP
update. Times are shown exactly as the feed provides them, in UTC. Some activations list
"(none)" for frequency or elevation — those simply weren't specified by the operator, and
the activator's comment often fills in the detail.

---

## 14. GP age and accuracy

### Theory — what GPs and SGP4 actually are

**Why a satellite needs "elements" at all.** You cannot just store a satellite's
position and velocity and expect it to stay useful — it sweeps through space at
~7.5 km/s and the orbit itself slowly changes shape. Instead, the satellite is
described by a compact set of numbers that, fed to a matching math model, *regenerate*
the position at any time. Those numbers are the **orbital elements**; the model is
**SGP4**. The two are a matched pair: the elements only mean what they mean *because*
SGP4 is the model that will consume them.

**TLE, OMM and "GP".** The historical packaging is the **Two-Line Element set (TLE)** —
two 69-character lines, a fixed-column format dating to punch cards, holding the
elements plus epoch, drag term, and catalog bookkeeping. **OMM** (Orbit Mean-elements
Message) is the modern, self-describing replacement carrying the *same* numbers as
JSON, XML or KVN instead of fixed columns. CelesTrak's umbrella term for "current
mean elements in whatever format" is **GP** (General Perturbations). CardSat downloads
**GP data as JSON/OMM** (it never has to parse the brittle column format off the wire),
but internally it renders each set back into a TLE line-pair — epoch encoded as
`YYDDD.dddddddd`, B\* in TLE exponent notation, with the line checksums — and hands
that to the SGP4 library, which still ingests elements the classic way. The element
values are identical; only the wrapper changes. (Because they're the same numbers,
a TLE can be mechanically decoded into the GP fields CardSat's manual entry asks
for — that's what the bundled **`tools/tle2gp.py`** helper does; see [§8](#8-screen-map-and-navigation).)

**What SGP4 is.** SGP4 (Simplified General Perturbations 4) is the analytic propagator
that NORAD/USSPACECOM elements are *fit to*. "Analytic" means it isn't numerically
integrating forces step by step; it applies closed-form expressions for the
perturbations that matter for a near-Earth satellite over days-to-weeks:

- **Earth's oblateness** — the equatorial bulge (the **J2** zonal harmonic, with smaller
  J3/J4 terms) that precesses the orbit plane and rotates perigee. This is the
  dominant non-Keplerian effect and the reason pass times march earlier each day.
- **Atmospheric drag** — modelled through the **B\*** ("B-star") term, a fitted
  ballistic-and-density coefficient that slowly shrinks the orbit. (A companion
  model, **SDP4**, extends the same scheme to deep-space orbits with period
  > 225 min by adding lunar/solar gravity and resonance terms — the pair is often
  written "SGP4/SDP4." In practice CardSat's birds are all near-Earth, so the SGP4
  branch is what runs.)

Crucially, the **"mean" elements are not osculating elements.** They are deliberately
*detuned* values chosen so that, after SGP4 adds its periodic terms back in, the output
matches reality. You cannot mix them with a different model, average them, or read the
mean motion as an instantaneous rev-rate and expect physical truth — they are
model-specific fitting constants. This is also why CardSat propagates with the **WGS72**
gravity constant set: that is the set the elements were fit against, and using WGS84
would introduce a small systematic error.

**From element to az/el — the frames.** Propagating gives position and velocity in the
**TEME** frame (True Equator, Mean Equinox — the quirky inertial-ish frame SGP4 outputs).
To point an antenna, CardSat rotates the observer's geodetic latitude/longitude/altitude
into the same TEME frame using **GMST** (Greenwich Mean Sidereal Time — Earth's rotation
angle for the instant), differences the two position vectors to get the slant vector,
and resolves that into topocentric **azimuth/elevation**. Range-rate (for Doppler) is
taken directly from the projection of the relative *velocity* onto the slant direction,
evaluated at the exact fractional second rather than by differencing whole-second range
samples — cleaner right at closest approach where range-rate changes fastest.

**Where the error comes from.** SGP4 near a fresh epoch is good to roughly a kilometre.
Error grows with **element age** because the small unmodelled accelerations (drag
fluctuations from space weather, higher-order gravity, solar radiation pressure)
integrate over time, and drag is the worst offender — it depends on upper-atmosphere
density, which swings with solar activity and isn't known in advance. So a low,
draggy orbit (ISS, cubesats) can develop noticeable along-track timing error (the
satellite arriving early or late along an otherwise-correct path) within a week or
two, while a high, stable orbit stays usable far longer. Along-track error shows up to
you as **pass-time slip**; cross-track error shows up as **pointing/Doppler error**.
The practical defence is simply to refresh elements often enough for the orbit you
care about.

### GP age indicator

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
   **all** transponders). Both are written to flash. The transponder cache runs
   in batches across several automatic reboots and finishes on "Cached all N
   transponders" — let it run to completion before going offline.
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
automatically: **Icom 8N1, Yaesu 8N2, Kenwood 8N1 — except 8N2 at 4800 baud.**
The TS-790 generation (IF-232C interface: TS-450/690/790/850/950) requires **two
stop bits at 4800 baud** (one stop bit at higher rates such as the TS-2000's
57600); CardSat selects this by baud automatically. Note the TS-790's CAT is via
the **optional IF-232C** adapter — its operating manual documents the interface
but not the command set, so the TS-790 mapping leans on the shared Kenwood
protocol and is the least bench-verified of the rigs here.

The CAT *interface circuit* differs by family. **Icom CI-V** uses a 3.3 V-safe
single-wire interface (**[CIV_INTERFACE.md](docs/interfaces/CIV_INTERFACE.md)**); you
can wire it as **separate TX/RX** (G2/G1, the default) or **single-pin** (one shared
open-drain wire, confirmed on an IC-821) — set this with **CI-V wiring** in Settings
and see [§3 → Icom CI-V](#3-connecting-your-radio) and
**[CIV_SINGLE_PIN.md](docs/interfaces/CIV_SINGLE_PIN.md)**. **Yaesu** and
**Kenwood** use RS-232-level serial — build a **MAX3232** level shifter as in
**[RS232_INTERFACE.md](docs/interfaces/RS232_INTERFACE.md)** (⚠️ untested; build at your own risk).

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
mode **off** at startup on the rigs that expose it over CAT (IC-910, IC-9100,
IC-9700). Downlink on SUB, uplink on MAIN. Read-back uses `0x03`; it is confirmed
working on hardware for the **IC-821** and supported in firmware for the
IC-820/910/970/9100/9700. Wrong-VFO fixes live in `radio_profiles.h`.

> **IC-820/821/970 satellite mode and band pairing are MANUAL.** Unlike the
> IC-910/9100/9700, these three rigs have **no working CI-V command** to toggle
> satellite mode (the IC-821's sat-mode command is a no-op on real hardware —
> confirmed on an IC-821) or to assign which band sits on MAIN vs SUB. Their
> `0x07 D0`/`D1` bytes are band *access* (which band a read/write targets), not a
> MAIN/SUB assignment. So on these radios you must **engage satellite/full-duplex
> mode, set up the band pair, and set any uplink PL tone on the radio's front
> panel**; CardSat then only Doppler-tunes within that layout. See
> **[RADIO_SETTINGS.md](docs/interfaces/RADIO_SETTINGS.md)** for the full per-radio CAT-vs-manual
> breakdown. This matches how Hamlib, OscarWatch, SatPC32, and Gpredict treat the
> same radios.
>
> By contrast, the **IC-910, IC-9100, and IC-9700 _can_ set up MAIN/SUB over CAT**,
> and **CardSat now orients MAIN/SUB automatically** on all three when you turn radio
> control on for a two-way transponder (once at engage, never per tick), so you don't
> pre-arrange the bands:
> - **IC-9100/9700** issue `0x07 D2 00/01 <bandcode>` to set MAIN/SUB band selection
>   directly.
> - **IC-910** reads MAIN (`0x07 D1` then `0x03`) and, if it's the wrong band, issues
>   one swap (`0x07 D0`) — the 910's MAIN/SUB can't share a band, so one check fixes
>   both. This also **fixes a latent bug**: the 910's MAIN-select byte was previously
>   `0x07 D0` (the *swap* command); it is corrected to `0x07 D1`.
>
> ⚠️ **Host-verified only — untested on real 910/9100/9700** (the author runs an
> IC-821, which has none of these commands); a leg outside 2 m/70 cm/23 cm is skipped
> and set manually. The 820/821/970 remain fully manual for band setup.

> **IC-910 satellite-mode & tone commands differ from the IC-9100/9700.** The
> IC-9100/9700 toggle satellite mode with CI-V `0x16 0x5A`, but the **IC-910 uses a
> different command group entirely: `0x1A 0x07`** (verified from the IC-910 CONTROL
> COMMAND table — on the IC-910, command `0x16` has no satellite-mode sub-command).
> Likewise the CTCSS tone encoder is `0x16 0x42` (Repeater tone) on the
> IC-9100/9700 but **`0x16 0x43` (Subaudible tone) on the IC-910** — its `0x42` is
> the auto-notch filter. CardSat sends the right command for each rig. (Earlier
> firmware sent `0x16 0x07`/`0x16 0x42` to the IC-910, so sat mode never engaged
> and the tone key toggled the notch; both are fixed.)

> **IC-820H vs IC-821H MAIN/SUB select.** These two rigs use the *same* CI-V
> command (`07`) for band select but with the **two sub-commands swapped** — a
> quirk confirmed from each radio's own manual (CI-V command table):
>
> | Radio | Address | Main band access | Sub band access |
> |-------|---------|------------------|-----------------|
> | IC-821H | `4C` | `0x07 D0` | `0x07 D1` |
> | IC-820H | `42` | `0x07 D1` | `0x07 D0` |
>
> CardSat ships the correct (reversed) mapping for each, so both tune the right VFO
> out of the box. If you ever swap one rig's CI-V address onto the other's profile,
> remember the band-select bytes differ too. The frame is otherwise identical:
> preamble `FE FE`, controller `E0`, command `07`, end `FD`.

**Icom over the network.** The **IC-9700** can be driven over WiFi/Ethernet instead
of the CI-V bus — set **CAT type → Icom LAN** in Settings (with the IC-9700 selected
as the radio model). CardSat carries the *same* CI-V frames shown below inside the
RS-BA1 UDP protocol, so MAIN/SUB, read-back, sat mode and CTCSS behave identically;
only the transport changes. Other networked Icoms (IC-705, IC-7610, IC-785x) use the
same RS-BA1 protocol so the link would establish, but they are single-receiver radios
without the MAIN/SUB satellite architecture and are not supported. Setup is in
[§3](#3-connecting-your-radio); the wire protocol is documented in
`docs/interfaces/ICOM_LAN_PROTOCOL.md`.

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

### CAT self-test

**Settings → Radio / CAT → Run CAT self-test** exercises the whole CAT link in one
shot, on the device, with no satellite pass needed. It needs the radio turned on first
(start CAT output with `r` on Track). The test sets and reads back the **downlink** and
**uplink** frequencies, sets the **downlink/uplink modes**, sends a **band select**,
toggles **sat mode**, and probes **PTT read** and **CTCSS** — reporting each step as
`[PASS]`, `[FAIL]`, or `[INFO]` (the latter for things a given radio doesn't support
over CAT, like sat-mode commands on the older rigs or PTT read where the protocol has
none). `;`/`.` scroll the results, `` ` `` exits. It's the quickest way to confirm a
new radio, address, baud, and wiring are all correct before a pass — a `[FAIL]` on the
set+read lines points straight at a CI-V address / baud / wiring problem, while
`[INFO] … not supported` lines are normal and just reflect that radio's capabilities.

---

## 17. Antenna rotator (GS-232, rotctl, PstRotator, Yaesu direct, rotctld server)

CardSat can both **drive** a rotator and **act as a rotator server** for other software.
The network *client* backend is labelled **rotctl** in Settings — it connects to a
rotctld server, it is not itself the daemon. Separately, CardSat can run its **own
rotctld server** (Settings → Rotator → *Rotctld server*, default port **4533**) so a
networked PC (Gpredict, …) drives whichever rotator CardSat is configured for; a `P`
from the PC disengages CardSat's own tracking. A **manual control** screen (Settings →
Rotator → *Rotator: manual control*) jogs az/el by hand with live read-back. With the
azimuth **el range** set to 180°, the *flip* decision is made once per pass and held to
LOS. The `rotctld` and `rigctld` servers have **no authentication** — run them on a
trusted LAN only.

CardSat can drive an az/el antenna rotator through one of several interchangeable
backends, chosen in Settings with **Rot type**:

- **GS-232** -- a directly-attached controller speaking the **Yaesu GS-232A/B**
  protocol (a Yaesu G-5500 with a GS-232B, a SPID controller in GS-232 mode, or any
  GS-232 emulator: K3NG / RadioArtisan / ERC). Because all three ESP32-S3 UARTs are
  already in use (USB, CAT, GPS), this link is created by an **I2C->UART bridge**
  (SC16IS750/752) on a second I2C bus, so it coexists with the radio and GPS.
- **Easycomm I / II / III** -- the open, plain-ASCII tracking protocol used by
  **SatNOGS, K3NG, ERC** and most homebrew rotator controllers (the same protocol
  Hamlib's `easycomm` backends speak). Choose the version your controller expects:
  **II** is the common decimal-degree form (`AZ123.4 EL045.0`), **I** is the older
  integer form, and **III** shares II's positioning grammar. Uses the same
  **I2C->UART bridge** as GS-232.
- **SPID Rot2Prog** -- the binary protocol of **SPID MD-01 / MD-02** (Alfa/
  RFHamDesign) controllers, as documented in Hamlib's `spid` (`rot2prog`) backend.
  Whole-degree positioning over the same **I2C->UART bridge**. (If your SPID box is
  set to GS-232 emulation, you can use **GS-232** instead.)
- **rotctl (net)** -- connects to a **Hamlib `rotctld` server** anywhere on the same WiFi
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
  **[ROTOR_INTERFACE.md](docs/interfaces/ROTOR_INTERFACE.md)** (⚠️ untested; build at your own risk).
  Calibrate from **Settings → Rotator → Rotator: manual control** (capture keys
  **1/2/3/4** at the axis endpoints).

> **The Easycomm and SPID backends are host-verified only.** The ASCII
> formatting/parsing and the SPID binary frame encode/decode were checked
> off-device, but neither has been exercised against a physical controller yet.
> Treat them as untested; verify carefully before trusting them with real hardware.

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
| Rot type | **GS-232** (I²C bridge), **Easycomm I/II/III** (I²C bridge), **SPID Rot2Prog** (I²C bridge), **rotctl (net)** (TCP), **PstRotator (net)** (UDP), or **Yaesu (direct)** (I²C ADC + output expander, no GS-232 box) |
| Net host | server IP / hostname (rotctl or PstRotator backend) |
| Net port | server port -- rotctld TCP **4533**, PstRotator UDP **12000** |
| Rot baud | GS-232 serial speed (1200 / 4800 / 9600); GS-232 backend only |
| Rot deadband | degrees; suppress moves smaller than this (anti-chatter) |
| Rot park az | azimuth the rotator parks at on LOS / when disabled |
| Rot pre-point | lead before AOS to pre-aim at the rise bearing (off / 30 s / 1-5 min) |
| Rot Az offset | added to commanded azimuth (mount alignment) |
| Rot El offset | added to commanded elevation |
| Rot az range | azimuth travel: **0..360** (default), **-180..+180** (centred on N), or **0..450** (90 deg overlap) |
| Rot az lookahead | `,`/`/` 0–10 s (default 3 s; `0` = off). On a **0..450** rotator only: CardSat predicts the bearing this many seconds ahead and, when a pass is about to cross north, commits early to the 361–450° overlap band so it makes a short move instead of unwinding ~360°. Has no effect on 0..360 or flipped passes. Tune to your rotator's slew speed |
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
**rotctl (net)**, and enter the server's address in **Net host** (an IP is
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
**rotctl (net)** and point **Net host** / **Net port** at the AntRunner's rotctld
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

## 18. Mobile web control

CardSat can serve a small **mobile-friendly web page over your WiFi network**, so a
phone, tablet, or laptop on the same network can drive it without touching the
keypad. It's **off by default**.

**Enable it:** *Settings → Network / data → Web control* (`,`/`/` or ENTER to turn
on; **Web port** sets the port, default 80). Once it's on and CardSat is connected
to WiFi, the **Web control** row shows the address to open — for example
`http://192.168.1.42`. Type that into any browser on the same network.

**What the page does:**

- **Satellite selection** — pick one of your favourites from the drop-down and tap
  **Track** to make it active. (If you haven't marked any favourites, the list
  falls back to the catalog.) A **filter box** narrows a long list as you type, and
  the **★** button beside the selector adds or removes the chosen satellite from
  your favourites, just like marking one on the device.
- **In-pass indicator & AOS countdown** — the header shows a live **"IN PASS — LOS
  in m:ss"** while the active satellite is up (with the peak elevation and time
  under the sky plot), and a **"Next AOS in m:ss"** countdown otherwise, so you
  know at a glance when to be ready. A small green **RAD** / **ROT** / **R+R** badge
  in the header lights up whenever the device's radio and/or rotator are tracking —
  mirroring the on-device header tag — so you can tell from your phone that the rig
  may be transmitting even after you've left the device's Track screen.
- **Live sky plot** — a polar plot (N up, elevation as distance from the rim) with
  a moving dot showing the satellite's current azimuth/elevation. While the
  satellite is **below the horizon**, the plot instead draws the **next pass as an
  arc** (its azimuth/elevation track), so you can see where it will rise and set;
  the live dot takes over the moment it comes up.
- **Adaptive refresh** — the page polls **faster during a pass** (sub-second, so the
  fast-moving Doppler frequencies stay current) and **slows down between passes** to
  save battery and network traffic, backing off automatically if the connection
  drops and showing **"reconnecting…"** until it recovers.
- **Live readout** — downlink (RX) and uplink (TX) frequencies, azimuth/elevation,
  and the current tune mode. Beside the RX label a small **±N.NN kHz** figure shows
  how much **Doppler shift** is being applied right now. **Tap either frequency to
  copy it** to the clipboard (handy for setting a second radio by hand).
- **Transponder selection** — a drop-down lists the active satellite's
  transponders by name and mode; choosing one selects it on the device (the same
  selection the `t` key cycles), so you can pick the FM channel or the linear
  passband from your phone.
- **Radio & rotator control** — buttons tune the passband down/up, step to the next
  transponder, cycle the Doppler tune mode, recenter the passband, toggle CAL, and
  switch **radio** and **rotator** output on/off. These do exactly what the
  corresponding keys do on the Track screen — the web page drives the same
  controls, it doesn't second-guess them. Rapid taps are debounced so a burst can't
  queue up stale commands.
- **Direct calibration entry** — type an exact **RX cal** or **TX cal** offset in
  Hz and tap **Set** to apply it (the value is saved per-satellite, the same as the
  on-device calibration); **Zero cal** clears both. For a linear bird the current
  passband position and bandwidth are shown. The displayed RX/TX dials are always
  computed live from the transponder plus Doppler plus this calibration, so the
  calibration is the operator-settable correction rather than a free-typed absolute
  frequency.
- **Manual (no-radio) tuning** — tap **Manual** to switch the control card to the
  hand-tuning calculator. It shows the **HOLD** leg (the frequency to park your own
  radio on) and the **TUNE** leg (the Doppler-corrected frequency to follow),
  exactly as the on-device Manual screen does, with the same round-trip Doppler
  correction on linear birds. **Swap leg** chooses whether you hold the downlink or
  the uplink; Tune ±, next transponder, CAL, and recenter work as on the device.
- **Orbital analysis** — tap **Orbit** for a read-only summary of the same numbers
  the on-device orbital-analysis pages compute: altitude and footprint, period,
  apogee/perigee, inclination/eccentricity, decay estimate, live range-rate and
  sub-point, sunlit/eclipse state, beta angle, J2 node and perigee drift (with a
  sun-synchronous flag), mean/true anomaly, time to perigee/apogee, and the
  multi-day pass outlook. It's view-only — nothing on the device changes.
- **Upcoming passes** — a table of the next passes (AOS, peak time, max elevation,
  duration, rise→set azimuth) with a **★** marking each **optically-visible** pass
  (sunlit satellite, dark sky). Above it, a compact **timeline bar** shows the same
  passes laid out in time — visible ones in orange, a white tick for "now" — so you
  can eyeball spacing at a glance. **Tap a pass** (row or bar) to arm a **browser
  AOS notification**: your phone alerts you when that pass begins (grant the
  notification permission when prompted).

The page is **responsive** — a single column on a phone, and a two-column layout on
a tablet or computer. It coexists with the rigctld/rotctld servers and the normal
on-device UI; the device keeps tracking and you can use its keypad at the same time.

> **Security.** This is plain **HTTP on the local network with no password** —
> anyone who can reach your WiFi can open the page and operate the radio and
> rotator. Use it only on networks you trust, and don't forward the port to the
> internet. Turning **Web control** off stops the server immediately. In the field,
> pairing this with a **phone hotspot** (see the second-WiFi network in
> [§7](#7-first-time-setup)) gives you a private network for phone-to-CardSat
> control with no other infrastructure.

### Network control roles: client vs server

Besides the web page, CardSat speaks the **Hamlib network protocols** — and it can sit
on *either* end of them. It helps to keep the two roles straight, because the names
differ by a single letter:

- **CardSat as the client** — *it* drives a remote device. **`rigctl (net)`** (a CAT
  type, [§3](#3-connecting-your-radio)) makes CardSat connect to someone else's
  **`rigctld`** server to control a radio; **`rotctl (net)`** (a rotator type,
  [§17](#17-antenna-rotator-gs-232-rotctl-pstrotator-yaesu-direct-rotctld-server)) makes
  it connect to a **`rotctld`** server to point an antenna. In both cases CardSat is the
  one issuing commands, and the daemon name ends in **-d**.
- **CardSat as the server** — *other software* drives **CardSat**. The **Rigctld
  server** (Settings → Radio / CAT) and **Rotctld server** (Settings → Rotator) make
  CardSat itself a **`rigctld`/`rotctld`** daemon, so a PC running Gpredict, WSJT-X, a
  logger, or plain `rigctl`/`rotctl` can drive whatever radio and rotator CardSat is
  connected to — CardSat does the Doppler and pointing, the PC just sends frequencies or
  bearings.

The rule of thumb: the **client** option in CardSat's menus is named for the *client
tool* (`rigctl` / `rotctl`), while the **server** option is named for the *daemon* it
becomes (`rigctld` / `rotctld`). Default ports follow Hamlib convention — **4532** for
rig, **4533** for rotator — and **none of these network services use authentication**,
so keep them on a trusted LAN.

---

## 19. Managing data and factory reset

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

## 20. Troubleshooting

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

**"Network problem" reboot prompt.** If downloads keep failing with refused
connections, CardSat first retries and hard-resets WiFi automatically (this clears a
wedged socket pool). If that still doesn't recover, it shows a **Network problem**
screen offering a reboot — press **ENTER**/`y` to reboot (the reliable cure once the
socket stack is stuck) or `` ` ``/`n` to keep running and try again later. CardSat
never reboots on its own; the choice is always yours.

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

## 21. Screen-by-screen reference

This section catalogs **every screen** in the firmware in a uniform format —
what it is for, how you get to it, what it shows, and what each key does. It
complements the task-oriented walkthroughs in [§8](#8-screen-map-and-navigation) (which
follow the natural operating flow); use this section when you want the complete
picture of one specific screen.

Throughout, the navigation keys are the printed arrow legends — `;` up, `.`
down, `,` left, `/` right — with **ENTER** to select and `` ` `` (or **DEL**) to
go back. `b` saves a screenshot on any screen. Only the *screen-specific* keys are
listed below.

### Home menu

![Home menu](docs/img/home.jpg)

- **Purpose** — the top-level launcher.
- **Reached from** — power-on lands here; `` ` `` from most top-level screens returns here.
- **Shows** — a scrolling list of the fourteen destinations: Satellites, Next
  Passes (all favs), Passes (sel), Track (sel), World Map, Sun / Moon, Space Wx,
  Weather, QRZ Lookup, Location, Update, Settings, Log, About. The header carries
  the clock and battery gauge.
- **Keys** — `;`/`.` move the highlight; **ENTER** opens the selected item.

### Satellites

![Satellites list](docs/img/satellites.jpg)

![Satellites — favorites filter](docs/img/satellites-favorites.jpg)

- **Purpose** — browse the catalog, choose favorites, and reach the per-satellite
  analysis tools.
- **Reached from** — Home → Satellites.
- **Shows** — the satellite list (up to 220 from GP data plus any manual sats). A
  right-edge mark shows AMSAT activity: a filled dot = heard, filled square =
  telemetry only, ring = not heard, nothing = no reports. Favorites are flagged.
- **Keys** — `;`/`.` move (`{`/`}` page); `f` toggle favorite; `v` show
  favorites only; `n` add a new satellite by hand; `x` delete a manual satellite;
  `o` open **Orbital analysis**; `s` open **Simulation**; `t` open the
  **Transponder database**; `e` open the **EQX table**; `k` open the
  **OSCARLOCATOR** view; `2` open the **sat-to-sat visibility finder**; `3` open
  the **3D Globe**; `d` open the **10-day pass overview**; `i` open the
  **Illumination** raster; **ENTER** opens **Passes** for the highlighted bird.

### Orbital analysis

![Orbital analysis — Info](docs/img/analysis-info.jpg)

![Orbital analysis — Live](docs/img/analysis-live.jpg)

![Orbital analysis — Next pass](docs/img/analysis-pass.jpg)

![Orbital analysis — Doppler](docs/img/analysis-doppler.jpg)

![Orbital analysis — Nodal](docs/img/analysis-nodal.jpg)

![Orbital analysis — Sun/Beta](docs/img/analysis-sunbeta.jpg)

![Orbital analysis — Pass outlook](docs/img/analysis-pass-outlook.jpg)

![Orbital analysis — Orbit position](docs/img/analysis-orbit-position.jpg)

- **Purpose** — a nine-page deep dive into one satellite's orbit: geometry,
  dynamics, lighting, and pass planning. Full theory and per-page detail are in
  [§8 → Orbital analysis](#orbital-analysis-o).
- **Reached from** — Satellites → `o`.
- **Shows** — one page at a time: Info, Live, Next pass, Ground track, Doppler,
  Nodal, Sun / Beta, Pass outlook, Orbit position.
- **Keys** — `,`/`/` flip pages; `r` recompute; on the **Doppler** page `f` sets
  the beacon frequency; `` ` `` leaves.

### Simulation

![Simulation](docs/img/simulation.jpg)

![Simulation — map view](docs/img/simulation-map.jpg)

- **Purpose** — a "time machine" that propagates the selected satellite to an
  arbitrary time so you can preview geometry past or future.
- **Reached from** — Satellites → `s`.
- **Shows** — the sub-point and footprint (and, in world-map view, the track) at
  the simulated instant, with the offset from now.
- **Keys** — `,`/`/` step the time backward/forward; `;`/`.` change the step
  size; `m` toggle the world-map view (sub-point + footprint); `x` reset to now;
  `` ` `` back.

### Transponder database

- **Purpose** — inspect every transponder/beacon entry CardSat holds for a
  satellite (from SatNOGS plus any you added).
- **Reached from** — Satellites → `t`.
- **Shows** — a scrollable list of entries with description, downlink (and mode),
  uplink, and tone/inverting flags. The currently selected entry is highlighted
  with a `>`; entries you added by hand are tagged with a `*`.
- **Keys** — `;`/`.` select an entry; `x` **delete** the selected entry, *only if
  it's a manual one* (`*`-tagged) — press `x` once to arm, `x` again to confirm.
  `` ` `` back. Deleting rewrites that satellite's `/CardSat/mtx_<norad>.json`
  file; SatNOGS-cached entries can't be deleted here (they'd return on the next
  Freq update). To **edit** a manual transponder, delete it and re-add it with `n`
  on the **Passes** screen.

### EQX table (OSCARLOCATOR)

- **Purpose** — a table of **equatorial crossing (EQX)** times and longitudes for
  the selected satellite, for use with a classic **OSCARLOCATOR** plotting board.
  Each EQX is an **ascending-node** crossing — the moment the satellite's
  ground track crosses the equator heading **north**. With the EQX time and
  longitude you rotate the Oscarlocator's track overlay to that longitude and read
  AOS/LOS and mutual visibility directly off the board, with no live computer at
  the operating position. A `d` keypress switches the table to the **descending
  node** (southbound equator crossing) if you'd rather reference that.
- **Reached from** — Satellites → `e`.
- **Shows** — a scrollable, day-grouped table covering the next **3 days**, each
  row an EQX **date**, **time in UTC**, and **sub-satellite longitude in
  West-positive** notation (`123.4 W`), matching the convention printed on
  Oscarlocator dials. Successive crossings step westward by roughly
  360°×(period/sidereal-day) per orbit (~28.7° for an AO-7-class orbit).
- **Computed on-device** from the satellite's current GP elements (SGP4) — no
  network needed — so the longitudes drift as the elements age; run **Update**
  every week or two to keep them fresh, the same as for tracking.
- **Keys** — `;`/`.` scroll a page at a time; `d` **toggle ascending ↔
  descending** node (recomputes the table — ascending/EQX is the default and the
  usual OSCARLOCATOR reference, descending gives the southbound equator crossing);
  `r` recompute (e.g. after the clock is set or new elements are loaded); `` ` ``
  back. Requires the **UTC clock** to be set (GPS or Location → `c`). The header
  shows **EQX** for ascending or **DEQX** for descending.

> The table mirrors the output of the **AO-7_OSCARLOCATOR** generator
> (github.com/prstoetzer/AO-7_OSCARLOCATOR), but works for any satellite in the
> catalog and runs entirely on the Cardputer.

### Next Passes (schedule)

![Next Passes schedule](docs/img/next-passes.jpg)

- **Purpose** — the unified upcoming-pass schedule across **all** your favorites,
  so you see what is next regardless of which bird it is.
- **Reached from** — Home → Next Passes (all favs).
- **Shows** — favorites' passes in time order (up to 24 favorites tracked), each
  with satellite, AOS time/countdown, max elevation and duration. Stale-element
  sats carry a red `!`.
- **Keys** — `;`/`.` select; **ENTER** opens **Pass detail**; `m` opens the live
  **World map**; `z` arms **deep sleep** until the next AOS; `` ` `` back.

### Passes

![Passes (per-satellite)](docs/img/passes.jpg)

- **Purpose** — the pass list for one selected satellite, and the jumping-off
  point to tracking, the visualizers, and the workable lists.
- **Reached from** — Satellites → ENTER, or Home → Passes (sel).
- **Shows** — up to twelve upcoming passes for the bird (AOS/LOS, max elevation,
  duration). A yellow `*` marks an **optically-visible** pass.
- **Keys** — `;`/`.` select; `d` **Pass detail**; `t` or **ENTER** open
  **Track**; `n` add a manual transponder; `r` recompute; `x` **Mutual windows**;
  `v` **10-day overview**; `V` **visible-pass list** (next 10 days); `i`
  **Illumination**; `g`/`w`/`e` workable grids / US states / DXCC **for this pass**;
  `` ` `` back (to Satellites, or Home if opened from the Home menu).

### Pass detail

![Pass detail (elevation)](docs/img/pass-detail.jpg)

- **Purpose** — the full numeric breakdown of one pass plus its polar plot.
- **Reached from** — Passes → `d`, or Next Passes → ENTER.
- **Shows** — AOS/TCA/LOS times and azimuths, max elevation, duration, and a polar
  (sky) plot of the arc.
- **Keys** — `p` toggle to the dedicated **Pass polar** view; `` ` `` back.

### Pass polar

![Pass polar plot](docs/img/pass-polar.jpg)

- **Purpose** — a full-screen polar (azimuth/elevation sky) plot of a single
  pass's arc.
- **Reached from** — Pass detail → `p`.
- **Shows** — the pass traced on a polar grid (zenith centre, horizon rim), with
  AOS/LOS marked.
- **Keys** — `p` toggle back to Pass detail; `` ` `` back.

### Mutual windows (co-visibility)

![Mutual windows](docs/img/mutual-windows.jpg)

- **Purpose** — find the times a satellite is simultaneously visible to you **and**
  to a second station — the windows in which a contact is geometrically possible.
- **Reached from** — Passes → `x`.
- **Shows** — the other station's grid (editable) and a list of mutual-visibility
  windows with start/end and the overlap duration.
- **Keys** — `;`/`.` scroll the windows; **`d`** open the **DX Doppler table** for
  the highlighted window; **ENTER** edit the remote grid; `` ` `` back.

### DX Doppler table

- **Purpose** — for a selected mutual window, the per-30-second RX/TX dial
  frequencies for **both stations** through the pass, for manual tuning of a
  coordinated contact.
- **Reached from** — Mutual windows → `d`.
- **Shows** — a scrolling table for the selected transponder, two lines per
  30-second step: your dials (green, "me") and the DX station's (cyan, "DX"), each
  with RX and TX — in one of three modes — **true rule**, **fixed downlink**, or
  **fixed uplink** — with an anchor dial and (for a linear transponder) a passband
  operating point. The header shows the passband point **relative to the centre of
  the passband's downlink** (e.g. `ctr`, `+7.5k`, `-12.5k`), so you can see at a
  glance how far off-centre you are working.
- **Keys** — `t` cycle transponder; `m` cycle mode; `a` cycle anchor (my RX/TX, DX
  RX/TX); `,`/`/` step the passband — in a **fixed** mode they move the **anchored
  dial** to the next **round 1 kHz** (so you park on, e.g., 145.950 MHz, not
  145.9502), and in **true rule** they nudge the operating point by 1 kHz; `;`/`.`
  scroll the time steps; `` ` `` back to the mutual list.

### Sat-to-sat visibility

- **Purpose** — the windows when the selected satellite and a second satellite are
  both above your horizon at once, over the next few days; detail in
  [§8 → Sat-to-sat visibility](#sat-to-sat-visibility-2-from-satellites).
- **Reached from** — Satellites → `2`.
- **Shows** — first a **pick screen** (choose the second satellite, no calculation
  yet), then after you start the search a scrolling list of overlap windows with
  start time (UTC), duration, and the peak elevation of each bird.
- **Keys** — `n`/`p` step the second satellite forward/back **instantly** (no search
  runs while you cycle); **ENTER** (or `r`) runs the window search for the chosen
  pair; `;`/`.` scroll the results; `` ` `` back to Satellites. Because cycling no
  longer triggers a multi-day search on every step, choosing a different second
  satellite is immediate.

### 10-day pass overview

![10-day pass overview](docs/img/ten-day-overview.jpg)

- **Purpose** — an at-a-glance visibility chart of one satellite's passes over ten
  days, modelled on InstantTrack's multi-day view.
- **Reached from** — Passes → `v` (or Satellites → `d`).
- **Shows** — one row per day (today at top), each a 24-hour UTC timeline; every
  pass is a bar shaded by peak elevation (dim-green < 15°, green < 40°, yellow
  above). A red tick marks now.
- **Keys** — `;`/`.` scroll one day (forward indefinitely, not before today);
  `r` recompute; `` ` `` back to Passes.

### Illumination

![Illumination](docs/img/illumination.jpg)

- **Purpose** — a 60-day solar-illumination raster (DK3WN *illum* style) showing
  when the satellite is sunlit vs. eclipsed across its orbit.
- **Reached from** — Passes → `i` (or Satellites → `i`).
- **Shows** — horizontal axis date (today → +60 d), vertical axis one orbital
  period; cells yellow in sunlight, dark in eclipse. A live readout shows current
  SUN/SHADOW status, eclipse minutes/percent per orbit, and time to the next
  transition.
- **Keys** — `,`/`/` scroll one day through the window (forward indefinitely, not
  before today); `r` recompute; `` ` `` back to Passes.

### Track

![Track — live Doppler & CAT](docs/img/track.jpg)

![Track — ground track](docs/img/track-groundtrack.jpg)

- **Purpose** — the main operating screen: live pointing, Doppler-corrected
  frequencies, transponder selection, calibration, and radio/rotator control. Full
  detail in [§8 → Track](#track), Doppler theory in
  [§9](#9-doppler-tuning-and-the-one-true-rule).
- **Reached from** — Passes → `t`/ENTER, or Home → Track (sel).
- **Shows** — az/el (and GP age), range/range-rate (ECL flag in eclipse), the
  transponder, DN/RX and UP/TX frequencies, the passband line, the calibration
  line, the radio status, a rotator status line, and a `PL` tone line on FM birds
  that need one.
- **Keys** — `m` switch TUNE/CAL; `d` cycle Doppler tune mode (linear birds);
  `t` next transponder; `c` set CTCSS/PL tone; `r` radio output on/off; `o`
  rotator on/off; `p` **Polar**; `z` large-font readout; `f` **Manual mode**;
  `l` **Log QSO**; `v` voice memo (SD card); `y` tilt tuning on/off (ADV, if IMU);
  `g`/`w`/`e` workable grids / US states / DXCC now; in TUNE: `,`/`/` move spot,
  `s` step, `x` recenter; in CAL: `,`/`/` trim downlink, `;`/`.` trim uplink, `s`
  step, `x` zero; **ENTER** save calibration; `` ` `` leave (returns to where you
  came from).
- **Leaving keeps tracking** — pressing `` ` `` returns to the previous screen and,
  if the radio/rotator are engaged, **they keep tracking in the background** (a green
  **RAD**/**ROT**/**R+R** header tag appears on other screens). Stop with `r`
  (radio) or `o` (rotator, which also parks).

### Manual mode

- **Purpose** — a no-radio frequency calculator: CardSat shows the
  Doppler-corrected dial frequencies so you can tune a non-CAT rig by hand.
- **Reached from** — Track → `f`.
- **Shows** — the same RX/TX figures as Track, formatted for manual dialing, with
  the passband/tune controls but no CAT output.
- **Keys** — mirror Track's tuning set: `m`, `t`, `s`, `x`, the `,`/`/`,`;`/`.`
  tune/cal keys, `g`/`w`/`e` workable lists, `l` log, `p` polar, `v` voice memo,
  `z` large-font Manual, `u`/`e`/`f` as on Track; `` ` `` back.

### Polar

- **Purpose** — a live full-screen polar sky plot of the satellite you are
  tracking, for visual aiming.
- **Reached from** — Track → `p`.
- **Shows** — the current az/el as a marker on a polar grid, with the pass arc.
- **Keys** — `p` toggle back to Track; `l` **Log QSO**; `v` voice memo (microSD,
  ADV); `` ` `` back.

### OSCARLOCATOR

- **Purpose** — a live azimuthal-equidistant "plotting board" showing the
  satellite's sub-point on the Earth in real time, with its footprint; the
  graphical companion to the tabular **EQX table**.
- **Reached from** — Satellites → `k`.
- **Shows** — a disc centred on your station (QTH mode) or a pole (polar mode),
  with a coastline, graticule, the satellite marker (sunlit/eclipse colour), its
  ground footprint, an amber **QTH range ring** (footprint radius at mean altitude),
  the blue **full ground-track arc** across the disc (AOS green, LOS orange, travel arrow), and a
  sub-point/az/el/range readout. In polar mode the North/South sheet is chosen
  automatically and flips live as the satellite crosses the equator.
- **Keys** — `m` toggle QTH-centred ↔ polar; `` ` `` back to Satellites.

### 3D Globe

- **Purpose** — an orthographic 3-D wireframe Earth that auto-follows the selected
  satellite, for an at-a-glance view of where it (and your other favourites) are on
  the planet, with the day/night terminator.
- **Reached from** — Satellites → `3`.
- **Shows** — a wireframe globe (near hemisphere only) with graticule, coastline,
  a yellow **day/night terminator**, your QTH (white cross), all favourites as
  green dots, the selected satellite centred (sunlit/eclipse colour) with its
  **ground footprint** and a blue **ground-track trail** (one orbit), and a
  sub-point/az/el/altitude readout. A **second (DX) location** entered by grid
  (`g`) adds an orange marker and footprint; the overlap with the satellite
  footprint is the mutual-visibility region. The globe rotates to keep the selected
  satellite centred.
- **Keys** — arrow keys turn the globe (free-look); **ENTER** re-snaps to
  auto-follow; **`g`** set a DX grid, **`G`** clear it; `` ` `` back to Satellites.

### Workable grids

![Workable grids](docs/img/workable-grids.jpg)

- **Purpose** — the Maidenhead grid squares currently (or, from Passes, during the
  pass) reachable through the satellite's footprint.
- **Reached from** — Track → `g` (live) or Passes → `g` (for the pass).
- **Shows** — the list/scatter of workable grids; from Track it refreshes live
  while radio and rotator control keep running.
- **Keys** — `;`/`.` scroll; `` ` `` back.

### Workable US states

![Workable US states](docs/img/workable-states.jpg)

- **Purpose** — the US states (for WAS chasing) under the footprint now or during
  the pass.
- **Reached from** — Track → `w` (live) or Passes → `w` (for the pass).
- **Shows** — the workable states list.
- **Keys** — `;`/`.` scroll; `` ` `` back.

### Workable DXCC

![Workable DXCC](docs/img/workable-dxcc.jpg)

- **Purpose** — the DXCC entities under the footprint now or during the pass.
- **Reached from** — Track → `e` (live) or Passes → `e` (for the pass).
- **Shows** — the workable DXCC list (derived from bundled cty.dat geometry).
- **Keys** — `;`/`.` scroll; `` ` `` back.

### Sun / Moon

![Sun / Moon](docs/img/sun-moon.jpg)

- **Purpose** — point the rotator at the Sun or Moon for sun-noise/EME work and
  antenna calibration; full detail in [§13 → Sun / Moon](#sun--moon-antenna-tracking).
- **Reached from** — Home → Sun / Moon.
- **Shows** — the selected body's live az/el (and a sky-dome graphic in graphic
  view). An orange SUN/MOON header tag appears while it is driving the rotator.
- **Keys** — `;`/`.` switch Sun↔Moon; `g` toggle graphic/list view; `o` rotator
  tracking on/off; **`s`** open the **Sky sources** plot; **`t`** open the **Sun/Moon
  transit finder** (below); `x` stop; `` ` `` park and back.

### Sun / Moon transits

- **Purpose** — find upcoming times the active satellite crosses (or closely
  approaches) the **Sun or Moon** from your location — the ISS-on-the-disc
  astrophotography event. Full detail in [§13 → Sun / Moon transits](#sun--moon-transits-t).
- **Reached from** — `t` on the **Sun / Moon** screen (predicts for the satellite
  currently selected).
- **Shows** — an incremental 48-hour scan with a progress bar, then a list of close
  approaches: body (Sun/Moon), a countdown, minimum angular separation, body
  elevation, and a **TRANSIT** (crosses the disc) vs **near** (conjunction) label.
  Requires the clock and your location to be set.
- **Keys** — `;`/`.` move the selection; `r` rescan; `` ` `` back to Sun / Moon.
  **Never observe a solar transit without proper solar filtering.**

### Sky sources

- **Purpose** — the planets and the strongest cosmic radio sources on a sky dome,
  for antenna pointing and as an RF-source reference; detail in
  [§13 → Sky sources](#sky-sources-s).
- **Reached from** — Sun / Moon → `s`.
- **Shows** — a sky dome (zenith centre) with planets as cyan dots and radio
  sources (Cas A, Cyg A, Sgr A\*, Tau A, Vir A) as orange crosses; a readout gives
  the selected object's az/el and type.
- **Keys** — `;`/`.` select an object; `` ` `` back to Sun / Moon.

### Space weather

![Space weather](docs/img/space-wx.jpg)

- **Purpose** — solar flux and geomagnetic indices with a plain-language operating
  outlook; detail in [§13 → Space weather](#space-weather).
- **Reached from** — Home → Space Wx.
- **Shows** — F10.7 flux, planetary Kp, running A index, each labelled and
  colour-coded, an HF/satellite outlook line, and a data-freshness note.
- **Keys** — `r` refresh over WiFi; `` ` `` back.

### Weather

![Weather](docs/img/weather.jpg)

- **Purpose** — terrestrial current conditions and a multi-day forecast for the
  operating site; detail in [§13 → Weather](#weather).
- **Reached from** — Home → Weather.
- **Shows** — current temperature, sky, wind and humidity, then per-day condition,
  high/low and precipitation chance, plus a freshness note. Units per Settings.
- **Keys** — `r` refresh over WiFi; `` ` `` back.

### QRZ callsign lookup

![QRZ callsign lookup](docs/img/qrz-lookup.jpg)

- **Purpose** — resolve a callsign to name, location, grid and licence class via
  the QRZ.com XML API; detail in [§13 → QRZ callsign lookup](#qrz-callsign-lookup).
- **Reached from** — Home → QRZ Lookup.
- **Shows** — prompts for credentials/WiFi if needed; otherwise the looked-up
  station's details.
- **Keys** — **ENTER** type a callsign to look up; `;`/`.` scroll a long result;
  `` ` `` back.

### Activations

- **Purpose** — list the upcoming satellite activations scheduled on hams.at (roves,
  grid activations, special operations); detail in
  [§13 → Upcoming activations](#upcoming-activations-activations).
- **Reached from** — Home → Activations.
- **Shows** — a scrollable list of upcoming activations (date, callsign, satellite,
  grid); ENTER opens a detail view with start/end times (UTC), mode, frequency and
  the activator's comment.
- **Keys** — `;`/`.` move; **ENTER** open the selected activation's detail; `r`
  refresh the feed; `` ` `` back (from detail, `` ` `` returns to the list).

### Location

![Location](docs/img/location.jpg)

- **Purpose** — set your station position and clock, and view the GPS sky plot.
- **Reached from** — Home → Location.
- **Shows** — current lat/lon/altitude, grid, time source and clock, and GPS
  status when enabled.
- **Keys** — `e` edit latitude; `o` edit longitude; `a` edit altitude; `g` enter a
  Maidenhead grid; `p` enable/disable GPS; `s` pick the GPS source; `c` set the UTC
  clock by hand; **`v`** open the **live GPS position** screen; **ENTER** open the
  **GPS sky plot**; `` ` `` back.

### Live GPS position

- **Purpose** — a full-precision position readout (DMS, decimal, grid) plus ground
  speed and course, for rovers and portable ops; detail in
  [§13 → Live GPS position](#live-gps-position-v).
- **Reached from** — Location → `v`.
- **Shows** — latitude/longitude in degrees-minutes-seconds to 0.001″ and in
  decimal degrees, altitude, Maidenhead grid, ground speed (km/h and knots) and
  course; a status line with fix/sat-count/HDOP.
- **Keys** — `` ` `` back to Location.

### LoRa Messages

- **Purpose** — CardSat-to-CardSat broadcast text chat over the Cap LoRa (SX1262);
  detail in [§8 → LoRa Messages](#lora-messages-home--messages).
- **Reached from** — Home → Messages.
- **Shows** — a scrolling message history (yours in cyan as `>me`, others in green
  by callsign) with a status line of frequency / SF / bandwidth / your callsign.
- **Keys** — `n` write + send a message; `;`/`.` scroll history; `r` retry the
  radio bring-up if it wasn't ready; `` ` `` back to Home.
- **Notes** — needs LoRa enabled in Settings and your callsign set; the LoRa radio
  is built into the standard binaries (RadioLib required at build time). Untested
  hardware path.

### GPS sky plot

![GPS sky plot](docs/img/gps-sky.jpg)

- **Purpose** — a polar plot of the GNSS satellites currently in view, by signal
  strength — useful for checking antenna/fix quality.
- **Reached from** — Location → ENTER.
- **Shows** — GNSS satellites placed by azimuth/elevation, coloured green (strong)
  to grey (weak), with fix data.
- **Keys** — `` ` `` back. (The plot updates live; no other keys.)

### Update

![Update](docs/img/update.jpg)

- **Purpose** — refresh everything that comes from the network in one place.
- **Reached from** — Home → Update.
- **Shows** — the last GP age and a note that `k` also refreshes the clock, AMSAT
  status, space weather and terrestrial weather.
- **Keys** — `k` (or ENTER) download GP + sync clock (NTP) + AMSAT + space wx +
  weather; `f` fast update (GP + AMSAT + favorites' transponders, skips space wx/weather);
  `a` fetch and cache **all** transponders for offline use; `w` connect WiFi only;
  `` ` `` back.

### GP source

- **Purpose** — choose where element sets come from.
- **Reached from** — Settings → GP source → ENTER.
- **Shows** — a picker: AMSAT (amateur, default), the CelesTrak category groups
  (Amateur first, then SatNOGS and the other groups), and Custom URL.
- **Keys** — `;`/`.` move (`{`/`}` page); **ENTER** select; `` ` `` back. The
  choice is saved immediately and used by the next Update.

### Settings

![Settings menu](docs/img/settings.jpg)

![Settings — Radio / CAT](docs/img/settings-radio-cat.jpg)

![Settings — Rotator](docs/img/settings-rotator.jpg)

![Settings — Station / display](docs/img/settings-station-display.jpg)

![Settings — Network / data](docs/img/settings-network-data.jpg)

- **Purpose** — all configuration, grouped into four submenus (Radio / CAT,
  Rotator, Station / display, Network). Each row and its adjust keys are tabulated
  in [§8 → Settings](#settings).
- **Reached from** — Home → Settings.
- **Shows** — the submenu list, then the rows within a chosen submenu with their
  current values.
- **Keys** — `;`/`.` move; **ENTER** open a submenu / edit a text field; `,`/`/`
  change a value in place; `` ` `` back.

### WiFi scan

- **Purpose** — pick a network from a live scan instead of typing the SSID.
- **Reached from** — Settings → WiFi SSID row → `s`.
- **Shows** — nearby networks, strongest first, `*` marking secured ones.
- **Keys** — `;`/`.` select; **ENTER** choose (then enter the password unless the
  network is open); `r` rescan; `` ` `` back.

### Rotator manual / calibration

- **Purpose** — jog the rotator by hand with live read-back, and — for a **Yaesu
  (direct)** rotator — calibrate the position ADC. Detail in
  [§17](#17-antenna-rotator-gs-232-rotctl-pstrotator-yaesu-direct-rotctld-server)
  and ROTOR_INTERFACE.md.
- **Reached from** — Settings → Rotator: manual control → ENTER.
- **Shows** — commanded and actual az/el; for Yaesu direct, the live ADC counts
  for each axis.
- **Keys** — `,`/`/` jog azimuth; `;`/`.` jog elevation; `s` step size; `x` stop;
  *(Yaesu direct only)* `1`/`2`/`3`/`4` capture the ADC counts at az 0 / az full /
  el 0 / el full; `` ` `` back.

### CAT serial monitor

- **Purpose** — a live on-device terminal showing the raw CAT traffic between CardSat
  and the radio, for confirming wiring, model, address, and baud without a PC. Detail
  in [§16 → CAT self-test](#16-radio-specific-notes).
- **Reached from** — Settings → Radio / CAT → *CAT serial monitor*.
- **Shows** — each frame as hex (or ASCII for Kenwood), **TX in cyan, RX in green**,
  with a decode line. While open it **actively polls the rig** (~every 700 ms) by
  reading the downlink frequency, so live traffic appears even with no pass running.
- **Keys** — `p` toggle polling on/off (off = passive view); `s` type a raw frame in
  hex and transmit it; `` ` `` back.

### CAT self-test

- **Purpose** — exercise the whole CAT link end-to-end in one shot and report what
  works, before trusting it for a pass. Detail in [§16 → CAT self-test](#16-radio-specific-notes).
- **Reached from** — Settings → Radio / CAT → *Run CAT self-test* (radio must be on —
  start CAT with `r` on Track first).
- **Shows** — a scrolling list of steps (downlink/uplink set+read, modes, band select,
  sat mode, PTT read, CTCSS), each marked `[PASS]`, `[FAIL]`, or `[INFO]` (the last for
  capabilities a given radio doesn't expose over CAT).
- **Keys** — `;`/`.` scroll; `` ` `` back.

### Log menu

![Log menu](docs/img/log-menu.jpg)

- **Purpose** — the QSO logging hub.
- **Reached from** — Home → Log.
- **Shows** — New QSO entry, View / edit log, Export to ADIF, Sign & upload to LoTW,
  Upload to Cloudlog, Voice Memos, Notes. (The list scrolls; a `^`/`v` marker shows when
  there are more items above or below.)
- **Keys** — `;`/`.` move; **ENTER** open the selected item; `` ` `` back.

### Sign & upload to LoTW

- **Purpose** — sign un-uploaded satellite QSOs into a `.tq8` and upload them
  directly to ARRL's Logbook of the World (microSD card + your LoTW key required;
  full setup in [§8 → LoTW upload](#logbook-of-the-world-lotw-direct-upload)).
- **Reached from** — Log menu → Sign & upload to LoTW.
- **Shows** — SD-card status, whether the LoTW key/cert are on the card, your
  station DXCC/CQ/ITU fields, and the count of QSOs not yet uploaded.
- **Keys** — `u` sign & upload (prompts for the key password if encrypted);
  `a` toggle re-send mode (include QSOs already uploaded); `` ` `` back.

### Upload to Cloudlog

- **Purpose** — upload satellite QSOs to a self-hosted **Cloudlog** (or **Wavelog**)
  online logbook over WiFi. Because a Cloudlog instance can itself forward QSOs to
  LoTW/eQSL, this is usually an alternative to the on-device LoTW upload rather than an
  addition. Tracked independently of LoTW, so a QSO sent to one isn't marked sent to the
  other. Full setup in [§8 → Cloudlog upload](#cloudlog--wavelog-upload).
- **Reached from** — Log menu → Upload to Cloudlog.
- **Shows** — your configured Cloudlog server, whether the API key and station ID are
  set, and the count of QSOs not yet sent to Cloudlog.
- **Keys** — `u` upload (connects WiFi if needed); `a` toggle re-send mode (include QSOs
  already sent to Cloudlog); `` ` `` back.

### Log entry

![Log entry](docs/img/log-entry.jpg)

- **Purpose** — create or edit one QSO record. Full field list in
  [§8 → Logging QSOs](#logging-qsos-log).
- **Reached from** — Track/Polar/Manual → `l`, or Log menu → New QSO entry, or Log
  list → ENTER on a record.
- **Shows** — every editable field: date, time, sat, mode, DL/UL MHz, call, RST
  sent/received, grid, notes.
- **Keys** — `;`/`.` move between fields; **ENTER** edit the selected field (the
  Sat field opens the satellite picker; Mode toggles SSB/CW on linear birds); `s`
  save; `` ` `` cancel.

### Log list (view / edit)

![Log list](docs/img/log-list.jpg)

- **Purpose** — review and correct stored contacts.
- **Reached from** — Log menu → View / edit log.
- **Shows** — a scrollable list of the most recent 60 QSOs, newest first.
- **Keys** — `;`/`.` move; **ENTER** open a record to edit; (within a record)
  `x` twice to delete; `` ` `` back.

### Voice Memos

- **Purpose** — browse, play back, and manage the voice memos on the SD card, and
  record new standalone memos.
- **Reached from** — Log menu → Voice Memos.
- **Shows** — all memos newest-first: date, time, the satellite recorded on (or `-`
  for a standalone memo), and length. A `REC` timer replaces the list while recording.
- **Keys** — `;`/`.` move; **ENTER** play the selected memo (any key stops);
  `n` record a new standalone memo (any key stops & saves); `d` delete (with confirm);
  `r` refresh; `` ` `` back to the Log menu.

### Notes (browser)

- **Purpose** — browse, open, create and delete the plain-text notes stored under
  `/CardSat/notes/`. Full description in [§8 → Notes](#notes-free-form-text-editor).
- **Reached from** — Log menu → Notes.
- **Shows** — your notes newest-first, each with the date and time it was last saved
  (UTC).
- **Keys** — `;`/`.` move; **ENTER** open the selected note; `n` new note; `d` then
  **ENTER** delete (with confirm); `` ` `` back to the Log menu.

### Notes (editor)

- **Purpose** — a full multi-line text editor for one note. Because `;` `.` `,` `/`
  are typed as punctuation, all editor commands use the **Fn** modifier.
- **Reached from** — Notes browser → ENTER on a note, or `n` for a new one.
- **Shows** — the note text with a block cursor; the header shows the note name and a
  `*` when there are unsaved changes; the footer shows the line number.
- **Keys** — type normally (**ENTER** = new line, **delete** = backspace);
  **Fn + `,`/`/`** cursor left/right; **Fn + `;`/`.`** cursor up/down;
  **Fn + `s`** save (prompts for a name the first time); `` ` `` exit (unsaved work
  is saved automatically, or you're prompted for a name).

### World map

![world-map](docs/img/world-map.jpg)

- **Purpose** — a live equirectangular map of all favorites' footprints with the
  day/night terminator.
- **Reached from** — Home → World Map, or Next Passes → `m`. Back returns to
  wherever you entered from.
- **Shows** — every favorite's sub-point and footprint, your station marker, the
  graticule, and the sun terminator (drawn automatically).
- **Keys** — `f` cycle which favorite is highlighted; **`c` recenter the map on
  your QTH** (press again to return to the classic 0°-centered view); `` ` `` back.
- **Recentering** — by default the map is centered on 0° longitude. Press `c` to
  center it on your station's longitude instead, so your QTH sits in the middle and
  the world wraps around it; this is remembered across reboots. Only the longitude
  is shifted — north stays up and the equator stays centered. The orbital-analysis
  ground-track map and the simulation map keep the standard 0°-centered view.

### Help

- **Purpose** — an on-device scrollable key reference, available almost anywhere.
- **Reached from** — `h` on most screens.
- **Shows** — per-screen key summaries.
- **Keys** — `;`/`.` scroll; `` ` `` back.

### About

![About](docs/img/about.jpg)

- **Purpose** — build and diagnostic information.
- **Reached from** — Home → About.
- **Shows** — firmware version, IP address, free heap and other read-only
  diagnostics.
- **Keys** — `` ` `` back.

### Edit (text/number entry)

- **Purpose** — the shared on-screen editor used by every field that needs typed
  input (frequencies, SSID/password, grid, callsign, host/port, clock, etc.).
- **Reached from** — automatically, whenever a field is opened for editing.
- **Shows** — the field title, the current buffer, and the entry footer. Certain
  fields default to **uppercase** (callsign, grid, SSID and similar) — hold shift
  for lowercase.
- **Keys** — type to enter; **DEL** backspace; **ENTER** accept; `` ` `` cancel.

---

## 22. Key reference (cheat sheet)

> A **printable version** of this reference is included as
> `CardSat_CheatCard_4x6.pdf` — a **4×6** landscape index card, front and back
> (two pages). Print at actual size; the front covers operating and the back
> covers setup and tools. Regenerate it with `python3 tools_make_cheatcard.py`.

**Global:** `;` up · `.` down · `,` left · `/` right · ENTER select · `` ` ``/DEL back · `{`/`}` page · `b` screenshot · `h` help.

| Screen | Keys |
|---|---|
| **Satellites** | `f` favorite · `v` favorites-only · `n` new GP sat · `x` delete manual sat (2-press) · `e` EQX table (OSCARLOCATOR) · `k` OSCARLOCATOR · `3` 3D globe · `2` sat-to-sat visibility · `o` orbital analysis · `s` simulation · `t` transponder database · `d` 10-day overview · `i` illumination · ENTER passes · right-edge AMSAT mark: filled dot = heard, filled square = telemetry only, ring = not heard, none = no reports · coloured down-arrow = decaying orbit (yellow watch / orange decaying / red imminent) |
| **Orbital analysis** | `,`/`/` page (Info / Live / Next pass / Ground track / Doppler / Nodal / Sun-Beta / Pass outlook / Orbit position) · Info: footprint diameter now/apogee/perigee (= longest possible QSO) + decay estimate & solar-bracket range · Live: az/el/range/Doppler, mean anomaly/phase, sunlit/eclipse + **eclipse depth** (deg; >0 = in shadow) · Next pass: slant ranges + path delay + peak eclipse depth · Doppler: `f` set beacon freq, peak shift + max range-rate · Nodal: J2 node/perigee drift, sun-sync, LTAN, repeat track, longest pass · Sun/Beta: solar beta angle, full-sun vs eclipsed, eclipse %/orbit, next transition · Pass outlook: 7-day pass count/>30° count/longest/avg gap + the best upcoming pass (elevation, when, duration) · Orbit position: mean/true anomaly, argument of latitude, time to perigee/apogee, RAAN, rev number · `r` recompute · `` ` `` back |
| **Simulation** | `,`/`/` step time · `;`/`.` step size · `m` world-map view (sub-point + footprint at the simulated time) · `x` reset to now · `` ` `` back |
| **Next Passes** | ENTER track · `m` world map · `r` refresh · `z` deep-sleep until AOS |
| **Passes** | `;`/`.` select · `d` detail · `t`/ENTER track · `n` add TX · `r` recompute · `x` mutual · `v` 10-day · `i` illum · `g` workable grids (this pass) · `w` workable US states (this pass) · `e` workable DXCC (this pass) |
| **Pass detail** | `p` polar of this pass · `` ` ``/ENTER back |
| **Pass polar** | `p` back to curve · `` ` ``/ENTER passes |
| **Track** | `m` TUNE/CAL · `d` cycle tune mode (FULL/DL/UL/hold) · `t` next TX · `n` jump to beacon · `N` per-sat operating note · `k` CW both legs (linear) · `c` CTCSS tone · `r` radio on/off · `o` rotator on/off · `p` polar · `z` large-font readout · `y` tilt tuning on/off (if IMU) · `f` Manual mode · `l` log QSO · `v` voice memo (SD card) · `g` workable grids now · `w` workable US states now · `e` workable DXCC now (radio/rotator keep running) · ENTER save cal |
| **Large-font readout** (`z` from Track) | big RX/TX + az/el + tune mode · `,`/`/` tune · `s`/`x` step/center · `m` TUNE/CAL · `d` mode · `t` next TX · `n` beacon · `k` CW (linear) · `r` radio · `o` rotator · `y` tilt · `l` log · `v` voice memo · `z`/`` ` `` back to Track |
| **Manual mode** (`f` from Track) | no-radio calculator; `u` swap fixed leg · `,`/`/` tune · `s`/`x` · `m` CAL · `t` next TX · `z` large-font · `l`/`p`/`g` (return here) · `` ` ``/`f` back |
| **Manual large-font** (`z` from Manual) | HOLD/TUNE legs in big digits · `u` swap leg · `,`/`/` tune · `s`/`x` · `m` CAL · `t` next TX · `z`/`` ` `` back to Manual |
| **Manual mode** | no-radio frequency calculator · `u` toggle which leg is fixed (HOLD vs TUNE>) · `,`/`/` move fixed freq in passband (linear) · `s` step · `x` center · `m` CAL · `t` next TX · `l` log · `p` polar · `g` grids (all return here) · ENTER save cal · `` ` ``/`f` back to Track |
| **Workable grids** | 4-char Maidenhead grids under the footprint (per-pass union or live, refreshing ~3 s; uncapped, works to high orbits) · count shown on a cyan line above the list · `;`/`.` and `{`/`}` scroll · `` ` `` back |
| **Track · TUNE** | `,`/`/` tune ∓ · `s` step (100/1k/5k) · `x` recenter |
| **Track · CAL** | `,`/`/` downlink ∓ · `;`/`.` uplink ∓ · `s` step (10/100/1k) · `x` zero |
| **Polar** | `l` log QSO · `v` voice memo (SD card) · `p`/ENTER/`` ` `` back to track |
| **Log (menu)** | `;`/`.` select · ENTER → new QSO / browse / export ADIF / voice memos / LoTW upload / notes |
| **Log · list** | `;`/`.` scroll · ENTER edit entry · `` ` `` back |
| **Log · entry** | `;`/`.` field · ENTER edit · `s` save · `x`×2 delete · `` ` `` back |
| **Notes (browser)** | `;`/`.` select · ENTER open · `n` new · `d`+ENTER delete · `` ` `` back · list shows last-saved date/time (UTC), newest first |
| **Notes (editor)** | type freely (ENTER = newline, DEL = backspace) · **Fn+`,`/`/`** cursor L/R · **Fn+`;`/`.`** cursor up/down · **Fn+`s`** save (names it the first time) · `` ` `` exit (auto-saves) |
| **Mutual** | `;`/`.` scroll · `` ` ``/ENTER back to passes |
| **10-day** | `;`/`.` scroll ∓1 day (forward indefinitely, oldest day off the top; not before today) · `r` recompute · `` ` ``/ENTER back |
| **Illum** | `,`/`/` scroll ∓1 day (forward indefinitely; not before today) · `r` recompute · `` ` ``/ENTER back |
| **Location** | `e`/`o`/`a` lat/lon/alt · `g` grid · `p` GPS on/off · `s` GPS source · `c` set clock · `v` live GPS position · ENTER GPS sky plot |
| **Live GPS position** | DMS + decimal lat/lon, altitude, grid, speed, course · `` ` `` back |
| **GPS sky plot** | live GNSS az/el coloured by signal · `` ` `` back |
| **Messages** | LoRa CardSat-to-CardSat chat · `n` write/send · `;`/`.` scroll · `r` retry radio · `` ` `` back |
| **World map** | `f` cycle highlighted favorite · `c` recenter on QTH / 0° (sun terminator drawn automatically) · `` ` `` back |
| **Rotator (manual)** | `,`/`/` az · `;`/`.` el · `s` step · `x` stop · *(Yaesu direct only)* `1`/`2`/`3`/`4` capture ADC at az 0 / az full / el 0 / el full · `` ` `` back |
| **Help** | `;`/`.` scroll · `` ` `` back |
| **Update** | `k`/ENTER GP (+clock/AMSAT/space-wx/weather) · `f` fast (GP + AMSAT + fav TX) · `a` cache all TX · `w` WiFi only |
| **Settings** | `,`/`/` change · ENTER edit/toggle · `s` scan WiFi (on SSID row) · (Reset = type ERASE) |
| **GP source** | pick **AMSAT** / any **CelesTrak** JSON-PP category (Amateur Radio first) / **Custom URL** · `;`/`.` move · `{`/`}` page · ENTER select |
| **Sun / Moon** | graphical sky-dome view (Sun/Moon glyphs on a polar dome) · `g` toggle graphic/data list · `;`/`.` pick Sun/Moon · `o` rotor track on/off (takes the rotator from sat tracking) · `s` sky sources (radio sources/planets) · `t` Sun/Moon transit finder · auto-parks while the body is below the horizon · header shows SUN/MOON tag on other screens · `x` stop · `` ` `` back |
| **Space Wx** (main menu) | solar 10.7 cm flux + planetary Kp + running A index, each labelled & colour-coded, with a plain-language HF/satellite operating outlook and a data-freshness note · shows cache then auto-fetches on entry (if on WiFi) with an "Updating Space Wx" bar + result · also refreshes with Update · `r` refresh · `` ` `` back |
| **Weather** (main menu) | terrestrial current conditions + multi-day forecast for the operating site from Open-Meteo · current temp/sky/wind/humidity then per-day hi/lo & precip chance · shows cache then auto-fetches on entry (if on WiFi) with an "Updating Weather" bar + result · also refreshes with Update · `r` refresh · cached offline · `` ` `` back |
| **QRZ Lookup** (main menu) | callsign lookup via QRZ.com XML (needs a QRZ XML subscription + credentials in Settings → Network) · ENTER type a callsign · shows name/address/country/grid/class · WiFi required · `` ` `` back |
| **Transponder DB** (Satellites → `t`) | scrollable list of the selected satellite's transponder/beacon entries (description; **D** downlink + mode; **U** uplink + tone/inv/lin flags) · `;`/`.` scroll · `` ` `` back |
| **Edit** | type · DEL backspace · ENTER ok · `` ` `` cancel |
| **About** | build/version, IP, free heap and diagnostics (read-only) |

---

## 23. Glossary

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
  Kenwood — behind one abstract rig interface, over a wired serial bus or (for the
  **IC-9700**) the **Icom LAN** RS-BA1 protocol.
- **CI-V** — Icom's Communications Interface-V serial control bus (Icom's CAT dialect).
- **MAIN / SUB** — the two VFOs/bands of a satellite-capable radio; by default
  CardSat uses MAIN for uplink and SUB for downlink (the **VFO Type** setting swaps
  them).
- **One True Rule** — KB5MU's principle: tune so the frequency *at the satellite*
  stays constant; the computer corrects both legs for Doppler.

---

## 24. Supporting AMSAT

CardSat exists because of the work **AMSAT** and its volunteers do — building and
keeping amateur satellites flying, publishing the orbital data this app depends on,
and advocating for amateur radio in space. **If you find CardSat useful, please
consider joining and/or donating to AMSAT at [www.amsat.org](https://www.amsat.org/).**
Membership and donations directly fund the next generation of satellites you'll
track and work with this very tool.

---

## 25. License

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
