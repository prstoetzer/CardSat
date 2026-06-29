# CardSat — Full Feature List

This is the complete feature reference for CardSat. The README carries a short
summary; this document has the full detail for every capability. For day-to-day
operating instructions see **[MANUAL.md](../MANUAL.md)**.

## Features

- **Constant-frequency-at-the-satellite Doppler** (KB5MU's *One True Rule*):
  both the uplink and downlink are continuously corrected so your signal never
  walks through the passband. Tune with the device keys **or with the radio's own
  knob** — let go and nothing drifts. A tune-mode cycle (`d`) covers full One True
  Rule, **downlink-only**, **uplink-only** (for an SDR / second-receiver setup),
  and hold-both.
- **Three CAT families, ten radios**, behind one abstract rig interface so the
  Doppler engine is protocol-agnostic: Icom **CI-V** (IC-820/821/910/970/9100/9700),
  Yaesu (**FT-847**, **FT-736R**), and Kenwood (**TS-790**, **TS-2000**). Wire-level
  command sets follow the Hamlib backends. Every frame is traced to the serial monitor.
  Per-radio settings and a CAT-vs-manual capability breakdown are in
  **[RADIO_SETTINGS.md](interfaces/RADIO_SETTINGS.md)** (note: on the older sat rigs the band
  pair, MAIN/SUB, sat mode, and tone are set up **on the radio** — CAT only
  Doppler-tunes within that layout).
- **Native Icom LAN control (no CI-V wiring).** The **IC-9700** can be driven over
  WiFi/Ethernet using the radio's own **RS-BA1 UDP** protocol — the same one Icom's
  remote software uses — with no level shifter or UART. (Among the radios CardSat
  supports, Icom LAN is **IC-9700 only**; other network-capable Icoms like the
  IC-705/7610/785x are not supported here.) Pick **CAT type → Icom LAN** in
  Settings; MAIN/SUB, Doppler, sat mode and CTCSS all work as on a wired Icom
  (CAT only — the audio stream is not opened).
- **Linear-transponder passband tracking** with correct inversion, and automatic
  sideband selection (USB down / LSB up; USB/USB for HF birds below 30 MHz).
- **Automatic PL/CTCSS tone** on FM uplinks (SO-50, AO-91, ISS, PO-101…): CardSat
  enables the rig's TX tone encoder at the right frequency from a built-in table,
  and clears it when you leave the bird or stop radio output. Tones are also
  **settable per satellite** (`c` on Track) for any FM bird, and persist. Icom /
  FT-847 / TS-2000.
- **Next Passes** — one schedule across *all* your favorites, soonest AOS first.
- **AOS alarm** — countdown beeps + a screen flash before a favorite rises.
- **Deep-sleep until the next pass** — park the unit between passes for big
  battery savings; it wakes ~60 s before AOS.
- **Jump to beacon** — one key (`n`) on the Track and large-font readout screens
  tunes straight to the satellite's beacon (its downlink-only/telemetry entry) with
  Doppler correction, for finding a bird by its beacon before working it.
- **Pass-detail plot** — elevation curve for a pass, colored by sunlit/eclipse,
  plus a **polar view** of that pass (ground track + direction of travel).
- **Polar sky plot with ground track** — the live polar shows the satellite's arc
  across the sky for the current pass (or the next one when it's below the horizon),
  with AOS/LOS markers and a travel-direction arrow.
- **OSCARLOCATOR live view** — a live azimuthal-equidistant plotting board (`k` on
  the Satellites screen) showing the satellite's **sub-point and footprint on the
  Earth** in real time, the graphical companion to the EQX table. It also plots a
  **QTH range ring** (footprint radius at mean altitude, centred on you) and the
  **full ground-track arc** (a whole orbit across the disc) with AOS/LOS markers. Toggle (`m`) between a
  **QTH-centred** view (your station at centre, true bearing/distance) and a
  **polar** view that auto-selects the North or South sheet and **flips live as the
  bird crosses the equator**.
- **3D globe** — an orthographic wireframe Earth (`3` on the Satellites screen) that
  **auto-follows the selected satellite**, rotating to keep its sub-point centred.
  Shows a graticule, coastline, a **day/night terminator**, your QTH, **all
  favourites** as dots, the selected bird's **ground footprint** and a blue
  **ground-track trail** (a full orbit) — all on the near hemisphere, with the far
  side hidden behind the curve. Enter a **second (DX) location** by Maidenhead grid
  (`g`) to overlay its footprint; where it meets the satellite footprint is the
  mutual-visibility region. Arrow keys turn the globe for free-look; ENTER re-snaps
  to follow.
- **Mutual-window finder** — enter a remote station's grid square and get the
  **co-visibility windows** for a satellite over the next **10 days**: when you
  can both see it at once, with each window's duration and the peak elevation at
  both ends. From a window, open a **DX Doppler table** (`d`) that lists the
  **RX/TX dial frequencies for both stations every 30 s** through the pass — in
  true-rule, fixed-downlink, or fixed-uplink mode, with a selectable passband
  operating point — for manually tuning a coordinated contact through a short
  window.
- **10-day pass overview** — InstantTrack-style visibility chart (rows = days,
  24 h timelines) for the selected satellite, off the Passes screen (`v`); `;`/`.` page through
  successive 10-day chunks (forward indefinitely).
- **Sat-to-sat visibility finder** — off the Satellites screen (`2`), the windows
  when the selected satellite and a second one (from your favorites) are **both
  above your horizon at once** over the next five days, with each window's duration
  and the peak elevation of both birds — for cross-satellite relay or back-to-back
  working.
- **60-day illumination** — DK3WN *illum*-style Sun/eclipse raster (date x
  orbit-phase) with a live solar-status readout, off the Passes screen (`i`); `,`/`/`
  page through successive 60-day chunks (forward indefinitely).
- **Sun & eclipse** — Sun azimuth/elevation, a Sun glyph on the polar plot, and
  whether the satellite is sunlit or in Earth's shadow.
- **GP age** — element-set age shown and color-graded so you know when elements are stale.
- **Antenna rotator (GS-232, Easycomm, SPID, rotctl, PstRotator, or direct Yaesu)** — point an az/el rotator (Yaesu
  G-5500 + GS-232B, SPID MD-01/02, K3NG/RadioArtisan, SatNOGS/ERC via **Easycomm I/II/III**) through an I²C→UART bridge so the radio
  and GPS keep their UARTs, or over WiFi to a **Hamlib rotctld** server or a
  **PstRotator** instance, or wire a **Yaesu G-5500-class controller directly**
  (I²C ADC + outputs, no GS-232 box — see **[ROTOR_INTERFACE.md](interfaces/ROTOR_INTERFACE.md)**,
  ⚠️ untested). Deadband, park-on-LOS, pre-positioning before AOS,
  alignment offsets, optional **per-pass flip**, and a **manual control** screen
  for jogging the antenna by hand with live position read-back.
- **CardSat as a network server.** Run a **rigctld server** so a PC (Gpredict,
  WSJT-X, a logger) drives the wired/LAN radio through CardSat, and/or a
  **rotctld server** so a PC drives the wired GS-232 rotator through CardSat —
  both over TCP on the LAN.
- **rigctl network radio.** Drive a radio attached to a remote **Hamlib rigctld**
  server over WiFi (Settings -> CAT type -> rigctl) — Doppler both legs via split.
- **World map with coastline** — recognisable continents with **all favourites'**
  footprints at once; `f` highlights one bird at a time, and **`c` recenters the map
  on your own location** so your QTH sits in the middle.
- **Time-step simulation** — off the Satellites list (`s`), step a satellite
  forward/back in time (`,`/`/`) at selectable steps to preview az/el, range and
  lighting; `m` switches to a world-map view that walks the sub-point and
  footprint across the map as you step.
- **GPS sky plot** — fix data plus a polar plot of the GNSS satellites in view
  (az/el, coloured by signal), off the Location screen.
- **Live GPS position** — off the Location screen (`v`), a full-precision readout
  for rovers and portable ops: latitude/longitude in **degrees-minutes-seconds**
  (to 0.001″) and decimal, altitude, **Maidenhead grid** (updates live as you
  move), plus **ground speed** (km/h and knots) and **course**.
- **Workable grid squares** — the 4-char Maidenhead grids under the satellite's
  footprint, either as the union over a selected pass (off Passes) or live now
  (off Track, with radio/rotator tracking uninterrupted) - for VUCC/grid chasing.
- **Workable US states** — the same idea for US states + DC (the `w` key), found
  by point-in-polygon against bundled simplified boundaries - for WAS chasing.
- **Workable DXCC** — and again for all **340 DXCC entities** (the `e` key): a
  hybrid of country polygons + island/micro-entity reference points from cty.dat.
- **AMSAT activity marks** — the Satellites list flags whether each bird has been
  reported heard (filled dot) or only not-heard (ring) recently, from the AMSAT
  OSCAR Status API, refreshed with each elements update.
- **Selectable element source** — AMSAT JSON by default, or any CelesTrak JSON-PP
  category (Amateur Radio first), or a custom URL, chosen from an on-device picker
  (no URL typing needed); CelesTrak's `OBJECT_NAME` is handled automatically.
- **Sun & Moon antenna pointing** — a Sun/Moon screen (off the main menu) with a
  graphical sky-dome view (Sun and Moon glyphs plotted by az/el; `g` toggles a
  data list) that can drive the rotator to track either, for sun-noise / Moon
  (EME) aiming and antenna calibration. A secondary **Sky sources** plot (`s`) adds
  the planets and the strongest cosmic radio sources (Cas A, Cyg A, the galactic
  centre, the Crab, Virgo A) on the same dome — for antenna pointing and as an
  RF-source reference.
- **Space weather** — a **Space Wx** screen (main menu) shows the solar **10.7 cm
  flux**, planetary **Kp**, and running **A index** from NOAA SWPC, each colour-coded
  with a plain-language HF/satellite operating outlook; cached for offline viewing
  and refreshed with each elements update.
- **Terrestrial weather** — a **Weather** screen (main menu) shows current
  conditions and a multi-day forecast for your operating site from **Open-Meteo**
  (free, no key): temperature, sky, wind and humidity now, then per-day high/low and
  precipitation chance. Units selectable (°F·mph / °C·km·h / °C·m·s); cached offline.
- **QRZ.com callsign lookup** — a **QRZ Lookup** screen (main menu) resolves a
  callsign to name, location, grid and licence class over the QRZ XML API (needs a
  QRZ XML-data subscription and WiFi) — handy for working a station you've just
  contacted.
- **Upcoming activations feed** — an **Activations** screen (main menu) downloads the
  **hams.at** upcoming-activations feed and lists scheduled roves, grid activations,
  and special operations (date, callsign, satellite, grid), with a detail view for
  each (start/end times UTC, mode, frequency, the activator's comment) and a refresh
  key. See who's planning to be on which bird, from where, and when. The list is
  **cached to the card**, so re-opening the screen shows the last-known roster even
  with no WiFi; an **"Activations…"** indicator shows on the bottom status bar while it
  refreshes online (like the Weather fetch), with the cached list visible underneath. The
  **Update** screen's `k` pulls a fresh activations list alongside the GP update, so one
  keypress refreshes both.
- **On-device Help** — press `h` on (almost) any screen for a scrollable key reference.
- **QSO logging + ADIF.** Press `l` while tracking to log a contact (UTC, satellite,
  up/downlink, mode, your grid + theirs, RST, notes) to a CSV on the card **without
  interrupting Doppler control** — or add one **after the fact** from the Log menu,
  picking the satellite (which defaults the frequencies to the transponder centre /
  nominal) and editing the **date, time, satellite and frequencies** as needed. The
  same fields are editable when you review past entries; **export ADIF** on demand for
  LoTW/eQSL or your main logger.
- **Direct LoTW upload** (microSD card + your LoTW key required) — **Sign & upload to
  LoTW** on the Log menu signs your un-uploaded satellite QSOs into a `.tq8` and sends
  them straight to ARRL's Logbook of the World over WiFi, with no PC and no TQSL. It
  builds the same cryptographically-signed file TQSL would (RSA-PKCS#1-v1.5 over SHA-1,
  via the firmware's built-in mbedTLS) and posts it to LoTW's self-authenticating
  service. You enroll once on a computer the normal way and copy your certificate to
  the card as two PEM files — a **bundled browser converter**
  (`tools/lotw_cert_converter.html`) turns your TQSL `.p12` into those PEMs entirely
  in-browser and offline, with the private key never leaving your computer (or use a
  one-line `openssl pkcs12` step). **Your private key lives on the SD card**, loaded
  only at upload time and never copied or transmitted except as the signature.
  The station location is a **unified, chained picker** that works the same for every
  entity: pick your **DXCC** from the full list (the entities that have subdivisions are
  grouped at the top, and a typeahead filters the list), then a **primary** subdivision
  gated by that DXCC (US state, Canadian province, Russian oblast, Japanese prefecture,
  Chinese province, Australian state, Finnish kunta — the row is labelled with the right
  term and shows *(n/a)* for entities without one), then a **secondary** gated by the
  primary (US **county** or Japanese **city/gun/ku**), plus a free-text **IOTA** field.
  No more separate US-vs-international entry — the US is just "United States → state →
  county," exactly like Japan's "prefecture → city." Each level is signed into the upload
  under the exact LoTW field name and sigspec order, fully data-driven from the
  TrustedQSL config (340 DXCC entities, all primary lists, every US county and Japanese
  city). Sent QSOs are flagged (an `uploaded` column) so they're never uploaded twice.
  See [MANUAL.md → LoTW upload](../MANUAL.md).
- **Cloudlog / Wavelog upload** — **Upload to Cloudlog** on the Log menu sends your
  satellite QSOs to a self-hosted **Cloudlog** (or compatible **Wavelog**) online logbook
  over WiFi, via its JSON QSO API. Set your instance URL, a read-write API key, and your
  numeric station profile ID in Settings. Because a Cloudlog instance can itself forward
  QSOs on to LoTW, this is an alternative to the on-device LoTW upload — the two are
  tracked independently (separate flags) so a QSO sent to one isn't assumed sent to the
  other. Supports re-sending already-uploaded QSOs. The API key is treated as a secret and
  never written to the serial log. See [MANUAL.md → Cloudlog upload](../MANUAL.md).
- **Voice memos.** Press **`v`** while tracking to record a quick voice note to the
  SD card **without interrupting Doppler control** — the filename is stamped with the
  UTC time and the **satellite** you're on (e.g. `memo_20260617_203145_AO-91.wav`). A
  **Voice Memos** browser (in the **Log** menu) lists them newest-first by date, time,
  satellite and length, and lets you **play** a memo back through the speaker, **delete**
  one, or record a **new standalone memo** (`n`, not tied to a satellite) on the spot.
  SD-card only; recording needs an **ESP-IDF 5.4.x** build on the ADV (see
  [MANUAL.md](../MANUAL.md)).
- **Notes.** A free-form, multi-page **text editor** on the **Log** menu for sked
  details, grids you still need, antenna settings, or any operating reminder. Each
  note is a plain `.txt` file under `/CardSat/notes/` (on the SD card if fitted,
  otherwise internal flash, so it works with or without a card), so you can also read
  or edit them on a computer. The browser lists notes newest-first with the last-saved
  date/time; the editor is full multi-line with a cursor moved by the **Fn** modifier
  (**Fn+`,`/`/`** left/right, **Fn+`;`/`.`** up/down, **Fn+`s`** save), leaving
  `;` `.` `,` `/` free to type as punctuation. Notes can be up to 4 KB each.
- **IR pass beacon** (optional, off by default) — on each AOS-alarm event CardSat can
  also blink the Cardputer's **built-in IR LED** with a distinct flash count per event
  (T-60/T-30/T-10/AOS/TCA/LOS), a 38 kHz carrier any common IR receiver decodes, so you
  can trigger external hardware (relays, preamps, recorders, shack lights) off a pass.
  Non-blocking; ⚠️ host-verified only, see [MANUAL.md](../MANUAL.md).
- **LoRa text messaging** — CardSat-to-CardSat **broadcast group chat** over the
  M5Stack **Cap LoRa (SX1262)**: every unit on the same frequency/SF/bandwidth sees
  every message, for a club net or SOTA/portable group. Selectable frequency
  (150–960 MHz; the SX1262 is unfiltered), spreading factor (7–12, default 12) and
  bandwidth; fixed-size message ring (no SD, no heap growth). Built into the standard
  binaries (requires the **RadioLib** library at build time). ⚠️ Untested hardware
  path — verify two units talk before relying on it; mind your band's bandwidth rules.
  See [MANUAL.md](../MANUAL.md).
- **Auto-refresh, power management, and diagnostics.** If WiFi is configured,
  CardSat connects and NTP-syncs at boot and **auto-refreshes GP when the cached
  elements are over a week old**; a **fast update** (`f` on the Update screen) refreshes
  just the elements, AMSAT activity and favorites' transponders, skipping space/terrestrial
  weather; the backlight blanks after a configurable idle
  time (any key wakes it); config + favorites **back up / restore to the SD card**;
  and an **About** screen reports version, storage, GP age, battery, and uptime.
- **Fully offline** once GP + transponders are cached. CardSat stores everything in
  a **`/CardSat` folder on the microSD card** by default, falling back to internal
  **LittleFS** if no card is present.
- **Screenshots** — press **`b`** on any screen to save a 24-bit BMP to
  `/CardSat/Screenshots/` on the SD card (handy for documentation).
- **Favorites**, **manual GP / transponder / time entry**, per-satellite
  **calibration**, and a **factory reset**.
