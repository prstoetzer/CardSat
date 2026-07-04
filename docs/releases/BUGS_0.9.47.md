# CardSat 0.9.47 — bug list

Bugs found after the 0.9.46 release, to be fixed in 0.9.47. Each entry records what's
wrong, where it is in the code, and the intended fix, so a fix session can go straight to it.

Status key: **OPEN** (found, not yet fixed) · **FIXED** (done, pending release) · **WONTFIX**.

---

## 1. Zap the Sats in-game footer shows the old controls — **FIXED**

**Symptom.** The footer on the Zap the Sats game (while playing) advertises the wrong
movement keys — it shows the old controls, not the ones the game actually uses as primary.

**Detail.** The game's real controls (`keyGame`, app.cpp ~11258) are **`T` = left, `U` =
right, SPACE = fire**, with `,` / `.` kept only as *legacy aliases* and tilt as an option.
The **attract screen** label is correct — it reads `"T / U  move    SPACE fire"` (app.cpp
~11206). But the **in-game footer** (app.cpp ~11240) still reads
`", / . move  SPACE fire  ` back"`, i.e. it presents the legacy `,` / `.` aliases as the
controls instead of the primary `T` / `U`. So the two labels disagree, and the in-game one
points at the superseded keys.

**Scope.** Label only — the controls themselves work correctly (both the primary keys and
the aliases). This is a cosmetic/documentation mismatch, not a gameplay bug.

**Fix.** Change the in-game footer at app.cpp ~11240 to match the attract screen and the
actual primary controls, e.g. `footer("T/U move  SPACE fire  ` back");` (dual-apply to
`CardSat.ino`). Consider whether to mention the `,` / `.` aliases or tilt; the attract screen
doesn't, so matching it (T/U + SPACE) is the consistent choice.

---

# Audit findings — 2026-07-03 full code & documentation audit

A systematic audit after the 0.9.46 packaging: the fresh 0.9.46 surface, footer-vs-handler
consistency everywhere (prompted by bug #1), documentation claims vs code, and the band-plan
data. Items below were **fixed in the tree immediately** (they are 0.9.46-surface bugs caught
before/at release); see the version note at the end.

## 2. Web-UI orbital velocity used wrong angle units — **FIXED**
`webdSendOrbitJson` computed the vis-viva radius with `cos(nu * D2R)`, but `nu` is already in
**radians** in that function (the surrounding code even converts it back with `nu / D2R`). The
double conversion made the web-reported velocity ≈ the perigee velocity regardless of orbit
position. The device page 9 was correct. Fix: `cos(nu)`.

## 3. Rotator masters not mutually exclusive — **FIXED**
Engaging `smOut` (Sun/Moon) or `rotOut` (Track) did not clear the new `emeRotOut`/`gcRotOut`
flags, and `emeRotOut` didn't clear `gcRotOut`. With `emeRotOut` active (it has a 1 s loop
hook), engaging Sun/Moon or sat tracking left two hooks fighting — the rotator would thrash
between the Moon and the Sun/satellite every second. Fix: every engage site now clears all
three other masters.

## 4. Stale activation anchor on the Mutual DX-Doppler path — **FIXED**
The 0.9.46 `dxdAnchorHz` (activation frequency, re-applied on transponder cycle) was never
cleared by the two **Mutual-window** entries into DX Doppler. After viewing an activation, a
later Mutual-path session on a different bird carried the stale frequency: `t` either snapped
to the old activation's freq (if it fit a leg) or force-reset the mode to true-rule. Fix:
both Mutual entries (`keyMutual`, `keyMutualDetail`) set `dxdAnchorHz = 0`.

## 5. QRZ→grid lookup froze the UI — **FIXED**
The edit-commit for the QRZ→grid callsign (editTarget 351) set "Looking up..." but called the
blocking `qrzGridLookup()` before any draw, so the user stared at the frozen editor for the
whole TLS fetch. The existing QRZ path (case 216) draws first. Fix: `draw()` before the
lookup, matching 216.

