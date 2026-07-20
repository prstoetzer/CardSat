# CardSat v0.9.61 — release notes

This release turns CardSat into a genuinely terrestrial-and-space-weather-aware
station, not just a satellite tracker. The space-weather suite grows from two indices
to a full operating picture (X-ray flares, solar wind, sunspots, HF/VHF outlook, and a
3-day forecast — all of it printable and readable from BASIC); a family of VHF/UHF/
microwave path tools lands for terrestrial weak-signal work; the EME suite gains the
analysis quantities dedicated moonbounce software provides and a 90-day planner; and the
Weather screen grows a full outdoor "field conditions" page with independently
selectable units. Under it all, the sixty offline Tools are reorganized into six
navigable categories, several fixes came back from the first hardware pass, and a
careful audit surfaced useful data that was already being fetched but never shown.

# New features

## Space weather & propagation, greatly expanded

The space-weather suite goes from two indices to a full operating picture, and it all
prints. Three new NOAA SWPC feeds are fetched alongside F10.7 and Kp: **GOES X-ray flare
class** (the real-time flare / HF-blackout indicator — an X-class flare shows "SEVERE -
HF blackout"), **real-time solar wind** (**IMF Bz** and speed — Bz turning south is the
leading storm indicator, hours ahead of Kp), and the **daily sunspot number**. The
solar-wind values come from the tiny SWPC dashboard summary products
(`/products/summary/solar-wind-mag-field.json` and `solar-wind-speed.json`, ~100 B
each): the legacy `/products/solar-wind/*.json` 5-minute files were retired by NOAA SCN
26-21 and 404 on the wire. Proton density is not fetched at all — no summary product
carries it and the full RTSW plasma file is too large for the no-PSRAM heap. The sunspot
number comes from SWPC's 30-day `daily-solar-indices.txt` text table (~2.5 KB); the JSON
alternative is 170+ KB and starved the heap on the bench.

Four derived features need no extra download: a **band-by-band HF outlook**
(40/20/15/10 m open/fair/weak/shut for day and night) with a **MUF estimate**, an
**aurora level** upgraded by southward Bz, a **VHF sporadic-E / auroral-E flag**, and the
**SWPC 3-day Kp forecast**. The propagation screen shows the new rows on a dedicated
**'o' outlook page**; a full report prints from either screen (**p** on propagation,
**x** on Space Wx). Estimates are labeled as the heuristics they are.

The propagation outlook page also surfaces data the 3-day and daily-indices feeds
already carried but which was previously parsed only for Kp/SSN: today's **C/M/X flare
counts** and **new-region count**, and today's NOAA **radio-blackout (R1-R2/R3)** and
**solar-radiation-storm (S1)** probabilities. Radio-blackout probability is a direct
HF-degradation predictor; daily flare counts convey activity level better than a single
latest-flare snapshot.

All the new data is readable from **Tiny BASIC**: `SSN`, `FLARE` (0-5 for
none/A/B/C/M/X), `BZ`, `SWSPEED`, `MUF`, and `FCKP1`/`FCKP2`/`FCKP3` join the existing
`SFI`/`KP`/`AINDEX`.

## Terrestrial VHF/UHF/microwave operations

A suite of tools for terrestrial (non-satellite) weak-signal work on the VHF, UHF, and
microwave bands. **Radio horizon (VHF+)** gives the k-factor line-of-sight distance from
two antenna heights (raise k for ducting). **Fresnel zone clearance** computes the
first-zone radius and 60% clearance for a near-LOS path. **Tropo ducting index** turns
surface temperature, dewpoint, and any inversion into a 0-6 enhancement outlook (seeded
from the last weather fetch). **Rain fade (microwave)** estimates ITU-style path
attenuation from frequency, rain rate, and length. **Terrestrial path budget** is a
two-way link budget (TX power, both antenna gains, feedline loss, frequency, distance)
with a workable/marginal verdict. **Terrain path profile** samples ground elevation
along the great-circle path to the grid set in the Grid dist/bearing tool (press **f**
with WiFi up) and flags whether the path clears the terrain with Fresnel margin. All six
print via **p**.

The **propagation screen** also gains a meteor-scatter line: the active major shower
(with a near-peak flag) or a sporadics-only note, alongside the existing Es/aurora
hints — a compact VHF conditions planner built on the space-wx data.

## Weather: an outdoor field-conditions page, with independent units

