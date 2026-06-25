# CardSat v0.9.31 — Release Notes

**0.9.31** is a point release over 0.9.30. It adds four observer/operator features:
**visual pass predictions**, **decay/reentry watch flags**, **Sun/Moon transit
predictions**, and **per-satellite operating notes**. The smoother manual-tuning
behavior, the selectable beacon/receive-only downlink VFO, and the rig-polling CAT
serial monitor shipped in 0.9.30 and are summarized below for reference.

## Visual pass predictions ("can I see it?")

The pass schedule now flags **visually observable** passes — satellite sunlit,
your sky dark, and the bird high enough — with a yellow `*`, and the pass-detail
screen shows a verdict and the reason ("Visible: YES", or "daylight" / "sat in
shadow" / "too low"). New settings (*Settings -> Station*): **Visible passes** on/off,
a **Sky-dark gate** (civil -6 / nautical -12 / astronomical -18), and a **Visible min
el**. Turns the schedule into a "what can I go outside and watch tonight" tool.

## Decay / reentry watch flags

The satellite list now shows a small coloured **down-arrow** on decaying orbits —
yellow (watch), orange (decaying), red (reentry imminent) — derived from perigee
altitude and the lifetime estimate, and the Orbital-analysis screen gained a
**Perigee** line with the level. An at-a-glance cue to work a bird before it's gone,
or to spot objects nearing reentry. It's an order-of-magnitude estimate from the
elements, not a precise reentry date.

## Sun / Moon transit predictions

A new **transit finder** (press `t` on the Sun/Moon screen) scans the next 48 h for
times the active satellite crosses or closely approaches the **Sun or Moon** from
your location — the ISS-on-the-disc astrophotography event. It runs incrementally
with a progress bar, then lists each event with a countdown, minimum separation,
body elevation, and TRANSIT/conjunction label. It's a point prediction for your exact
site. **Use proper solar filtering for any solar transit.**

## Per-satellite operating notes

Press **`N`** on the Track screen to attach a short **operating note** to a
satellite (active modes, schedule, PL tone, your own reminders). It's keyed by NORAD,
shows on Track (a `*` by the name and the note text), and persists across reboots and
reflashes. A field notebook that travels with each bird.

# From 0.9.30 (shipped previously, for reference)

## Smoother knob tracking (less "fighting the dial")

Manual tuning during Doppler correction is much less likely to fight the operator's
knob. Three changes, informed by how Gpredict and SatPC32 handle the same problem:
the operator-knob-move threshold is now **mode-aware** (about 30 Hz on SSB/CW, 250 Hz
on FM) instead of a too-tight fixed 5 Hz that mistook rig rounding and read-back jitter
for dial moves; the threshold is **floored at the rig's tuning step** so quantization
never reads as a move; and a short **tuning-grace window** holds off Doppler writes for
~400 ms after a detected dial move, so CardSat stops pushing back while you are actively
tuning and resumes correcting once you let go. The tuning-grace
window applies only to the **downlink** write (the leg connected to your knob); the
**uplink keeps following** your new passband point immediately, so it tracks downlink
moves without lag.

## Beacon / receive-only downlink VFO is now selectable

A new **Settings -> Radio -> "Beacon/RX-only DL"** option chooses which VFO carries the
downlink for receive-only entries (beacons, telemetry, SSTV, CW). Previously these were
always forced to MAIN, which overrode your VFO layout and was disruptive when swapping
to a beacon mid-pass. Choose *Follow VFO* (default) to keep the downlink on the same VFO
as a normal transponder, or force *Main* / *Sub*.

## CAT serial monitor now polls the rig

The CAT serial monitor (*Settings -> Radio -> CAT serial monitor*) was passive -- it
only showed traffic that tracking happened to be sending, so opening it without a pass
in progress showed "(no traffic yet)" and never updated. It now **actively polls** the
rig (~once per 700 ms) while open, so you always see live frames -- your read request as
TX and the radio's reply as RX -- which makes it a real bench diagnostic for the CI-V
link with no satellite pass required. Press **`p`** to toggle polling off for a purely
passive view. Polling pauses automatically while Doppler tracking is engaged so the two
don't collide.

## From 0.9.29 (for reference)

**Single-pin CI-V** works end-to-end on hardware (verified on an IC-821): CardSat drives
and receives the full CI-V exchange over one shared open-drain GPIO. Select it in
*Settings -> Radio -> CI-V wiring -> 1-pin G2 / 1-pin G1*; still needs correct 5 V /
3.3 V level interfacing (see `CIV_SINGLE_PIN.md`). The separate TX/RX path remains the
simplest option. The **CAT serial monitor** (*Settings -> Radio -> CAT serial monitor*)
shows live raw CI-V/Yaesu/Kenwood traffic as hex with raw-hex send for diagnostics.

## DX Doppler: 1 kHz dial stepping + passband shown from centre

Two changes to the **DX Doppler table** for linear transponders:

- **Step the anchored dial to round 1 kHz.** In a fixed mode, the `,`/`/` keys now
  move the **anchored dial** to the next **round 1 kHz** (grid-aligned to passband
  centre), so you park your fixed RX or TX on a clean number — 145.949, 145.950,
  145.951 MHz, never 145.9502. Cycle the anchor (`a`) to the dial you care about,
  then step. CardSat nudges the passband so the dial lands exactly on the kHz
  (converging to within a Hz, accounting for the Doppler dial/passband ratio) and
  recomputes the rest of the table around it. In true-rule mode `,`/`/` keep the
  plain 1 kHz passband nudge. (The old 5 kHz `<`/`>` shift and the short-lived `s`
  snap key are removed in favour of this.)
- **Passband shown relative to centre.** The header displays the operating point as
  a signed offset from the **centre of the passband's downlink** — `ctr`, `+7.5k`,
  `-12.5k` — instead of an absolute up-from-bottom figure, so it's obvious how far
  off-centre you're working.

## Sat-to-sat: pick the second satellite before searching

The sat-to-sat visibility finder no longer runs a multi-day search every time you
cycle the second satellite. It now opens on a **pick screen**: `n`/`p` step
through your favorites **instantly**, and the window search only runs when you press
**ENTER** (or `r`). A `p` key was added for stepping backward. This makes choosing
the right second satellite immediate instead of waiting through a full calculation
per cycle. The selected-row highlight on the results list was also fixed — it was
hard to read (black cells punched through the highlight bar, green text invisible on
green); the selected row is now a clean solid bar with black text, matching the
other lists.

## Fix: DX Doppler fixed-uplink / fixed-downlink now actually hold the dial

The **DX Doppler table**'s Fixed-UL and Fixed-DL modes were not holding the
anchored dial constant — e.g. in Fixed-UL the "fixed" uplink frequency still drifted
with Doppler across the window instead of staying put. The cause was that the locked
dial value was recomputed from the live per-step range-rate each step, which made the
computed passband drift collapse to zero. The anchor dial is now **locked once at the
window reference instant** and the satellite-frame operating point is solved per step
so the anchored dial reproduces exactly, with the other three dials (and the
cross-band leg, respecting transponder inversion) following correctly. Verified
against a multi-step host simulation for all four anchor choices (my/DX × RX/TX).

## CW mode on linear transponders

On a linear (SSB/CW) transponder you can now press **`k`** on the **Track** or
**large-font** screen to switch **both legs to CW** and work CW through the bird.
CardSat sets CW on the uplink and downlink; on an inverting transponder the
sideband flips but CW is CW on both ends, so you just zero-beat your own downlink.
The choice is **per channel** — it resets whenever you change satellite or
transponder — shows a `CW` tag on the Track and large-font passband lines, and
applies live for both running CAT tracking and manual tracking (it re-applies the
moment you toggle it). On an FM bird the key is a no-op.

## Continuous LoRa monitoring + message notifications

CardSat already listened for LoRa messages in the background; now it **tells you**
when one arrives while you're on another screen. An **envelope badge with an unread
count** appears in the header on every screen, and a brief **banner** shows the
sender's callsign (with an **opt-in beep**). Opening **Messages** clears the badge.
A new **Msg notify** setting (Settings → Network/data) selects *off* / *banner*
(default) / *banner+beep*. In the Charge / Sleep screen the badge updates silently
with no banner or beep, keeping that mode minimal.

## Charge / Sleep screen (Launcher-style low-power mode)

New **Charge / Sleep** item at the bottom of the home menu. It parks CardSat in a
minimal low-power state for charging: the backlight blanks and tracking output
stops, so almost nothing runs. **Any key wakes the screen** to show battery status
(a large gauge, charging state, and cell voltage), then it auto-blanks again after
~5 seconds. **ESC/back** returns to the home menu.

- **More accurate battery %**: the screen derives charge from the cell **voltage**
  against a LiPo discharge curve (3.30 V = 0 % … 4.20 V = 100 %), which tracks the
  flat mid-discharge region far better than the PMIC's coarse linear level. Falls
  back to the raw fuel-gauge level if voltage is unavailable. Uses the Cardputer
  ADV PMIC's `isCharging()` for charge state.
- **On-demand heap de-fragmentation** (press **H** in charge mode): over a long
  session, repeated TLS handshakes can fragment the no-PSRAM heap until the largest
  contiguous block is too small for a new TLS context, even when total free heap
  looks fine — and HTTPS fetches start failing. The heap reset drops and reconnects
  WiFi/TLS (`net.hardResetWifi()`), freeing and coalescing those transient
  allocations, and reports the before/after largest block.

## Scope documents (design only — no code)

Five forward-looking scope/design documents were added to the repo:
- **SAME_BAND_DOPPLER_SCOPE.md** — same-band / half-duplex (PTT-switched) Doppler
  for ISS-packet-style and in-band birds (SatPC32ISS / Gpredict Simplex TRX review).
- **CW_MODE_SCOPE.md** — CW-on-both-legs override for linear transponders.
- **HALFDUPLEX_RADIOS_SCOPE.md** — adding half-duplex split-VFO support for more
  all-mode V/U rigs (IC-705 over LAN in scope; USB-only rigs out of scope).
- **CARDPUTER_ZERO_PORT_SCOPE.md** — porting CardSat to the Linux-based Cardputer
  Zero (USB-host CAT, multi-radio, Hamlib).
- **LORA_MONITOR_SCOPE.md** — continuous LoRa monitoring with arrival notifications.

## Automatic MAIN/SUB band assignment (IC-9100 / IC-9700 / IC-910)

When radio control is turned on for a two-way transponder, CardSat now puts the
correct band on each VFO automatically on the rigs that support it over CAT.

- **IC-9100 / IC-9700** — uses CI-V `07 D2 00/01 <bandcode>` (01=144, 02=430,
  03=1.2 GHz) to set MAIN and SUB band selection directly.
- **IC-910** — has no `07 D2`; instead CardSat reads MAIN's frequency (`07 D1`
  then `03`) and, if MAIN is on the wrong band, issues one **swap (`07 D0`)**.
  Because the 910's MAIN/SUB can never share a band, that single check fixes both
  legs. This mirrors how Hamlib drives the 910.

