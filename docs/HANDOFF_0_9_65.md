# CardSat 0.9.65 — session handoff

Status: **work in progress, partially bench-tested, several known-open defects.**
Do not treat this as a release candidate. See `OUTSTANDING_0_9_66.md` for the work queue.

---

## Current build state

| | |
|---|---|
| `CardSat.ino` md5 | `275e134dca330f346341cf9e361ee9f3` |
| `firmware/CardSat-merged.bin` md5 | `263bc21f168af4952f37069ee3ec2511` |
| `firmware/CardSat-app.bin` md5 | `3da8e0d740908501cac1c59fc7745295` |
| Flash | 2,965,934 bytes (94% of the 3 MB `huge_app` app slot) |
| Static RAM | 163,792 bytes (49%) |
| Compile | `EXIT=0` |
| Nine gates | all pass |
| §9 packaging parity | verified (zipped `.ino` == compiled; zipped `merged.bin` == firmware README) |

Flash is at **94%** and rising. The 8 MB partition expansion (below) is the release valve
and is now close to being needed rather than merely nice.

---

## Non-negotiable working rules

These are the rules that caused real bugs when broken. They are not style preferences.

1. **Dual representation.** Every change goes into BOTH `src/*.{h,cpp}` and the monolithic
   `CardSat.ino`, byte-identically. Diff each touched function body between the two before
   moving on. A stale `.ino` has silently shipped fixes-that-weren't more than once.
2. **Nine gates after every source touch**, all in `tools/`: `check_balance`,
   `check_parity`, `check_screen_text`, `check_settings_rows`, `check_defines`,
   `check_ino_dupes`, `audit_screen_geometry`, `check_stream_guards`, `check_switch_dupes`.
3. **Compile command** (must use `setsid`; sync the `.ino` into the build sketch dir first):
   ```
   cp src-tree/CardSat.ino /home/claude/fullbuild/CardSat/CardSat.ino
   setsid bash -c 'arduino-cli compile \
     --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,CDCOnBoot=cdc \
     --library /root/Arduino/libraries/M5Cardputer \
     --output-dir /home/claude/fullbuild/out /home/claude/fullbuild/CardSat \
     > /home/claude/fullbuild/build.log 2>&1; echo "EXIT=$?" > /home/claude/fullbuild/done.flag'
   ```
   Takes 6–8 minutes. Poll `done.flag`. Do **not** pass `build.extra_flags` (breaks HWCDCSerial).
4. **§9 packaging check** before presenting a zip: the zipped `CardSat.ino` md5 must equal
   the compiled one, and the zipped `merged.bin` md5 must equal the value written in
   `firmware/README.md`.
5. **Release notes live only in the release directory**, not the source tree. Recreate them
   on every cut.
6. **RAM is the binding constraint.** No PSRAM. Prefer streaming parsers, fixed buffers, and
   heap-on-demand over permanent `.bss` or long-lived `String` members.

---

## What this cycle contains

Delivered and code-verified (compile + gates + parity), unless noted:

- **Tiny BASIC** tutorial/reference (Fn+T) and twelve example programs, all re-audited
  against the live interpreter via a standalone host harness.
- **Narrow-paper print** layout fixes; Sun/Moon and BASIC-reference print paths.
- **Orbital thermal analysis** tool (first-order cubesat single-node model).
- **AO-7 mode-switch estimator**, substantially rewritten this cycle — see below.
- **Storage/timing/validation hardening** pass.
- **Charge/Sleep** rework — *partially working, still defective, see outstanding memo*.
- **String → fixed-buffer refactor** of six permanent-heap status members.
- **Print-report audit**: `printPassPolar` and `printAwards` now self-build rather than
  depending on a screen having been visited first.
- **"Nearby & DX" hub** with APRS.fi, DX cluster, and ADS-B radar — *new, lightly tested,
  several defects found and fixed on the bench, parsers still unvalidated against live data*.

### AO-7 estimator — the one substantial algorithm change

Worth understanding before touching it. The fit no longer does least-squares on bracketed
switch midpoints. It **maximises mode agreement**: for each candidate (period, phase,
parity) on a grid, it scores how much of the weighted evidence that hypothesis explains,
using every report rather than a handful of derived midpoints. That closes a structural
blind spot — the old objective gave zero weight to long confirmed single-mode runs, so
nothing stopped it predicting a switch in the middle of a stretch where the mode had been
repeatedly confirmed unchanged.

Supporting changes: 15-minute report resolution recovered from the API's `period` field;
two-stage coarse/fine search; horizon-gated "Not Heard" reports used as negative evidence;
geometry sanity filter on "Heard"; per-mode fetch caps (a real sampling-bias bug);
observation cache on LittleFS to extend the baseline past the API's 30-day window.

Host test: `tools/host_orbit_audit/ao7_agree_test.sh`, pinned to real 2026-07-24 AMSAT data.

**Open question:** the new objective fits ~18.5 h where the old one fitted ~19.5 h. They
genuinely disagree because they weigh different evidence. Only sustained comparison against
the live AMSAT status page will settle which tracks reality.

---

## Verification status — read this before trusting anything

- **Everything is code-verified. Very little is hardware-verified.**
- What the operator has confirmed working on hardware: the build boots; settings rows are
  reachable; the polar-pass and awards print fixes were the reported bugs and were fixed.
- What the operator has confirmed **broken** on hardware: charge/sleep display and battery
  reading; heap declining during operation; activations footprint lookup; several
  Nearby & DX UI defects (now fixed but not re-tested).
- **No feed parser has ever seen a live response** — not APRS.fi, not the DX feed, not
  ADS-B. Field-name mismatch remains the most likely failure for all three.

---

## Honest note on this session's defect rate

The "Nearby & DX" work introduced an uncharacteristic cluster of bugs, and the operator
was right to call it out. The pattern was consistent: **new UI was wired without reading
how the existing wiring worked.**

- Five editors were given `editTarget` numbers in the 900s without checking `editHome()`,
  which ends in a catch-all `if (t >= 720) return SCR_SKEDENTRY`. Every one of them
  returned to the new-activation editor.
- Two editors were opened without setting `editTitle`, so they displayed whatever title
  the previous edit had left behind.
- A band-filter key was bound to `b` without checking that `b` is the global screenshot
  hotkey.
- Settings *fields* were added without the corresponding settings *rows*, so nothing was
  reachable at all.

None of these needed hardware to catch. Each needed one grep of the existing mechanism
before using it. **Before adding a screen, editor, hotkey, or setting: read how the
existing ones do it, end to end, including where control returns afterwards.**
