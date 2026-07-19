# Feature comparison: CardSat vs SatObserver-MX (0.9.60 audit)

Reviewed against https://github.com/exoplanet5/SatObserver-MX (README, July 2026):
a macOS/Windows desktop visualization app — Python stdlib backend, satellite.js
SGP4 + three.js frontend, packaged native. Different platform class (mouse, GPU,
gigabytes) and different center of gravity: SatObserver-MX is a **visualization
instrument**; CardSat is an **operating instrument** that also visualizes. The
useful comparison is therefore three lists, not a scoreboard.

## Convergent choices (independent agreement, worth noting)

- **JSON/OMM-first with synthesized TLE lines.** Both fetch GP as JSON with
  integer `NORAD_CAT_ID` and render TLE text only to feed an SGP4 core.
  Their README highlights 6-digit catalog support with Alpha-5 encoding;
  CardSat's `encCatalog()` already implements Alpha-5 (A–Z, I/O skipped,
  100000–339999) with a documented low-5 fallback beyond — **parity**, verified
  in source this audit.
- **CelesTrak 2 h courtesy caching, stale cache served offline.** Byte-for-byte
  the same policy CardSat adopted in 0.9.59 (persisted across reboots on our
  side).
- **Whole-catalog search.** Theirs via Space-Track credentials; CardSat's
  0.9.59 `/` search covers all of CelesTrak by name/CATNR without an account.
- **Optical pass visibility.** Their ●/☼/✕ pass flags = CardSat's visible-pass
  list, sky-dark gate, and per-pass `*` (plus our 10-day illumination view).

## SatObserver-MX features CardSat lacks — with verdicts

**Worth adopting (idea list):**
- **Star layer for the sky dome** — they draw ~1000 stars to mag 4.6 plus
  constellation lines/names, live from the clock. A compact RA/dec/mag table is
  a few KB of flash rodata; on the Sky Sources dome it would make night-time
  antenna alignment genuinely easier. *Best single steal.* **Adopted in 0.9.60.**
- **Named ground-station presets.** They keep multiple stations and switch the
  active one. CardSat has QTH + GPS + rove-plan grids; a small named-QTH list
  (settings-backed) switching `loc` would serve multi-site operators cheaply. **Adopted in 0.9.60.**
- **Space-Track as a credentialed source** (NORAD/INTLDES/name, batch refresh).
  Real value for analyst objects; needs their login flow, ToS-compliant rate
  care, and credential storage. Medium effort — idea list, not a promise.
- **SATCAT enrichment** (launch date & site) in the satellite detail screen.
  One cached lookup; low weight.
- **App-wide time-travel clock.** Their master clock repropagates *everything*
  at −1000×…+1000× and passes are click-to-jump. CardSat's Sim screen
  time-travels one satellite; extending sim time app-wide is architecturally
  heavier (every screen reads `nowUtc()`) — noted, unranked.
- **Mike McCants classfd/inttles ingestion** — niche classified-TLE archives;
  zip handling on-device is the cost. Low priority.

**Platform-inappropriate here (deliberate non-goals):**
- Textured Blue Marble / night-lights globe, terrain 2D map: on a no-PSRAM
  ESP32-S3 at 240×135, CardSat's wireframe globe + vector map are the right
  rendering, not a lesser one.
- Floating windows, mouse multi-select, per-family colors/batch toggles:
  desktop-UI idioms; favorites + per-screen views are the handheld equivalents.
- Nadir "FS" camera ride: charming; the 3D globe's follow mode covers the need.

**Small honest gap found while checking ourselves:** the world map draws the
sub-solar point but not a sub-lunar one. Trivial add; idea list.

## CardSat features SatObserver-MX lacks

The entire operating half: CAT Doppler control (10 radio profiles, 4 transports
incl. USB), 7 rotator protocols on 3 wires, transponder DB + passband planner,
QSO logging with LoTW/Cloudlog upload, AMSAT status + hams.at + reporting, LoRa
messaging (incl. GP-over-LoRa), voice memos, printing/reports, Tiny BASIC + 54
tools, EME suite, space & terrestrial weather, AOS alarms + deep sleep, seven
games — pocketable and offline-first. Also higher-orbit pass finding is
first-class here (0.9.59 scan finder, Skyfield-verified); their pass engine is
satellite.js over a browser clock, unstated for GEO/HEO edge cases.

*Verdict: no urgent gaps; one small bug-class finding (sub-lunar point absent),
one strong idea (star layer), several ranked ideas above.*
