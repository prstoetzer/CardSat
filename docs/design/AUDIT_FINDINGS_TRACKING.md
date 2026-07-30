# Audit findings — status and deferred work

This document tracks the third-party security/lifecycle audit findings for v0.9.64 and
their disposition. Two audit rounds were received (both in July 2026): the first covering
the USB additions and documentation, the second an expanded all-functionality pass. This
file records what was fixed for the v0.9.64 testing release and what is deliberately
deferred, so the remaining work can be verified independently without re-deriving it.

The v0.9.64 release is a **testing release**; the deferred items below are tracked for a
follow-up corrective pass and, where relevant, for hardware validation.

## Round 1 — fixed in v0.9.64

All high-priority findings H1–H10 and medium findings M1, M2, M4, M5, M6, M7, M8, M10,
M11, M12, M13, M14, M15 were addressed, along with the lower-priority style items (task
stack comment, `rig.cpp` magic numbers → `CatType` enum, filename normalization,
WIP/release vocabulary). See `RELEASE_NOTES_0.9.64.md` and the source comments tagged with
the finding IDs (`H1`..`H10`, `M2`, etc.).

## Round 1 — deferred

These are real but larger than a point fix, and are intentionally left for a dedicated
pass rather than rushed into a testing release:

- **M3 — Adapter registry access is unsynchronized across tasks.** The USB host task
  writes `s_serDev[]` entries while the main task reads them. `s_serDevN` is incremented
  last, which reduces but does not formally establish a barrier. Correct fix is a
  snapshot/critical-section handoff, which needs careful design and hardware
  fault-injection to validate. Low observed impact (writes happen during a bounded
  enumeration window), so deferred.
- **M9 — `drawSettings()` builds `String rows[110]` every redraw.** A performance/RAM
  optimization (render only visible rows, reuse a fixed buffer), not a correctness bug.
  Wants its own pass with before/after heap-churn measurement.
- **H3 enumeration-completeness half.** The scan now releases its temporary host (fixed),
  but still returns as soon as the first adapter appears rather than waiting a bounded
  quiet interval for a possible second adapter. Same root as the registry work (M3);
  deferred with it.
- **Unified USB resource manager.** The audit's ideal end-state is one explicit owner
  model (host generation + owner bits + one rollback path). The round-1 fixes address the
  concrete defects (H1/H2/H3/H7 rollback and registry reset); the wholesale refactor is
  the larger architectural follow-up.

## Round 2 — assessed; see per-finding disposition

The second round raised 33 new findings (H11–H19, M16–M39). The dominant theme is failure
atomicity and resource lifecycle outside the USB path. Disposition is tracked here; fixes
landed in v0.9.64 are noted, and the remainder are scheduled for the corrective pass.

### Fixed in v0.9.64 (round 2)

Landed and compiled (all mirrored src↔`.ino`, gates passing). Source comments carry the
finding IDs.

- **H16** — voice-memo `start()` frees the 4 KB capture buffer on the file-open and
  mic-begin failure paths (was leaking per failed attempt).
- **H18** — added `Location::endGps()` (end + delete the `HardwareSerial`, clear state),
  called before every `beginGps()` and when GPS is toggled off; no more UART/object leak on
  re-enable or source change.
- **M16** — Wi-Fi `ssid` / `pass` explicitly NUL-terminated after `strncpy`, like the
  adjacent fields.
- **M18** — Maidenhead parse accepts exactly 4 or 6 chars and validates subsquare letters
  A–X (was using chars 5–6 arithmetically without checking).
- **M19** — `toGrid()` rejects non-finite input and clamps poles/dateline so exact +90/+180
  can't emit out-of-alphabet glyphs.
- **M20** — LoRa RX frequency clamped to the SX1262 150–960 MHz range on the `,`/`/` keys
  and the config-row adjust (not just typed entry); one shared pair of bounds constants.
- **M22** — rigctl `xchg()` counts consecutive empty replies and, past a threshold of 3,
  marks the transport not-ready and closes the link (streak reset on a real reply or fresh
  connection), so a silent peer no longer imposes repeated 400 ms Doppler stalls behind a
  falsely "ready" status.
- **M25** — direct-Yaesu `outWrite()` honors the I2C `endTransmission()` result and drops
  `_ok` on a dead bus, so a stop/motor command isn't believed when the expander didn't ACK.
- **M26** — direct-Yaesu `point()` refuses a target when not ready/calibrated instead of
  returning success.