## 6. Band-plan data errors — **FIXED** (one item flagged for confirmation)
- **Mode letters swapped**: "Mode V/U (B)" and "Mode U/V (A)" were wrong — V/U (145 up /
  435 down) is **Mode J**; U/V (435 up / 145 down) is **Mode B** (AO-7's Mode B). Fixed.
- **2 mm band**: was "142-149 GHz"; the amateur allocation is **134–141 GHz**. Fixed.
- **2.5 mm band**: was "119.98-120.02 GHz"; the allocation is **122.25–123 GHz**. Fixed.
- **4 mm band**: "75.5-81.0" updated to the current **76–81 GHz**.
- **29 MHz designator**: changed **T → A** (Mode-A heritage) in the table and in every doc
  that lists the letters (manual, release notes, cheat card). → **CONFIRMED by Paul (0.9.47):
  A is correct.** Also consistent with amsBandLetter() (24–30 MHz → 'A') used by the
  mode-aware AMSAT reporting path, so the band plan and the reporter agree.

## 7. `emeMutShown = (emeMutN >= 0)` was always true — **FIXED**
Tautology (int ≥ 0); replaced with the intended `= true`. Note: entering a **bad grid** for
the mutual-Moon scan still lands in the sub-view showing "No common window in 14 days" while
only the status line says "Bad grid" — slightly confusable, left as-is for now.

## 8. Manual errors and omissions — **FIXED**
- Grid-calculator section claimed a grid can be set "via Settings"; it can only be set on the
  **Location** screen (editTarget 103 opens from `keyLocation`). Corrected.
- §21 screen-by-screen reference was missing **four** of the five new 0.9.46 screens (EME,
  Grid dist/bearing, QRZ→grid, HF/6m propagation — only the band plan was present). Entries
  added in the reference format.
- §22 key reference had none of the new keys. Added: Sun/Moon `e`, Space Wx `p`, Help `f`
  (with the full Help key list), the Phys page in the Orbital-analysis row, and rows for the
  four new screens.

## Observations logged, not changed
- **Rotor Runner** accepts undocumented `A`/`L` aliases alongside the advertised arrows
  (arrows are the complete 4-way story, so the footer is defensible; noting for consistency).
- The Space Wx **no-data** branch footer doesn't advertise `p` (deliberate — no indices to
  interpret — but `p` does work there and shows its own no-data message).
- The EME **mutual-Moon scan** is a blocking ~4,000-step loop (5-min step × 14 days, two
  ephemeris calls each); host-side it's instant, but **bench-verify it stays sub-second on
  the ESP32**.
- `keyBandPlan` clamps scroll to N−1 and `drawBandPlan` re-clamps to N−rows: redundant but
  harmless.

## Version note
These fixes were applied to the tree **after the 0.9.46 packaging**, as the start of **0.9.47**: `FW_VERSION` is now bumped to 0.9.47, bug #1 (Zap footer) is
also fixed, and items 1–8 above are the 0.9.47 changelog seed.

---

# Manual review — 2026-07-03 (consistency, flow, language)

A full editorial pass over MANUAL.md against the 0.9.47 code. All items below are **FIXED**.

- **§13 was mistitled.** "Sun and eclipse" actually contained Sun/Moon tracking, Space
  weather, Weather, the Transponder database, QRZ lookup, the Grid calculator, and
  Activations. Renamed to **"Sun, Moon, weather, and reference tools"** (header + TOC).
- **§21 Home-menu entry was several releases stale** — it said "fourteen destinations" and
  omitted Activations, AMSAT status, Overhead now, Grid dist/bearing, Messages, and
  Charge/Sleep. Rewritten to the real twenty, matching the code's menu array.
- **§21 was missing the AMSAT status screen entirely** (a 0.9.45 feature). Entry added.
- **§21 Sun/Moon Keys lacked `e`** (EME) and **Space weather Keys lacked `p`**
  (propagation). Added.
- **§22 had two duplicate "Manual mode" rows** with differing key lists. Merged into one
  (the fuller), correctly titled.
- **§18 web-control orbital enumeration** now includes the 0.9.47 velocity and
  launch-year/age values.
- **The Games section's Zap controls were stale** (same class as bug #1): said `,`/`/`;
  now `T`/`U` primary with `,`/`/` and tilt as alternatives.
- **Mixed British/American spelling normalized to American** (45 instances): favourite,
  colour(-coded), centre/centred, grey, behaviour → favorite, color, center/centered,
  gray, behavior. Rationale: the firmware's own UI strings are American ("favorites"),
  so quoted UI text must be American anyway.
- **Anchor-link audit**: all 93 internal links resolve (an initial checker false-positive
  on "/"-containing headers was verified against the real header set).
- Not changed, noted: **FEATURES.md, README, and the cheat-card source still contain a few
  British spellings** ("colour-coded", "labelled", "favourite toggling") — normalize in a
  follow-up if desired; and the §21 AMSAT-status link points at the nearest §14 anchor
  since the AMSAT prose lives under the GP-age section.

