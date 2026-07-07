# CardSat 0.9.51 — change tracker

Status key: **OPEN** · **DONE** (implemented, host-verified, pending flash) · **WONTFIX**.

## 1. Documentation consolidation pass — **DONE**
FEATURES.md drift from several 0.9.50 feature waves, fixed:
- Tool count corrected **26 -> 30** (line ~229) and the stale bottom "Tools hub" summary
  paragraph replaced with a concise pointer to the detailed per-tool entries (it predated
  the ARRL math tools / orbit explorer / reference screens and enumerated an old set).
- DXCC entity-count inconsistency resolved: the doc said both "340" and "402"; now
  consistently **340**, with a clause noting the lookup DB and the workable-DXCC reference
  use the same 340-entity set. Removed a garbled "(degree trig, `Ans`)" fragment.
MANUAL/README were already current for 0.9.50 (image refs resolve); screenshot debt carried.

## 2. Rove planner (SCR_PLANNER / SCR_PLANDETAIL) — **DONE**
A from-a-hypothetical-place/time, all-favorites pass survey, hung off **Next Passes -> `p`**
(no new home-menu slot -- the home grid is a locked 10x2 = 20 items).
- **Input form**: grid, date, time, +/- window hours, GO. Fields edited via the existing
  edit-field idiom (new editTargets 370-373, routed in editHome() + committed in keyEdit;
  date/time keep the other component; grid validated via gridToLatLon).
- **Survey**: for each favorite, predictPasses() over [center-window, center+window] from the
  entered site; per pass, count workable US states + DXCC. **Jobbed** one favorite per loop()
  (planJobRunning), progress line, ~500 ms redraw while building -- UI stays responsive.
- **Results**: fixed .bss array PlanRow[PLAN_MAX=28] (~20 B/row), sorted by AOS; row shows
  name / AOS / maxEl / state-count / DXCC-count.
- **Detail**: reuses drawPolarGrid + drawPolarArc from the entered site + row's sat; shows
  AOS/LOS/maxEl/length + workable counts; `s`/`d` open the full workable state/DXCC lists.
- **Heap**: flat. New helpers countWorkableStates()/countWorkableDxcc() take an EXPLICIT sat
  (footprint is site-independent), reuse the existing stateBits[7]/dxccBits[43] scratch and
  **save/restore** them so the live Workable screens are unaffected. No new large arrays, no
  per-frame alloc, no network. CPU cost is the only cost -- handled by jobbing.

### Verification (host)
balance 0; parity all-present; dispatch src<->ino match; all planner functions + keyEdit/
keySchedule/loop/drawDxcc mirror-identical. Window/date-time math host-tested (center 14:30
+/-3h -> 11:30..17:30; time edit preserves date). Compile-safety sweep: no bare-macro locals,
all called symbols exist (cfg.minPassEl, Location::toGrid, db.indexOfNorad, drawPolarArc,
predictPasses 5-arg form). Used mktime (TZ=UTC0), matching the codebase's UTC idiom (not
timegm). **Caught during build:** a str_replace consumed the drawDxcc() signature when
inserting a helper -- balance.py flagged -1, restored.

### Pending (needs Paul)
- Compile/flash 0.9.51 on device.
- Screenshot the rove planner + its pass detail (documented text-only this round).
- Confirm on-device the survey timing across a full favorites list feels responsive.

## 3. State-vector -> GP-element fitter (SCR_GPFIT, Tools) — **DONE**
A Tools entry that computes **GP mean elements** from a launch-provider state vector, with
optional save-as-manual-sat. Correct method (differential correction), not the naive
osculating-stuffing that yields multi-km errors.
- **Forward model**: new `Predictor::temeStateAt(SatEntry, unix, r[3], v[3])` propagates
  candidate GP elements via CardSat's own Vallado SGP4 (gpToTle -> local Sgp4 -> sgp4()),
  returning TEME r/v. Uses a LOCAL Sgp4 so the live tracker (_sat) is untouched.
