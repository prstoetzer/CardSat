# 0.9.66 scope: partition-map expansion, dual-rig, APRS.fi, DX cluster, ADS-B

Status: **scoped, not implemented** (0.9.66 planning). Four requested features plus a
foundational build-configuration change that the first of them (and the flash-budget
crunch generally) depends on. The partition-map change was **verified today** with a real
test compile against a custom 8 MB partition table dropped into the sketch directory —
not just reasoned about on paper — and the results below are measured, not estimated.

---

## Part A — Can the partition map change? Yes, verified, and it should.

### The constraint driving this

Flash is currently at **93.7%** (2,947,490 of 3,145,728 bytes) under the `huge_app`
partition scheme, which is sized for a **4 MB flash chip**: a single 3 MB app slot (no
OTA) plus a spiffs/LittleFS partition of `0xE0000` = 917,504 bytes — under 900 KB, short
of the 1 MB the request asks for. Only ~198 KB of app headroom remains. Four new
network-integration features (each bringing new screens, parsers, and — per Part B — a
live-feed protocol client) will not fit in that remainder; some would need to be deferred
or the others trimmed.

### The chip actually has 8 MB — confirmed two ways

1. **The module's own part number already says so.** `firmware/README.md` identifies the
   board as *"M5Stack Cardputer ADV (ESP32-S3FN8, 8 MB flash, no PSRAM)"* — Espressif's
   `N8` suffix denotes 8 MB flash / 0 PSRAM by their own naming convention. This is
   independent corroboration, not just Paul's statement.
2. **Board-default build config is silently using only half the chip.** The Arduino-ESP32
   core's `esp32s3` board entry defaults to `build.flash_size=4MB`
   (`boards.txt:801`), and the `huge_app` partition CSV is hand-sized to exactly
   `0x400000` (4 MB) — the two defaults happen to agree, which is *why* nothing has ever
   errored, but it means **the upper 4 MB of the physical chip has never been addressed
   by any build.**

### Verified live: a custom 8 MB partition table works with zero firmware code changes

Arduino-ESP32's build system has a documented override: if a file named `partitions.csv`
exists directly in the sketch directory (next to `CardSat.ino`), it is used **in place of**
the `PartitionScheme` menu selection, with no `boards.txt` edits needed
(`platform.txt:113`, `recipe.hooks.prebuild.1`). This is the standard, toolchain-update-safe
mechanism — no changes to shared Arduino core files, and the table lives in the repo where
Paul can see and version it.

I built and compiled this table today:

```
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x600000,
spiffs,   data, spiffs,  0x610000, 0x1E0000,
coredump, data, coredump,0x7F0000, 0x10000,
```

That's exactly 8 MB (`0x9000+0x5000+0x2000+0x600000+0x1E0000+0x10000 = 0x800000`), 64 KB-
aligned throughout (required for the app partition), and gives:

- **app0: 6 MB** (was 3 MB) — current 2,947,490-byte build would sit at **46.8%** instead
  of 93.7%, with **~3.19 MB of headroom** instead of ~198 KB.
- **spiffs (LittleFS): 1,920 KB ≈ 1.875 MB** (was ~896 KB) — clears the "at least 1 MB"
  ask with room to spare.
- **coredump: 64 KB**, unchanged.
- **No OTA**, same as today — Paul flashes manually via `esptool.py`/M5Burner already, so
  this isn't a regression; it's the same trade-off `huge_app` already makes, just against
  a bigger chip.

I compiled the actual `CardSat.ino` against this table with
`--fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,CDCOnBoot=cdc,FlashSize=8M` (the
`FlashSize=8M` menu option is required *alongside* the custom CSV — the CSV controls
partition **offsets**, but a separate build setting controls the flash-size byte embedded
in the image header and how much of the chip the bootloader believes exists; the two must
agree or the bootloader can reject the table as out-of-range at boot). Result:

- **EXIT=0**, real compile, no code changes.
- `CardSat.ino.merged.bin` came out to exactly **8,388,608 bytes** (8 MB) — confirms
  `FlashSize=8M` took effect.
