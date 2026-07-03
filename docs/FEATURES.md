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
  remote software uses — with no level shifter or UART. The LAN path is
  **hardware-confirmed** (it successfully controlled an IC-705 over the network),
  though the IC-9700 itself is intended-but-not-yet-tested, and only the IC-9700 has the
  full-duplex MAIN/SUB satellite architecture CardSat drives (the IC-705/7610/785x speak
  the same protocol but are single-receiver). Pick **CAT type → Icom LAN** in
  Settings; MAIN/SUB, Doppler, sat mode and CTCSS all work as on a wired Icom
  (CAT only — the audio stream is not opened).
- **Linear-transponder passband tracking** with correct inversion, and automatic
  sideband selection (USB down / LSB up; USB/USB for HF birds below 30 MHz).
- **Automatic PL/CTCSS tone** on FM uplinks (SO-50, AO-91, ISS, PO-101…): CardSat
  enables the rig's TX tone encoder at the right frequency from a built-in table,
  and clears it when you leave the bird or stop radio output. Tones are also
  **settable per satellite** (`c` on Track) for any FM bird, and persist. Icom /
  FT-847 / TS-2000.
- **Next Passes** — one schedule across *all* your favorites, soonest AOS first,
  with a **Sky-at-a-glance timeline** view (`t`): a horizontal time axis with one row
  per favorite and pass bars coloured by peak elevation (green ≥30°, yellow below).
- **AOS alarm** — countdown beeps + a screen flash before a favorite rises. An
  optional **AOS lead alert** (off / 2 / 5 / 10 / 15 min) adds an earlier "get ready"
  chirp minutes ahead of AOS, and the **home screen** shows the next favorite pass with
  a live countdown.
- **AMSAT live status** — each bird's recent on-air status (heard / telemetry / not
  heard) from the AMSAT OSCAR Status feed, with **"heard N ago" recency** for all three
  states, a **configurable window** (3/6/12/24/48/72 h, default 24), and a **dedicated
  AMSAT status screen** (sorted most-active-first; `s` from the sat list or the main
  menu) that lists every reported bird with its status, recency, and report count, and
  lets you adopt one as the active satellite with ENTER.
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
- **Point-here arrow** — a glanceable screen (`a` on Track) that draws a big compass
  arrow to the satellite's azimuth plus an elevation bar, sized for hand-aiming a
  portable Yagi without reading numbers off the screen mid-pass; the radio and rotator
  keep tracking underneath while it's open.
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
- **Grid distance & bearing** — a great-circle calculator (main menu) for
  terrestrial VHF/UHF: enter a Maidenhead grid for the **distance and beam heading**
  (short and long path, km and miles), **point the rotator** at the bearing, or **look
  up a callsign's grid** via a separate QRZ→grid screen that seeds the calculator.
- **Band-plan reference** — a scrollable worldwide amateur band reference (off Help),
  **LF to light**: HF with **ITU Region 1/2/3** splits, VHF/UHF/microwave with
  **EME/calling** frequencies, **satellite subbands**, IARU **band designators**, and
  common sat modes including QO-100.
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
  (I²C ADC + outputs, no GS-232 box — see **[ROTOR_INTERFACE.md](interfaces/ROTOR_INTERFACE.md)**).
  Deadband, park-on-LOS, pre-positioning before AOS,
  alignment offsets, optional **per-pass flip**, and a **manual control** screen
  for jogging the antenna by hand with live position read-back.
  *(Status: the **rotctl** and **PstRotator** network paths are confirmed to emit
  accurate commands on the wire, but have not been driven against a physical rotator;
  the **GS-232/Easycomm/SPID** I²C→UART bridge and the **direct-Yaesu** backend are
  host-tested only — verify before keying real motors.)*
- **CardSat as a network server.** Run a **rigctld server** so a PC (Gpredict,
  WSJT-X, a logger) drives the wired/LAN radio through CardSat, and/or a
  **rotctld server** so a PC drives the wired GS-232 rotator through CardSat —
  both over TCP on the LAN.
