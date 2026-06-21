# CardSat v0.9.24 — Release Notes

## Icom CI-V tracking, audited against OscarWatch

Two changes to bring the Icom MAIN/SUB control in line with how the known-good
OscarWatch tracker drives these rigs (especially helpful on slow single-wire rigs
like the IC-821):

- **Uplink write is deferred one tick after a downlink write or knob move.** After
  CardSat writes the downlink (or the operator moves the receive dial), the uplink
  write now waits one Doppler tick so the SUB read and the receive dial settle
  before the bus is used for the MAIN uplink. During a fast Doppler slew the uplink
  still services every other tick, so it never starves.

- **Receive-only birds turn satellite mode off and tune the downlink on MAIN.** A
  transponder with no uplink (beacon, telemetry, SSTV, CW) now commands the rig's
  satellite mode **off** and routes the downlink to the **MAIN** band regardless of
  the VFO Type setting. This matches OscarWatch and the SDR-Control apps, and on the
  IC-821 the MAIN band reads back far more reliably than SUB.

## Sat-to-sat window search no longer stalls

The "both satellites visible" finder used to compute its multi-day search in one
long blocking pass, which could starve the system and appear to hang (or never
finish) on hardware. It now runs as an **incremental background job**: it samples
a few hundred points per loop tick, shows a **live progress bar (0→100 %)**, keeps
the back key responsive throughout, and always completes. The results are
identical to the old one-shot search.

## Web sky plot shows the next pass when the satellite is below the horizon

When the active satellite is below your horizon, the web control page's polar plot
now draws the **next pass as an arc** (azimuth/elevation across the upcoming pass)
instead of an empty plot, so you can see where the satellite will rise and set. The
moment it comes above the horizon the live position dot takes over and the arc
clears. The `/api/passes` response now also includes an `arc` array for this.

## Fix

- **IC-821 CI-V read reliability.** Reworked the Icom CI-V frequency-read flow for
  the IC-821, whose SUB band (the satellite downlink) often won't answer the read
  command. CardSat now re-selects the band immediately before each read and, when
  no valid reply arrives, falls back to the last frequency it commanded instead of
  failing — so the downlink keeps Doppler-tracking and a bad read is never mistaken
  for an operator knob move. Transponder mode-setting was reordered to set the
  uplink (MAIN) leg first and leave the radio on the downlink (SUB) band, which is
  where the read happens. The `0x1C 0x00` poll (read transmit/PTT status, used to
  pause knob-follow while transmitting) and the band-select model are documented;
  rigs that don't support the status poll are detected and it is dropped.

- **DX Doppler starts at the centre of the linear passband.** The Doppler table
  now opens with the operating point in the middle of the selected linear
  transponder's passband (and re-centres when you switch transponders with `t`),
  matching how the on-device tracker centres a linear bird, instead of starting at
  the low edge. FM and single-channel transponders are unaffected.

## Mobile web control improvements

The web control page (Settings → Network/data → Web control) gained:

- a **live sky plot** — a polar plot with a moving dot at the satellite's current
  azimuth/elevation;
