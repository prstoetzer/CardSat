# Outstanding work — next session

Ordered by priority. Items 1–3 are explicit operator requests that were deferred rather
than done badly at the end of a long session; start there.

---

## 1. Heap-on-demand for the Nearby & DX record arrays  *(operator request, deferred)*

**Why it's open.** The operator asked for this directly, and it is the established pattern
in this firmware ("we just did a bunch of work reducing rarely used items to allocate on
demand"). It was deferred because it touches three fetch paths plus every draw guard, and
the session had already produced a cluster of careless wiring bugs — adding an allocation
lifetime bug on top would have been worse than waiting.

**Current state.** Three fixed `.bss` arrays in `App`:

| array | cap | approx bytes |
|---|---|---|
| `aprsSta[APRS_MAX]` | 20 | ~720 |
| `dxcSpot[DXC_MAX]` | 30 | ~1200 |
| `adsbAc[ADSB_MAX]` | 30 | ~1080 |

Caps were already halved once (recovering a measured 3,000 bytes) as a stopgap. Converting
to heap-on-demand recovers the remaining ~3 KB of permanent `.bss`.

**What to do.**
- Allocate on entry to each feed's screen (or on first fetch), free on leaving the hub.
- Every draw path must tolerate a null pointer — the screens are reachable before any
  allocation and after a failed one.
- Follow the existing raster/VoiceMemo heap-on-demand conversions as the model.
- **Do not allocate during a TLS fetch.** Allocate before the HTTPS call, not inside the
  parse loop, or the allocation competes with the handshake for contiguous heap.
- Consider one shared buffer sized for the largest of the three, since only one feed screen
  is ever open at a time — that is strictly better than three independent lifetimes and
  removes most of the fragmentation risk that made this worth being careful about.

---

## 2. Charge / Sleep — still defective  *(operator: "still quite buggy", "display very wonky", "battery reads way low")*

**Two separate defects; do not conflate them.**

### 2a. Display behavior
Current implementation: `Display.sleep()` (SLPIN) on park, no drawing at all while dark,
paint-then-`wakeup()` on keypress, CPU parked at 80 MHz, WiFi/IMU/speaker off, plain
`delay(50)` idle. The 250 ms light-sleep loop was removed on the theory that LEDC backlight
PWM stops across `esp_light_sleep_start()`.

**That theory was inference, not measurement, and the screen is still wonky.** Next step is
to stop inferring: read what bmorcelli/Launcher actually *does* — the source, not the
changelog. Last session only read the changelog, which is exactly why this is still open.
Relevant Launcher history: it fixed "random restarts when dimming screen" and changed "Dim
Screen now turns the screen off", so its display-power path has already been through the
failure modes being hit here.

Specific things to check in Launcher's source: the order and timing of SLPIN/SLPOUT versus
backlight changes; whether it delays between them; whether it re-initializes the panel on
wake rather than only issuing SLPOUT.

### 2b. Battery reads low
Almost certainly unrelated to the sleep path — suspect the ADC read/calibration route in
`batteryPercent()` / `M5Cardputer.Power.getBatteryVoltage()`. Check whether the reading is
taken while the CPU is at 80 MHz (ADC reference behavior can shift with clock/power state)
and whether it needs a settling delay or averaging after wake. Compare a reading taken
immediately on wake against one taken a second later — if they differ, that's the answer.

The wake window was extended 20 s → 60 s and that part is believed fixed.

---

## 3. Activations: footprint lookup fails for near-term passes  *(operator, twice)*

**Symptom.** A JO-97 pass ~1.5 h out shows "Footprint: sat not in your list" (now reworded);
same for FO-29. Both satellites are in the catalog and work for *later* activations.

**What was done.** Diagnosis only, not a fix. `activationFootprint()` returned the same
state for five different failures — satellite lookup, short date, empty start, unparseable
date, unparseable start — and the drawing code blamed the satellite for all of them. Date
and time failures now return a distinct state and print the offending strings.

**What to do next — this is now a one-question fix:**
- If the screen shows **`Bad feed date/time: <date> <start>`** → the hams.at feed is
  emitting a date/time format the `sscanf` doesn't handle for near-term entries. The
  printed strings say exactly what. Fix the parse.
- If it still shows **"sat not in catalog"** → `db.findByServiceName(a.sat)` is genuinely
  failing, meaning the feed names the satellite differently on those entries. Note that
  `Activation::sat` is `char[12]`, so any service name longer than 11 characters is
  silently truncated at parse time — a strong candidate, and easy to confirm by printing
  the stored string.

Ask the operator which message appears; do not guess again.

---

## 4. Heap declines during operation  *(operator: "less stable than before")*

Undiagnosed. The ~6 KB lower boot heap was explained and partially recovered (the feed
arrays, item 1). The **decline over time** is separate and unexplained. Nothing added this
cycle allocates per-operation outside the explicit `f` fetches, so it may be pre-existing
churn that the lower boot headroom has merely made visible — but that is a hypothesis, not
a finding.

**Cheapest way to localise it:** ask the operator to sit on one screen at a time for a
minute each while watching the Perf/heap readout. A screen that declines with no fetching
points at a draw path; decline only after fetches points at the three new parsers. That one
observation narrows it from "somewhere in the firmware" to a specific function.

Known long-lived `String` members remaining in `App` (candidates if the leak is real):
`basicBuf`, `basicOut`, `basicName`, `editBuf`, `graphExpr`, `graphExpr2`, `dxQuery`,
`printPath`, `qrz*`, `calcBuf`, `calcTape[]`.

---

## 5. Validate the three feed parsers against live responses

**None has ever seen real data.** All three were written against documented or observed
shapes with alternate field spellings accepted. Field-name mismatch is the single most
likely failure.

Suggested sources (verified available as of this session):
- **ADS-B:** a LAN `dump1090`/`tar1090` `aircraft.json` first — no key, no rate limit, no
  internet. Otherwise `https://api.adsb.lol/v2/lat/<LAT>/lon/<LON>/dist/<NM>` (ODbL, open
  source, currently keyless; note **nautical miles**, and 135 NM ≈ the 250 km default ring).
- **DX cluster:** `https://www.hamqth.com/dxc_csv.php?limit=50` — free, no registration,
  caret-delimited CSV (`Call^Frequency^Date/Time^Spotter^Comment^...`). The parser sniffs
  JSON vs CSV on the first real character.
- **APRS:** `api.aprs.fi` with the operator's own API key.

A bug already found and fixed this way, worth remembering as the shape of the risk: every
real ADS-B source wraps its list in an outer object (`{"ac":[...]}` or
`{"now":...,"aircraft":[...]}`), and the parser originally collected from the first `{`,
swallowing the whole document and returning **zero aircraft from every source**.

Also unimplemented: `printAprs()`, `printDxc()`, `printAdsb()` — the three feeds have no
print reports. Declarations were removed to keep the parity gate honest; re-add both
together.

---

## 6. AO-7: settle the period disagreement

The mode-agreement fit gives ~18.5 h; the previous boundary-midpoint fit gave ~19.5 h. Both
are defensible from their own objective. The new one explains 100% of positive evidence on
the pinned dataset, which is the stronger claim, but it is still a claim.

**Only live observation settles this.** Compare the tool's predicted switch window against
the AMSAT status page over a couple of weeks. Also confirm on hardware that:
- the fetch completes in acceptable time (horizon gating adds a few hundred SGP4
  evaluations; the grid search adds roughly a second of compute — neither timed on device);
- the LittleFS observation cache accumulates across runs (`/CardSat/ao7obs.csv` exists, the
  `c` marker appears, evidence count grows beyond a single fetch).

---

## 7. 8 MB partition expansion  *(scoped and verified, not implemented)*

Flash is at **94%** and this cycle added ~20 KB. This is now the constraint on adding
anything substantial.

Fully documented in `docs/design/LIVE_FEEDS_0_9_66_SCOPE.md`, Part A, including a **real
test compile** that produced a valid 8 MB image. Summary: a sketch-local `partitions.csv`
plus `FlashSize=8M` gives a 6 MB app slot (94% → ~47%) and 1.875 MB LittleFS, with no
firmware code changes — `LittleFS.begin()` finds its partition by the label `"spiffs"`,
which the new table keeps.

**Two things to do first, in this order:**
1. Run `esptool flash_id` on the actual device to confirm 8 MB before writing an 8 MB image.
2. Add `-fs detect` to the documented flash command, so esptool verifies against real
   hardware rather than trusting the image header.

Also add a size-check gate: with a custom `partitions.csv`, arduino-cli's "Maximum is N
bytes" line stays keyed to the menu scheme and will report a stale, falsely reassuring
ceiling.

---

## 8. Longer-horizon items

- **Dual-rig integration into the main firmware** — scoped in
  `docs/design/DUALRIG_MAINFW_INTEGRATION_SCOPE.md`, not started.
- **Fixed-buffer refactor, second pass** — six status `String`s were converted; the
  remaining long-lived ones are listed under item 4.
- **APRS local-area map** — deliberately deferred in favour of list-plus-bearing, which
  reuses the proven LoRa compass renderer. A multi-station plot is new rendering with its
  own scale/radius design problem.
- **DX cluster via Telnet** — deliberately not done. A Telnet cluster is a persistent,
  open-ended socket polled from `loop()`, architecturally unlike every other network
  feature here, all of which are bounded one-shot fetches. Revisit only if a continuous
  live feed proves to matter more than periodic refresh.

---

## Standing reminder

Before adding any screen, editor, hotkey, or setting: **read how the existing ones do it,
end to end, including where control returns afterwards.** Four defects this cycle
(`editHome` routing, missing `editTitle`, the `b` hotkey collision, missing settings rows)
were all instances of not doing that, and none of them required hardware to catch.