- **M28** — `Notes::list()` clamps the caller-supplied `nameCap` to the fixed 32-byte row
  width, closing the future-caller overflow hazard.
- **M38** — WAV finalize verifies reopen/seek/write of the 44-byte header and demotes the
  result to failure (discarding the file) instead of reporting a header-patch failure as
  "saved."
- **M21** — LoRa TX path now clears the software IRQ latch BEFORE re-arming receive, closing
  the window where a packet arriving between rearm and clear was dropped.
- **M31** — mutual-window predictor uses `new (std::nothrow)` + null check, degrading to
  "no windows" on low heap instead of dereferencing null.
- **M33** — `scanWifi()` remembers whether it was associated and rejoins the remembered
  network after the scan (new `rejoinAfterScan()`), so opening the scan no longer silently
  kills web control / rigctl / rotctl / printer sessions.
- **M34** — IPP capability probe drains bytes buffered after peer close (`connected() ||
  available()`) and reports when the 2,560-byte cap was hit, so a truncated read isn't
  mistaken for "format not supported."
- **M35** — removed the deprecated whole-response `Net::fetchGp()` and
  `Net::fetchSatnogsTransmitters()` (no callers; they allocated 200–400 KB RAM Strings that
  can't fit the heap). The streaming `...ToFile()` variants remain.

Also, **M23**'s magic-number half was already resolved in round 1 (`makeRig()` now uses the
`CatType` enum); the residual "fall through to wired for an invalid LAN+model combo" is a
deliberate defensive default (better than a null rig) and the invalid combo shouldn't be
reachable from the UI, so it is left as-is with this note.

Storage / durability cluster (added `Store::writeFileAtomic()` — temp-write → verify →
rotate live→backup → promote → restore-on-failure — as shared infrastructure, then routed
the vulnerable writers through it or the same pattern):

- **H12** — `httpsGetToFile()` rejects a body truncated at the `maxBytes` cap (new
  `DownloadError::BodyTooLarge`) and a chunked stream that never reached its terminal zero
  chunk, instead of returning success on a partial file.
- **H13** — transmitter-cache refresh downloads to a sibling temp and promotes only on
  success, so a failed refresh can't destroy the last known-good cache.
- **H14** — GP promotion rotates live→backup before renaming the temp in and restores the
  backup on failure, closing the no-catalog window.
- **H19** — `Settings::save()` is transactional via `writeFileAtomic()` (serialize to a
  String first); a power loss / short write can't truncate config.json into invalid JSON.
- **M29** — note writes routed through `writeFileAtomic()`.
- **M32** — `SatDb::saveTxCache()` routed through `writeFileAtomic()`.

Other round-2 fixes this cycle:

- **M17** — new `Settings::validate()` (called at the end of `load()`, so it also covers the
  restore-from-backup path) clamps the array-index enum `gpsSource` and non-finite /
  out-of-range `lat`/`lon`/`altM` and the QTH presets; the one unguarded `GPS_PROFILES[]`
  index in `app.cpp` is now bounded too.
- **M24** — `makeRig()` uses `new (std::nothrow)` for every backend; callers already
  null-check `rig`.
- **M27** — the GS-232 / Easycomm `puts_()` and SPID `putb_()` serial write helpers drop
  `_ok` on a fully-failed write, so `ready()` reflects a disconnected bridge / full stream
  instead of tracking believing commands land.

### Fixed in v0.9.65 (corrective pass — code- and compile-verified, not yet hardware-tested)

Three of the deferred items were completed in the v0.9.65 cycle. All edits are mirrored
`src/`↔`.ino`, all nine gates pass, and the firmware compiles clean.

- **Storage transactionality (H12, H13, H14, H19, M29, M32) — DONE.** The rotate/promote/
  restore sequence is now owned by a single shared primitive,
  `Store::promoteFileTransactionally(live, tmp)` (temp is pre-written+verified by the caller;
  it rotates live→`.bak`, promotes temp→live, and restores the backup on a failed promote so
  there is never a no-live-file window). `writeFileAtomic()` (config save, note writes, tx
  cache) and both download promotions in `net.cpp` (GP catalog, transmitter cache) now route
  through it, replacing three hand-copied variants. **Also fixed a latent bug this surfaced:**
  `SatDb`'s context-file rewrite (`satdb.cpp`) previously did a bare `remove(live)` then
  `rename(tmp, live)` with no backup — a real no-catalog window on a failed rename; it now
  promotes transactionally, and the "last entry removed" case cleanly retires the live file
  plus any stale backup. The remaining validation this item wanted — **power-cut
  fault-injection on hardware** — is still outstanding.
