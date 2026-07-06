# CardSat 0.9.50 — bug list

Status key: **OPEN** · **FIXED** (done, pending release) · **WONTFIX**.

## 1. AMSAT catalog map empty -> only one transponder/mode offered when reporting — **FIXED**

**Field report:** when reporting AMSAT status, the transponder/mode is limited to one
option -- e.g. AO-7 could only be reported as V/A (Mode A), with no way to report U/V
(Mode B).

**Root cause:** the report picker (SCR_AMSRPICK) already cycles every AMSAT name for a
satellite, and the live AMSAT catalog does carry AO-7 as two entries (AO-7_[U/v] and
AO-7_[V/a]). But the picker's list was empty because the catalog *name map* was empty:
`SatDb::applyAmsatCatalogFile()` scanned for the fixed bytes `"name":"` while the AMSAT
catalog API pretty-prints its JSON with spaces around the colon (`"name": "..."`). The
fixed-byte match found nothing on the live payload, so the map held 0 entries (visible as
`[amsat] catalog map: 0 entries` in the boot serial log). With no mapped names,
`amsNamesFor` returned 0 and `amsPickNameFor` fell back to the single status-file name --
hence exactly one mode.

**Fix:** replaced the rigid `"name":"` matcher with a small whitespace-tolerant state
machine (MS_KEY -> MS_COLON -> MS_PRE -> MS_CAP) that matches the literal `"name"`, then
optional whitespace, `:`, optional whitespace, and the opening quote before capturing the
value. Host-verified against the live catalog payload: 81 names now parse (was 0),
including both AO-7 modes. Edge-tested: the compact no-space form still works (backward
compatible); tabs/newlines around the colon work; and `"display_name"` does NOT
false-match (the key pattern starts with a quote, so the `name` inside `display_name` is
not a match). Applied byte-identically to src/satdb.cpp and the .ino;
applyAmsatCatalogFile is mirror-identical.

**Effect:** restores the full catalog map, so the report picker's "As" row now offers
AO-7 (U/v) and (V/a) to choose between -- and unblocks AMSAT reporting generally (the map
powers posting by API name). This is both the "stuck on one mode" report and the
catalog-map-empty defect noted in the 0.9.50 planning doc; they were the same bug.

**Bench check:** boot serial should read `[amsat] catalog map: N entries` with N in the
~80s (not 0). Open the AO-7 report picker (AMSAT status -> p, or Track's report path) and
confirm the "As" row cycles U/v <-> V/a with ,//. Sending a report in a chosen mode is
also the first real exercise of the reporting path (see planning doc section 4).

**Follow-up done (pretty display):** the report picker's "As" row and the Track one-key
"Report ... Heard?" banner now show the prettified name (`AO-7 [V/a]`) instead of the raw
API form (`AO-7_[V/a]`). Rather than capture the catalog's `display_name` field (extra RAM
+ parser work), a tiny display-only transform reproduces it exactly: `_[` -> ` [` and any
stray `_` -> space, verified to match display_name across all 81 catalog entries. The raw
name is still what gets POSTed and what amsTagFits() sees; only the shown text changes.
Helper amsPretty() host-compiled and tested. **Still tracked:** verify the active-transponder
disambiguation (`amsTagFits`) maps the [U/v]/[V/a] tags onto CardSat's mode strings so the
one-key "I heard it" path auto-selects the right mode when a transponder is active.

## 1b. amsTagFits AO-7 two-mode disambiguation — **VERIFIED (no change)**

Follow-up from the catalog-map fix: now that AO-7 resolves to two names, checked that the
one-key "I heard it" auto-select maps the tags onto the active transponder correctly.
Host-tested amsTagFits: Mode A transponder (up V / down A) matches [V/a] only; Mode B
(up U / down V) matches [U/v] only. Works as designed; no code change.

## 2. Feature: SatNOGS active/inactive marking + priority transponder sort — **DONE**

Transponder struct gained an `active` flag (from SatNOGS `status`/`alive`; defaults active
for old caches) plus helpers `isTwoWay()`, `freqIsAmateur()`, `isAmateur()`. The list is now
ordered once over the complete set (SatNOGS + manual) by `prioritizeTransponders()` with a
rank: two-way (+8) > amateur-band (+4) > active (+2) > linear (+1). Amateur is weighted ABOVE
active on purpose so **two-way stays preferred** and **every non-amateur transmitter sorts
strictly last** (both stated requirements). Verified against live SatNOGS payloads (ISS 50
records, AO-7 7 records) and a host-compiled unit test: two-way first incl. inactive linear
above one-way beacons; Soyuz VHF / S-band TT&C sink to the end. Inactive transponders render
dimmed grey with a "(off)" tag in the database view. SatNOGS API re-verified live (fields
status/alive/type/service present). Old `prioritizeTwoWay` removed; parser no longer sorts
(done once in app layer after manual tx appended). Mirror-identical.