- **Follow-up (docs-wide spelling pass):** all project documentation (34 .md files, 160
  words) normalized to American English — the same word set as the manual pass, extended
  with labeled/license/kilometer/defense/canceled/catalog/optimization/serialized/etc.
  Excluded on purpose: this file (its log quotes the British→American mappings verbatim),
  and **all firmware source and tools** per instruction — code comments, UI strings, and
  the cheat-card generator strings retain a few British spellings until a code pass is
  approved. One deliberate remnant: an all-caps "GREY" in HANDOFF_0.9.31.md that names the
  CL_GREY code constant.

- **Settings reorganized to six top-level categories** (was four): Radio / CAT (21),
  Rotator (16), **Passes / alerts** (9), **Display / sound** (8), **Station / logging**
  (10: callsign + the LoTW and Cloudlog blocks), Network / data (21). The former
  27-item "Station / display" grab-bag is dissolved; Msg notify moved to alerts, the
  AMSAT status window to Network / data, LoRa enable now leads its block, the WiFi
  test row is labeled "(1/2)", the top level shows per-category counts, and `{`/`}`
  page within a category. A programmatic union check confirmed all 85 rows survive
  exactly once. In-category letter-jump was deliberately skipped: row labels are
  built in drawSettings, so a parallel first-letter table would be a drift hazard
  (the stale-footer bug class from this cycle).

- **Home menu: two-column grid.** All twenty destinations now visible at once
  (10x2, column-major so the curated band order survives; thin rules mark the six
  groups; `,`/`/` hop columns; the ^/v scroll hints are retired). Item order and the
  positional ENTER dispatch are unchanged, so no muscle-memory or drift risk; the
  label "Next Passes (all favs)" shortened to "(favs)" to fit the 118 px column.
  The label/action table unification is deferred until a reorder actually needs it.
- **Back-navigation audit (all ~80 key handlers, scripted).** Every screen's back
  target matches its parent; multi-parent screens correctly use return variables
  (passesReturn, logReturn, mapReturn, helpReturn, lotwReturn). One soft finding,
  logged not changed: DX Doppler opened from Mutual *Detail* backs to the Mutual
  list, skipping the detail level -- fixing needs a dxdReturn variable; **FIXED** -- a dxdReturn
  variable now routes back to whichever view opened it.

- **Unsurfaced-data pass (2026-07-03).** From the sources already downloaded:
  (1) **hourly cloud cover** added to the existing Open-Meteo fetch
  (`&hourly=cloud_cover&forecast_hours=48`, ~2 KB growth) and surfaced as a
  color-coded percentage on the **visible-pass list** and **transit finder** rows,
  with a tolerant weather-cache extension (trailing "C" line; old caches load fine)
  and an ISO-local→unix conversion validated against Python ground truth;
  (2) **launch siblings** (catalog scan on the COSPAR YYYY-NNN prefix) and the
  (3) **element-set number** added to the orbital Phys page and the web orbit card.
  Dropped with reason: AMSAT reporter geography — the summary.php payload CardSat
  downloads carries only name/report/report_count; per-report grids live in a
  different endpoint, so it fails the "already downloaded" constraint.

- **AMSAT reporter geography, built via the v1 reports API.** The earlier "not in
  the downloaded feed" finding stands for summary.php; per Paul's direction the
  per-satellite detail is now fetched **on demand**: `g` on the AMSAT status screen
  calls `reports.php?name=<AMSAT name>&hours=<window>&limit=24`, parses callsign /
  grid_square / report / reported_at (scan validated against a synthetic payload
  incl. the meta/links trailer and blank grids), and shows the rows plus a
  distinct-grid count on a new SCR_AMSRPT screen. To make the query (and any future
  report *submission*) name-safe, `applyAmsatStatusFile` now retains the **matched
  AMSAT API name** per satellite (`SatEntry.amsatName`, repopulated from the cached
  summary at boot).