- **Fitter** (gpfSolve): rv2coeSeed() gives the osculating initial guess; then Newton/
  Gauss-Newton iteration with a numerical 6x6 Jacobian over [n,e,i,RAAN,argp,M], 6x6
  Gaussian-elimination solve (solve6), convergence to <1 m position residual (accept <1 km).
  Heap-flat: a few 6-vectors + one 6x6 on the stack; ~7 SGP4 props/iter x <=12 iters.
- **UI** (SCR_GPFIT, standalone Tools #11): form (epoch + rx ry rz / vx vy vz), SOLVE,
  results (six mean elements + derived apo/peri + residual + B*=0), `s` save-as-manual-sat
  (prompts name -> db.addGp). editTargets 380-387, routed in editHome + committed in keyEdit.
- **Honest constraints, shown on-screen**: input MUST be TEME (no on-device frame xform);
  B*=0 (single state, no drag); re-acquire once cataloged. Menu 30->31, standalone 10->11
  (TOOLS_FIRST_FORM 10->11, static_assert 31-11==20 both files).

### Verification (host)
**Fitter validated against python-sgp4 round-trip**: real ISS TLE -> TEME state -> fit
recovered the mean elements to 5+ sig figs (n=15.500000, e=0.0004, i=51.6400, RAAN=100.0000,
argp=50.0, M=310.0) in 3 iterations, 1 mm residual. Confirms the osculating seed (e=0.00161,
argp=46.6) is correctly pulled to the true mean values -- i.e. it does the mean<->osculating
correction, not the naive stuffing. balance 0, parity all-present, dispatch match, all
GP-fit functions + temeStateAt + keyEdit/keyTools mirror-identical. Sgp4 local is stack-safe
(sequential, not nested; same type as the existing persistent _sat).

### Pending (needs Paul)
- Compile/flash; screenshot the tool (form + results), documented text-only.
- Optional future: dominant J2000->TEME precession term so J2000 input can be accepted
  directly (currently TEME-in only, by design).

## 4. GP-fit J2000 input + rove-planner grids view + text export — **DONE**

### 4a. J2000 frame option for the state-vector -> GP tool
The GP fitter now accepts **J2000 (GCRF/ICRF)** input as well as TEME. A frame selector on
the form toggles TEME / J2000; when J2000 is chosen, the state is rotated into TEME before
the fit via a new `j2000ToTeme()` (IAU-76 precession + truncated 13-term IAU-80 nutation,
applied to both r and v; UTC~=TT approximation, <1e-4 arcsec). **Host-validated against
astropy TEME<-GCRS: ~1.3-1.4 m over 2024-2030, stable; the C++ port reproduces astropy to
1.36 m at 2026-03-15.** The equation-of-equinoxes sign was pinned by the +1/-1 test (sign -1
gave 388 m). Elementary-rotation sequence verified equal to the matrix composition to 2e-12.
This removes the earlier TEME-only limitation while keeping it honest (still ~1 m, still B*=0).

### 4b. Workable grids in the rove planner
- New `countWorkableGrids(sat,a,b)` (footprint-based, explicit sat), mirroring the state/DXCC
  count helpers. Does NOT save/restore the ~4 KB grid bitset (every grids-screen entry
  rebuilds it first), avoiding a fragile temp alloc on the no-PSRAM heap.
- `PlanRow` gains a `grids` count; computed in buildPlanner, shown in the pass detail.
- Pass detail gains a **grids view**: `g` opens the existing SCR_GRID for that pass's
  sat+window (s/d/g = states/DXCC/grids), reusing buildGrids + drawGrid.

### 4c. Rove-plan text export
- New `exportRovePlan()` writes a formatted `.txt` to `/CardSat/RovePlans/rove_<stamp>.txt`
  on the active filesystem (LittleFS or SD): header (grid/centre/window/pass count) then one
  block per pass with pass details, the workable **state** list and **DXCC** list (recomputed
  per pass and enumerated via STATE_CODE / dxccCode), and the workable-grids **count only**
  (per spec -- the grid list is deliberately not written). Triggered by `w` on the planner
  results screen. Format host-tested for readability.

### Verification (host)
balance 0 (caught a second consumed-signature: countWorkableGrids insert ate drawGrid()'s
signature -> restored), parity all-present, dispatch match, all touched App:: methods +
j2000ToTeme/rv2coeSeed/solve6 + temeStateAt mirror-identical. J2000 transform validated in
both Python and a standalone C++ port against astropy. Export format validated in a sim.

## 5. Future-epoch (pre-launch) state-vector audit + fix

Pre-launch state vectors carry an epoch days/weeks ahead of "now". Audited every path that
uses element-set age or the epoch, to be sure a future epoch doesn't break pass prediction.

**Bug found + fixed (functional).** The fleet GP auto-refresh (loop, ~line 287) took
`minAge = min(gpAgeDays)` across all sats and refreshed when `minAge > GP_STALE_DAYS`. A
future-epoch sat has a *negative* age, so it became `minAge` and made the `>` test false --
one pre-launch sat in the catalog silently suppressed GP refresh for every real satellite,
even when they were badly stale. Fix: skip `a < 0` sets in the min, and guard
`minAge < 1e9` so an all-future catalog doesn't false-trigger.

**Display (cosmetic) improvement.** Added `gpEpochFuture()` (clock set, epoch > 0, epoch >
now). The three age-display sites (passes/schedule header + two Track-family screens) now
show **`pre-lnch`** in cyan for a future epoch instead of hiding the indicator (they guarded
on `age >= 0`). `ageColor(d<0)` changed GREY->CYAN to match.

**Verified safe, no change needed:** `gpToTle` encodes a future epoch correctly (`tm_year%100`
+ date-agnostic day-of-year, fine within the TLE 1957-2056 window); `predictPasses` uses a
bounded `nextpass(200)` so it can't hang and returns false if the object never rises; SGP4
handles negative `tsince` (backward propagation) -- host-tested: a 10-day-future epoch
propagates err-free both after the epoch (tsince +2880 min) and at "now" before it (tsince
-14400 min), orbit smooth. `db.addGp` accepts future epochs (only rejects norad==0 /
meanMotion<=0). Staleness `!` marker (>=14) and deep-sleep AOS math ((nextAos-now) with a
`<5` guard) are both negative-safe. The About-screen min-age line is cosmetic and guards
`>= 0`. Passes computed before the epoch are the nominal pre-launch orbit (a usefulness
note, not a fault).

### Verification (host, item 5)
balance 0, parity all-present; `gpEpochFuture` helper + auto-refresh fix + 3 display sites
mirror-identical src<->ino. Future-epoch propagation validated against python-sgp4.

### Pending (needs Paul)
- Compile/flash; screenshots (GP form now has a frame row; planner detail has a grids line;
  results screen has the `w` export) -- documented text-only.
- Confirm a real launch-provider J2000 vector fits sanely on-device.
- Confirm the `pre-lnch` age indicator shows on-device for a future-epoch manual sat.
- Note: DXCC export uses prefix codes (no cheap full-name table on-device).

## 6. On-device rove-plan browser (Feature 2) + web file download (Feature 1, download-only)

Implemented from the FILE_XFER_AND_LORA_SHARE_SCOPING design (Features 2 and 1-downloads).

**Feature 2 -- saved rove-plan browser/viewer.** New `SCR_ROVELIST` (browser) + `SCR_ROVEVIEW`
(read-only viewer). `buildRoveList()` enumerates `/CardSat/RovePlans/*.txt` newest-first
(mtime desc, name desc tiebreak -- the timestamp filenames sort the same way, so no-mtime
filesystems still order right). `roveViewLoad()` reads a **bounded** slice (ROVEVIEW_MAX=4000
bytes) into a String so a large plan can't exhaust the no-PSRAM heap; over the cap it shows a
"(truncated -- download for full file)" note. Viewer reuses the note text-wrapper
(noteWrap/NoteVRow/NOTE_ROWS). Reached with `l` on SCR_PLANNER; `ENTER` view, `d` delete
(2-step confirm, memo-style), `r` rescan. Modeled on the Notes + voice-memo browsers.

**Feature 1 (download-only) -- web file browser.** New `/files` page (own PROGMEM doc,
streamed like the main page) + `GET /api/files?dir=` (JSON dir listing, flushed incrementally
so a big dir never builds a huge String) + `GET /api/file?path=` (streams a download in a
256-byte buffer, never whole-file in RAM). A `webSafePath()` guard confines every access to
the `/CardSat` tree: decode, force leading '/', root relatives under /CardSat, reject `..` /
backslashes / NUL / anything outside the tree. `webPctDecode()` handles %-escapes and '+'.
**No upload path exists** -- the server still only reads request line + headers, never a body.
Files link added to the main control page header.

**Verification (host):** balance 0 (caught TWO consumed-signatures during the big inserts:
the rove insert ate dxccGcKm()'s signature -> -1 imbalance -> restored; the web insert ate
webdSendStatusJson()'s signature -> -1 imbalance -> restored -- both found via a raw-aware
brace scan and balance.py). parity all-present; dispatch parity 1/1 for both new screens
(handleKey + draw) src<->ino; all new App:: methods + webPctDecode/webSafePath +
WEB_FILES_PAGE byte-identical src<->ino. Reused helpers (noteWrap/NoteVRow/NOTE_ROWS,
Store::ready/fs, File getLastWrite/size/available/readBytes, jsonEsc) all confirmed present.

### Pending (needs Paul)
- Compile/flash; confirm the rove list/viewer render and scroll on-device.
- Confirm the /files page browses + downloads from a phone (LittleFS and SD builds).
- Screenshots (rove list, rove viewer, /files page) -- documented text-only.
- Upload path (Feature 1 upload) intentionally NOT built (needs a request-body reader); a
  separate future item per the scoping doc. Feature 3 (LoRa object sharing) not started.

## 7. LoRa GP-element sharing (Feature 3, stage 1 -- GP elements only). UNTESTED radio path.

Implemented the GP-elements slice of FILE_XFER_AND_LORA_SHARE_SCOPING Feature 3, per the
"prototype GP-only first" recommendation. Notes/rove-plan transfer deliberately NOT built.

**Protocol.** A second LoRa frame type (magic 0xC6, distinct from the 0xC5 text frames) carries
a chunked object: [0]=0xC6 [1]=VER [2]=OBJTYPE [3]=XFERID [4]=SEQ [5]=COUNT [6..]=payload.
6-byte header + <=48-byte payload keeps each frame <=54 B, under loraPoll()'s 64-byte RX
buffer. OBJTYPE 1 = a GP element set, serialized as pipe-delimited text
"GP1|CALL|NAME|NORAD|EPOCH|INCL|ECC|RAAN|ARGP|MA|MM|BSTAR|CRC16" -- ~108 chars -> 3 chunks
for a typical LEO set (cap 8 chunks / 384 B). CRC16-CCITT over the body (loraCrc16) guards
the whole object end-to-end so a garbled element set is never imported (the PHY CRCs each
packet, but an object spanning chunks needs its own check).

**TX** (loraObjSendGp + loraObjTxTick): jobbed one frame per loop tick with a ~250 ms gap
(pumped next to the planner/sat-to-sat jobs) so the half-duplex radio and cooperative loop
are never blocked. **RX** (loraObjRxFrame, hooked at the top of loraPoll before the text
path): reassembles ONE object at a time (no PSRAM for several), tolerates out-of-order and
duplicate chunks via a per-seq bitmap, times out a stalled transfer at 20 s. On a complete,
CRC-valid GP object it parses (loraParseGpBody) and opens **SCR_GPIMPORT**, a confirm screen
showing sender/name/NORAD/incl/ecc/MM and whether it adds or updates; `y` imports via
db.addGp() (same validation/persistence as the fitter; NORAD-keyed so it updates in place),
`n`/back declines. **No ARQ**: a missing chunk fails the transfer, sender re-broadcasts.

**Entry points.** `L` on the Satellites screen shares the selected sat; `L` on the GP-fit
result screen shares the just-fitted set (nice for pushing a pre-launch vector to a group).
Both gate on cfg.loraEnable && lora.ready().

**Verification (host):** balance 0 (caught a consumed-comment on the insert: the compass
comment's first two lines were eaten by the object-block insert -> restored; no code lost,
balance stayed 0 but the comment was fixed). parity all-present; dispatch parity 1/1 for
SCR_GPIMPORT; all new App:: methods + loraCrc16/loraSerializeGp byte-identical src<->ino. A
Python port of the serialize->CRC->chunk->reassemble(out-of-order+dup)->CRC->parse pipeline
round-trips a realistic ISS GP set to field precision and rejects a 1-byte corruption.

### Pending (needs Paul)
- Compile/flash; **bench-test the transfer between two units** (this is the untested radio
  path -- confirm frames send, reassemble, CRC-verify, and import correctly, and measure
  airtime/reliability at the configured SF/BW).
- Screenshots (SCR_GPIMPORT confirm screen; the send-progress status) -- text-only for now.
- Notes / rove-plan transfer (OBJTYPE 2/3) and any ARQ remain future work per the scoping doc.

## 8. On-device bug fixes from Paul's 0.9.51 flash test (nine issues)

1. **LoRa send status never cleared.** loraObjTxTick completion status had no timeout and the
   send screens (SATLIST/GPFIT) are static, so "GP element set sent" persisted. FIX: completion
   status now has a 2500 ms timeout + forces one repaint; added `loraObjTxActive` to the live-
   redraw cadence (so progress shows) and SATLIST/GPFIT to the banner-auto-clear branch.
2. **Rove-planner grid field (editTarget 370) didn't uppercase.** FIX: added 370 to the keyEdit
   uppercase-by-default list.
3. **No planner hint on Next Passes footer.** FIX: drawSchedule footer now shows "p plan".
4. **Rove-planner table misaligned.** Header was one string; columns didn't match the row printf.
   FIX: header labels placed at the exact pixel columns the row format produces (name@4 AOS@58
   El@100 St@122 Dx@140).
5. **"Surveying/computing" never cleared.** When the job finished, planJobRunning went false and
   the redraw cadence stopped before the results frame painted. FIX: force a draw() when the
   survey completes.
6. **Footer cut off on the rove-plan results screen** (42 > 40 chars). FIX: shortened to
   ";/. row  ENT detail  w save  g form".
7. **Files browser couldn't open /CardSat.** Some ESP32 FS cores won't open a bare "/CardSat" as
   a directory. FIX: webdSendFileList now mkdirs the dir if missing and retries with a trailing
   slash before reporting failure.
8. **Heap exhaustion (33 KB free / <18 KB max block) -> downloads fail** after a rove plan
   gen+view. Two ~4 KB blocks stayed resident and fragmented the no-PSRAM heap: the viewer's
   roveViewBuf and the lazily-malloc'd gridBits. FIX: free roveViewBuf on exit from the viewer
   and the list; free gridBits when the survey completes (lazily re-allocated on next grid-view
   use); reduced ROVEVIEW_MAX 4000->3000 to shrink the contiguous grab.
9. **Fitter hung on the sun-sync sample vector ("stuck at Solving", no result).** ROOT CAUSE:
   during a Gauss-Newton step, a transient out-of-range element (angle or mean-motion) overflowed
   its fixed-width TLE field in gpToTle (putAt copies unbounded), corrupting line 2 -> a malformed
   TLE that spun SGP4's init/propagator. FIX (defence in depth): (a) gpToTle now wraps angles into
   [0,360), clamps mean-motion to the 11-char field, and zeroes non-finite values -- proven on host
   to keep every field within its column even for a pathological iterate; (b) rv2coeSeed clamps all
   acos arguments into [-1,1] (no NaN seed); (c) gpfSolve bails if a step produces non-finite
   params. Host reproduction (faithful port: rv2coeSeed -> sgp4 -> solve6) converges all sample
   vectors, sun-sync in 4 iters, residual < 1e-4 m.

### Verification (host)
balance 0; parity all-present; every touched App:: function + the rv2coeSeed acos block +
gpToTle (satdb.cpp, also inlined in the .ino) byte-identical src<->ino. Sample-vector fits
re-run and converge; hardened gpToTle field-width proven against a pathological input.

### Pending (needs Paul)
- Re-flash and re-run the §1/§2 checklist items, especially: LoRa send status clears; the fitter
  now returns on the sun-sync vector; the /files browser opens /CardSat; and a download succeeds
  after a rove plan gen+view (watch free-heap/max-block recover).

## 9. State-vector -> GP fitter: on-device convergence (root cause finally found)

Symptom: SOLVE never converged on-device (best residual = seed residual, e.g. 11.7 km for ISS),
though it converged in host validation. Debugging took several passes; the true root cause was
in the SGP4 library, not our math.

**Root cause.** Hopperpop/SparkFun `Sgp4::init()` caches on line 1:
`if (strcmp(longstr1, line1) == 0) return false;` -- it skips re-parsing when line 1 is
unchanged from the previous call. The fitter perturbs only LINE-2 elements
(incl/ecc/raan/argp/ma/mm) with a fixed line 1 (same catnum/epoch/B*=0), so every candidate
after the first was a no-op re-init: the satrec kept the FIRST candidate's elements, every
perturbed forward eval returned the same state, and the numerical Jacobian was exactly zero
(on-device trace: `colnorms n=0 i=0 argp=0 M=0`). No non-zero Jacobian -> no step -> stuck at
seed. (A prior "static Sgp4" attempt made it worse by persisting the cache across the solve.)

**Fix (predict.cpp temeStateAt).** Defeat the cache: cycle the element-set-number field (line 1
cols 65-68, metadata that Vallado's twoline2rv/sgp4init ignore for propagation) and recompute the
line-1 checksum each call, so line 1 always differs and init() genuinely re-parses. Confirmed on
the device: Jacobian columns now non-zero (`colnorms n=292.6 argp=118.6 ...`) and all sample
vectors converge in 1 iteration (ISS 39 m, eccentric 17 m -- at the TLE-quantization floor).

**Also this cycle (gpfSolve, app.cpp).**
- Epoch bug: unpack() didn't set epochUnix -> forward model propagated from epoch 0 (~decades) ->
  huge residual. Fixed (unpack sets s.epochUnix = gpfEpoch).
- Solver hardened Gauss-Newton -> Levenberg-Marquardt with step acceptance: lambda*diag
  regularizes rank-deficient Jacobians (near-circular/equatorial degeneracy) and only accepts a
  step that reduces the residual, so it can neither stall on a singular Jacobian nor diverge.
- Central-difference Jacobian; perturbations raised above each TLE field quantum; early-exit at
  <50 m (the achievable TLE-round-trip floor; <1 m was unreachable); reports best-residual params.
- gpToTle hardened earlier so no transient iterate can overflow a fixed-width TLE field.

Host-validated the LM solver against a TLE-quantized forward model (all 3 cases converge), then
confirmed on-device. Debug traces ([gpfit]/[teme]) removed; build is release-clean.

Note (not ours): boot log shows `task_wdt: TWDT was never initialized` spam during "Listing files
stored on SD" -- emitted by the SD library/launcher layer, not CardSat code. Cosmetic, pre-existing.