The Weather screen's same Open-Meteo fetch now also pulls apparent ("feels like")
temperature, wind gusts, barometric pressure (with a 3-hour trend derived from the
hourly series), the daily UV index, and sunrise/sunset times. Press **`f`** for a second
page that surfaces all of it: feels-like beside the air temperature with a day/night
marker, steady wind plus gusts (flagged **MAST** when gusts are high enough to worry a
portable mast), pressure and its rising/steady/falling trend, the UV index with a
low→extreme risk word, today's sun times (for daylight setup/teardown and grayline), and
a per-day UV/gust/sun table. Everything is cached to flash with the rest of the forecast,
so the page works offline. The summary page is unchanged; `f` toggles between them.

Weather **units are now three independent settings** under *Settings → Display*:
**Weather temp** (°C / °F), **Weather wind** (km/h / mph / m/s), and **Weather pressure**
(hPa / inHg). Any combination works (e.g. °F with m/s, or °C with inHg). The fetch now
stores values in canonical units and converts at display time, so changing any unit is
instant with no re-fetch; old bundled configs and old weather caches migrate
automatically.

The weather report can now be **printed** — press **`p`** on the Weather screen for the
current conditions, the field figures, and the multi-day forecast in the selected units,
to the configured print sinks. It is also in the *About → Print* menu as "Weather
report" and over the USB console as `print weather`.

## EME planning & analysis, expanded

The moonbounce suite gains the analysis quantities dedicated EME software provides. The
live EME screen shows the **spatial polarization offset** (the parallactic angle of the
Moon at your station — the number that explains 144 MHz fades even with the Moon up and
low degradation) and a coarse **144 MHz Faraday** estimate when solar flux is known. A
new **'a' analysis page** puts the printed per-band table on screen: self-echo Doppler,
Faraday rotation, sky temperature, and libration spread for 50/144/432/1296 MHz and
10 GHz, plus two-way path loss (+6 dB lunar reflection) per band and the ground-gain
window state.