- **Input/boundary validation (M16–M20) — DONE (the validation-centralization half).**
  `Settings::validate()` gained the missing LoRa clamps: region (0–2), frequency
  (150–960 MHz, mirroring the `lorarx.cpp` M20 authorities), spreading factor (7–12),
  bandwidth (snapped to the supported ladder), TX power (−9…+22 dBm), and message-notify mode.
  `validate()` now also runs at the **start of `save()`**, so an out-of-range edit is clamped
  before it is persisted — not only caught on the next load (load/restore/migration were
  already covered, migration running inside `load()`). The Wi-Fi SSID/pass NUL-termination
  (M16) and the LoRa RX-frequency bound (M20 edit-path) had already landed in v0.9.64.
- **Wrap-safe timing (M36) — DONE.** Added `timeReached(now, deadline)` using signed-difference
  arithmetic (`(int32_t)(now - deadline) >= 0`) and converted all 11 absolute
  `millis() < untilMs` deadline comparisons in `app.cpp` (status banner, AOS/sked flash,
  playback-OOB banner, LOS handoff prompt, repeater-arm double-tap, keps animation). A wrap
  unit test covering the rollover boundary was added to `tools/host_orbit_audit/`.

### Deferred to the corrective pass (round 2)

- **SD remount recovery (H11).** Separate the selected backend from mount health so a
  transient SD failure after LoRa activity can retry SD instead of latching to an unmounted
  LittleFS. Needs hardware fault-injection.
- **Reset semantics (H15).** Split into **Reset settings** vs **Erase all CardSat data**
  (recursive `/CardSat` removal), and reconcile the manual. A UX + docs change requiring a
  preservation matrix and test.
- **Voice-memo lifecycle (H16, H17, M38).** Start-failure buffer leak, final-block cancel
  ordering vs async playback, and WAV-finalization failures reported as success. Needs the
  M5Unified `playRaw()` ownership contract confirmed on hardware.
- **GPS teardown (H18).** Add `endGps()` that ends+deletes the `HardwareSerial` before every
  restart and on disable. Hardware-validate against Grove pin sharing.
- **Backend health honesty (M22–M27, M39).** Distinguish transport-open from
  device-responding for rigctl and the serial/direct rotator backends; check I2C and stream
  write results; verify Kenwood MAIN/SUB mode writes on hardware.
- **Robustness (M21, M24, M28, M30, M31, M33, M34, M35, M37).** LoRa TX/RX IRQ ordering;
  unchecked `new` on backend/predict allocations; `Notes::list()` row-width hazard; logstore
  per-line open/flush cost; Wi-Fi scan disconnect restoration; IPP drain-after-close;
  removing/guarding the deprecated whole-response `net` APIs; converting long foreground
  operations to cooperative jobs.

## Notes

- Findings marked **potential** by the auditors (H17, M21, M34, M39) have a concrete source
  basis but need hardware or fault-injection to confirm; they are grouped with the deferred
  work above rather than treated as confirmed regressions.
- Nothing in either audit objects to the project's trusted-network / hobbyist / low-RAM
  design decisions; the findings are local lifecycle, state, and resource-ownership issues.

## CardSatDualRig audit (July 2026) — status

A dedicated end-to-end audit of the Dual-Rig companion path found 3 Critical + 14 High +
16 Medium. The three Critical are all CardSat-side confirmed code bugs (release blockers)
and are fixed. Findings split between CardSat firmware (`src/`, Claude maintains + compiles)
and the separate `companion/CardSatDualRig` M5StickS3 sketch (compiles independently).

### Critical — all fixed (CardSat side), compile-verified

- **C1** — `RigctlGroveRig::begin()` had the wrong arg order (RX+TX both landed on GPIO 1).
  Signature corrected to the base `begin(baud, uartNum, rxPin, txPin)`; RX=G1, TX=G2.
- **C2** — Grove baud couldn't hold 115200 (`catPort` is uint16_t, shared with TCP/LAN port).
  Added a dedicated `uint32_t catGroveBaud = 115200` with load/save, migration, `validate()`
  clamp to {9600,19200,38400,57600,115200}, a `makeRig` parameter, and all editor/display sites.
- **C3** — model parser bounded its loop by `sizeof(pointer)` -> 0, so zero models parsed
  while reporting a green link. Bound fixed to `DR_MAX_MODEL`; empty catalog now fails
  visibly (red link, "No models from companion").

### High — CardSat side, fixed