The uplink/downlink land on the right bands per the VFO-type setting, so you no
longer pre-arrange MAIN/SUB on these radios. Fired **once at engage** (never per
tick); a leg outside 2 m/70 cm/23 cm is skipped; no-op on every other radio.

**Also fixes a latent IC-910 profile bug:** its MAIN-select byte was set to
`07 D0` (which is actually the *swap* command), so band addressing could be
inverted. Corrected to `selMain = 07 D1` (Select MAIN VFO), confirmed from the
IC-910 command table and a live Hamlib/gpredict trace.

> ⚠️ **Untested on hardware.** The author runs an IC-821, which has none of these
> commands, so this whole path is host-verified only. The IC-910 in particular has
> a non-standard MAIN/SUB CAT model; a 910/9100/9700 owner should confirm and
> report back via a GitHub issue with a serial trace.

## Per-radio CAT capability audit + IC-820/821 sat-mode fix

Audited what each supported radio can control over CAT versus what the operator
must set up by hand, cross-checked against the radios' own command tables, Hamlib,
OscarWatch, SatPC32, and Gpredict. Outcomes:

- **IC-820 and IC-821 `hasSatMode` → false.** Their CI-V satellite-mode command is
  a no-op on real hardware (confirmed on an IC-821), so CardSat no longer pretends
  to toggle it. On these rigs — and the IC-970 — satellite mode, the band pair, the
  MAIN/SUB assignment, and any uplink PL tone are **manual front-panel** operations;
  CAT only Doppler-tunes within that layout. Their `0x07 D0/D1` bytes are band
  *access*, not a MAIN/SUB assignment.
