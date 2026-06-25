# CardSat — Session Handoff Memo (as of v0.9.31)

This memo is written for a new assistant session picking up CardSat development. It
captures the project invariants, the current state, the four features added in 0.9.31,
the hard-won lessons, and exactly how to resume safely. Read it fully before touching
code.

---

## 0. The one-paragraph orientation

CardSat is an open-source amateur-satellite tracker + multi-radio CAT Doppler
controller + rotator controller, written by **Paul Stoetzer (N8HM)**, AMSAT Executive
VP, for the **M5Stack Cardputer ADV** (ESP32-S3FN8, 8 MB flash, **no PSRAM**). Repo:
**github.com/prstoetzer/CardSat**. Paul commits/pushes himself and flashes the device
himself — **the assistant cannot flash Xtensa**, so all verification here is host-side
(x86 g++ logic sims + brace/parity checks), and **Paul's bench scope/serial logs are
the only authoritative test of hardware behavior.** His only bench radio is an
**IC-821H**.

---

## 1. Non-negotiable invariants (break these and you ship bugs)

**1a. Dual representation.** Every code change must be **byte-identical** in BOTH the
modular `src/*.cpp/.h` AND the single-file **`CardSat.ino`** (Arduino compiles the
`.ino`). `#include` lines are the one exception — they live only in `src/`, not the
`.ino`.

**1b. Verify after EVERY change.** Run both checks after any function edit:
- **balance**: a comment/string-aware brace/paren/bracket tokenizer must return
  `(0,0,0)` on all **36 files** (`CardSat.ino` + 35 in `src/`).
- **parity**: token counts (`grep -c`) for every new/changed identifier must match
  between the concatenated `src/*` and the `.ino`. Add a `rows[NN]` audit whenever you
  touch settings.

**1c. Never write specs/docs from memory.** Read the actual source first. Protocol and
interface docs must be extracted from real code, not summarized from recall.

**1d. Host limits.** x86 g++ sims and balance/parity do **not** catch C++
forward-reference errors, ESP32 pin-mux behavior, or timing. When a hardware fix fails
repeatedly, **ship a diagnostic, not another theory** — Paul's scope is the debugger.

**1e. Child-of-1d corollary on judgment calls.** Thresholds and timings (dial
deadbands, decay levels, transit step) are starting points. Paul's on-air/bench feel is
the real tuning signal; say so in the handoff and don't over-claim precision.

---

## 2. Environment & build

- **Working tree:** `/home/claude/cardsat/CardSat-main/` — **no git** here (use `mv`,
  not `git mv`). **The filesystem resets between sessions**; at the start of a new
  session, confirm the tree exists, and if not, ask Paul to re-upload it as
  `CardSat-main.zip`. (This session the tree happened to survive — always verify.)
- **Toolchain (Paul's):** Arduino IDE, esp32 core **3.2.1**, **Huge APP** partition,
  PSRAM disabled, USB-CDC on boot, RadioLib 7.7.1, ArduinoJson v7, M5Cardputer /
  M5Unified / M5GFX, TinyGPSPlus, SGP4.
- **FW_VERSION** lives in BOTH `src/config.h` (~L101) and `CardSat.ino` (~L209). It is
  currently **"0.9.31"** in both. The PDF build script greps the version from
  `config.h`.
- **Docs/PDF build:** `bash tools/build_manual.sh` (stamps the manual PDF with the
  version) and `python3 tools_make_cheatcard.py`
  (OUT=`/home/claude/CardSat_CheatCard_4x6.pdf`). Sync both into `docs/` after building.
  A `★` "missing glyph" warning from the manual build is cosmetic, ignore it.
- **Packaging:** zip `CardSat-main` to `/mnt/user-data/outputs/CardSat-<ver>.zip`
  (exclude `.git`, `.DS_Store`, `__pycache__`, `*.pyc`), copy the `.ino` out, stage
  human docs in a `release_<ver>_docs/` folder, then `present_files`. Current bundle =
  **133 files** (129 code/docs + 4 new scope docs).

---

## 3. Current state — v0.9.31

**Released before 0.9.31 (do NOT re-announce as new):**
- **0.9.29:** single-pin CI-V working end-to-end on hardware (one shared open-drain
  GPIO, verified on the IC-821); CAT serial monitor.
- **0.9.30:** smoother manual tuning (anti-"dial fighting"); selectable beacon/RX-only
  downlink VFO; the CAT serial monitor gained an active rig-poll. **0.9.30 shipped from
  Paul's repo.** This working tree does **not** contain `RELEASE_NOTES_0.9.30.md` (the
  file was renamed forward). Do **not** fabricate it — Paul's repo has the real one.