- Decoding the partition table baked into the binary (`gen_esp32part.py` in reverse)
  reproduces the CSV exactly: `app0,app,ota_0,0x10000,6M` / `spiffs,data,spiffs,0x610000,
  1920K` / `coredump,data,coredump,0x7f0000,64K`.
- **`LittleFS.begin(true)` in `storage.cpp` needs no change.** The Arduino-ESP32 LittleFS
  wrapper locates its partition by the conventional label `"spiffs"`, which the new table
  keeps — the *label* is a naming convention independent of which filesystem is actually
  written there, so the existing mount call works unmodified against the bigger partition.

**One caveat the compile log itself surfaced:** with a custom `partitions.csv`, arduino-cli's
`"Sketch uses X bytes (Y%) of program storage. Maximum is Z bytes"` line stays keyed to
whatever `PartitionScheme` menu value is selected (`upload.maximum_size` is a `boards.txt`
property, not derived from the custom CSV) — it kept reporting the stale 3,145,728-byte
ceiling even against the 6 MB table. The number is cosmetically wrong; the real ceiling is
whatever `app0`'s size is in `partitions.csv`. **Follow-up needed:** add a small check
(a `tools/check_*.py` gate, or a line in the compile-and-package script) that compares the
actual `.bin` size against the real `app0` size parsed from `partitions.csv`, so a future
build that actually overflows 6 MB doesn't get a falsely reassuring in-range percentage
from arduino-cli.

### Safety before flashing real hardware