- **Mobile web control panel** — CardSat serves a phone/laptop-friendly page over
  your WiFi (Settings -> Web control) laid out for **working a pass**: a **fast
  calibration pad** (big one-tap RX/TX cal nudges with a tappable 10/100/1000 Hz step,
  no keyboard), a **tuning cluster** with a visible passband step, live RX/TX with the
  applied **Doppler shift**, an **AMSAT activity** line, an **in-pass** header and frame,
  transponder and satellite pickers (the tracked bird is always present), favourite
  toggling, and a **polar plot that always shows the pass arc** — current pass in-pass,
  next pass otherwise — with a **direction-of-travel arrow**. A Manual card mirrors the
  hand-tuning calculator, and an Orbit card shows the analysis numbers.
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
  A **prefix filter** (`f`) narrows the list to grids starting with what you type
  (`EM`, `EM2`, or `EM21`), entered upper-case per the grid rule; `c` clears it.
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
- **EME / moonbounce** — a dedicated EME screen (`e` from Sun/Moon) with **self-echo
  Doppler** per band (50/144/432/1296/10368 MHz), computed **topocentrically** so it
  reflects the dominant observer-rotation term (kHz at 1296, tens of kHz at 10 GHz);
  live **topocentric range and range-rate**; **path degradation** vs perigee with a
  perigee/apogee note; a coarse galactic **sky-noise** flag; a **mutual-Moon window**
  finder against a DX grid (common windows over the next two weeks); and **rotator
  Moon-tracking**.
- **Space weather** — a **Space Wx** screen (main menu) shows the solar **10.7 cm
  flux**, planetary **Kp**, and running **A index** from NOAA SWPC, each colour-coded
  with a plain-language HF/satellite operating outlook and an **aurora-likelihood**
  line derived from Kp (unlikely / possible / likely, with latitude); cached for
  offline viewing and refreshed with each elements update.
- **HF / 6m propagation guide** — a propagation screen (`p` from Space Wx) that turns
  the solar-flux and Kp data into band guidance: **HF band conditions** (which bands
  are open in daylight, a 10/15/20 m read), the **geomagnetic** effect on HF, an
  **auroral-VHF** likelihood (6 m / 2 m, with beam direction), and **D-layer
  absorption** — rule-of-thumb, from data already fetched, no new source.
- **What's overhead now** — an **Overhead now** screen (main menu) scans the whole
  loaded catalog for every satellite **above the horizon at this instant** and lists
  them sorted by elevation, with azimuth and rise compass direction (high passes in
  green, near-horizon in yellow) — a quick "what can I work or see right now" glance,
  with an `r` rescan for the current moment.
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
  keypress refreshes both. From an activation's detail view, **`a` sets a sked
  reminder** for it — a countdown (T-60/30/10) of beeps and a "SKED!" flash at the
  scheduled start time, independent of the favorites AOS alarm, so you don't miss a
  planned contact even if that satellite isn't one of your starred favorites. You can
  also **enter your own activations or skeds** (`n` on the list, `e` to edit) in the
  exact same format as the feed — for ops that aren't posted to hams.at — which are
  stored separately, merged into the list alongside the fetched ones (marked with a
  `*`), and survive feed refreshes; they work fully offline and can carry sked reminders
  like any other entry.
- **On-device Help** — press `h` on (almost) any screen for a scrollable key reference.
  The Help screen also links to five built-in references, each formatted for the Cardputer
  screen and scrollable: a **Glossary & math** (`g`, the terms plus the orbital and Doppler
  formulas), a **User guide** (`m`, a concise end-to-end manual), a **Ham satellite history**
  (`s`), a **Tech help** guide (`t`, portable antennas with the Arrow Yagi as the top pick,
  feedline/pointing/operating tips, and getting the interfaces and logging working), and a
  **Learn** screen (`l`, in-depth radio and orbital theory and how amateur satellites work
  internally).
- **QSO logging + ADIF.** Press `l` while tracking to log a contact (UTC, satellite,
  up/downlink, mode, your grid + theirs, RST, notes) to a CSV on the card **without
  interrupting Doppler control** — or add one **after the fact** from the Log menu,
  picking the satellite (which defaults the frequencies to the transponder centre /
  nominal) and editing the **date, time, satellite and frequencies** as needed. The
  same fields are editable when you review past entries; **export ADIF** on demand for
  LoTW/eQSL or your main logger.