**New in 0.9.31 — four observer/operator features (all host-verified, NOT yet
flashed):**

1. **Per-satellite operating notes.** `N` on Track opens the text editor
   (`editTarget = 260`) for a short free-text note keyed by NORAD, stored in
   `FILE_NOTES` = `/CardSat/notes.txt` (tab-delimited `norad<TAB>text` lines, mirrors
   the per-sat calibration store). Caps: `NOTE_MAX=120` chars, `NOTE_FILE_MAX=64`
   notes. Shows on Track (a `*` after the sat name in the header + a cyan note line).
   Functions: `loadNoteForSat`/`saveNoteForSat`/`satHasNote` (app.cpp ~L1283). Loaded
   at the 3 sat-change sites next to `loadCalForSat`.

2. **Visual pass predictions.** `visEvalPass(aos,los,maxEl,&vis)` (app.cpp ~L1467)
   samples a pass: visible iff satellite **sunlit** AND observer **dark**
   (`LiveLook.sunEl < cfg.visSunElMax`) AND peak el ≥ `cfg.visMinEl`. Returns a reason
   code (1 visible / 2 daylight / 3 sat-in-shadow / 4 too-low). Computed per pass in
   `buildSchedule()`; flagged with a yellow `*` on the schedule and a verdict line on
   pass-detail. Settings (SET_STN rows **66/67/68**): `visPasses`,
   `visSunElMax` (−6/−12/−18), `visMinEl`. Persisted keys `vispass`/`vissun`/`visel`.

3. **Decay / reentry watch.** Reuses the existing King-Hele `estimateDecayDays()`.
   New helpers `perigeeAltKm(s)` and `decayLevelFor(s)` (app.cpp ~L6844) classify
   0 none / 1 watch / 2 soon / 3 imminent from perigee (<400/<300/<200 km) and
   lifetime (<730/<180/<30 d). Shown as a coloured **down-triangle** on the satellite
   list (yellow/orange/red) and a **Perigee** line on the orbit screen. **No precise
   reentry claims** — order-of-magnitude only; points to CelesTrak/Space-Track.

4. **Sun/Moon transit predictions.** New screen **`SCR_TRANSIT`**; `t` on the Sun/Moon
   screen launches it. `transitStartJob()`/`transitJobTick()` (app.cpp ~L8044) run an
   **incremental** 48 h scan (chunked per loop tick, watchdog-safe like the sat-to-sat
   finder), recording close approaches to Sun/Moon via `angSepDeg()` +
   `skyObjAzEl()`. Minima detection uses **per-body two-step history**
   (`transitSep1/2[2]`, `transitHist[2]`) — fires once per dip (this was a bug fixed in
   host test; see §5). Results capped at `TRANSIT_MAX=16`; `central` = separation <
   ~disc radius (Transit) else conjunction. Has a solar-filter safety warning.

**Verification status of 0.9.31:** all 36 files balance `(0,0,0)`; full src↔.ino parity
on every new token; `rows[]` audit matches; settings count **N=69**. Each feature's
logic was host-tested in isolation (note round-trip; visibility cases; decay
classification vs ISS/high/low/very-low; angular sep + minima detection). **None of it
has been flashed** — the device-side confirmation is Paul's job.

---

## 4. Architecture quick-reference