Both corroborating signals (the `ESP32-S3FN8` part number, and Paul's own statement) point
to 8 MB, but writing an 8 MB-sized image to hardware that turns out to be smaller would be
a real failure mode — a partition table with entries past the physical chip's end can leave
the bootloader unable to boot, or a write can silently wrap/corrupt. Two cheap safeguards,
both worth doing before the first real flash of an 8 MB image:

1. **Read the actual chip size before writing anything:** `esptool.py --chip esp32s3
   --port <PORT> flash_id` decodes the SPI flash ID and reports true detected size —
   takes a few seconds, no risk.
2. **Add `--flash-size detect` to the documented flash command.** Today's
   `firmware/README.md` instructions omit an explicit flash-size flag, which means
   esptool's `write_flash` defaults to `keep` — it trusts whatever flash-size byte is
   already embedded in the image header (i.e., whatever `FlashSize=8M` baked in at build
   time) rather than checking the real chip. Adding `-fs detect` makes esptool verify
   against the actual hardware at flash time regardless of what the image assumes,
   which directly guards against exactly the failure mode above.

### What does *not* change

- RAM is untouched by this — the ESP32-S3's SRAM (327,680 bytes total, 49% used today) is
  a **separate physical resource from flash** and is unaffected by how the flash chip is
  partitioned. The no-heap-growth, stream-don't-store discipline that governs every feature
  below is exactly as tight after this change as before it. Flash headroom buys *code and
  asset* space; it does not buy runtime memory for holding more data at once.
- No OTA capability, before or after (matches the current `huge_app` trade-off).
- No firmware source changes — this is a build-configuration + repo-file change
  (`partitions.csv` added to the repo root alongside `CardSat.ino`, compile command
  updated to add `FlashSize=8M`, `firmware/README.md` flash instructions updated with the
  new offsets and the `-fs detect` safeguard).

### Effort estimate

- Add `partitions.csv` to the repo, update the compile command, one clean full compile +
  the nine gates + a package cut to confirm nothing regresses: under an hour.
- The stale-ceiling follow-up check: a small script, ~30–60 minutes.
- Everything else (updating `firmware/README.md`'s offsets/instructions, a release-notes
  entry) is documentation, not engineering.

This is squarely worth doing **before** committing to how much of Part B ships in 0.9.66 —
it changes the calculus for every feature below from "which of these four can we afford"
to "how much of each can we build well."

---

## Part B — The four features

### 1. Dual-rig support

**Scoped separately already: `DUALRIG_MAINFW_INTEGRATION_SCOPE.md`.** That document covers
bringing the two-radio capability currently provided by the external CardSatDualRig
companion (M5StickS3) directly into the main firmware — the radio catalogue, per-dialect
CAT encoders, dual-USB host registry, two-leg VFO state machine, and rigctld server. Its
own feasibility re-assessment already argued the historical RAM blocker has eased; this
document doesn't re-scope it, only notes that the flash-budget crunch that document didn't
originally need to worry about (it's primarily a RAM/USB-heap question) is now also
resolved by Part A, removing one more reason to defer it.

### 2. APRS.fi data view

**Goal:** show APRS stations — near the operator's own position, or in a target grid —
with distance/bearing, similar in spirit to the existing LoRa peer-bearing screen
(`drawLoraCompass`, `SCR_LORACOMPASS`), and optionally plotted.

**What already exists to build on:**
- `greatCircle(lat1, lon1, lat2, lon2, dist, brg)` — the distance/bearing primitive, already
  used by `drawLoraCompass` and reused throughout the codebase (LoRa roster, EME planning).
- **The bearing-rose renderer itself.** `drawLoraCompass` is a complete, working
  North-up bearing display (fixed rose, peer marked at true bearing, distance formatted by
  magnitude, staleness-aware age readout) — this is the direct template for a single-station
  detail view; very little new drawing code is needed, mostly a new data source feeding the
  same rendering shape.
- **A live-feed roster pattern.** `SCR_AMSATSTAT`'s fetch → tag → list → pick-for-detail
  flow (`fetchAmsatStatus`, roster screen, per-entry detail) is the structural template for
  "fetch N stations, list them, pick one for the bearing detail."
- **Credential storage precedent.** `Settings` already stores a third-party login for
  exactly this shape of problem — `qrzUser`/`qrzPass` (QRZ.com XML subscription, for the
  callsign-lookup screen) and separately `clUrl`/`clKey`/`clStation` (Cloudlog, a
  self-hosted service with an API key). APRS.fi's API (`api.aprs.fi`) needs an API key, not
  a username/password, so the `clKey`-shaped field (a single opaque token) is the closer
  precedent: one new `char aprsfiKey[N]` setting.
- **Grid math.** `Location::gridToLatLon()` already validates a locator and yields lat/lon —
  covers the "in target grid" query mode directly (resolve the grid to a center point, query
  around it).
- **The streaming-fetch discipline**, freshly re-learned this cycle on the AO-7 estimator:
  stream the HTTP response object-by-object into a small fixed scratch buffer via
  `Scratch::Lease`, matching `SatDb::streamGpFileEntries()`, and size `maxBytes` generously
  (a 60 KB cap silently truncated a larger AO-7 response this cycle — that mistake is exactly
  what this pattern avoids going forward).

**Open design questions:**
- **API surface.** `api.aprs.fi`'s `get` endpoint supports lookup by callsign list or by
  area (bounding box); "near me" and "in target grid" both reduce to a bounding-box query
  centered on a point, with the existing `greatCircle()` used to re-derive exact
  distance/bearing per station from the response (the API's own distance field, if any,
  shouldn't be trusted over the same great-circle math used everywhere else in the app, for
  consistency).
- **"Map them"** — no native map-rendering screen exists in the firmware today (the only
  precedent, `GROUND.BAS`/`SKYDOME.BAS`, are BASIC examples using `LINE`/`PSET`/`CIRCLE`
  graphics primitives, not a compiled screen). Two honest options, worth Paul's call:
  (a) **list + single-station bearing detail** (reuses `drawLoraCompass` almost verbatim,
  cheap), or (b) **a small local-area plot** — a new, from-scratch equirectangular mini-map
  centered on the operator, plotting multiple stations at once (closer in spirit to what
  "map them" implies, but new rendering code, and multi-station means picking a sensible
  fixed radius/scale rather than the single-peer case's implicit "just show the bearing").
  Recommend starting with (a) since it's nearly free given existing code, with (b) as a
  fast-follow if the roster view proves too limited in practice.
- **Rate limits / etiquette.** APRS.fi's API has documented query-rate limits; the fetch
  should be on-demand (a keypress, like the AO-7 tool's `f`), not polled automatically, and
  the response should be cached for the session rather than re-fetched on every screen visit.
- **Bounded roster size.** Cap the station list (e.g. the AMSAT status roster's precedent)
  so a busy area near a major city doesn't blow past a fixed array size — reject/truncate
  gracefully with a "N of M shown" note, don't grow dynamically.

**RAM/flash shape:** small fixed station-record array (callsign, lat/lon, symbol, last-heard
age, comment — all short fixed-size fields, no `String` growth), one new settings field, one
new roster screen + one bearing-detail screen (or a mini-map screen if (b) is chosen).

### 3. DX cluster view by band, up to microwave

**Goal:** show recent DX spots, filterable/organized by band, spanning HF through the
microwave bands.

**What already exists to build on:**
- **A band table almost certainly already exists** for `SCR_BANDPLAN` — reuse it directly
  for the band filter/display rather than building a second one; a spot's frequency maps to
  a band via the same lookup the band-plan screen already does.
- The same streaming-fetch and bounded-roster discipline as APRS.fi, if the JSON-API route
  (below) is chosen.

**Open design fork — this needs a decision before implementation, not just before ship:**
Classic DX cluster access is a **line-based Telnet protocol** (raw TCP, typically port 7300
or 8000, login by sending a callsign, spots arrive as an unbounded live text stream) — not
an HTTPS/JSON request-response like every other network feature in this firmware. The
alternative is a **modern HTTP/JSON aggregator** (e.g. a site like DXHeat or similar spot
aggregators expose a JSON feed) that fits the existing one-shot-fetch pattern much more
naturally.

- **Telnet route:** `WiFiClient` (already used underneath the HTTPS stack) can open a plain
  TCP socket with no TLS overhead at all — genuinely *lighter* on the heap than every other
  network feature here, since there's no BearSSL handshake. But it's architecturally
  different from anything else in the firmware: every existing network feature is a bounded
  one-shot GET-to-file; a Telnet cluster feed is a **persistent, open-ended connection**
  that needs to be polled from `loop()`, parsed line-by-line as an unbounded stream, and a
  cluster **node** picked (there's no single canonical DX cluster; users typically choose a
  node like `dxc.w1nr.net:7300`), which itself becomes a new settings field (host + port).
  This is a materially bigger lift than "filter by band" suggests at first glance — it's the
  first "always-on background feed" feature in the codebase.
- **JSON aggregator route:** fits the established one-shot-fetch-and-parse shape exactly,
  much less new infrastructure, but depends on a third-party aggregator's availability,
  format stability, and possibly its own rate limits or API key.

Recommend the **JSON aggregator route** for a first cut, specifically because it reuses
everything already built this cycle (streaming parse, bounded roster, on-demand fetch)
rather than introducing a new "persistent background socket" architecture — and flag the
Telnet route as a larger, separate scoping effort if a live/continuous feed turns out to
matter more than periodic refresh.

**Worth Paul's judgment call, said plainly:** this is the one of the four that's least
tied to CardSat's satellite-tracking mission — it's a general HF/VHF/UHF/microwave DX
awareness tool, useful to an operator but orthogonal to satellite passes. Not a reason to
skip it, just worth naming rather than quietly treating it as equally core as the other
three.

**RAM/flash shape:** a rolling fixed-size buffer of the most recent N spots (call,
frequency→band, mode, spotter, time, comment), filterable by band after the fact rather than
re-fetched per band.

### 4. ADS-B data view — radar plot, aircraft-scatter application

**Goal:** show nearby aircraft on a radar-style plot; note the aircraft-scatter propagation
use case (bouncing VHF/UHF/microwave signals off aircraft for extended range — a real,
established technique, closely related in spirit to the meteor-scatter and EME tools
CardSat already has).

**What already exists to build on:**
- **`drawPolarGrid` / `drawPolarArc`** (`app.cpp:34692`) — an existing, working polar-plot
  renderer: range rings, N/E/S/W labels, and an angle/radius plotting helper, currently used
  for satellite pass ground-tracks (`az`/`el` in, screen `x,y` out via `rr = R*(90-el)/90`).
  The **rendering shape is directly reusable** for a radar plot; the **semantics need to
  swap** — aircraft don't have elevation in the satellite sense, so the radius axis should
  map to **range** (e.g. `rr = R * min(rangeKm, capKm) / capKm`) with angle as **bearing**,
  and a capped-range indicator (a marker on the outer ring, or a distinct color) for contacts
  beyond the display's fixed range cap rather than clipping them silently.
- `greatCircle()` again, for range/bearing to each aircraft from the operator's position.

**Open design questions:**
- **Data source, each with different auth/rate-limit shape:** the OpenSky Network REST API
  (free tier, no login for basic anonymous queries, has rate limits), ADS-B Exchange
  (broader coverage, typically requires an API key / paid tier), or a self-hosted
  `dump1090`/`readsb`/`tar1090` instance on the operator's own LAN (`aircraft.json`, no
  internet dependency, no rate limit, but only sees what a local receiver hears). A LAN
  source is the cheapest to fetch (no TLS, no external rate limit) and the most in keeping
  with the amateur-radio "run your own gear" ethos CardSat already leans into elsewhere
  (rotators, radios); worth asking Paul whether he already runs or plans to run a local
  ADS-B receiver, since that would make the LAN route both simplest to build and most
  directly useful.
- **Aircraft-scatter geometry as a stretch goal.** A genuinely differentiated feature beyond
  "here's a radar plot" would be: given a target grid/DXCC/bearing (the operator's intended
  scatter path) and the live aircraft positions, flag which currently-visible aircraft sit
  in a geometrically useful position to support scatter between the operator and that
  target. That's real added value tying this back to the ham-radio mission rather than being
  a generic aviation toy, but it's a second phase — the geometry test itself (roughly:
  is the aircraft's position within some angular tolerance of the great-circle path,
  weighted by altitude) is a small, well-defined addition once the basic radar plot and
  live aircraft feed exist, not a reason to hold up phase one.

**RAM/flash shape:** same bounded-fixed-array shape as the other two feeds (aircraft
callsign/ICAO, lat/lon, altitude, heading, last-seen age), capped count, on-demand refresh.

---

## Cross-cutting notes for whichever subset ships in 0.9.66

- **RAM is still the binding constraint, unchanged by Part A.** All three live-feed
  features must follow the same streaming-parse, fixed-array, no-heap-growth discipline as
  the AO-7 estimator and GP data fetch — flash headroom does not relax this at all.
- **TLS/heap contention across simultaneous features hasn't been bench-tested.** APRS.fi and
  (if the JSON route is chosen) DX cluster both need HTTPS; ADS-B likely does too unless the
  LAN-source option is chosen. The existing USB-CAT-vs-TLS mutual exclusion precedent
  (`audioAcquire()`) is a reminder that "we have the heap for one HTTPS fetch" doesn't
  automatically mean "we have the heap for that fetch *while* dual-rig's second USB radio
  and CAT dialect stack are also active" — worth a bench pass once more than one of these
  features exists together, not something safely assumed from source alone.
- **Settings-row and screen/menu-id growth.** Each feature likely wants at least one new
  settings row (a credential or host/port) and at least one new top-level screen (plus a
  roster/detail pair for APRS.fi and DX cluster). Follow the same pattern used for the AO-7
  and thermal tools this cycle: new `SCR_*` enum entries, `TOOLS_NAMES`/`TOOLS_ORDER` (or a
  new top-level menu entry, matching `SCR_AMSATSTAT`'s precedent rather than the `Tools`
  submenu, since these are live-feed views rather than one-shot calculators) with coverage
  asserts bumped, and the nine gates re-run after every touch.
- **Suggested phasing, non-binding:** given the scope of all four together, consider
  treating this document's four items as independently cuttable rather than a single
  0.9.66 commitment — Part A (partition map) is worth doing regardless and unlocks
  everything else; among the four features, APRS.fi and the ADS-B radar plot reuse the most
  existing code (bearing rose, polar plot) and are the cheapest early wins, dual-rig has its
  own mature scope doc ready to execute independently, and the DX cluster's Telnet-vs-JSON
  fork is the one genuinely open architectural question worth resolving before committing
  it to a specific release.