- **New [RADIO_SETTINGS.md](RADIO_SETTINGS.md)** — a per-radio settings and
  CAT-vs-manual capability reference covering all ten radios (recommended baud,
  read-back, sat-mode, tone, and what must be set up on the radio), with the
  cross-references to how Hamlib / OscarWatch / SatPC32 / Gpredict treat each rig.
- **Doc consistency pass** — MANUAL.md and README.md updated to match; the README's
  Icom-LAN claim corrected to **IC-9700 only** (IC-705/7610/785x are not supported).

> Only the IC-821 is bench-verified by the author. Feedback from users with other
> radios is welcome — a serial trace in a GitHub issue is the most useful form.

## CAT self-test (Settings → Radio / CAT)

A new **Run CAT self-test** action exercises every CAT function the active backend
supports and reports a PASS/FAIL/INFO line for each — both on a scrollable results
screen and echoed to the serial monitor (115200, `[CAT-TEST]` prefix) alongside the
existing `[CI-V TX]`/`[CI-V RX]` frames, so a failure sits right next to the NAK or
no-reply that caused it. It checks downlink/uplink frequency set (with read-back
verify where supported), mode set on both legs, MAIN/SUB band select, satellite mode
toggle, PTT/transmit-state read, and the CTCSS encoder.

It is deliberately **non-destructive**: it never keys the transmitter (PTT is only
*read*), it saves the dial up front and restores both legs when done, leaves
satellite mode and the CTCSS encoder **off**, leaves band access on the downlink, and
re-syncs the Doppler send-guards so normal tracking resumes cleanly. If CAT isn't
engaged it shows a short "Radio not ready" message instead.

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
