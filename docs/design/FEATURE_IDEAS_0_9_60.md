# Feature ideas — 0.9.60 planning

> **Status update (0.9.59):** Sections **2**, **3**, and **5** were built into 0.9.59 —
> 2 and 3 in their entirety **except `INPUT`** (the no-interactive-programs rule stands;
> 2g shipped gated-off by default, as designed), and 5 in full except the speculative 5g
> QR idea. Section 1's item **1d** (whole-catalog search) also shipped. Sections 1
> (remainder) and 4 stay open for 0.9.60.

Five requested areas, every idea designed against the board's real constraint:
**minimum RAM cost** (no PSRAM; ~178 KB free after globals, flash at 86%). The
recurring techniques are the ones already proven in the codebase: the
`Scratch::Lease` transient arena, **stream-don't-store** file parsing (the file
*is* the database), the `accept()` filter hook already built into
`SatDb::scanGpFile`, snapshot-per-run for BASIC, and drawing that computes
per-frame into the already-resident canvas sprite. Each idea carries its RAM
bill explicitly; "0 RAM" means no new permanent allocation.

---

## 1. Selecting from among large CelesTrak satellite groups

The problem shape: groups like `active` (~11,000 objects) or `starlink` dwarf
both the 150-sat resident cap and RAM. The answer is never to hold a group in
memory — fetch it transactionally to a file (already solid), then treat the
file as a read-only database and move small windows over it.

**1a. Curated group picker.** A screen listing ~20 useful CelesTrak GROUP slugs
(amateur, cubesat, weather, noaa, goes, stations, visual, gps-ops, glonass,
galileo, beidou, iridium-NEXT, starlink, oneweb, planet, science, geodetic,
last-30-days, analyst) with a rough size hint each. Selecting one runs the
existing transactional fetch into the normal catalog path. *RAM: 0 (a const
table + the existing fetch). Highest usability per byte of anything here.*