- **AMSAT status reporting + catalog name map.** Built against the live v1 API
  (read-only requests verified against production; the POST path is deliberately
  untested live -- no fake reports in AMSAT's public data). (1) `catalog.php` is
  fetched with each GP update and cached (FILE_AMSCAT); a **four-step matcher**
  (parenthesised designator -> whole-name -> delimited token -> legacy base) maps
  every API entry to a catalog sat, fixing the legacy miss on names like
  "OSCAR 7 (AO-7)" -- validated against all 81 real catalog names with zero false
  positives. `applyAmsatStatusFile` now matches map-first. (2) **Mode-aware
  reporting**: `i`x2 on Track posts Heard, choosing `AO-7_[U/v]` vs `_[V/a]` from
  the active transponder's band letters (tag-fit validated on real AO-7
  frequencies; beacon/unresolvable cases open the picker); `p` on AMSAT status is
  the full picker (canonical four statuses). (3) **Pre-release bug fixes caught by
  live testing**: the reports GET field is `reported_time` (not `reported_at`,
  which only the POST uses) -- the earlier parse would have matched nothing -- and
  the query encoder now percent-encodes `[ ] /` etc., not just spaces.

- **Resolver validated against the real AMSAT GP bulletin** (newark192, 94 entries,
  AMSAT_NAME preferred by the parser) x all 81 live catalog names: **47 matched, zero
  false positives, zero ambiguous multi-matches**. Step census on real data: whole-name
  41 (AMSAT_NAME is designator-style), legacy de-zero base 2 (**AO-07 <- AO-7_[U/v]
  and _[V/a]** -- the flagship case), parenthetical 2 (HYPERVIEW-1G (RS66S) style),
  token 2. 33 of the 34 unmatched are simply absent from the bulletin (ISS-deployed /
  very new birds the status API tracks first). **One true alias gap: CAS-3H (status)
  <-> LILACSAT-2 (bulletin)** -- no lexical bridge, pre-existing under the legacy
  matcher too; fixable upstream (bulletin AMSAT_NAME "LILACSAT-2 (CAS-3H)") or via a
  small firmware alias table if preferred.

- **Alias table added (step 5 of the resolver):** three clearly-marked entries with
  no lexical bridge -- CAS-3H<->LILACSAT-2 (the proven bulletin gap), IO-117<->GREENCUBE,
  LO-19<->LUSAT -- matched with the same tolerant normalization. Revalidated against
  the live catalog x real bulletin: **48/81 matched** (CAS-3H_[FM] now resolves to
  LILACSAT-2 via step 5), still zero false positives and zero multi-matches.

# First hardware compile (0.9.47) -- four build breaks fixed

The initial Arduino/ESP32 compile surfaced four errors, all now fixed and
dual-applied. Three were mirror gaps of a kind parity.py could not see (it
checks function bodies, not declarations); the fourth was a latent macro clash.

1. **`pi` macro collision.** SparkFun/Hopperpop SGP4 does `#define pi 3.14159...`,
   so the local loop counter `int pi = 0;` in `applyAmsatCatalogFile` expanded to
   `int 3.14159... = 0`. Renamed the counter `pi` -> `pp` in both files. Verified by
   host-compiling the function with `#define pi` reproduced (g++ -fsyntax-only, clean).
2. **Screen enum drift.** `SCR_READY, SCR_EMEPLAN, SCR_AMSRPT, SCR_AMSRPICK` were used
   in the .ino (case labels, screen assigns) but never added to its `enum Screen`.
   Added.
3. **`SatEntry::amsatName` drift.** The struct member existed in satdb.h and the .ino
   *used* it, but the .ino struct never declared it. Added.
4. **`HOME_ITEMS` use-before-definition.** In the concatenated .ino the definition sits
   ~13.5 k lines after `keyHome` uses it; also latent in src (keyHome precedes the
   definition in app.cpp). Added `extern const char* const HOME_ITEMS[];` before
   `keyHome` and dropped `static` from the definition (external linkage), both files.

**Tooling hardened:** parity.py now also checks non-function declarations -- Screen
enum values, SatEntry members, and HOME_ITEMS ordering -- scoped to the actual
enum/struct blocks. Confirmed it flags each of the four had it existed earlier.

# Field-test fixes (0.9.47, on-device)

Four issues reported from the first hardware run of the AMSAT features, all fixed and mirrored:

1. Home "next pass" line crowded at the bottom. The two-column grid filled to y=108, leaving the persistent next-pass line jammed 8 px above the footer. Tightened the grid to a 9 px row pitch from y=16 (was 10 px from y=18); the last row now ends about y=97 and the pass line moved to y=114 with clear separation.
2. AMSAT reports screen showed no data. The reports.php parser bounded the record scan at the first "]" -- but every mode-tagged name ("AO-91_[FM]") contains a "]" inside the first record's string, so the scan ended before parsing anything. Rewrote the boundary to walk flat brace records and stop only when "]" precedes the next "{". Validated against the real AO-91_[FM] payload: 0 records before the fix, 3 after.
3. The "AMSAT status/reports..." work banner never cleared until a keypress. The fetch set a 2.5 s status banner and switched screens, but nothing cleared it, so it lingered over the new screen. Added a status-clear at the end of fetchAmsatStatus and fetchAmsatReports (the established clear-before-show idiom).
4. Status footer confusing and truncated. The old string was 288 px wide (screen is 240) so "update" was cut, and reports/report read alike. Reworded to "g who-heard  p report  u update  back" (234 px, fits; distinct verbs).

Mirror-tooling incident (resolved). While mirroring these, the ad-hoc brace matcher used for function-body swaps miscounted char-literal braces in fetchAmsatReports and over-extended, corrupting drawMessages/drawAwardList in the .ino. Caught by balance.py (+1 brace) and a function-count check. Recovered the .ino from the last good prep zip and re-applied all changes with a correct literal-aware extractor built on balance.py's own state machine (saved as mirror_tool.py at the repo parent). A full signature-based audit then found and fixed 8 genuine pre-existing mirror gaps (the catalog-map boot/update hooks in setup/doUpdateGp/doFastUpdate, and stale Messages footer wording), leaving zero residual drift and all 430 methods present exactly once.

- **Phys page: launch siblings now listed by name**, not just counted. The count row
  stays ("N in catalog"); beneath it the sibling names are wrapped ~38 chars/line in
  cyan, stopping before the footer (y<=110) with a "..." marker if an unusually large
  launch batch would overrun. Validated: AO-7-style short lists on one line, 12-name
  batches wrap to four lines ending at y=90, extreme lists clip cleanly.

- **Home menu row spacing fixed (on-device).** The two-column grid had been tightened
  to a 9 px pitch with separators at row_y+6 to make room for the next-pass line; on
  real hardware the rows and their underlines crowded together. Restored a 10 px pitch
  (rows y=15..105), moved separators into the mid-gap (row_y+8) so they never touch the
  text above or below, and set the next-pass line to y=116 (4 px under the last row,
  11 px above the footer). All 20 items still fit; the crowding is gone.

- **Offline/field robustness pass.** Audited persistence and offline behavior across
  every network data source. Findings: GP, weather (incl. 48 h cloud), space weather,
  and AMSAT status marks + catalog map were already cached at write time and reloaded
  in setup(), and all four screens already render cache-first (show cached data, then
  refresh only if WiFi is reachable, with graceful "WiFi failed" rather than a blank
  screen; loaders handle a missing cache). **One real fix:** fetchAmsatStatus and
  fetchAmsatCatalog wrote their downloads *directly* to the live cache file, so a
  mid-transfer drop in the field (httpsGetToFile truncates the dest once a 200 begins)
  could corrupt the cache and poison the next offline boot. Both now stage through
  FILE_DL_TMP and atomically rename over the real cache only on success -- matching the
  weather/spacewx pattern -- so a failed refresh always keeps the last good data. The
  report-submission and who-heard-it detail paths inherently need live network and fail
  gracefully offline.

- **Tools hub added** (About -> `t`, three new screens: SCR_TOOLS / SCR_CALC /
  SCR_TOOLFORM). Ten offline tools: an infix scientific calculator (recursive-descent
  parser -- + - * / ^ () unary-minus, pi/e/Ans, sin cos tan asin acos atan sqrt ln log
  exp abs, degree trig; 16/16 host-compiled parser tests incl. error cases), and nine
  live-recalc forms sharing one form engine: coax loss/power (k*sqrt(f) matched loss +
  SWR-added loss, 8-cable table), dipole (468/f), vertical/ground-plane (234/f), yagi
  (driven/refl/dir), quad (1005/f loop), RF units (dBm/dBW/Vrms/Vpp @50ohm), SWR/return
  loss/refl%, FSPL (32.44 + 20log d + 20log f), and a length/temp/distance converter.
  All formulas validated against known references (40m dipole 65.9 ft, 2m 1/4-wave
  19.5 in, LMR-400 loss, SWR 2->9.54 dB RL, etc.). Placed off About because the main
  menu is full; no main-menu slot consumed.

