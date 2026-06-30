# CardSat v0.9.40 — release notes

Interface improvements (an out-of-passband warning when tuning a linear transponder, and
proper line-wrapping for received LoRa messages), several new operating aids (a point-here
arrow for hand-aiming, a "what's overhead now" screen, sked reminders from activations,
aurora context, and visual-pass rise directions), on-device reference screens, a logging fix
so edited QSOs are re-uploaded, and a guide to using your own hand-curated GP data offline.

## Tuning

- **Out-of-passband warning.** On a linear transponder in full-Doppler (One True Rule)
  tuning, if you turn the radio's knob **past either edge of the transponder passband**, a
  flashing red **"OUT OF PASSBAND"** banner appears at the bottom of the Track-family
  screens while CardSat pulls the downlink back to the passband edge. The banner names
  which edge you overran (low or high) and clears as soon as you tune back inside. Before,
  CardSat silently clamped you to the edge with no indication you'd run off the band.

## Messaging

- **Received messages now wrap to a second line.** A LoRa message too long to fit on one
  line (after the sender's callsign prefix) now continues onto a second, indented line
  instead of being cut off at the right edge. Scrolling accounts for the taller messages,
  and the newest message stays anchored at the bottom of the list.

## Help & About

- **On-device glossary, user guide, satellite history, tech help, learn, and license/credits
  screens.** The Help screen (`h` anywhere) now links to five new references: **`g`** opens a
  **Glossary & math** screen (definitions of AOS/LOS/TCA/grid/footprint and the orbital +
  Doppler math), **`m`** opens a concise **User guide** (setup, working a pass, CAT/rotator,
  logging, the analysis screens), **`s`** opens a **Ham satellite history** (the OSCAR program
  from 1961, the Phase 2/3/4 orbit classes, QO-100, CubeSats/ISS, how transponders work, and
  why Doppler matters), **`t`** opens a **Tech help** guide (portable antennas with the Arrow
  Yagi as the top pick, feedline and polarization tips, how to point at and work a satellite,
  and getting CardSat's interfaces, logging and uploads going), and **`l`** opens a **Learn**
  screen — an in-depth explainer of the radio and orbital theory behind satellite operating
  (Newton/Kepler, the orbit classes, link budget, antenna gain, the physics of Doppler) and
  how amateur satellites work internally (transponders, FM repeaters, beacons, store-and-
  forward, power/attitude, satellite classes). The About screen now offers **`l`** for a
  **License & credits** screen with the no-warranty and hardware disclaimers, credit for the
  outside data sources (Celestrak/SpaceTrack, NOAA, Open-Meteo, hams.at, SatNOGS/AMSAT), and a
  recommendation to support AMSAT. All scroll with `;`/`.` and are formatted for the Cardputer
  screen.

## Operating aids

- **Workable-grids filter (`f`).** The workable-grids screen now takes a **prefix filter**:
  press `f` and type a partial grid to narrow the list to matching squares — `EM` shows every
  workable EM grid, `EM2` narrows to EM2x, `EM21` shows just that grid if it's workable. The
  filter is entered upper-case (the standard grid capitalization rule; lower case is accepted
  and converted) and validated to well-formed Maidenhead characters; the count line shows the
  active filter (e.g. `EM2: 9 of 1370`) and `c` clears it. Handy on high-orbit birds whose
  footprint floods thousands of grids — you can jump straight to the ones you need.
- **Point-here arrow (Track → `a`).** A new glanceable screen for hand-aiming a portable
  antenna: a large compass arrow points to the satellite's azimuth and an elevation bar
  shows how high to raise the antenna, alongside the numeric az/elevation/range and the rise
  compass direction. The arrow is green above the horizon and dim below; the radio and
  rotator keep tracking underneath while it's open.
- **"Overhead now" screen (Home menu).** Scans the entire loaded catalog for every satellite
  **above the horizon at this instant** and lists them sorted by elevation, with azimuth and
  rise compass direction (high passes in green, near-horizon in yellow) and a count of how
  many are up out of how many were scanned. `r` rescans for the current moment.
- **Sked reminders from activations.** From an activation's detail view on the **Activations**
  screen, **`a`** sets a sked reminder for it — a T-60/30/10 countdown of beeps and a "SKED!"
  flash at the scheduled start time, independent of the favorites AOS alarm, so a planned
  contact isn't missed even when that bird isn't one of your starred favorites. `c` on the
  list clears a pending sked.
- **Manual activation / sked entry.** You can now enter your own activations or personal
  skeds on the **Activations** screen (`n` to add, `e` to edit your own) in the same format
  as the hams.at feed — for operations that aren't posted to that site. Entries are stored
  separately and merged into the list alongside fetched ones (marked with a leading `*`),
  survive feed refreshes and reboots, work fully offline, and can carry sked reminders like
  any other entry.
- **Aurora context on Space Wx.** The space-weather screen adds an aurora-likelihood line
  derived from the Kp index (unlikely / possible / likely, with latitude), color-graded
  alongside the existing flux and geomagnetic readouts.
- **Visual-pass rise direction.** The visible-pass list (`V` from Passes) now shows the
  **rise compass direction** — where on the horizon to look as the satellite comes up — in
  place of the LOS time, which is the more useful number for spotting a pass.

## Documentation

- **Offline / SD-card GP data guide.** A new guide explains how to curate your own orbital
  elements by hand and load them from `/CardSat/gp.json` on the microSD card instead of the
  online update — the OMM/JSON field format, a worked example, and a step-by-step checklist —
  for field use with no WiFi or for pinning a fixed, known-good set. See
  **docs/guides/OFFLINE_GP_DATA.md** and the new subsection in **MANUAL.md** §15.
- **Desktop feature-gap scope.** A new design document surveys the features in the major
  desktop trackers (SatPC32, Nova for Windows, Gpredict, Orbitron, SkyRoof), identifies what
  CardSat doesn't yet do, and scopes each gap for feasibility on the Cardputer versus a future
  port — separating realistic additions (award progress, dupe-checking, an external az/el/Doppler
  feed, voice announcements, a day/night terminator) from the SDR/DSP-class features that need
  more capable hardware. See **docs/design/DESKTOP_FEATURE_GAPS_SCOPE.md**.

## Logging
- **Editing a QSO re-arms its upload, and the upload flags are now editable.** If you edit a
  logged QSO that was already uploaded to LoTW and/or CloudLog, the corrected record is now
  marked as not-yet-uploaded so it is re-sent on the next upload (previously the edit kept the
  uploaded flags, so a correction never reached the services). The Edit QSO screen now also
  shows two rows — **LoTW** and **Cloudlog** — for an existing QSO; press ENTER on either to
  toggle whether that QSO is considered uploaded. So you can let an edit re-arm the upload (the
  default), or, after a cosmetic fix, mark the QSO back as already-uploaded so it isn't re-sent
  — or just flip the flags directly without changing anything else.
- **Callsign and grid entry is consistently upper-cased.** The worked-station callsign and
  grid on the Log/Edit QSO screen and your own station callsign in Settings are now stored
  upper-case to match the rest of the app (DX grid, QRZ lookup, IOTA, and the new sked-entry
  fields already did this). This keeps logged records and the ADIF/LoTW/Cloudlog exports
  (CALL, GRIDSQUARE, STATION_CALLSIGN) in the conventional upper case regardless of how they
  were typed.

## Extras

- **Zap the Sats.** A tiny built-in game, tucked behind `z` on the About screen, for
  when you're waiting on a pass: a Space-Invaders homage where the marching invaders are
  satellite sprites and your gun is a ham operator with an arrow antenna firing signals
  upward. `,`/`/` move, space fires, ENTER starts; clear a wave to level up. It uses only
  fixed in-RAM state and a few KB of code — no heap allocation, no effect on tracking,
  CAT, or anything else.

## Power & fixes

- **Deeper low-power Charge / Sleep mode.** Parking on the Charge / Sleep screen now
  does more than blank the backlight: it **powers the WiFi radio fully off** and **drops
  the CPU from 240 MHz to 80 MHz**, and the main loop skips all network and radio
  services while parked — so the device draws far less while charging or sitting idle.
  A keypress still wakes it instantly (the keyboard is always polled), and exiting with
  ESC/back restores full speed and reconnects WiFi. The favorites AOS alarm keeps running
  while parked, so pass-countdown beeps still fire on the charger.
- **Point-here arrow layout fixed.** The arrow screen's bottom status line no longer
  overlaps the footer, and the elevation meter no longer runs into the range readout —
  the compass, bar, and numeric column were re-spaced to fit the 240×135 screen cleanly.
- **Source consistency restored.** Several screens had drifted between the two source
  representations; they've been reconciled so the built firmware matches the maintained
  source exactly. (This was the underlying cause of the arrow-layout issue above.)

## Awards, timeline & map

- **Awards tracking (Log → Awards).** A new summary reads your QSO log and tallies
  total QSOs, unique grid squares, distinct satellites, unique US states (of 51) and
  unique DXCC entities, with **scrollable lists of the actual worked grids, states and
  entities** (`g`/`s`/`d`, with paging) and a per-satellite drill-down that scopes the
  same lists and counts to one bird. States and DXCC are **derived from each QSO's grid
  square** by the same point-in-polygon machinery the workable-states and workable-DXCC
  footprint screens use — they are not stored per QSO — so a contact logged without a
  grid still counts toward the QSO/satellite totals but can't be placed in a state or
  entity. The QSO, grid and satellite counts are exact.
- **Sky-at-a-glance timeline (Next Passes → `t`).** A horizontal timeline of the next
  few hours for all favorites: time runs left to right with a "now" marker, one row per
  favorite, and a bar per pass coloured by peak elevation (green ≥ 30°, yellow below,
  matching the Overhead-now convention; a white tick marks an optically visible pass).
  The fastest way to see which birds are coming up and where they overlap.
- **Day/night shading on the world map.** The world map now shades the night
  hemisphere a dim grey, computed live from the sub-solar point, so you can see at a
  glance which footprints — and which part of your own sky — are in darkness.
- **ADIF import (`tools/adif2csv.py`).** A new converter turns a standard ADIF export
  into CardSat's on-device CSV, keeping only satellite QSOs (`PROP_MODE = SAT`) and only
  the fields CardSat uses, so you can seed the device log from your main logger. Calls
  and grids are upper-cased and frequencies converted to Hz to match CardSat's format.

## Notes

- No orbital-engine or CAT-protocol changes; existing station settings, logs, and cached
  data carry over untouched.