- **H9** — `dlOnSub()` forces the companion-correct downlink=VFOA mapping for the rigctl
  backends so the general VFO-layout setting can't reverse the two physical radios; the UI
  shows "(fixed: DualRig)".
- **H10** — new `groveCatVsGpsArbitrate()` enforces the Grove CAT vs Grove GPS conflict
  (both would open UART1 on G1/G2); called from the CAT-type, GPS-source, and GPS-toggle paths.
- **H11** — widened `DrDevice::serial`/`drSerial` from 20 to 24 to match the companion, and
  percent-encode serials containing spaces in `\csdr_set` (companion decodes in applyConfigKV).
- **H12** — baud-aware reply deadlines for large vendor replies (`\csdr_models`/`_get`/`_status`)
  so they don't time out at low Grove baud; ordinary RPRT commands keep the short deadline.
- **H14** — added per-leg CAT baud (`drBaud[2]`): parsed from status, editable on the setup
  screen (4th field per leg, sharing the CI-V row to fit the display), sent in `\csdr_set`.
- **M7** — Grove `linkOpen()` now probes for a live companion and backs off if absent, instead
  of marking an empty UART "ready" forever.
- **M8** — added `~RigctlGroveRig()` to close Serial1 on backend deletion.

### High/Medium — companion side (separate M5StickS3 sketch), fixed + compiles

The companion sketch builds clean (37% flash / 18% RAM). Fixed: H1 (portal no longer wipes
Wi-Fi creds; empty=keep), H2 (2 s refresh no longer clobbers edits; absent serials stay
selectable), H3 (`reconfigureAndRebind()` clears ports + rebinds live), H4 (Grove/TCP serviced
in config mode), H5 (Button A requires release before config-mode reboot), H6 (CI-V read
collects echo+reply, scans all frames), H7 (`f`/`i` return RPRT -1 on failed live read), H8
(per-port set/read settle), M2 (button labeling), M3 (freq/mode input validation), M4 (line
overflow rejection), M5 (JSON escaping), M6 (full TCP session reset), M14 (CAT trace off by
default), M15 (blank-SSID Wi-Fi guard).

### M16 documentation — corrected

Companion README + scope doc updated to say the CardSat integration IS implemented (was
"not built"); model-ID example corrected (FT-818 is model 17, not 11). The CDCOnBoot flag
claim was a false positive (`CDCOnBoot=default` == "Disabled", so prose and FQBN agree).

### Deferred (companion, lower priority)

M1 (richer runtime status per leg), M9 (hex CI-V entry / model-default reset), M10/M11
(RX-only labeling + cached-mode honesty beyond current PTT refusal), M13 (multi-client control
arbitration doc). None are release blockers; tracked for the companion's own iteration.

### Release posture

C1-C3 and the required Highs (H9-H12, H14) are fixed and both firmwares compile. Per the
audit, CardSatDualRig should still NOT be described as production-operational until the
two-radio regression matrix (one CI-V + one non-CI-V, over both TCP and Grove) is run on real
hardware; the v0.9.62 "not hardware-tested" honesty note stays prominent until then.

## 0.9.68 cycle-open documentation audit

Full doc audit (repo + on-device) against code ground truth; 22 finding groups, all
fixed or dispositioned in **docs/design/DOC_AUDIT_0_9_68.md**. Headlines: Nearby & DX
was almost entirely undocumented in MANUAL/README/FEATURES (now covered in §13, §22
×4 entries, §23 ×3 rows, README bullet, FEATURES bullets, cheat-card tile, on-device
help section); stale counts corrected everywhere (tools 55/60 → 63, reports 29/40 →
30-item menu of 50 total, games six → seven, orbit pages 9 → 11); Home documented as
the two-column grid it is (QRZ Lookup → Nearby & DX; stale t/q key claims removed);
`FILE_TELNET` comment and the "APRS.fi" banner corrected; 488-replacement American-
English pass over live docs and both source representations (parity gates green).

## 0.9.68 source-comment audit