- **Programmer's calculator added to Tools** (SCR_PCALC, second entry in the hub). A
  64-bit accumulator shown simultaneously in hex/dec/bin/oct with a selectable display
  width (8/16/32/64, masking the value); base-aware digit entry; bitwise AND/OR/XOR,
  NOT ('n'), negate ('m'), shifts ('<'/'>'), and +-*/ via a pending-op model; 'b'
  cycles base, 'w' cycles width, 'C' clears, DEL drops the low digit. Logic host-tested
  (masking, base-validated digits, multi-base formatting, bitwise ops, two's-complement
  negate -- all correct). NOTE: while wiring this, found and fixed a pre-existing
  src<->ino divergence -- the Tools dispatch cases and About 't' key/footer existed only
  in the .ino (the earlier Tools-wiring run into src had aborted); src is now
  reconciled and a full dispatch-case audit confirms all 100 cases match between the two
  representations.

- **Compile fix: F() macro collision in toolFormInit.** The field-setup lambda was named
  `F`, which collides with Arduino WString.h `#define F(string_literal)` (the flash-string
  helper) -- the preprocessor expanded `F(0, "Cable", ...)` as that 1-arg macro and errored
  on all 13 call sites. Renamed the lambda F -> FLD in both files (14 occurrences each).
  Verified by host-compiling toolFormInit with `#define F` reproduced (clean). Same class
  as the earlier `pi` collision; proactively scanned the rest of the Tools/pcalc code for
  other Arduino-macro names (PI, HIGH, LOW, bit, sq, byte, word, etc.) as identifiers --
  none found.

- **Tools UX pass (field feedback).** (1) The Tools menu now scrolls (11 rows visible,
  ^/v indicators) -- the eleventh entry, Unit converter, was previously off-screen and
  undiscoverable. (2) The scientific calculator is now a traditional tape interface:
  entry line with a scrolling history of expressions and results above it ([/] scroll;
  the ; and . arrow keys must remain expression characters -- '.' is the decimal
  point, a conflict caught during implementation), function hints pinned above the
  footer. (3) Yagi and quad forms take an element count (2-12 / 2-8) and list every
  element (yagi directors taper ~1% per element as starting dimensions); ,// scroll
  the output when it exceeds the screen. (4) Programmer calc: 'b' (base) and 'C'
  (clear) collided with hex digits -- typing hex B was impossible. Base is now moved
  with the ;/. up/down arrows across the highlighted hex/dec/bin/oct rows (also more
  intuitive), and 'x' (not a hex digit) clears. All new logic host-tested: element
  math, base-cycle order matching the screen rows, and the 12-entry tape ring.

- **Calculator: DEL is edit-only.** DEL on an empty entry previously exited to the
  Tools menu -- one backspace too many threw you out mid-session. DEL now only deletes
  characters; back remains the sole exit.

- **Tools: DEL exited instead of editing (real root cause).** The Cardputer keyboard
  delivers the DEL key via the ks.del flag, which arrives as the handleKey 'back'
  parameter -- and isBack(c, back) returns true for it. So keyCalc/keyPCalc/keyToolForm
  all exited on DEL because their leading isBack() check fired before the backspace
  code (8/127) was ever tested; the raw code never arrives. (An earlier attempt only
  removed the empty-buffer exit and so appeared to do nothing.) Fixed to the proven
  keyEdit idiom: only the backtick exits; DEL (via the back flag, or codes 8/127) is
  handled as backspace. Applied to all three Tools key handlers -- the calculator, the
  programmer calc (drops the low digit), and the live-recalc forms (backspace the edit
  buffer) -- none of which exit on DEL anymore.

# Release review (packaging gate)

Full documentation review before packaging: release notes written
(RELEASE_NOTES_0.9.47.md, themed per house style); README "New in v0.9.47" blurb added
above 0.9.46's; FEATURES gaps closed (space-wx trend deltas, a UI-refinements bullet for
the two-column home / six-category settings / named launch siblings); cheat card gains
Track i-report and the About Tools line (still 2 pages, auto-fit). Ground-truth checks:
every documented footer/key matches the code verbatim; the 85-settings claim verified by
a fresh union of the six SET_ arrays (21+16+9+8+10+21); 100 Screen enum values = 100
dispatch cases, matching between src and .ino; FW_VERSION 0.9.47 in both. parity.py's
rows audit was itself stale (still checking the pre-reorg SET_STN name) -- updated to the
six-category arrays; its rows[NN] anchor also matches the unrelated NoteVRow rows[128]
(cosmetic, both files identical). Suite at gate: balance 0/43, all parity checks green.