## 3. Feature: link budget pre-fill from live slant range — **DONE**

`lbInit()` now calls new `lbSyncFromSat()`: if a satellite is tracked, the Distance field is
pre-filled from its live slant range and the frequency from the selected transponder downlink;
`lbSynced` flags it and the field shows "(live)". New `p` key re-syncs; editing Distance by
hand clears the flag. Footer gains "p sync-sat". Mirror-identical.

## 4. Tier B tool polish — **DONE**

RF-exposure: added "estimate / not a station eval" disclaimer line. Debris: altitude field
relabeled "Alt(perigee)" + "elliptical? use perigee alt" note (both scroll within the existing
result-scroll mechanism). Char lookup: Baudot row now annotates the CCITT No.2 figures that
differ from US-TTY (codes 5/9/13/17/20/26) rather than adding a full second table + toggle to
an already-dense screen -- delivers the informational value at low risk. Mirror-identical.

## 5. Hardening + docs — **DONE**

Hardening: replaced the hand-maintained magic `7` (menu-index<->form-id offset, in two places)
with a named `TOOLS_FIRST_FORM` constant, guarded by a `static_assert` tying it to TOOLS_NAMES
and the TOOL_* enum -- a future tool insertion mismatch now fails the build instead of
mis-indexing. About screen: free-heap line now also shows the largest allocatable block
(`getMaxAllocHeap`) for field heap-fragmentation reports (planning §1.2).
Docs: FEATURES.md "all twenty" home-items claim VERIFIED CORRECT against the code's own
static_assert (no change needed -- planning §2.1 was a false alarm). Reworded the two dated
"As of v0.9.4x" manual notes to describe current behavior. Documented the transponder sort +
inactive marking and the link-budget live-slant-range sync in MANUAL.md and FEATURES.md.

## Release review (packaging)

RELEASE_NOTES_0.9.50.md written. README New-in-v0.9.50 blurb added. Manual rebuilt at
v0.9.50; docs updated for the transponder sort/inactive marking and link-budget live-range
sync; two dated As-of notes reworded; debris manual wording reconciled with the new
Alt(perigee) label. Cheat card updated (transponder-db entry notes the sort + (off) marking)
and rebuilt at 2 pages. Ground-truth: every release-notes claim confirmed present in code
(whitespace parser, txRank/prioritizeTransponders, (off) tag, lbSyncFromSat, TOOLS_FIRST_FORM
+ static_assert, getMaxAllocHeap). Gate: balance 0/46, parity green, 108 dispatch cases
match, 75 manual + 15 README image refs resolve, FW_VERSION 0.9.50 both files. **Paul
compiled+flashed 0.9.48 and 0.9.49; 0.9.50 pending a compile/flash on device.** New/changed
screens still need on-device screenshots (transponder list with (off) entries; link budget
with (live) distance). Release zip: CardSat-0.9.50.zip.

## 6. Tools hub expansion (all tiers of TOOLS_PLAN) — **DONE**

Menu: 20 -> 26 tools. Standalone count 7 -> 9 (TOOLS_FIRST_FORM=9, static_assert holds
26-9==17 in both files). New form tools: Phasing line/stub (coax VF-aware electrical
length for CP antennas & stubs), Wavelength/frequency, Attenuator pad (pi/T), dB chain
sum. New standalone screens: Operating references (Q-codes/phonetics/RST, SCR_OPREF) and
CTCSS tone reference (SCR_CTCSS, surfaces knownCtcssHz sat tones). All math host-compiled &
checked (phasing 1/4wave LMR-400@146=0.436m; 6dB pi pad @50=150.5/37.4; attenuator/dBchain
verified).