Comments audited against current code and pinned libraries; findings dispositioned
in **docs/design/COMMENT_AUDIT_0_9_68.md**. Headlines: the usbserial.cpp forensic
comments described the removed finishUninstall()/poke machinery in the present
tense and claimed EspUsbHost "omits" the unblock escape hatch — rewritten as an
explicit HISTORY block after source-verifying the pinned 2.5.2 (end() self-unblocks,
3 s wait, checked+logged uninstall via releaseClientResources()/
uninstallHostLibrary(); begin() refuses over live handles); two vestigial includes
removed from BOTH representations (the .ino prologue had its own stale copies);
eleven "2.4.1" ongoing-behavior annotations → "2.4.1+"; /api/orbit "nine pages" →
count-free; API_STATUS.md → WEB_API.md; six/'b'-key/usbLastError()/Kenwood's
fixes; 13 more AmE stragglers (quantise/normalise/summarise family). Verified
correct: all three feed size-math comments, DXC wire order, 24 bands, 5 presets,
1018 stars, MAX_SATS=150, AO-7 constants, PA↔keyPrintAbout 30/30, games 0-6.
IMPORTANT session-memory correction recorded: the "resident host forever" design
is 0.9.58 history; current code fully tears down via the library's fixed end()
with the M2 ESP_ERR_TIMEOUT guard. Rebuild differs from the pre-audit binary
only in the two ESP-IDF image hashes (ELF SHA in esp_app_desc + trailing image
SHA, 65 bytes total); code/rodata/strings byte-identical, strings-diff empty —
proof the audit was documentation-only.

## 0.9.68 bench round 1 (dual rig) — three reports, and a gate for the family

Bench (N8HM) on the native dual-rig build:
1. **ESC from a dual-rig leg edit landed in the new-activation editor.** Edit
   targets 920-931 had no `editHome()` rule and fell into its `t >= 720 ->
   SCR_SKEDENTRY` catch-all. This is the SECOND time this exact trap fired — the
   Nearby & DX 900s did it first, and the fix comment for that is three lines
   above where mine fell in. Fixed (920-931 -> SCR_DUALRIG, above the catch-alls).
2. **"Where does the IC-705's IP go?"** The leg row rendered `LAN:<host>:<port>`,
   which reads as one field, and Settings' Host row said only "per leg". Now the
   row is `IP:<addr>  port:<n>`, non-LAN buses say "set Bus to LAN first", the
   edit titles name the format (`192.168.1.50`, "IC-705: 50001"), and MANUAL says
   explicitly that the address goes on the leg row, not Settings' LAN host.
3. **Settings row still read "Dual-Rig setup (Stick)" in native mode.** The row
   label was unconditional. Now it reads "Dual-Rig setup (2 radios) > IC-705+FT-817"
   (or "set legs") when catType is CAT_DUAL. Also fixed while there: entering the
   screen in native mode no longer calls `drAlloc()`/`drQuery()` — it was querying
   a companion that isn't in the picture, stalling on `\csdr_get` and reporting
   "No reply from Stick" on a screen with no Stick.

**New gate: `tools/audit_edit_home.py` (15th).** Parses the key-dispatch table for
handler->screen, interprets `editHome()`'s ordered rules, and verifies every
`editTarget = N` cancels back to the screen that launched it. It immediately found
five MORE pre-existing instances of the same family, all fixed:
- **760** target-search grid — had an explicit rule, but it sat BELOW `t >= 720`,
  so it was dead code; cancel went to the new-activation editor.
- **784** QTH preset name — no rule; same landing. (Commit was fine; only cancel
  was broken, exactly like finding 1.)
- **701 / 702** LoRa freq and TX power — fell into `t >= 500`, dumping the
  operator into the QSO log-entry editor.
- **350** grid-calculator target grid and **360** EME DX grid — fell into
  `t >= 320` to the Passes screen.
Allow-list (deliberate cross-screen cancels, each justified in the script): 104,
203, 210, 216, 230, 240, 326, 351 (one target, two legitimate launchers), 600, 710.

## 0.9.68 — Van Allen belt model replaced with IGRF-14 (L, B/B0)

Bench report (N8HM): satellites that never approach the outer belt were being
listed as outer-belt transits. Diagnosis: the model gated on `L >= 3 && altKm >=
1000` with L from a **centered dipole**. Because L = (r/RE)/cos^2(magnetic
latitude), L diverges toward the poles: a 1200 km satellite reaches L = 3 at only
51 deg magnetic latitude, so every high-inclination bird above the altitude floor
scored a belt transit over the auroral zone. The belts' equatorial altitude for
those shells is 12,700-38,200 km. An altitude floor cannot fix this — the flux
tube passes through every altitude at its horns.

**Replacement.** McIlwain (L, B/B0) from the real field:
- **IGRF-14** (IAGA 2024 release), degree 13, epoch 2025.0 with published secular
  variation to 2030, 1.2 KB of coefficients. Regenerate with `tools/make_igrf.py`
  from the archived IAGA distribution file in `tools/host_geomag/`.