- an **in-pass indicator and AOS countdown** in the header ("IN PASS — LOS in
  m:ss" / "Next AOS in m:ss");
- **transponder selection** from a labelled drop-down (mirrors the `t` key);
- **direct calibration entry** — type exact RX/TX offsets in Hz and Set/Zero them
  (saved per-satellite);
- a **filter box** to narrow a long satellite list;
- a **responsive layout** that uses one column on phones and two columns on
  tablets/computers.

Internally, all satellite and transponder names served as JSON are now escaped, so
a name containing a quote or backslash can no longer break the page. Two small
endpoints were added (`/api/tx` for the transponder list/select, `/api/cal` for
calibration). As with the rest of the LAN servers, the web page is HTTP on the
local network with no authentication — use it only on trusted networks.

## Changes

- **OSCARLOCATOR opens in polar view** by default (press `m` for the QTH-centred
  view); the polar sheet auto-selects N/S and flips at the equator.
- **Sat-to-sat finder shows a "Calculating windows…" status** while it searches,
  instead of appearing to pause during the computation.
- **LoRa region presets.** A new **LoRa region** setting (Settings → Network/data)
  seeds a legal amateur frequency for your area: **US** → 33 cm band, 906.875 MHz
  (default); **EU** → 70 cm band, 433.775 MHz; **Japan** → 430 MHz band,
  431.000 MHz — all at 125 kHz. The default region is now **US (906.875 MHz)**
  instead of 433.775 MHz. You can still set any carrier 150–960 MHz by hand; the
  region just provides a sensible, in-band starting point. You remain responsible
  for operating within your licence and local rules.

## Fixes from device testing

- **Sat-to-sat finder no longer freezes.** The visibility search was calling the
  SGP4 element re-initialiser twice per 30-second step over a 5-day window
  (~29,000 full re-inits), which blocked long enough to trip the task watchdog and
  freeze the unit. It now samples each satellite once across the window into a
  compact in-RAM track (two re-inits total), yields periodically, and scans those
  tracks for overlaps — fast and watchdog-safe.
- **DX Doppler transponder selection works, and the table is readable.** The DX
  Doppler screen now has a **`t`** key to cycle the transponder directly, and each
  30-second step is shown on two lines — your dials ("me", green) and the DX
  station's ("DX", cyan), each with RX and TX — so the four frequencies no longer
  run into each other. Mode changes (true rule / fixed DL / fixed UL) are easier to
  read against the clearer layout.
- **OSCARLOCATOR and Sky-sources plots clear the header.** The polar/azimuthal
  discs and their compass labels were drawn far enough up that the disc top and the
  "N" label overlapped the title bar. The OSCARLOCATOR disc is now centred lower,
  and the shared polar-grid compass labels sit just inside the disc edge.
- **LoRa Messages status bar clears the header.** The frequency / SF / bandwidth
  line was flush against the title bar; it (and the divider and message list) now
  start below it.
- **SD card stays accessible after using LoRa.** The Cap LoRa SX1262 and the
  microSD card share one SPI bus (SCK 40 / MISO 39 / MOSI 14, different chip
  selects). LoRa bring-up was reconfiguring the bus in a way that left the SD
  driver unable to use it. LoRa now shares the existing bus on the SD pins and
  keeps the SD chip-select idle, so the card remains reachable.

This release adds three significant new satellite views — an **OSCARLOCATOR**
azimuthal plot, a **3D globe**, and an on-device **voice-memo browser** — plus a
round of documentation accuracy work across the README and Manual.

## New: LoRa text messaging (Home → Messages)

CardSat-to-CardSat **broadcast group chat** over the M5Stack **Cap LoRa (SX1262)**
module. Every CardSat tuned to the same frequency, spreading factor and bandwidth
sees every message — no addressing or routing, which keeps it simple for a club
net, a SOTA/portable group, or any outing where there's no cell or internet.

- **Selectable band** — the SX1262 (and the unfiltered Cap LoRa front-end) covers
  150–960 MHz, so the frequency is fully selectable (default **433.775 MHz**):
  arrow-adjust in 100 kHz steps or type an exact MHz value. **Spreading factor** is
  selectable 7–12 (default **12** for maximum range/sensitivity), and **bandwidth**
  is 62.5 / 125 / 250 kHz. Both ends must match to hear each other.
- **Messaging screen** — `n` writes a message on the full keyboard and **ENTER**
  sends; `;`/`.` scroll history. Your messages show in cyan as `>me`, others in
  green by callsign. The status line shows frequency / SF / bandwidth / your call.
- **Low-footprint by design** — fixed character buffers and a fixed 24-message ring
  (no `String` in the radio path, no heap growth, no SD card). History is lost on
  reboot.
- **Settings** (Network/data): LoRa msg on/off, frequency, SF, bandwidth, TX power
  (0–22 dBm). Set your callsign first (Station settings / QRZ screen).

> ⚠️ **Untested hardware path, and needs an extra library.** The radio send/receive
> is written to the M5Stack Cap LoRa reference and the SX1262 datasheet but has not
> been confirmed on a device. It requires the **RadioLib** library (Arduino Library
> Manager) and a build with **`CARDSAT_HAS_LORA=1`**; without that the firmware
> still compiles and the Messages screen reports the radio is off. Verify two units
> talk before relying on it, and mind your band's rules — 433.775 MHz at 125 kHz is
> the worldwide default but exceeds the US 70 cm 100 kHz occupied-bandwidth limit,
> where 915 MHz ISM is the cleaner path. The protocol framing and the message ring
> are host-verified; the radio layer is not.

## New: OSCARLOCATOR view (Satellites → `k`)

A live **azimuthal-equidistant** plotting board — the classic OSCARLOCATOR — that
shows the satellite's sub-point and footprint on the Earth in real time. It's the
graphical companion to the tabular **EQX table** (`e`).

- Two projection modes, toggled with **`m`**: a **QTH-centred** view (your station
  at the centre, with the satellite at its true bearing and great-circle distance)
  and a **polar** view (a pole at the centre). In polar mode CardSat picks the
  North or South sheet automatically and **flips between them live** as the bird
  crosses the equator, so the satellite always stays on the visible chart.
- Draws a coarse coastline, a lat/lon graticule, the satellite marker (yellow when
  sunlit, cyan in eclipse), and the satellite's **ground footprint**.
- A dashed amber **QTH range ring** marks the footprint radius at the satellite's
  mean altitude, centred on your station — when the sub-point reaches inside it,
  you have a workable pass.
- The **full ground-track arc** (one orbit's worth of sub-points) is drawn across
  the disc in blue, like a real OSCARLOCATOR; green/orange markers show where the
  current pass's AOS and LOS fall along it, with a travel-direction arrow.

## New: Live GPS position (Location → `v`)

A full-precision position readout off the **Location** screen, for rovers, portable
ops, and grid-line activations. Shows **latitude and longitude in
degrees-minutes-seconds** to 0.001″ and in decimal degrees, **altitude**,
**Maidenhead grid** (recomputed live as you move), and — from a live fix — **ground
speed** (km/h and knots) and **course over ground** (degrees true with a cardinal
label), plus a fix/satellite-count/HDOP status line.

## New: Sat-to-sat visibility finder (Satellites → `2`)

Finds the windows over the next five days when the selected satellite **and a second
satellite** (chosen from your favorites) are **both above your horizon at the same
time** — for cross-satellite relay experiments or back-to-back working on one
outing. Each row shows the window's start (UTC), duration, and the peak elevation of
each bird. **`n`** cycles the second satellite; **`r`** recomputes.

## New: Jump to beacon (`n` on Track / Big readout)

One key on the **Track** and large-font readout screens tunes straight to the
satellite's **beacon** — its downlink-only/telemetry entry (preferring one whose
description names a beacon) — with Doppler correction, for finding a bird by its
beacon before working it. If the satellite has no beacon listed, a status note says
so.

## New: DX Doppler table (Mutual windows → `d`)

From a mutual-visibility window, **`d`** opens a table of the **RX and TX dial
frequencies for both your station and the DX station, every 30 seconds across the
window**, for the transponder you've selected (with `t` on Satellites). Because
each station's Doppler differs, their dials differ — this lays out exactly where
both operators should tune to stay on the same channel through a short pass.

- Three modes (**`m`**): **true rule** (operating point fixed in the satellite
  passband, every dial Doppler-tracks), **fixed downlink**, and **fixed uplink**
  (the anchor station holds one dial constant in real RF and the other three
  follow).
- **`a`** cycles the anchor dial (my RX/TX, DX RX/TX); for a **linear**
  transponder, **`,`/`/`** (and `<`/`>`) move the passband operating point.
- **`;`/`.`** scroll the 30-second steps. RX is shown in green, TX in yellow.

Built for short windows and manual tuning of coordinated DX contacts.

## New: Sky sources plot (Sun/Moon → `s`)

A secondary plot off the **Sun / Moon** screen (**`s`**) showing the classical
**planets** and the strongest **cosmic radio sources** on a sky dome (zenith
centre, North up, elevation = radius) — for antenna pointing and as an RF-source
reference.

- Planets **Mercury, Venus, Mars, Jupiter, Saturn** are computed live (cyan dots);
  fixed radio sources **Cassiopeia A, Cygnus A, the galactic centre (Sgr A\*), the
  Crab nebula (Tau A), and Virgo A (M87)** are drawn as orange crosses, plus a few
  bright stars for orientation.
- Objects below the horizon sit just outside the rim in grey so their bearing is
  still readable. **`;`/`.`** step through the objects; the readout gives the
  selected object's az/el, above/below-horizon status, and type.

Positions use the same low-precision ephemeris as the Moon view — far finer than
any amateur antenna beamwidth.

## New: 3D Globe (Satellites → `3`)

An orthographic 3-D wireframe Earth that **auto-follows the selected satellite**,
rotating so its sub-point stays centred. Only the near hemisphere is drawn, so the
far side of the Earth is hidden behind the curve.

- Graticule and coastline (clipped at the limb), a yellow **day/night terminator**,
  and your QTH as a white cross.
- **All favourites** plotted as dim-green dots; the **selected satellite** centred
  and larger (sunlit/eclipse colour) with its **ground footprint** drawn around it
  (the footprint wraps around the limb naturally as the bird nears the horizon).
- A blue **ground-track trail** — a full orbit of the selected satellite's
  sub-points projected onto the globe.
- An optional **second (DX) location** entered by Maidenhead grid (**`g`**): drawn
  as an orange marker with its own footprint ring, so the overlap with the
  satellite's footprint shows the live mutual-visibility region. **`G`** clears it.
- **Arrow keys** turn the globe by hand (free-look); **ENTER** re-snaps to
  auto-follow.

## New: Voice Memos browser (Log → Voice Memos)

Voice memos can now be reviewed and managed on the device, not just copied off the
SD card on a computer:

- **Browse** every memo on the card, **newest first**, showing each memo's date,
  time, the **satellite** it was recorded on, and its length.
- **Play back** a memo through the speaker (**ENTER**); any key stops it.
- **Record a standalone memo** (**`n`**) that isn't tied to a satellite — handy for
  a quick note when you're not tracking. Any key stops and saves.
- **Delete** a memo (**`d`**, with a confirm prompt); **`r`** refreshes the list.

The browser lives under the **Log** menu (Home → Log → Voice Memos). Recording
while tracking still works exactly as before with **`v`** on the Track-family
screens. Memos are now named with the tracked satellite as well as the UTC
timestamp, e.g. `memo_20260617_203145_AO-91.wav`; standalone memos omit the tag.

> **Voice memo recording on the Cardputer ADV requires an ESP-IDF 5.4.x build**
> (Espressif esp32 Arduino core 3.2.x). See MANUAL.md section "Voice memo".

## Documentation

- README **Highlights** now cover the OSCARLOCATOR view, the 3D globe, voice memos
  and the memo browser, the **IR pass beacon**, **Easycomm I/II/III** and **SPID**
  rotator protocols, **world-map recenter** (`c`), and the Update screen's **fast
  update** (`f`) — several of which had shipped without a README mention.
- The Manual gained full sections for the OSCARLOCATOR view, the 3D globe, and the
  voice-memo browser (in section 8 and the screen-by-screen reference), and its
  help-screen and key tables were audited against the code.
- The cheat card and on-device help screen were updated for all of the above. The
  printable cheat card has also moved from a 3×5 to a roomier **4×6** index-card
  format (still front/back, two pages) — larger, more legible type with space for
  the new screens.

## Verification status

The voice-memo recorder and the ES8311 microphone bring-up are confirmed working on
hardware. The **OSCARLOCATOR view, the 3D globe, and voice-memo playback** are new
drawing/audio code paths that have been verified on the host and against faithful
pixel-accurate mockups, but are still being confirmed on-device — please report
anything that looks off on your hardware via the project's issue tracker.

## Notes

- Carries forward everything in 0.9.22 (working ADV voice-memo recording; the 8 bpp
  palette canvas that keeps downloads from freezing the screen on the 5.4.x
  toolchain).