- **Awards tracking.** A summary on the Log menu tallies your worked totals from the
  log — total QSOs, unique grids, distinct satellites, US states (of 51) and DXCC
  entities — with **scrollable lists of the actual worked grids, states and entities**
  (`g`/`s`/`d`) and a per-satellite drill-down (each bird's own counts and lists).
  States and DXCC are **derived from each QSO's grid square** by the same
  point-in-polygon machinery as the footprint screens, not stored fields.
- **ADIF import.** `tools/adif2csv.py` converts an existing ADIF log to CardSat's CSV,
  keeping only satellite QSOs (`PROP_MODE = SAT`) and only the fields CardSat uses, so
  you can seed the on-device log from your main logger.
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
  **Large logs upload automatically in small batches** (6 QSOs each, to keep each `.tq8`
  small) — all **in a single session**, so you enter the LoTW key passphrase **once** for
  the whole run (works for both un-uploaded-only and re-send modes).
  **Editing a logged QSO clears those flags** so the corrected record is re-sent on the next
  upload, and the Edit QSO screen exposes **LoTW** and **Cloudlog** rows you can toggle to
  override that (mark a QSO as already-uploaded after a cosmetic fix, or flip a flag directly).
  See [MANUAL.md → LoTW upload](../MANUAL.md).
- **Cloudlog / Wavelog upload** — **Upload to Cloudlog** on the Log menu sends your
  satellite QSOs to a self-hosted **Cloudlog** (or compatible **Wavelog**) online logbook
  over WiFi, via its JSON QSO API. Set your instance URL, a read-write API key, and your
  numeric station profile ID in Settings. Because a Cloudlog instance can itself forward
  QSOs on to LoTW, this is an alternative to the on-device LoTW upload — the two are
  tracked independently (separate flags) so a QSO sent to one isn't assumed sent to the
  other. Supports re-sending already-uploaded QSOs. Large logs upload in **15-QSO batches**,
  all in one session (fully automatic, since Cloudlog uses an API key
  rather than a passphrase). The API key is treated as a secret and
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
  bandwidth; fixed-size message ring (no SD, no heap growth). **Actionable messages:** a
  message carrying `@lat,lon`, `#SAT`, or `!SAT date time` (plain text, so fully
  interoperable) decodes on ENTER into a **bearing compass** to the sender (with distance,
  bearing and Maidenhead grid), a **satellite detail** with next pass, or a **pre-filled
  sked** — with `p`/`s`/`k` keys to send those for the current satellite and your location.
  Satellite references carry the **NORAD catalog number** (`#name/norad`) so they resolve
  even when two stations' databases use different display names (e.g. RS95S vs QMR-KWT2).
  A **station roster** (`o` key) lists everyone heard reporting a position — callsign,
  grid, distance/bearing, signal and age — and an optional **automatic position reply**
  setting (off by default, loop-guarded) makes CardSats answer a position request with
  their own. Built into the standard binaries (requires the **RadioLib** library at build
  time). ✅ Hardware-verified — two-way messaging confirmed against a LilyGo T-LoRa unit
  running the companion CardSat Pager firmware; mind your band's bandwidth rules. See
  [MANUAL.md](../MANUAL.md).
- **Auto-refresh, power management, and diagnostics.** If WiFi is configured,
  CardSat connects and NTP-syncs at boot and **auto-refreshes GP when the cached
  elements are over a week old**; a **fast update** (`f` on the Update screen) refreshes
  just the elements, AMSAT activity and favorites' transponders, skipping space/terrestrial
  weather; the backlight blanks after a configurable idle
  time (any key wakes it); config + favorites **back up / restore to the SD card**;
  and an **About** screen reports version, storage, GP age, battery, and uptime, with a
  **License & credits** sub-screen (`l`) carrying the no-warranty and hardware disclaimers,
  data-source attributions, and a recommendation to support AMSAT.
- **Six built-in mini-games** — reached with **`z`** from the About screen (Games menu),
  something to do while waiting on a pass and mostly a light nod to real operating: **Zap
  the Sats** (a Space-Invaders homage with satellite "invaders"), **Doppler Lock** (hold a
  marker on a drifting Doppler S-curve), **Catch the Pass** (time a "QSO" as a bird crosses
  the workable window), **Rotor Runner** (an IMU-tilt or arrow-key antenna-slewing game),
  **Morse Meteors** (clear falling letters by keying their Morse, with a real CW sidetone),
  and **Grid Chase** (a Maidenhead grid-square trainer). Game sounds follow the speaker
  volume and can be turned off in Settings. See [MANUAL.md](../MANUAL.md).
- **Fully offline** once GP + transponders are cached. CardSat stores everything in
  a **`/CardSat` folder on the microSD card** by default, falling back to internal
  **LittleFS** if no card is present.
- **Screenshots** — press **`b`** on any screen to save a 24-bit BMP to
  `/CardSat/Screenshots/` on the SD card (handy for documentation).
- **Favorites**, **manual GP / transponder / time entry**, per-satellite
  **calibration**, and a **factory reset**.