**1b. Import filters as `accept()` policies.** `scanGpFile` already takes an
accept callback; today it's only "favorites first." Add selectable import
policies for oversize groups: **name contains** (type-to-filter text),
**altitude band** (min–max km), **inclination band**, **band around the active
satellite** (the debris screen's ±150 km test, generalized). Needs a one-line
API change so the callback sees the parsed `SatEntry`, not just the NORAD.
*RAM: 0 — the filter runs during the existing stream; rejected objects never
exist in memory.*

**1c. Windowed group browser.** After a fetch, browse the file page-by-page:
ten names per screen, ENTER toggles an object for import, `f` marks favorite.
The trick for large files is a **coarse offset index** built during one
streaming pass — the byte offset of every 64th object. For 11,000 objects
that's ~172 `uint32_t` = **688 bytes in a Scratch lease**; a page turn seeks to
the nearest indexed offset and scans forward ≤63 objects (fast even on
LittleFS). Selections accumulate as a small NORAD list (64 × 4 B) and a second
streaming pass imports exactly those. *RAM: ≤1 KB, transient.*

**1d. Whole-catalog name search (no group at all).** CelesTrak's
`gp.php?NAME=<text>&FORMAT=JSON` searches the entire catalog server-side. Wire
that into the existing manual-add fetch: type "NOAA", get the matches streamed
to a temp file, pick from the windowed browser (1c), append to the resident DB.
This gives access to **every cataloged object** without ever downloading a
group. *RAM: temp file + the 1c window. Probably the single most powerful
feature in this section.*

**1e. Truncation transparency.** When a group exceeds 150, say so with numbers
("kept 150 of 7,214 — favorites preserved; filters can narrow this") — `_seen`
already exists for exactly this. *RAM: 0.*

*Honest note:* GP JSON is ~800 B/object, so `active` is ~9 MB — fine on SD,
tight on internal LittleFS. The picker should show the size hint and the fetch
already refuses cleanly on StorageFull; 1d (server-side search) is the path
that sidesteps size entirely.

---

## 2. More data integration into BASIC

The 0.9.57 design stance is good and worth keeping: **read-only bare names,
snapshotted once per run, halt-with-error instead of sentinels, and no
TX / rotator / network / storage writes.** Everything below extends the
snapshot; the two items that bend the stance are explicitly gated.

**2a. Indexed multi-satellite reads.** Today's names cover the active
satellite. Add `NSAT`, and indexed forms taking a DB index: `SATAZ(i)`,
`SATEL(i)`, `SATRNG(i)`, `SATNOR(i)`, `SATNAM$(i)` if strings ever land (else
a `SATSEL i` statement that re-snapshots the bare names against satellite *i* —
cheaper: no parser changes for indexed args, one statement, and the snapshot
philosophy carries over unchanged: each `SATSEL` is one SGP4 call, bounded by
the statement budget). Enables user-written "what's up now" scanners and custom
alarms. *RAM: 0 — reuses the interpreter's snapshot slots.*

**2b. Transponder snapshot.** `NTX`, and per-`SATSEL` bare names for the
selected transponder: `TXDL`, `TXUL`, `TXBW`, `TXINV`, plus a `TXSEL i`
statement. Lets BASIC compute dial pairs, its own Doppler math (`SATRR` exists),
passband positions. *RAM: 0.*

**2c. More time and pass depth.** `UTCYR/MO/DY/HR/MI/SE` split fields, `LSTHR`
(local sidereal, the astronomy folks will use it), and pass lookahead beyond
the next one: `PASSAOS(k)`, `PASSLOS(k)`, `PASSMAX(k)` for the k-th upcoming
pass of the selected satellite (bounded k ≤ 8, computed at snapshot from the
existing predictor arrays). *RAM: 8 × 3 values in the interpreter heap.*

**2d. GPS and device.** `GPSOK`, `GPSLAT`, `GPSLON`, `GPSALT`, `GPSNSAT`,
`HEAPFREE`, `UPTIME`. *RAM: 0.*

**2e. `LPRINT` — BASIC prints through the report sinks.** The one write-path
worth adding, because it produces *paper*, not RF or network state: `LPRINT`
routes lines through the existing `Printer::begin/line/end`, opened lazily on
first `LPRINT` and closed at program end. A program can print its own custom
report — pass tables, logging worksheets — through whatever sinks the user
configured. (Yes, a network printer technically touches the network; gate it
behind the same sink configuration the 29 reports already use — if the user
configured a printer, they've consented to printing.) *RAM: 0 buffered — lines
stream straight to the sinks.* This is also the bridge to section 5.

**2f. Canvas graphics statements** — `CLS`, `PSET x,y[,c]`, `LINE x1,y1,x2,y2`,
`CIRCLE x,y,r`, `TEXT x,y,"s"`, `SHOW` (push the sprite). Draws into the
**already-resident** canvas; a BASIC program becomes able to render its own
instrument screens — a user-defined tracking display, a telemetry gauge.
*RAM: 0. Flash: small. The single biggest capability jump per byte in this
section, and it feeds section 4.*

**2g. Gated file lines (design question, default OFF).** `FOPEN "name"` /
`FPRINT` / `FCLOSE` writing only under `/CardSat/Basic/`, one handle, gated by
a Settings toggle ("BASIC may write files") that defaults off to preserve the
0.9.57 stance. Enables logging between runs (the snapshot model means a
program can't loop forever collecting — but a scheduled re-run could append).
*RAM: one `File` handle + a line buffer from the Scratch arena, transient.
This is the one to debate, not just build.*

---

## 3. More functions — graphing calculator, scientific calculator, BASIC

Every calculator function is one `word()` entry and a lambda; every BASIC
keyword is one `kw()` branch. All are 0-RAM, small-flash.

**Scientific calculator.**
- Math: `atan2(y,x)`, `hypot`, `mod`, `min`, `max`, `sign`, `log2`, `cbrt`,
  `fact` (n ≤ 170), `ncr`, `npr`, `d2r`/`r2d`, `rnd()`.
- RF/ham, chosen to pair with the new tools: `swr2rl` / `rl2swr`,
  `mml(swr)` mismatch loss, `fspl(mhz,km)`, `nf2t(nf)` / `t2nf(k)` (noise
  figure ↔ noise temperature — the cascade tool's little sibling),
  `dbd(dbi)` / `dbi(dbd)` (±2.15), `dop(mhz,rr)` Doppler in Hz from range
  rate in km/s.
- Orbital flavor for this audience: `porb(altkm)` circular period in minutes,
  `vorb(altkm)` km/s, `fpr(altkm)` footprint radius km. Three lines each,
  straight from the audited formulas.
- Suffix: add `f` (femto) to the p n u m k M G T set.

**Graphing calculator** (all draw into the existing plot loop):
- **Trace cursor** — `;`/`.` walk x, readout of (x, y) in the footer. *A few
  floats.*
- **Root / extremum find** — scan the plotted samples for sign changes and
  slope changes, refine by bisection, mark and report. *0 RAM.*
- **Second expression Y2** — one more expression buffer (~64 B) drawn in a
  second color; with it, **intersection find** comes almost free.
- **Numeric `dy/dx` at the cursor** and **∫ between two cursor marks**
  (Simpson over the visible samples). *A few floats.*
- **Table mode** — the same expression evaluated into a scrolling x/f(x) list.
  *0 RAM (computed per row per frame).*
- **CSV plot mode** — plot a file from `/CardSat/plot.csv`, streamed and
  min/max-downsampled to the 235 plot columns on the fly, so a 100,000-row
  log plots in **flat RAM** (two floats per column, on the stack). Pairs with
  BASIC 2g logging or host-side data dropped on the SD card.

**BASIC language** (the current set is PRINT LET IF FOR GOTO GOSUB REM END +
ABS COS SIN SQR INT RND — lots of low-hanging fruit):
- `INPUT` (numeric, using the existing line editor) — programs become
  interactive.
- `TAN`, `ATN`, `LOG`, `EXP`, `SGN`, `MIN`, `MAX`, and `MOD` / `AND` / `OR` /
  `NOT` in expressions.
- One numeric array `A(0..N)` with `DIM A(N)` capped (e.g. 256 elements =
  1 KB) from the interpreter's existing heap allocation, freed with it.
- `DATA` / `READ` / `RESTORE` — costs nothing, reads the program text.
- `ON x GOTO l1,l2,...` and `FOR ... STEP` negative (if missing).
- `SAVE "name"` / `LOAD "name"` / `FILES` — multiple stored programs under
  `/CardSat/Basic/` (read/load is within the stance; SAVE writes only the
  user's own program text, which the editor already persists — extend, don't
  invent).

---

## 4. More graphic displays

The 240×135 canvas is already allocated; the discipline is compute-per-frame
(or per-entry with a progress bar) and cache only bytes.

**4a. Sky dome — "everything up right now."** All loaded satellites on the
polar chart at once, favorites highlighted, top three labeled. Cost model:
one `temeStateAt` per satellite is too slow per frame for 150, so refresh
**round-robin** (e.g., 10 satellites per frame into a cached az/el table:
150 × 2 × int16 = **600 B**), full sky refreshed every ~1.5 s. This is the
classic missing screen. *RAM: 600 B.*

**4b. Pass Gantt (planning timeline).** Next 12/24 h as horizontal bars, one
row per favorite (up to ~9 rows), computed with the existing `predictPasses`
into the existing pass arrays, drawn as bars with elevation-tinted color and a
"now" line. The single most useful *planning* view for a rove day. *RAM: 0
beyond existing arrays; one-time compute with the usual progress status.*

**4c. Sun & Moon arcs on the polar chart.** Today's sun path (24 hourly
points), moon path, and current positions overlaid on the polar screen —
directly supports sun-noise G/T pointing and EME planning. *RAM: 0 (computed
per draw; the solar/lunar almanacs are cheap).*

**4d. Orbit side-view.** A closed-form ellipse of the active orbit to scale
against a shaded Earth disc: apogee/perigee markers, the satellite's current
true-anomaly position, the eclipse cylinder as a shaded band, beta angle
noted. Pure geometry from elements, educational, prints well as ASCII too.
*RAM: 0.*

**4e. Doppler truth plot.** During a pass with CAT engaged, plot the planned
Doppler S-curve and overlay dots of what the radio actually reported (the One
True Rule loop already reads the dial): a live visual of tracking quality.
*RAM: a 64-sample ring of (t, Δf) = 512 B, transient per pass.*

**4f. Elevation histogram ("how good is this bird here").** Bars of max-pass
elevation distribution over the next 7 days for the active satellite —
answers "is this a horizon-hugger from my QTH." One-time compute with
progress. *RAM: 18 × uint8 bins.*

**4g. BASIC-drawn displays** — not a screen we build, but the enabling of 2f:
users compose their own gauges from `PSET`/`LINE`/`TEXT`. Zero incremental
cost once 2f exists.

---

## 5. Printing in the newly implemented tools (0.9.59)

The pattern to follow is the transponder planner's contextual `p` (streams
lines straight to `Printer::` — no buffering) and the 0.9.56 precedent of
ASCII renderings of graphic screens.

**5a. Universal form-tool printing — one refactor, 34 tools.** `drawToolForm`
already funnels every computed line through the `out(label, value, color)`
lambda. Add a member flag (`tfEmit`): when `p` is pressed, open the sinks,
re-run the same compute switch with `out()` teeing each line to
`Printer::line` (label padded, color dropped), print the *input fields* first
(label = value unit), then close. Every current and future form tool becomes
printable with **zero per-tool code** and zero buffering. Header: tool name,
callsign/grid, UTC stamp like the existing reports. *RAM: 0. This is the
centerpiece of the section.*

**5b. Conjunction report.** `p` on the results screen: satellite pair, element
ages, the ≤5 approaches (UTC, miss, relative velocity), and the TLE-accuracy
caveat printed verbatim — a record worth stapling into a log. *RAM: 0.*

**5c. Orbital-neighborhood report.** The band-overlap table as printed:
active satellite's band, then each neighbor with band, inclination, gap/OVLP.
*RAM: 0.*

**5d. Debris-group report.** Group name, fetch timestamp, objects retained,
and the closest-approach table — the paper trail for a screening run. *RAM: 0.*

**5e. Link-margin curve — table + ASCII art.** A margin table every 5° of
elevation, then a small ASCII plot (the 0.9.56 screen-rendering precedent),
generated line-by-line. *RAM: 0.*

**5f. Keep them contextual.** None of these should join the 29-entry Print
menu — the established rule is that screens print themselves with `p`, and the
menu stays for station-level reports. Update the cheat card's key hints for
the five new screens instead.

**5g. (Speculative) ESC/POS QR.** Many receipt printers accept `GS ( k` QR
codes; a QR of the transponder plan or a satellite's GP object would be a fun
field trick — but it's transport-specific (ESC/POS only, not PWG/serial), so
it's a curiosity to prototype behind the format check, not a commitment.

---

## If only five get built next

1. **5a** universal form printing (34 tools for one refactor, 0 RAM),
2. **1d** whole-catalog name search (+1c's windowed browser),
3. **2f** BASIC canvas graphics (largest capability-per-byte in the file),
4. **4b** the pass Gantt,
5. **3's** graph-calc trace + root find (turns the plotter into an instrument).