- `shellAt()` walks the field line **downhill in |B| only**, stopping at the
  turning point: that minimum is the shell's magnetic equator, giving B0 and
  L (its geocentric radius in RE). One direction, no feet — 22-102 field
  evaluations for points actually in a belt.
- Membership = belt L range **AND** `B/B0 <= ZONE_BRATIO_MAX` (3.0, about
  |magnetic latitude| <= 30 deg). The altitude floor is gone; a 300 km atmospheric
  cutoff remains.
- `maybeInBelt()` is a ~20-flop analytic-dipole pre-filter with 3x margins that
  rejects the common (and most expensive to trace) case without tracing at all.

A criterion I tried first and rejected: the loss-cone fraction
sqrt(1 - B_sat/B_foot). It does not discriminate — it is ~0.4 even at ISS
altitude, because the locally trapped pitch-angle cone is wide anywhere above the
atmosphere. Measured, not assumed; B/B0 is the coordinate that tracks flux.

**Verification.** New `tools/host_geomag` (16th check) compares the extracted
evaluator against **ppigrf** (an independent implementation of the same model) at
six points from the surface to GEO: worst relative error 3.5e-5, i.e. float32
rounding. `tools/host_zones` extended with the regression cases (polar 1200 km at
50/65/70 N and 1400 km at 60 S must all read "not in belt") and updated for the
new physics.

**Side effect worth noting:** with a real field the **SAA now satisfies the
inner-belt test on its own** (ISS in the SAA: L = 1.23, B/B0 = 1.44) — the
anomaly is the inner belt dipping into LEO, which the centered dipole could not
reproduce and which is why the SAA needed a hand-drawn ellipse. The geographic SAA
zone is retained as its own entry deliberately.

Cost: +5.9 KB flash, +8 B static RAM. **Bench items:** confirm the zone scan's
run time on hardware for a high-inclination LEO (worst case for tracing) and for
a GTO/Molniya bird; confirm the status line's new `L=x.xx B/B0=y.y` readout.

## 0.9.68 orbital-decay model rework

Reviewed on request; the estimator turned out to predict ~1/5 of true remaining
life and to never land within ±30% of a real re-entry. Four defects (factor-2
da/dt error, a 38*B* constant tuned on ISS that masked it, no King-Hele
eccentricity factor — a GTO read 43 days — and perigee falling far too fast),
all detailed with the replacement and its scoring in
**docs/design/DECAY_MODEL_0_9_68.md**. Now anchored on the observed
MEAN_MOTION_DOT (already parsed, previously unused) with B* as fallback: median
1.05x, 89% within ±30%, validated against 244 objects that actually re-entered.
New gate tools/host_decay (16th). Two rejected-alternative notes worth keeping:
the loss-cone criterion tried first does not discriminate (it is ~0.4 even at ISS
altitude), and a density calibration cannot be applied to the anchored path
because it cancels exactly — the correction belongs on drag.

## 0.9.69 — external audit of the 0.9.68 release

An outside functional review of v0.9.64→v0.9.68 was evaluated finding by finding;
full disposition in **docs/design/AUDIT_RESPONSE_0_9_69.md**. Both P0 findings
were CORRECT and both were 0.9.68 regressions: (1) `Settings::load()` clamped
`catType > CAT_USB`, silently discarding the new `CAT_DUAL` on every boot —
the same bug the comment above that clamp documents from the CAT_USB era, now
fixed with a switch whitelist and covered by the new gate
`tools/audit_settings_clamps.py` (validated against the real defect); (2)
`applyRadioFromCfg()` tore down only CAT-A, stranding CAT-B with a CDC, an adapter
and the USB host — fixed by a single `App::usbCatTeardown()` used by both paths,
plus a third related defect the audit implied (the reconciler's outer gate also
tested only CAT-A). Mediums fixed: battery state centralized on
`batteryPercent()`/`batteryCharging()` so the web API cannot contradict the charge
screen; a minimal Telnet IAC refuser; RX-only uplink now refused rather than
warned; USB enumeration now waits for a bounded quiet period instead of stopping
at the first adapter, with a release fence publishing registry entries.
IMPORTANT correction to our own claim: 0.9.68's "zero warnings" was measured at
arduino-cli's default `--warnings none` (i.e. `-w`) and was meaningless; a real
`--warnings all` build reports 103 warnings, 70 ours. Three substantive ones fixed
(-Wreorder in the code I added, -Wint-in-bool-context, an unused array); the
remaining 67 are recorded as a tracked baseline, not accepted silently.