Refinements: (2.1) Tools menu first-letter jump + last-tool memory (no reset on entry).
(2.2) per-form-tool value persistence to FILE_TOOLDEF via Store::fs() (round-trip
host-tested) with 'x' reset-to-defaults (tfDefOnly guard). (2.3) antenna-length units
setting (antUnits, Settings->Display row 85; N bumped 85->86 to avoid the rows[] overflow I
caught) via an antLen() formatter -- **CONSTRAINT ENFORCED: antLen used ONLY in
dipole/vertical/yagi/quad/phasing; ZERO uses in debris/xarea/fspl. Orbital distances,
altitudes and satellite sizes stay metric always, per requirement.** (2.4) coax velocity-
factor column (published VFs -- flagged for Paul to sanity-check), RF-exposure per-mode
duty presets (pick-list: FM/SSB/CW/FT8), unit converter gained kg<->lb and in<->cm.

§3.7 char-lookup full-table view -- **DONE**. Added a browsable full-table mode (printable
ASCII 32-126, each row: char / decimal / Morse / Baudot code with L/F shift tag) as a
display mode inside the existing Char lookup tool, toggled with the = key. (Originally on
Shift+T, but that made the letter T the one character you could not look up directly -- a
real conflict caught on review, so the toggle moved to = and lowercase now folds to
uppercase for the Baudot column since ITA2 is uppercase-only.) New state clkTableMode/clkTableScroll, new
drawCharLkTable(); scroll via ;/. in table mode. Host-compiled the Morse+Baudot lookup
(A=.- /03L, 0=-----/22F, ? = 25F, etc). Mirror-identical (drawCharLk/keyCharLk/
drawCharLkTable all match src<->ino).

Verification: balance 0/46, parity green (447 methods), SCR dispatch match, all 11
touched/new functions mirror-identical src<->ino, static_assert holds both files, 75
manual + 15 README img refs resolve. Docs: manual gains the 6 new tools + refinements
section; FEATURES + cheat card updated (card re-trimmed to 2 pages). **New tools need
on-device screenshots and a compile/flash.** VF values + Q-code selection await Paul.
## 7. On-screen documentation additions — **DONE (history draft needs Paul's vetting)**

Three additions from the on-device docs audit:
1. **Help/Keys gained a TOOLS section** (the 26-tool hub was previously invisible in help):
   entry point, first-letter jump, value persistence + x reset, pick-list cycling, link-budget
   p-sync, char-lookup shift+T table, and where the antenna-units setting lives. The ABOUT
   help entry also now lists its own sub-screens (r readiness, t Tools, z games, l license).
2. **Learn gained a COAX & VELOCITY FACTOR section** (electrical vs physical length, why
   phasing harnesses give circular polarization, cut-long-and-trim advice, pointer to the
   phasing tool), and the tech-help POLARIZATION section now explains WHY sat antennas go
   circular (crossed elements + 90-deg line; ~3 dB vs linear but no cross-pol fades).
3. **History gained a THE 2020s section**: FM workhorses (SO-50 since 2002, AO-91), the
   linear-CubeSat wave, GreenCube/IO-117 as the landmark (2022, MEO ~5800 km, digipeater
   footprint enabling transatlantic contacts, reach unseen since Phase 3), and ARISS as the
   ongoing on-ramp. **FACTS FOR PAUL TO VET** (written conservatively but he is the
   authority): SO-50 launch year 2002; IO-117 orbit ~5800 km and "first amateur MEO
   digipeater" framing; "reach not seen since Phase 3" comparison; AO-91 operational
   status framing.

All within existing scrolling screens -- no new screens, no new screenshots, no dispatch
changes. Line widths checked against house style (<=32 ch). All four enclosing functions
(drawHelp/drawLearn/drawSatHistory/drawTechHelp) mirror-identical src<->ino.
## 8. Orbit explorer, orbit animations, calculator extensions — **DONE**

Three features from the scoping doc (heap-safe pair + calc extension):
- **Orbit explorer** (Orbit page 11 "Explore"): editable apogee/perigee/inclination seeded
  from the active sat (oxSeedFromSat), recomputing period/revs/ecc/velocities/footprint/
  max-pass/nodal-drift/sun-sync WITHOUT mutating real elements. oxInit reset on fresh Orbit
  entry; x reseeds. Pure math into text -- heap-flat. Host-verified: ISS-like -> 92.9min
  /7.66km/s/15.5rev; GEO -> 1436min/1.0rev.
- **Orbit animations** (SCR_ORBITZOO, Help->o): 6 archetypes (LEO/polar-SSO/MEO/GEO/
  Molniya/GreenCube) as to-scale ellipses with Earth at the focus, Kepler-iterated dot
  (fast at perigee), fading trail from a FIXED static ring buffer (OZ_TRAIL=24, int16 x/y
  -- no per-frame alloc, no String churn), caption + period. Dedicated ~15fps cadence
  branch (66ms) only while this screen is active. Host-verified ellipse focus geometry
  (Molniya perigee/apogee radii + 11.97h period correct).