- **Keys:** `;` up, `.` down, `,` left, `/` right, `` ` ``/DEL back. Palette RGB565
  `CL_*` (BLACK/WHITE/GREEN/CYAN/GREY/YELLOW/ORANGE/RED/DGREEN/SELBG).
- **Screens:** enum in app.h ending `…SCR_SATSAT, SCR_MESSAGES, SCR_CATTEST,
  SCR_CHARGE, SCR_CATMON, SCR_TRANSIT`. App singleton `static App* s_self` (private);
  static-member trampolines (`tlsBusyTrampoline`, `catMonTrampoline`) reach it.
- **Settings:** `Settings` struct in `settings.h`; `save()/load()` = LittleFS JSON in
  `settings.cpp`. Row-ID arrays `SET_RADIO[]`/`SET_ROTOR[]`/`SET_STN[]`/`SET_NET[]`
  (~L4415); `rows[N]` labels (`const int N = 69`); adj-lambda cycles values (param is
  `dir`, ~L4319+); ENTER handler separate; `SCR_EDIT` commit dispatched by
  `editTarget` in `keyEdit`; `editHome(t)` routes back. **editTarget map (in use):**
  100–104, 200–218, **250** (CAT-mon hex send), **260** (per-sat note), 300–340,
  400, 500–508, 600, 700–702. Free-text/case-preserve handling is special-cased in
  the editor char-append; `260` has a `NOTE_MAX` length cap there.
- **Rig/CAT:** civ/yaesu/kenwood `begin(baud,uartNum,rxPin,txPin)`; base virtuals
  `setAddress/setPinMode/sendRaw`. CI-V single-pin logic lives in `CivRig::begin()`
  (civ.cpp) — keep open-drain via the **pad_driver register**, clear UART inversion,
  re-assert RX input; `static HardwareSerial* hs` persists across rig recreate;
  `lastA/lastB` track attached pins to reset on mode switch. `makeRig()` in rig.cpp.
  icomnet.cpp = RS-BA1 UDP, **IC-9700 only**. CAT trace sink (`catTraceSink` /
  `catTrace()`) defined in rig.cpp, declared in rig.h, mirrored in the `.ino`; TX/RX
  taps in civ/yaesu/kenwood feed the CAT serial monitor.
- **Doppler/dial:** One True Rule (KB5MU) block in `src/app.cpp` (~L2683, find via
  `lastRxSet`/`knobMoveThreshHz`). Mode-aware knob thresholds
  `KNOB_MOVE_SSB_HZ=30`/`KNOB_MOVE_FM_HZ=250`, floored at `RIG_STEP_HZ=10`, grace
  window `TUNE_GRACE_MS=400` (downlink-only — **uplink must NOT be gated on
  `tuningNow`**, that was the 0.9.30 regression). `dlOnSub()` honors `cfg.rxOnlyVfo`
  (RXO_FOLLOW/MAIN/SUB) for receive-only entries.
- **Predictor:** `LiveLook` exposes `el`, `rangeKm`, `rangeRate`, `sunlit`, `sunAz`,
  `sunEl`. `Predictor::look(t)` / `azelAt(t,&az,&el)`; `pred.setSite()/setSat()`.
  Sun/Moon via `skyObjAzEl(t,lat,lon,isMoon,&az,&el)`.
- **SatEntry** (satdb.h): `name[26]`, `norad`, `intlDes`, `meanMotion`, `ecc`,
  `bstar`, `ndot`, `epochUnix`, `amsatStatus` (0 none /1 heard /3 telemetry).
- **Incremental jobs pattern** (copy for any long scan): `*JobPhase` flag, `*JobI`/
  offset, `*JobPct`; advanced from `loop()` via `if (jobPhase) jobTick();`; screen
  added to the periodic-redraw condition while the job runs. Sat-to-sat and transit
  both use it.

---

## 5. Hard-won lessons (the failure modes that actually bit us)

- **Anchor collision in str_replace.** A note-function insert silently consumed the
  CTCSS function's header line; caught only by the brace balance going `(-1,0,0)`.
  *Always* balance after a multi-edit batch. Disambiguate identical blocks across
  civ/yaesu/kenwood with class-specific neighbor lines.
- **"ok" from a patch script ≠ correct mirror.** A header `repl` reported success but
  the new token wasn't actually present in the `.ino`; parity (`satNote` src=11 vs
  ino=10) caught it. Verify the *specific new token* count in both files, not just the
  script's exit.
- **Logic bugs hide in plausible code.** The transit minima detector fired on every
  rising step under threshold (2 false hits/dip) and shared one history across Sun &
  Moon. Host-testing the *algorithm* before mirroring caught both; the fix was
  per-body two-step "down-then-up" detection.
- **The 0.9.30 uplink regression.** Gating the uplink on `tuningNow` (not just the
  downlink) made the uplink "rarely update" while tuning. A 2-second sim showed 2
  writes vs 11. Lesson: the grace window is **downlink-only**; the uplink isn't on the
  operator's knob.
- **Heredoc patch discipline:** multiple `repl()` then one `write()`, each with
  `assert count==1` so a missed anchor aborts the batch instead of half-applying.
- **File-search pattern:** `grep -n` with term variants → `head`, then targeted `view`
  with a line range once the anchor is found. Cross-file constant discovery in one
  grep with `2>/dev/null`.

---

## 6. Open / deferred items (candidate next work)

- **Flash-and-confirm the four 0.9.31 features.** Highest priority — they are
  host-verified only. Especially: transit scan completes without watchdog trips,
  progress bar updates, predicted transit times match a reference (Transit-Finder) for
  Paul's site; decay thresholds light up the right birds; visual-pass flag matches
  Heavens-Above for an evening ISS pass. All four need clock + location set.
- **Decay thresholds** (perigee 200/300/400 km; lifetime 30/180/730 d) are judgment
  calls — tune once Paul sees which objects flag.
- **Dial thresholds** (30/250 Hz, 400 ms grace) — same; Paul's on-air feel decides.
- **Single-pin CI-V level interface.** Paul researched the Elecrow/y2kblog
  3.3↔5 V module: must use an **open-drain (BSS138/I²C-style) channel**, jumpers
  pattern ③ (J4/J5=3V3→5V, J9=3V3) or ④ (J9=5V if the radio's CI-V truly swings to
  5 V — verify with a meter). Exact channel pin mapping is locked in the schematic
  image (fetch tools can't read it); Paul to confirm the BSS138-with-10K channel from
  the PNG, or use a discrete CT-17-clone / single-BSS138 interface. No code involved.
- **Optional LoRa hardening** (set preamble/CRC/header explicitly in lora.cpp rather
  than relying on RadioLib defaults). LoRa hardware path is still UNTESTED in firmware.
- **Scope docs that remain design-only** (no code yet): CW decoder, SSTV capture,
  Doppler-corrected APRS/packet RX — all flagged higher-risk (audio DSP on no-PSRAM).

---

## 7. How to resume (checklist)

1. `cd /home/claude/cardsat/CardSat-main` — confirm it exists. If not, ask Paul to
   re-upload `CardSat-main.zip`.
2. Confirm `FW_VERSION` (config.h + ino) and run the balance check on all 36 files.
3. Confirm the four 0.9.31 features are present (grep `loadNoteForSat`, `visEvalPass`,
   `decayLevelFor`, `SCR_TRANSIT`) and parity is clean.
4. For any code change: read the real function first → edit `src/` → host-test the
   logic in g++ if there's any algorithm → mirror byte-for-byte to the `.ino` (heredoc
   with `assert count==1`) → balance all 36 → parity-check the new tokens → rebuild
   PDFs if docs changed → repackage zip → `present_files`.
5. Respect the framing rule: a new version's release notes headline **only** genuinely
   new items; prior-version content goes under a "From 0.9.xx (shipped previously)"
   heading. Don't fabricate release-notes files for versions whose real notes live only
   in Paul's repo.
6. Mind child-safety / weapons / malware refusal rules are irrelevant here — this is
   benign ham-radio firmware — but the **no-overclaiming** discipline (host vs
   hardware) and **Paul's authority over his own project** always apply.

---

## 8. Latest artifacts (in /mnt/user-data/outputs/)

- `CardSat-0.9.31.zip` (133 files) — full source tree.
- `CardSat.ino` — single-file build.
- `release_0.9.31_docs/` — RELEASE_NOTES_0.9.31.md, README.md, MANUAL.md,
  CardSat_Manual.pdf, CardSat_CheatCard_4x6.pdf.
- `scope_docs/` — the four 0.9.31 scope docs (VISUAL_PASSES, DECAY_WATCH,
  SUNMOON_TRANSIT, SAT_NOTES).

Manual PDF and cheat card are stamped **v0.9.31**. README has per-version "New in"
callouts for 0.9.31 and 0.9.30 (and older); all release-notes links resolve.