The **printable EME report** carries the full picture: polarization offset (subtract the
DX station's for a sked) and a per-band table of round-trip Doppler, one-way **Faraday
rotation**, **sky temperature in kelvin**, and **libration Doppler spread** across
50 MHz–10 GHz, plus an **absolute round-trip path-loss** budget and a **ground-gain**
window flag. The **planning window grew from 30 to 90 days** (three months), and the day
table — on screen and in print — marks good days (high northern declination with low
degradation) so you can spot the best EME weekends a quarter ahead. EME reports print
with **'w'** (the Fn+p chord proved unreliable on the bench keyboard).

## Tools menu reorganized into categories

The sixty offline tools had grown into one long flat scroll; they're now grouped into
**six categories** (Calculators & programming, Satellite & orbital, Terrestrial
propagation, Antennas & feedlines, RF chain & measurement, Electronics & references).
Tools opens on the category list; ENTER descends into a category and `` ` `` steps back
out. Each category row shows its tool count, the first-letter jump works at both levels,
and Tools remembers the category you were browsing. No tool moved or changed — only the
menu that reaches them; a compile-time check enforces that every tool lands in exactly
one category.

## Magnetic declination for pointing

A new **Rot bearing: true / magnetic** setting (default **true**) optionally converts
every commanded rotator azimuth from true to magnetic using a built-in IGRF-2020
declination model. Most rotators are aligned to true or self-correct, so CardSat sends
true by default; enable this only if your controller expects magnetic and does not
already apply declination. The Grid dist/bearing tool shows both the true (**T**) and
magnetic (**M**) heading with the local declination, and the tracking side panel, OSCAR
display, and globe view append M### to the true azimuth for compass hand-pointing. The
model is approximate (mean ~4°, worst in the South Atlantic anomaly) — ample for
pointing, and labeled as approximate in the UI. Readable in BASIC as `MAGDECL`.

## Transponder database shows baud rate

The transponder database now shows each transmitter's **baud rate** (data rate; CW shown
in WPM) from SatNOGS — it was in the JSON but not previously parsed. It tells you what
decoder or terminal settings a telemetry beacon needs.

# Fixes and polish

**On-device help refreshed for the current feature set.** The compiled-in Help/Keys
screen and User Guide now match everything added this cycle: the EME analysis page and
'w' print key, the propagation outlook page, the Weather field-conditions page and its
independent units, the six-category Tools menu (with the correct 60-tool count), the
expanded space-weather indices, and the corrected report count (40) in the printing
section.

**Performance screen shows the accurate heap watermark.** The on-screen "Free heap min
ever" was sampled once per loop, so it could miss a dip that happened and recovered
between samples — exactly when a TLS handshake briefly grabs a large buffer — and thus
understate real memory pressure. It now shows the hardware lifetime minimum
(`heap_caps_get_minimum_free_size`), labelled "min (boot)" since that watermark is
since-boot and not resettable, matching the printed report.

**hams.at pass matching: wider window, tougher name matching.** hams.at doesn't refresh
its orbital elements often, so its posted activation times drift — by up to an hour —
from the pass an on-board SGP4 propagation predicts. The co-visibility search that pairs
an activation with a real mutual-Moon (footprint) window widened from ±30 to ±60
minutes, and when several passes fall inside the hour it now picks the one **closest** to
the listed time rather than the first. Satellite-name matching moved to the full
source-independent bridge everywhere in the hams.at flow, and that bridge gained a
**hyphen/space-insensitive tier** so `AO-91` / `AO 91`, `RS-44` / `RS44`, and `XW-2A` /
`XW 2A` all resolve to the same catalog object.

**Magnetic azimuth on the pointing screens.** The manual rotator screen adds the
magnetic target heading with the local declination (and flags when the correction is
also being sent to the rotator); the tracking side panel, OSCAR display, and globe view
append M### to the true azimuth for compass hand-pointing.

**Roomier Tools menu rows.** The highlighted rows looked crowded — the selection bar of
one entry touched the next, because the row pitch (9 px) equalled the highlight height.
Rows now use an 11 px pitch with a 9 px highlight, so a clean 2 px gap separates adjacent
bars and the label sits centred. Eight entries are visible at once instead of eleven; the
list scrolls, so the trade buys legibility at no real cost.

# Under the hood

**Endpoint audit.** A pass over every feed CardSat already downloads confirmed the AMSAT
status feed is fully utilized (name, report, count, and latest-report time are all
consumed) and surfaced the two unused-but-useful items now shown: the SWPC flare/
blackout data on the propagation page and the SatNOGS baud rate on the transponder
database. Endpoint schemas were verified against live payloads before wiring.

**Weather units are stored canonically.** The fetch requests °C, m/s, and hPa
regardless of the display setting, and conversion happens at draw time. This makes unit
changes instant and removed a class of cache-versioning bugs; a small migration path
converts pre-split configs and caches on first load.

**Board-support pin documented.** The handoff memo now records *why* the arduino-esp32
core is pinned to 3.2.1 (IDF 5.4): the ADV's voice-memo capture uses M5Unified's ES8311
mic over the legacy I²S driver, which the IDF 5.4→5.5 refactor broke (silent/constant
samples — espressif/esp-idf#18621, still open). arduino-esp32 3.3.x moved to IDF 5.5, so
it reintroduces the bug; M5Unified through 0.2.18 has not migrated the mic path. Do not
raise the pin without re-checking both and bench-testing the mic.

## Deferred into this cycle from 0.9.60

- **RAM Proposal 1 — staged `ScreenCtx`.** Move ~10 KB of isolated cold screen state
  (`dgSat`, `skyBars`, `tsHits`+`tsNextPass`, `hamsatList` first) into one heap block
  allocated on screen entry and freed on exit. Incremental; prove the pointer-lifetime
  discipline on hardware before extending. NEVER stage the hot arrays (`activeTx`,
  `logRecs`, `msgRing`). See docs/design/RAM_STAGING_ANALYSIS_0_9_60.md.
- **`wifiAp[]` retirement (~0.5 KB)** — if secondary Wi-Fi is genuinely unused; clean
  config-key retirement, the one worthwhile piece of RAM Proposal 3.
- **Dual-radio support** — two FT-817/818 (uplink + downlink). Scope is complete at
  docs/design/DUAL_RADIO_SCOPE.md; Phase 0 (single-radio FT-817/818 backend + table
  entry) is the low-risk start. Do NOT begin without revisiting the scope; RAM
  (dual-USB host clients) is the flagged hard constraint.

## On-hardware verification still owed

Carried in docs/THINGS_TO_VERIFY.md — items that are software-complete and gate-clean but
need a real Cardputer (and, for netplay, two): KESSLER LoRa netplay (terrain match, shot
mirror, score sync), voice memos under USB CAT (heap-headroom threshold feel),
high-orbit pass durations on the Schedule screen, the KESSLER off-top-return fix,
list-wrap spot-checks, menu-order (right tool opens), star field vs a planetarium app,
QTH round-trip across reboot, LAN buffers via WSJT-X/GPredict. New this cycle: the three
independent weather units cycling correctly (especially inHg with two decimals), a
pre-split weather cache migrating on first load, and the printed weather report's
formatting on a real print sink.