- **Calculator extensions**: added constants c/kB/Re/mu/g0 and functions dbm/w/db/undb/
  wl/fq (via non-capturing lambdas -> fn ptr, host-confirmed compiles) plus sinh/cosh/tanh
  /floor/ceil/round. word() boundary-awareness host-tested to confirm c vs cos/cosh/ceil
  and w vs wl / db vs dbm don't shadow. New ' key toggles a second function-hint page.

Heap note: feature 2 is the only animated one; its discipline (fixed trail buffer, zero
frame-path allocation) keeps it flat -- consistent with the scoping doc's condition. All
8 touched/new functions mirror-identical src<->ino; balance 0, parity green, dispatch
match. Docs: manual (Explore page, calc helpers, Orbit animations section, Help key row),
FEATURES note. Needs on-device compile/flash + screenshots (Explore page, orbit-animation
frames).
## 9. ARRL Radio Mathematics tool enhancements (A-E) — **DONE**

Implemented all five items from the ARRL-supplement scoping doc:
- **A. Calculator eng-notation + metric prefixes**: number literals accept p/n/u/m/k/M/G/T
  suffixes (case-sensitive M vs m, applied only when not followed by an identifier so it
  never eats a function name -- host-tested incl 100k/2.2n/146M/1e3/100k+5). New calcEngFormat()
  helper + \ key toggles engineering-notation output (4700 -> "4.7 k", 0.5 -> "500 m").
  NOTE: the prefix code lives in the CalcP::atom() nested struct, NOT the calcEval body, so
  body_by_sig reported calcEval "identical" -- had to mirror that region separately (caught it).
- **B. Radio math reference** (SCR_MATHREF, Tools standalone #10): scrolling cheat sheet
  (dB table, AC Vrms/peak factors, prefixes, constants, reactance/resonance/SWR/time-const/
  Ohm formulas). Static PROGMEM, same idiom as CTCSS/opref. 39 lines, all <=32ch.
- **C. Complex/polar** (TOOL_COMPLEX): a+jb -> magnitude/angle + 1/Z. Host-verified 50+j25 ->
  55.902 @ 26.57deg.
- **D. Reactance & resonance** (TOOL_REACT): Xl/Xc/net + LC resonance. Host-verified 7MHz/
  10uH/100pF -> Xl 439.8 / Xc 227.4 / f0 5.033MHz.
- **E. RC/RL time constant** (TOOL_TIMECONST): tau=RC, 1/3/5-tau %, cutoff f. Host-verified
  1k/1uF -> 1ms / 159.2Hz.

Menu 26 -> 30 tools; standalone 9 -> 10 (TOOLS_FIRST_FORM 9->10, static_assert 30-10==20
holds both files). All touched/new functions mirror-identical; balance 0, parity green,
dispatch match. Docs: manual (calc prefixes/eng, 5 new tool entries), FEATURES note. Not
built (per scoping): Boolean algebra, graph plotting, radiation patterns. Needs compile/
flash + screenshots for the new screens.

## 10. Compile-error fixes (device build) — **FIXED**

Paul's Arduino compile of CardSat.ino surfaced two errors in this session's orbit code:
1. **oxSeedFromSat: TWO_PI_/MU/RE not in scope.** These physical constants are
   function-LOCAL throughout app.cpp (no file-scope definition); oxSeedFromSat is a
   standalone method that referenced them without defining them. Fix: added the local
   const line (MU/RE/TWO_PI_) as every other orbital function does. (The orbit-explorer
   page 10 compiled fine because it lives INSIDE drawOrbit, which defines them.)
2. **drawOrbitZoo: local named TWO_PI collided with the Arduino #define TWO_PI macro**
   -> the preprocessor expanded 'const double ... TWO_PI = ...' into a numeric-constant
   assignment (garbage). Classic macro collision. Fix: renamed the local to TWO_PI_
   (the codebase convention) at all 4 use sites.
Swept all session-added functions for (a) other bare-macro locals (TWO_PI/PI/HALF_PI) and
(b) references to file-scope-absent MU/RE/TWO_PI_ -- none remain (C/D/E tools correctly use
M_PI from math.h). Both fixes mirror-identical; balance 0, parity green.
