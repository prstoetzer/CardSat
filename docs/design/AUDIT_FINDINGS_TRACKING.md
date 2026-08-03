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

## 0.9.70 — TH-D74/D75 (LEGF_KWHT) dialect was wrong; rewritten

Bench: TH-D75 enumerated on USB but ignored all CAT. The Kenwood-handheld dialect
was ported verbatim from the CardSatDualRig companion and never checked against a
protocol reference -- it was wrong in three independent ways, any one of which
would have caused silence: (1) it used a nonexistent `FQ` set-frequency command,
where the family actually has NO set-frequency command at all -- the frequency is
a field inside the `FO <band>` record and a set is a read-modify-write of that
whole record; (2) `MD` requires a space before its parameters (`MD 1,4`); (3) AM
and DV were transposed in the mode map. Verified against Hamlib
`rigs/kenwood/thd74.c`. Fixed: `legBuildReadFreqFrame` emits `FO <band>`, a new
pure `legKwFoPatch()` does the record patch (so the harness verifies the exact
bytes), `PlainCatRig::kwSendFreq()` does the query/patch/write sequencing, and the
parse reads the fixed ten digits at offset 5. Nine new harness vectors including
the echo-skipping and junk-refusing cases.

LESSON, recorded because it will recur: "ported verbatim from the companion" was
treated as equivalent to "validated". It was not -- the companion's encoders were
bench-validated for the radios its author owned, and the Kenwood-handheld path
evidently was not among them. Any dialect not yet driven against real hardware
should be checked against an independent protocol reference (Hamlib, or the
manufacturer's command document) before it is described as supported.

## 0.9.70 — Yaesu + Kenwood dialect audit (owner-requested)

Audited every non-CI-V leg dialect against Hamlib, after the TH-D74/D75 dialect
turned out to be wrong. Result: 2 of 4 correct as shipped, 2 radios in the wrong
family entirely.

**Correct, unchanged:**
- **LEGF_YBIN / FT-817, FT-818, FT-857, FT-897** -- opcodes 01/07/03, big-endian
  10 Hz BCD, and every mode byte (LSB 00, USB 01, CW 02, AM 04, FM 08, DIG 0A)
  match Hamlib's ft817.c / ft857.c / ft897.c, which are byte-identical here. Only
  change: round to nearest 10 Hz rather than truncate, as those backends do.
- **LEGF_YTXT / FT-991, FT-991A** -- `FA<9 digits>;`, `MD0<c>;` and the mode chars
  (1 LSB, 2 USB, 3 CW, 4 FM, 5 AM, C PKTUSB) match newcat.c exactly.
- **Kenwood SAT rigs (TS-790/TS-2000, kenwood.cpp)** -- mode digits 1/2/3/4/5 and
  6 = FSK, and the 11-digit `F<vfo>` form, match kenwood.c's mode table exactly.

**Wrong, fixed (new dialects LEGF_Y100 and LEGF_YVR5):**
- **FT-100** shared only the 5-byte frame length with the FT-817 family: frequency
  opcode 0x0A not 0x01, LITTLE-endian BCD (Hamlib `to_bcd`) not big-endian
  (`to_bcd_be`), mode byte in data[3] with opcode 0x0C not data[0] with 0x07, its own
  mode values (FM 0x06, DIG 0x05), read opcode 0x10 not 0x03, and a status reply
  whose frequency begins at offset 1 behind a band number. Nothing we sent applied.
- **VR-5000** shares the FT-817 framing, but Hamlib maps RIG_MODE_FM to MODE_FMN =
  **0x88** for it (plain 0x08 is absent from its table), and it has **no frequency
  read command** -- `vr5000_get_freq` answers from Hamlib's own cache. `LegProfile`
  gained a `canRead` flag (false only for the VR-5000) so knob-follow no longer
  polls a radio that can never answer and burns the read budget waiting.

Eight new harness vectors pin both. NOTE ON METHOD: two of my hand-computed
expected vectors were wrong (I wrote big-endian bytes for the little-endian case)
and the harness failed against correct code. Verified the code against the
algorithm before touching either -- the tempting error is to "fix" working code to
match a bad test.

Both fixes ported to companion/CardSatDualRig (standing instruction) and compiled;
binaries and README MD5s refreshed. FTX-1 remains unverifiable -- too new for
Hamlib -- and is documented as assumed-newcat.

## 0.9.70 — CI-V dialect audit (owner-requested)

Audited the CI-V leg dialect and the wired sat-rig CI-V path against Hamlib.
CI-V was in far better shape than the Yaesu/Kenwood dialects -- framing, encoding
and mode map all correct:

- Frame `FE FE <addr> E0 <cmd> ... FD`, controller 0xE0; set-freq cmd 05 with
  5-byte little-endian BCD at 1 Hz; read cmd 03; reply parse and echo-skipping.
- Mode bytes are exactly Icom standard (LSB 00, USB 01, AM 02, CW 03, FM 05), and
  RM_DATA -> 0x01 matches Hamlib's own PKTUSB -> S_USB.
- **All 14 CI-V leg radios are 5-byte frequency** (`civ_731_mode = 0` in every
  backend), and **all 14 default CI-V addresses match Hamlib byte-for-byte**,
  including the IC-705 (0xA4) and IC-905 (0xAC), which live in Hamlib's shared
  ic7300.c rather than files of their own.

**One defect, fixed:** the set-mode command always appended a filter byte
("06 <mode> <filter>"). Hamlib deliberately omits it for a named list -- "IC-375,
IC-731, IC-726, IC-735, IC-910, IC-7000 don't support passband data" -- of which
CardSat drives three: **IC-475 and IC-7000 as legs, IC-910 as a wired sat rig**.
Both catalogs gained a `modeFilter` flag and both senders honor it. Nothing checks
the CI-V ACK, so the failure mode was invisible: mode changes would simply stop
working on those radios with no error anywhere.

**One place Hamlib appears WRONG, deliberately not followed:** its `ic821h.c` sets
`civ_731_mode = 1`, which would mean a 4-byte frequency AND no filter byte. Four
bytes is eight BCD digits at 1 Hz -- a ~100 MHz ceiling -- which cannot express 145
or 435 MHz on a 144/430 radio; the file also uses `ic737_ts_sc_list`, an HF radio's
tuning-step table, suggesting a copied template. The three-byte mode form and
5-byte frequency are bench-proven on the owner's IC-821. The bench wins; no change
made, and the reasoning is recorded in radio_profiles.h so it is not "fixed" later.

METHOD NOTE: this is the counterweight to the TH-D75 lesson. "Check against a
reference" does not mean the reference is authoritative -- it means cross-check,
and when the reference and the hardware disagree, work out which is physically
possible before changing anything.

**Known limitation, documented not fixed:** Hamlib sends a 6-byte frequency to the
IC-905 above 5.85 GHz; CardSat always sends 5. Out of scope for the amateur
satellite bands a leg would use, and the IC-905 is already flagged untested.

Ported to companion/CardSatDualRig (standing instruction), compiled, binaries and
README MD5s refreshed.

## 0.9.70 — IC-905 six-byte frequency + FTX-1 verified against Yaesu's own manual

**IC-905 wide frequency (implemented).** Above 5.85 GHz the IC-905 takes a SIX-byte
CI-V frequency field (Hamlib icom.c: `if (RIG_IS_IC905 && freq > 5.85e9) freq_len =
6`). Five bytes is ten BCD digits, topping out just under 10 GHz -- it cannot
express the 10 GHz band at all. Both catalogs gained a `wideFreq` flag (IC-905
only), the pure builder takes it as a parameter, the CI-V read parser now accepts
either an 11- or 12-byte reply by locating the terminator rather than assuming an
offset, and the LAN path (IcomNetRig::setFreqNet, which also serves the IC-905)
applies the same threshold. Ported to the companion.

**FTX-1 verified -- against the MANUFACTURER'S document, not a third-party
implementation.** Yaesu publishes an official "FTX-1 Series CAT Operation Reference
Manual"; the PDF was retrieved and read directly. It confirms every element of
CardSat's YTXT encoding for this radio:
  * `FA014250000;` -- FA plus NINE digits plus ';' (matches `FA%09llu;`)
  * `FA;` to read (matches)
  * `MD P1 P2 ;` with P1 0 = MAIN-side (matches `MD0%c;`)
  * P2 mode characters: 1 LSB, 2 USB, 3 CW-U, 4 FM, 5 AM, ... C DATA-U -- all six
    CardSat uses match exactly, including DATA -> 'C'
  * 38400 baud on CAT-1/CAT-3 (matches the catalog default)
No change needed. The FTX-1 is now the BEST-verified radio in the leg catalog: it is
the only one checked against the manufacturer's own command reference rather than
against Hamlib.

METHOD NOTE (third occurrence): my hand-computed BCD expectation was wrong again
while the code was right. Expected vectors are now derived from the SPECIFICATION
in a short independent script (little-endian BCD, 2 digits/byte, 1 Hz) and
round-trip-checked before being hardcoded -- still independent of the C
implementation, but no longer dependent on my mental arithmetic.

## 0.9.70 — eight SSB-capable VHF/UHF radios added to the leg catalog

Scoped by the owner to radios that do **SSB on VHF/UHF**. That deliberately excludes
the FM-only candidates researched first (TM-D710(G)/TM-V71(A), TH-D72A, ID-5100,
ID-51, ID-31, IC-2730, IC-R6): they would have looked supported while being unable
to work a linear satellite, which is the same "configured but cannot function" trap
the RX-only-uplink refusal exists to prevent. The IC-375 was also excluded -- there
is no amateur satellite service at 220 MHz.

**Added, plain CI-V (catalog rows only, addresses + 5-byte frequency verified in
Hamlib):** IC-271 (0x20), IC-471 (0x22), IC-575 (0x16), IC-1275 (0x18) -- the
classic all-mode base stations, direct siblings of the IC-275/475 already present --
plus IC-706MKII (0x4E) and IC-706 (0x48), which have 2 m SSB but no 70 cm.

**Added, NEW dialect LEGF_KWTS:** TS-711 and TS-811. Generic Kenwood ASCII CAT --
`FA` + ELEVEN digits + `;`, `MD<digit>;`, 4800 baud, mode table 1 LSB / 2 USB /
3 CW / 4 FM / 5 AM / 6 FSK. This is the same encoding the firmware already uses for
the TS-790/TS-2000 as a full-duplex rig (verified against kenwood.c in the earlier
audit), so the new dialect is a near-copy of code already known correct. A
TS-711 + TS-811 pair is the canonical two-radio all-mode satellite station, i.e.
exactly the case native dual-rig was built for. Eight harness vectors, including one
asserting that a 9-digit Yaesu-style reply is REJECTED (the two ASCII families differ
only in digit count, so that is the confusable case).

**Persistence hazard handled, not ignored.** `cfg.dualModel` is a raw INDEX into
LEG_RADIOS, so inserting radios shifts every later entry and would have silently
repointed a saved leg at a different radio -- the same invisible-corruption class as
the stale CAT type clamp. Added `LEG_CATALOG_VER` and `cfg.dualCatVer`: on a
mismatch the leg selections reset to None and the operator re-picks. Resetting is
the honest outcome; driving the wrong radio confidently is not.

Ported to companion/CardSatDualRig, compiled, binaries and README MD5s refreshed.
None of the eight is bench-tested.

## 0.9.70 — dual-rig leg swap, and a visibility audit that found three clipped surfaces

**Swap (`x`) on the Dual-Rig screen.** Exchanges the two legs WHOLESALE -- radio,
bus, CI-V address, baud, LAN host/port/login and the USB adapter pin all move
together, then save + re-apply. Swapping only the radio names would leave a
configuration that was never valid (an IC-705 pointed at the other leg's LAN host,
say), so the swap is all-or-nothing.

**Visibility audit of the Settings entry and the Dual-Rig screen.** Text is 6 px per
character on a 240 px sprite and NOTHING truncates -- `canvas.print()` writes
straight in and the remainder is silently clipped. Budgets: 39 columns at x=4 or
x=6, 38 at x=10, 39 in the status bar (x=2). Three defects, all mine, all shipped:

1. **Settings row, up to 51 columns.** "Dual-Rig setup (2 radios) > IC-706MKIIG+
   IC-706MKIIG" lost the entire uplink radio name off the right edge. Shortened to
   "Dual rig: <dn>+<up> >" (35 worst case); the CAT-type row above already says
   "Dual (2 radios)", so nothing is lost by the shorter label.
2. **Status line, 41 columns.** "downlink only - uplink not CAT controlled" ->
   "DN only - UP not CAT driven".
3. **Bus row.** A real adapter label is "FTDI FT232R 0403:6001 #A50285BI" (31),
   and the row prefix already spends 17 of 38 columns -- about ten characters ran
   off. Now tail-truncated, keeping the serial number, which is the part that
   distinguishes two otherwise identical adapters.

Plus **three engage refusals over the 39-column status bar**, which is the worst
place to clip because the actionable half is at the end: "...move rotator off USB",
"...renominate", "...not an uplink".

**New gate: `tools/audit_status_width.py` (17th).** Sums the string literals in each
setStatus() call and fails when a single rendered branch cannot fit 39 columns. Two
corrections were needed to make it trustworthy, both caught by testing it against
real code rather than by assuming:
  * It first SUMMED ternary alternatives (`cond ? "a" : "b"`), only one of which
    ever renders -- immediate false positives. Now literals are masked (so a ':'
    inside a message is not read as a ternary) and branches are MAXed.
  * The flat 12-character allowance for an interpolated value was applied to the
    whole call rather than to the branch that actually interpolates, charging a
    long literal for a runtime value sitting on the other branch.
It then found **11 pre-existing over-width messages**, all shortened. One remaining
finding is allow-listed with its reason: the interpolated value is a 1-3 digit
constant, so the flat allowance over-charges it -- recorded rather than papered over
by distorting the message.

## 0.9.70 — list-wrap audit, three new gates, and a Doppler harness

**List wrap (reported: the MUF region list did not wrap).** Audited all 155 key
handlers: 106 selection movements. The codebase already distinguishes `*Sel` (which
item is highlighted -- must wrap) from `*Scroll` (how far down a document -- must
clamp, since a manual that jumps from the last line to the first is worse), so that
naming convention became the enforceable rule. Six handlers clamped and were fixed:
`keyMuf` and `keyMufMap` (the report), `keyBasicFiles`, `keyEme`, and both Dual-Rig
cursors. `keyDxc` was the interesting one -- it clamped inside a FILTERED list, so
the reachable range depended on the band filter; it now wraps to the first/last
VISIBLE row. On a 240x135 screen with no scrollbar, a clamped list is
indistinguishable from a key that has stopped working.

**New gate `audit_list_wrap.py` (18th)** enforces it, with one documented exemption
(`keyTgtSearch`, whose selection is normalised in its draw function where the count
lives). Validated by reverting the MUF fix and confirming it fails.

**New gate `audit_settings_persist.py` (19th)** -- every `struct Settings` field must
be both written in save() and read in load(). It immediately found a real bug:
**`rotMagCorrect` was never persisted at all.** It is a live Settings row (the
rotator's bearing reference, true vs magnetic) that calls save(), yet the field
appeared nowhere in settings.cpp -- so the operator could select magnetic, it took
effect, and it silently reverted on the next boot, leaving the rotor mispointed by
the local magnetic declination. Fixed. One exemption (`cfgFileMissing`, runtime-only).

**New gate `audit_table_sizes.py` (20th)** -- a table declared `T name[X_COUNT]` must
have exactly X_COUNT rows. The radio catalogs are enum/table pairs that the compiler
never compares; the catalog grew from 27 to 35 radios this cycle with the check done
by hand each time. Two corrections were needed before it was trustworthy: comments
and string literals desynchronised the brace scanner (it reported 14 rows for an
11-row table), and tables of scalars have no braced rows at all.

**New harness `tools/host_doppler` (9th)** for the most important math in the
product. Notably it does NOT merely re-run the functions: the round-trip cases
simulate the physical link independently -- ground TX, uplink Doppler, transponder
mapping, downlink Doppler -- and require the loop to close on the frequency the
operator parked. That simulation is written from the definition of Doppler shift and
of an inverting transponder, so agreement is evidence rather than tautology. All
zero-calibration round trips close exactly, in both directions, inverting and
non-inverting: the One True Rule implementation is correct.

TWO THINGS THE HARNESS FOUND, both worth recording:
  * My first simulation assumed the operator holds a Doppler-COMPENSATED uplink;
    the documented contract is that they hold the uncompensated `ulOp+calUl`. The
    harness was wrong, not the firmware -- the third time this session that a
    hand-written expectation lost to correct code.
  * **An open question about calibration semantics.** `dopplerFreqs()` adds calDl
    AFTER the Doppler shift (a DIAL correction); the round-trip functions fold it in
    BEFORE (part of the frequency). Both cannot be right, and the difference is
    exactly one calDl -- the same order as the Doppler correction itself for a
    few-kHz calibration. Zero-calibration behaviour is asserted strictly; the
    calibrated cases pin the CURRENT convention precisely, so a deliberate change
    shows up as a decision rather than drift. NOT changed unilaterally: which
    convention is intended is a judgement about what a per-satellite calibration
    means (it absorbs both radio and satellite oscillator error), and it would alter
    behaviour for anyone with saved calibrations.

## 0.9.70 — BASIC immediate mode, and the Doppler calibration defect (documented, unfixed)

**Immediate mode (SCR_BASICIMM).** The interpreter turned out to be already shaped
for it: `BasicVM::execLine(text, curIdx)` handles statement sequencing, `:`
separators and same-line FOR/NEXT resume, and `run()` is nothing but a loop over it
across the program's line table. So direct mode is "call execLine on the typed
line", and the language at the prompt is identical to the language in a program by
construction rather than by parallel implementation.

Design points worth recording:
  * The system snapshot was inline in `basicRun()`. It is now `App::basicFillSys()`,
    shared by both paths -- two copies would drift, and a BASIC whose SYS names mean
    something different at the prompt than in a program would be worse than no
    prompt. The prompt re-snapshots per LINE, so `PRINT SATEL` reads the elevation
    now rather than when the prompt opened.
  * `basImmVm` is a `void*` (a BasicVM*) because BasicVM is deliberately file-scope
    in app.cpp and must not be nameable from a header. Same for basicFillSys's arg.
  * The VM is ~3.8 KB, resident only while the prompt is open, and its line table
    goes unused. The existing BASIC screen-transition hook now frees it on every way
    out, alongside the program output buffer.
  * Program-only statements (GOTO/GOSUB/RETURN/DATA/READ/RESTORE) are REFUSED by a
    quote-aware whole-word scan. Two concrete reasons rather than tidiness: with no
    line table a GOTO silently falls through the interpreter's documented
    "out of range: fall through (classic)" path, and RESTORE/READ would leave the
    DATA cursor pointing into a String that dies when the handler returns. The
    handler also clears `forResumeP` and `dataP` after every line for the same
    lifetime reason.
  * EVERY navigation key sits behind Fn, following the editor's precedent. This is
    the trap worth remembering: the arrow keys ARE ';' '.' ',' '/', and a BASIC
    prompt cannot give up the decimal point, the PRINT separator or division.
  * Scrollback is trimmed from the front at 2 KB, because a prompt accumulates
    without bound and a permanently-held few KB starves the contiguous block a TLS
    upload needs (the same reasoning as basicFree()).

**Doppler calibration defect -- DOCUMENTED, NOT FIXED, by owner decision.**
`uplinkForFixedDownlink()` and `downlinkForFixedUplink()` measure the passband
displacement from the UNCALIBRATED nominal, so a calibration on one leg is treated
as a passband move and mapped (inverted, on an inverting transponder) onto the
other. At ZERO Doppler a +1500 Hz RX calibration moves TX by -1500 Hz, which cannot
be right. Confined to the hold-downlink / hold-uplink modes with a calibration set;
ordinary two-leg Doppler goes through dopplerFreqs() and is correct. The fix is one
term in each function (measure from the calibrated nominal) but it changes on-air
behaviour for anyone with saved per-satellite calibrations, so it is a judgement
call being held open deliberately. `tools/host_doppler` pins the current behaviour
and prints the defect loudly on every run so it cannot quietly become permanent.

## 0.9.70 — BASIC self-selects its satellite; twelve new system names; examples gated

**The real defect behind "the user shouldn't have to select anything first."**
`SATSEL i` already worked across the whole catalog, but `TXSEL` read `activeTx[]` --
the transponders loaded when the OPERATOR picked a satellite outside BASIC. So after
`SATSEL 42`, `TXSEL 0` silently returned the PREVIOUS satellite's frequencies. Not a
missing feature: wrong data, quietly.

Fixed by making `SATSEL` carry the transponder context. The hook's out[] gained the
transponder count, and BASIC got its own transponder view (`basTx`, 5 KB, allocated
on demand and ONLY when a program crosses to a satellite other than the tracked one
-- the tracked case reuses activeTx[] for free). `SATSEL` also clears `TXOK` so a
stale transponder cannot be mistaken for a fresh one, and `basTxNorad` resets at the
start of every run so nothing is inherited between programs.

**`TXOK` was never readable.** `sys.txOk` had always been set by TXSEL and was absent
from the name table, so a program could not test whether the snapshot succeeded. It
matters more now that SATSEL clears it. Found by the new examples harness, not by
reading.

**Twelve new system names**, all data the firmware already computes:
`LSHELL`/`BRATIO`/`BFIELD` (IGRF-14 shell geometry), `INBELT`/`INSAA`,
`DECAYD`/`DECAYSRC` (days to re-entry and whether it came from the measured n-dot or
a modelled B*), `BATTMV`/`CHARGING`, `HEAPBLK` (largest free block, which is what
actually limits an allocation), `DOPPRX`/`DOPPTX`, plus `TXOK`.

**New harness `tools/host_examples` (10th).** Runs EVERY shipped `examples/basic/*.BAS`
through the extracted VM with stub host hooks and fails on any parse or run error.
Justification: the examples are documentation people paste into a device, so a broken
example is a broken instruction. It paid for itself on the first run by finding the
missing `TXOK`, and then by exposing two STALE RULES in the examples README that had
been telling people the wrong thing:
  * "`:` does not chain statements" -- it does, and always did in this interpreter;
    `10 FOR I=1 TO 3: PRINT I: NEXT` works. Verified by running it.
  * "after `THEN` only a line number, PRINT, LET, GOTO or an assignment" -- the
    interpreter's own comment records that this subset was REMOVED because
    `IF C=3 THEN TEXT ...` used to fail; any statement is allowed now.
Also worth recording: two examples first appeared to FAIL under the harness, and the
cause was my incomplete stub (posOk/timeOk false), not the programs. The harness was
wrong, not the code -- the same lesson as the BCD vectors and the Doppler simulation.
It cannot check whether a PICTURE is correct; graphics calls are accepted and
discarded, so eyes on hardware are still required for the drawing programs.

**Examples:** `DOPPLER.BAS` now finds its own satellite instead of instructing the
operator to "pick one on Track first". New: `PICKSAT.BAS` (the self-sufficient
catalog-scan idiom), `BELT.BAS` (IGRF shell geometry, and why a high-latitude pass on
a belt field line is not a belt transit), `DECAY.BAS` (re-entry watch that always
reports whether each estimate was measured or modelled), `HEALTH.BAS` (battery, heap
vs largest block, uptime, element age).

## 0.9.70 — agency-tool audit correction, and zone dwell summary

**Correction to the previous session's audit: the "one clear gap" did not exist.**
I reported that CardSat had "no path to hand-enter classical elements for an object
with no TLE yet" (the ESA OPOT virtual-satellite comparison). Wrong: `n` on the
satellite list runs a complete manual GP entry chain (name, NORAD, epoch, all six
classical elements, BSTAR) committing via db.addGp(), with manual transponder entry
behind it -- which OPOT does not have. It is in the footer and fully documented in
the manual, including the deliberate never-auto-fetch rule for hand-entered birds.
My audit greps missed it because I searched for phrases ("manual element", "enter
elements") instead of walking the satellite-list key handler. METHOD LESSON: an
absence claim needs a walk of the relevant handlers, not a vocabulary guess --
grep proves presence, never absence.

**Zone dwell summary (implemented).** The one remaining item from the NASA/ESA
comparison. The orbital-zones screen reported transits and current L/B-ratio but
not accumulated dwell -- "SAA minutes per day" for SEU budgeting, "% of orbit in
the belt" for dose, which is the number a CubeSat or telemetry operator actually
wants from a zone tool. Implemented as a pure summary over the scan the transit
list already runs (same refined edges; open tail counts to the horizon so a bird
parked inside reads ~100%, entry before scan start clamps to the scan). Shown as
one grey line on the zone screen and a Dwell row on the printed report. The scan
window is 3-36 h, so min/day is a normalisation of the scanned orbits, not an
extrapolation claim -- the label says "scanned" for that reason.

Two implementation notes for the record:
  * The first placement (y=58) collided with the transit header (y=62) by 4 px --
    8 px glyphs. Width checks alone never catch vertical collisions; the column is
    now documented in the draw code (44/53/63/74).
  * The dwell arithmetic (windows -> %, min/day, with the open-tail and pre-entry
    clamps) was verified against an independent Python model before shipping:
    SAA-like 6x10min/day = 4.17% / 60.0 min/day, parked-inside = 100% / 1440,
    pre-scan entry clamps correctly.
  * NOT exposed to BASIC: dwell is scan-derived (a 4-orbit propagation), far too
    heavy for the per-line system snapshot. Documented as deliberate.

## 0.9.70 — UI pass (owner-approved items 4/5/6/7/8; item 3 scrapped, item 2 deferred, item 1 not approved)

**4. Palette: the last three 4bpp slots spent deliberately.** CL_DKRED 0x6000
(background for armed/destructive/refusal states -- red TEXT already means "bad
value"; a refusal should read at a glance before the words do) and CL_AMBER 0xCC40,
chosen for dual duty: amber-on-black is the de-emphasis color for asides, and
black-on-amber is the WARNING fill. Slot 15 stays RESERVED with a comment requiring
the next taker to document its semantics first -- repainting is cheap, re-deciding
what a color means across 150+ screens is not.

**5. Status bar severity.** setStatus() gained `StatusSev` (INFO dark green /
WARN black-on-amber / ERR white-on-dark-red), defaulting to INFO so 400+ existing
call sites are untouched. 73 call sites per representation reclassified by a
CURATED exact-string list (hard failures + refusals -> ERR; degraded/caution ->
WARN), applied identically to src/ and the .ino so parity held by construction.
Severity deliberately does not change duration.

**6. Scrollbar helper.** App::scrollbar(): 2 px DGREY track at x=238, proportional
GREY thumb (6 px minimum), draws nothing when everything fits. Replaces the ^ / v
edge arrows, which showed neither position nor extent -- the exact confusion behind
this cycle's list-wrap report. 15 standard arrow pairs converted mechanically by
one regex run on BOTH representations (counts matched 15/15); **21 arrow prints in
nonstandard shapes remain** and were deliberately left rather than forcing a regex
onto guards not individually read. Follow-up candidates, not silent debt.

**7. Footer vocabulary.** Census: 216x "` back", 41x "` bk", 17x "`bk", 5x "`back",
1x "` exit". Rule adopted: **"` back" unless the row cannot fit it, then "` bk"** --
nine footers use bk BECAUSE they are at the 39-column limit, so uniformity was the
wrong goal. 59 footers normalised per representation, width-checked before each
replacement. Two footers were ALREADY over budget (40/43 cols, clipping on
hardware); both trimmed ("status"->"stat"; dropped a stray "*") to land at 39.
The held-Fn overlay mentioned alongside item 7 was NOT built -- it needs accurate
per-screen Fn maps read from each handler, which is its own pass.

**8. Hierarchy on the dense exemplar (Dual-Rig setup).** Row lambda split into
grey LABEL / white VALUE (both black on the selection bar -- contrast beats
hierarchy on the row being acted on); DGREY hairline between the two leg blocks;
CIV/baud and IP/port blocks restructured to sequential prints with the same split;
the '*' catalog-default markers now render in CL_AMBER as the aside they are; the
"set Bus to LAN first" placeholder stays label-grey since nothing on it is
editable. Not yet applied to Settings/Orbit Info -- exemplar first, then extend
with the pattern proven.

Items NOT done, for the record: 3 (Track hero number + pass bar) scrapped by owner;
2 (LGFX Font2/Font7 typography) deferred by owner pending a flash-budget check;
1 (palette-swap night mode) proposed but not approved -- unimplemented.

## 0.9.70 — overlap/crowding audit of the UI pass (owner-requested)

Checked every NEW element against the actual geometry instead of asserting. Two
real defects found in my own work from the same session, both fixed:

1. **The Dual-Rig hairline struck text.** Rows TILE (fill y-1..y+8, next fill at
   y+9) -- there is no free pixel between them -- so the line at y-3 landed on the
   bottom pixel row of the previous text, straight through the descender of the
   'p' in "port:". Fixed by giving the line its own space (draw at y, advance 2);
   the resulting column is derived in a comment at the site: leg-1 rows 72..102,
   last fill ends 110, row 111 clear, hint at 112. Lesson recorded: WIDTH gates
   cannot see VERTICAL collisions; every new hline/rule needs the tiling checked.

2. **BandPlan's range column ran under the scrollbar.** Its buffer allows 39 chars
   printed at x=96 -- reaching x=330, i.e. it was ALREADY sprite-clipped at 240
   before this pass. Display now capped at 23 chars ((238-96)/6), ending x=234
   with 4 px clearance; nothing visible was lost because everything past column
   24 never rendered.

Verified clean, with the reasoning on record:
- **Scrollbar x-extent (238-239)**: strictly RIGHT of the old arrows (230-235), so
  any row wide enough to touch the bar would have collided with the arrows for
  years. Checked beyond that argument anyway: NOTE_COLS=39 at x=4 ends 237; memo
  rows peak ~29 chars at x=6 (ends ~180); Eqx 25; Loconv 16; TxDb truncates at
  37/38; tools badge "%2d" at 214 ends 225; static ref arrays covered by
  check_screen_text.
- **Scrollbar y-extent, all 15 screens**: no track enters the header (min yTop 18
  > 15). Planner's track bottom (128) sits in the footer BAND but footer text
  (x=2, <=39 chars) ends <=235 -- no x overlap. The transient status strip
  (114..124, full width, drawn later) covers the bar's lower end while showing,
  exactly as it always covered the arrows -- intended overlay, not a collision.
- **Dual-Rig severity/amber/label changes**: inline color changes only, no cursor
  or geometry deltas. Status bar geometry unchanged from pre-severity.

## 0.9.70 — USB code re-audit (owner-requested; CardSat + companion; findings only, no code)

Read in full: usbserial.cpp (all four surfaces: CAT-A, CAT-B, rotator, scan),
usbserial.h data path, the app-side engage/teardown/reconciler call sites, the
companion's USB layer, and the pinned library's bind/connected()/callback
semantics where CardSat's assumptions depend on them.

**FINDING A (design gap, dual-port sessions): no disconnect handling -- the adapter
registry accumulates stale entries while a shared host is live.** Only
onDeviceConnected() is registered; s_serDev[] is append-only, deduped by ADDRESS,
and reset only at fresh host start. A replug while ANY port is engaged (the
dual-rig and CAT+rotator sessions this feature exists for) enumerates at a NEW
address: the new entry appends, the stale one stays, and both share the same
serial-first KEY. All three resolvers (catPickAdapter, cat2PickAdapter, rotBegin's
inline loop) and waitForAdapterKey() take the FIRST key match -- the STALE
lower-index entry -- so a nominated-adapter engage binds a dead address. The
4-slot array can also simply fill. The library DOES provide
onDeviceDisconnected() (verified in the pinned 2.5.2 header); the companion
already uses exactly this (registerSeen update-or-insert + unregisterSeen on
disconnect), so the model to follow is in-tree. Note removal must respect the
existing publication rules (host-task writer, main-task readers): a tombstone
flag is safe where compaction is not.

**FINDING B (silent dead engage): CAT-A's FRESH path never verifies connected()
after setAddress().** The enum wait checks connected() at ANY_ADDRESS -- i.e. "the
first-enumerated adapter is ready". After catPickAdapter() the port is re-pointed
(setAddress + begin) at the PICKED adapter, and s_bound=true follows with no
check. connected() is address-specific (serialReady(address_), verified in the
library), so when the nominated adapter differs from the first-enumerated one --
or is a stale Finding-A address -- CAT-A reports ENGAGED on a port whose
connected() is false, and every Doppler write silently goes nowhere. cat2Begin()
and rotBegin() both do the bounded post-setAddress connected() wait; the REBIND
fast path checks too. The fresh path is the one surface missing it.

**FINDING C (M2 hole): hostUpForRotator()'s begin-failure path deletes the host
without the ESP_ERR_TIMEOUT check.** It runs `s_host->end(); delete s_host;
consoleUp();` unconditionally. Every other teardown site (end(), rotEnd(),
releaseHostIfIdle(), CAT-A's own begin-failure path) checks lastError() ==
ESP_ERR_TIMEOUT and RETAINS the object + latches s_hostTeardownStuck rather than
deleting under live tasks and reclaiming the PHY. Reachable from rotator-only,
CAT-B-first, and scanAdapters() engages. Same fix shape as CAT-A's freed/latch.

**MINOR 1: scanAdapters()'s temporary-release guard omits s_cdc2.** The condition
`if (s_host && !s_cdc && !s_rotCdc)` predates CAT-B. Harmless in effect --
releaseHostIfIdle() itself checks all three ports and refuses -- but the "scan:
releasing temporary host" trace line lies when CAT-B alone is engaged.

**MINOR 2: cat2Begin()'s already-open early-return does not check connected().**
An unplugged-while-engaged CAT-B reports success on a settings re-apply. Consistent
with active()/cat2Active() not tracking liveness (radio-level timeouts own that),
so recorded as a semantic note rather than a defect.

**COMPANION -- clean, with one hardening suggestion.** The RX ring is
portMUX-protected on both sides (host-task push, main-task pop) with correct
overwrite-oldest behaviour; the inert EspUsbHostCdcSerial control-line trick is
confirmed safe (no destructor exists anywhere in the class chain, so the temporary
neither attaches nor detaches); disconnect handling and update-or-insert already
implement the model CardSat's Finding A needs. Suggestion: in bindDevice()'s
enumeration-order fallback, a device whose serial matches SOME leg's pin can be
order-bound to the OTHER (unpinned) leg if the old address's disconnect event has
not yet been delivered on a fast replug -- both legs then drive one physical
radio. One guard ("never order-bind a device whose serial matches any leg's pin")
closes it.

Severity ordering for the fix pass, if approved: B first (silently dead CAT on the
nominated-adapter path is the worst failure mode), then C (use-after-free), then A
(the enabler that makes B likely in the field), then the minors.

## 0.9.70 — USB audit findings B, C, A FIXED (owner-approved, in that order)

**B (fixed): CAT-A verifies the PICKED address.** The fresh path now does the same
bounded post-setAddress connected() wait cat2Begin()/rotBegin() always had, with a
full unwind ("Radio adapter not responding") on failure -- disarm the freeze
watchdog, drop the port, releaseHostIfIdle() (M2-safe), clear state. The REBIND
fast path additionally gained the bounded wait before its existing check: after a
re-pin to a replugged adapter, CDC readiness can lag the bind exactly as on a
fresh engage.

**C (fixed): hostUpForRotator() honors the M2 timeout rule.** On a host-begin
failure whose end() reports ESP_ERR_TIMEOUT, the host object is RETAINED,
s_hostTeardownStuck/s_hostReleased latch, the console stays down, and the error
says "reboot to reuse USB" -- identical to end()/rotEnd(). The delete+consoleUp()
now runs only when release is confirmed.

**A (fixed): registry tombstones + update-or-insert.** SerialDev gained a volatile
`dead` byte; onGone() (registered via the library's onDeviceDisconnected at BOTH
host-start sites) tombstones by address with a single byte store on the host task.
onDev() reworked: address-dedup now checks LIVE entries only (a dead entry at a
reused address is a NEW device, not a duplicate); a matching KEY refreshes that
slot IN PLACE (tombstone -> fence -> rewrite -> fence -> un-tombstone, so
first-match resolvers find the live address at the unchanged index and keyed
devices can never duplicate); otherwise any dead slot is recycled; otherwise
append as before. One self-caught bug during implementation: the append path
inherits whatever the recycled index held before the last registry reset, so a
stale tombstone had to be cleared BEFORE the publication fence or the appended
entry was invisible to every resolver. All six resolver loops (catPickAdapter x2,
cat2PickAdapter x2, rotBegin x2) and waitForAdapterKey() skip dead entries; both
adapter-listing traces annotate "(unplugged)" so the log tells the truth.

NOT fixed, by scope: Minor 1 (scanAdapters' stale trace guard), Minor 2
(cat2Begin's already-open liveness), and the companion order-bind guard -- all
recorded above, awaiting a decision.

Bench notes: the replug matrix is the test -- engage dual-USB or CAT+rotator,
unplug one adapter, replug it (same and different port; through a hub), and
confirm: the picker shows no duplicates, re-engage binds the NEW address, and a
nominated engage with the adapter genuinely absent reports "not responding" /
"not found" instead of claiming success.

## 0.9.70 — USB audit minors + companion order-bind guard COMPLETED

**Minor 1 (fixed):** scanAdapters()'s temporary-release guard now excludes all
THREE ports (s_cdc2 was missing; it predated CAT-B). releaseHostIfIdle() always
enforced correctly, so this fixes the lying "scan: releasing temporary host" trace,
not a release bug.

**Minor 2 (fixed):** cat2Begin()'s already-open early-return now requires
connected(). A CAT-B whose adapter was unplugged while engaged no longer reports
success on a settings re-apply: the dead port is dropped (cat2End(); its
releaseHostIfIdle() is a no-op while CAT-A/rotator own the host) and the fresh
bind below runs -- which, with the finding-A registry, resolves the replugged
adapter's NEW address. "Apply settings again" is now the recovery it looks like.

**Companion order-bind guard (fixed):** serialPinnedToSomeLeg() -- a device whose
serial matches ANY leg's pin is never bound by enumeration order, at all three
sites (bindDevice's tryOrder, with a serial log line; bindSeen's tryOrder). Closes
the fast-replug hole where the new enumeration arrives before the old address's
disconnect event: the pinned leg still shows bound, tryPin skips, and tryOrder
would have handed the pinned radio to the OTHER leg -- both legs driving one
physical radio. Pinned serials are reserved for their legs unconditionally; the
late disconnect frees the leg and the device rebinds where it belongs.

**PROCESS TRAP (recorded):** /home/claude/compbuild/run_comp.sh builds a STAGED
COPY at /home/claude/compbuild/CardSatDualRig, not the tree's companion directory.
The first companion build after this change compiled clean at the OLD MD5 -- the
edit never reached it. Caught because the refreshed bin's MD5 was byte-identical
to the previous release, which a changed source cannot produce. Sync the tree's
.ino + catradio_types.h into the staging dir before every companion build; the
HANDOFF_MEMO standing instruction now says so.

Companion firmware/README.md MD5 refreshed (a3e00f3432997971765c47afcd63976a).

## 0.9.70 — TH-D75 CAT root cause (library defect) + stuck-host triage. NO blind fixes shipped.

**TH-D75: root cause identified in EspUsbHost 2.5.2's descriptor walk, not in CardSat.**
The two CDC-ACM interface latches are ASYMMETRIC:

    isCdcAcmControlInterface = class==CDC_CONTROL && subclass==ACM;   // NO guard
    isCdcAcmDataInterface    = class==CDC_DATA && hasCdcControlInterface
                               && !device->hasCdcDataInterface;       // guarded

(The keyboard latch nearby DOES carry !device->hasKeyboardInterface, so the missing
guard on the control interface reads as an oversight rather than intent.)

On a device with ONE CDC function this is harmless. On a device with TWO -- which is
what a TH-D75 is (CAT/COM plus KISS-TNC/GPS) -- the consequence is exact:
  * DATA endpoints stay on the FIRST function (guarded latch wins first).
  * cdcControlInterfaceNumber ends up on the LAST control interface parsed, and
    configureCdcAcm() re-runs against it.
  * Both control transfers address that number: SET_LINE_CODING (wIndex =
    cdcControlInterfaceNumber, EspUsbHost.cpp:10751) and SET_CONTROL_LINE_STATE
    (:10775).
So the baud/framing and DTR/RTS are applied to the WRONG CDC function while data
flows over the right one's endpoints. A CAT port that never receives its line coding
and never sees DTR asserted is precisely "enumerates, is detected, but nothing
works" -- and it explains why the DTR/RTS work that fixed other radios did nothing
here: the assert landed on the other function.

Candidate one-line fix (in the LIBRARY, not this tree):
    const bool isCdcAcmControlInterface = ... && !device->hasCdcControlInterface;
That makes control and data agree on the FIRST function. It does NOT let the app
choose WHICH function, so if the D75's CAT port turns out to be function 1 rather
than 0, the real requirement is an app-selectable CDC function index -- worth raising
upstream either way.

DECISION REQUIRED (not taken unilaterally): EspUsbHost is an external dependency the
owner installs, so patching it here would not reach his build. Options: (a) vendor a
patched copy into the repo, (b) document a required local patch, (c) upstream it and
pin the fixed release.

CONFIRMATION BEFORE ANY FIX: the library logs "CDC control interface ready: iface=%u"
and "CDC data interface ready: iface=%u" at ESP_LOGI. CardSat compiles at
DebugLevel=error so these vanish. A one-off build at DebugLevel=info with the D75
attached will show whether TWO control interfaces appear and which numbers were
latched. That turns this from a strong inference into a measurement.

**Stuck USB host: NOT diagnosed, and deliberately not "fixed".**
Two theories were tested against the code and BOTH were rejected before shipping
anything:
  1. "The finding-B failure path created a retry hammer loop." REJECTED: the
     reconciler's gate is `want = catUsesUsb() && radioOut && rig`, and the engage
     failure branch sets radioOut = false, so a failed engage stops retrying by
     itself. A backoff was written, then REVERTED as dead code that would also have
     refused the operator's own explicit 'r' retry.
  2. "Finding B increased teardown frequency for the D75." Rejected as the likely
     cause: the new check only fires when connected() is FALSE at the picked
     address, and a D75 that enumerates as a valid CDC reports connected() true --
     so that path should not be reached at all on this radio.
Most probable remaining explanation: repeated manual engage/disengage cycles while
chasing the D75, where one s_host->end() eventually hits its 3 s timeout and latches
s_hostTeardownStuck (M2) -- which is pre-existing, documented behaviour, not new.

WHAT WOULD SETTLE IT: the SD log already carries everything needed -- the "ENGAGE
ATTEMPT" header per attempt, the last stage reached, "## uninstall diag:", and
"## DISENGAGED: stack released=yes/NO". A copy of /CardSat/logs covering a session
that ends stuck will name the failing teardown and the stage. Guessing further
without it risks another change like the reverted backoff.

## 0.9.70 — "no USB devices beyond a powered hub": two real bugs, and what it explains

Bench report: with a powered hub attached, only the hub is visible.

Ruled out first (both checked, not assumed):
  * IDF hub support disabled -- NO: the Arduino esp32 3.2.1 sdkconfig for the S3
    carries CONFIG_USB_HOST_HUBS_SUPPORTED=y and CONFIG_USB_HOST_HUB_MULTI_LEVEL=y.
  * Device-slot exhaustion -- unlikely at ESP_USB_HOST_MAX_DEVICES=4 (hub + 3).

**Bug 1 (timing): the enumeration settle logic mistook the hub for the whole bus.**
The wait broke as soon as nothing new had appeared for 400 ms. A directly-attached
hub enumerates fast and satisfies that on its own -- while the IDF hub driver still
has to power its ports, wait bPwrOn2PwrGood, debounce ~100 ms per USB 2.0 s9.1.2,
reset 10-50 ms and run a full enumeration PER CHILD, sequentially. So the scan
settled at roughly 700 ms and reported the hub and nothing else. Fixed with
hub-aware budgets: enumCapMs() 2500 -> 9000 and enumQuietMs() 400 -> 1200 the moment
a hub is seen, and "settled" now additionally requires at least one NON-HUB device.
Applied to BOTH waits (hostUpForRotator's scan and begin()'s own enum wait, which
had the same flat 2500 ms cap).

**Bug 2 (selectability): a hub was registered as a selectable adapter.** onDev()
recorded every device. A hub has no serial OUT endpoint so it can never carry CAT,
yet it appeared in the Settings picker and an un-nominated engage could take it as
"the first adapter" -- which, before the finding-B check landed, reported ENGAGED on
a hub. onDev() now skips hubs entirely (still stamping s_lastDevMs so the settle
timer counts the hub's arrival) and sets s_sawHub instead.

**WHAT THIS LIKELY EXPLAINS BEYOND THE REPORT.** The Cardputer has ONE USB port, so
two adapters REQUIRE a hub -- meaning dual-USB CAT could never have enumerated its
second radio, and possibly not its first. This is a strong candidate for why native
dual-radio has never driven a real radio on the bench, independent of every dialect
and binding fix made for it. It does NOT explain the TH-D75 (single adapter, no hub
needed) -- that remains the library's CDC control-interface asymmetry recorded above.

**Slot budget, flagged not changed:** ESP_USB_HOST_MAX_DEVICES=4 must now cover the
external hub PLUS every downstream device. Hub + 2 CAT adapters + a USB rotator is
4 devices and may need 5-6 slots depending on whether the root hub consumes one.
Raising it costs ~512 B of vendor-RX buffer plus tables per slot and needs a FULL
rebuild (the core cache does not watch build_opt.h). Left at 4 pending a bench read
of how many devices actually appear; s_serDev[4] would need raising in step.

## 0.9.70 — EspUsbHost vendored with the CDC-ACM patch

Shipped third_party/EspUsbHost: upstream 2.5.2 (MIT, LICENSE and attribution
preserved) plus one patch, `0001-cdc-acm-bind-first-control-interface.patch`, adding
the `!device->hasCdcControlInterface` guard.

Verification performed, not assumed:
  * Upstream licence confirmed MIT via the GitHub API before vendoring anything
    (the installed copy ships no LICENSE file; the text was fetched from the repo).
  * The patch applies to a CLEAN 2.5.2 with `patch -p1 --dry-run`, and the result is
    byte-identical to the vendored src/EspUsbHost.cpp.
  * CardSat rebuilt against the patched library: EXIT=0, 0 warnings, and the binary
    MD5 CHANGED (753d11e5 -> 4a2fb1e3), which is the proof that the patched code is
    actually being compiled in rather than a stale library being reused.
  * The vendored tree is COMPLETE (all of src/ including EspUsbHostHid and keymap/,
    plus library.properties and keywords.txt). A first attempt copied only the two
    main files -- a partial library that cannot build is worse than none, so it was
    redone.

UPSTREAM_ISSUE.md is written and ready to file but NOT filed: it needs a GitHub
account, and the standing rule here is never to touch the git remote. It asks for the
one-line fix AND for the larger capability the fix does not provide -- an
app-selectable CDC function index, since binding "function 0, whole" is coherent but
still cannot reach a radio whose CAT port is the SECOND function.

STILL UNPROVEN: whether this makes the TH-D75 work. The asymmetry is objectively a
bug and the patch is objectively correct, but which CDC function carries the D75's
CAT port is unknown until the bench says so. If a patched build still fails, the
DebugLevel=info run described in the earlier entry is the next step, and the answer
is then the selectable-index feature rather than another guess.

## 0.9.70 — CORRECTION: how USB radios actually enumerate (sourced), and what it means for the D75

Researched against manufacturer documentation rather than inference. The picture is
NOT what my earlier dual-CDC hypothesis assumed, and the TH-D75 conclusion has to be
withdrawn.

**TH-D75 / TH-D74 -- ONE CDC-ACM port, not two.** Kenwood's own virtual-COM-port
driver page for the TH-D74/D75 (kenwood.com and www2.jvckenwood.com, "Virtual COM
Port Driver", v1.00, 16 Jan 2024) shows Device Manager listing a SINGLE entry,
"TH-D75 (COM3)", and describes selecting "this port number" -- singular -- in the
software. The driver package is `USB_CDC_Driver_TH-D75_V100.zip`, i.e. a CDC driver
distributed with an .inf.
=> The vendored EspUsbHost patch fixes a REAL asymmetry, but it almost certainly does
NOT explain the TH-D75 failure: with one CDC-ACM function, first-vs-last control
interface is the same interface. PATCHES.md and UPSTREAM_ISSUE.md must not be read as
"this fixes the D75".

**The genuinely dual-port radios are CP210x, not CDC-ACM.** Yaesu's own CAT manuals
(FT-710 CAT OM, FTX-1 CAT reference, SCU-17 driver manual) describe TWO virtual COM
ports presented by a "Silicon Labs Dual CP210x USB to UART Bridge": an **Enhanced COM
Port (CAT-1)** carrying CAT (frequency/mode) and a **Standard COM Port (CAT-2)**
carrying TX control (PTT, CW keying, FSK) or, optionally, CAT. Icom is the same
family: the IC-9700 CI-V manual in this project's own knowledge base documents
**USB (A)** and **USB (B)** as separate serial functions -- USB SEND/Keying selects
between `USB(A) DTR/RTS` and `USB(B) DTR/RTS`, and `USB (B)/DATA Function` sets
USB (B) to RTTY Decode or DV Data while CI-V rides the other.
=> For these radios the question is NOT "which CDC function" but "which CP210x
channel", which is a different code path in EspUsbHost (vendor-serial, not CDC-ACM),
and a different fix if one is needed.

**Remaining D75 hypothesis worth testing.** Kenwood shipping an .inf-based CDC driver
rather than relying on the in-box usbser.sys hints the descriptors may not be fully
class-compliant (the Linux kernel carries quirks such as NO_UNION_NORMAL and
"castrated device" handling for exactly this). EspUsbHost requires
`hasCdcControlInterface` before it will accept a data interface, so a non-standard or
union-less descriptor set could leave the device with no serial OUT endpoint at all --
which would present as enumerating fine, `connected()` false, and (since the 0.9.70
finding-B check) an honest "Radio adapter not responding".

**DECISIVE NEXT STEP, cheap:** `lsusb -v` for the TH-D75 from any Linux PC settles
all of this in one command -- interface count, classes, subclasses, union descriptors
and endpoint layout. Worth capturing for the IC-9700 and an FT-991A/FTX-1 at the same
time, since those answer the CP210x channel question. Until then no further USB
guessing.

## 0.9.70 — TH-D75 descriptors measured (owner's ioreg dumps). Hypothesis WITHDRAWN.

Owner captured `ioreg -p IOUSB -l` in COM and KISS modes. The device node's
`UsbDeviceSignature` decodes to the full picture:

    6621 2390 0001 | ef 02 01 | 02 02 01 | 0a 00 00 | 01 01 00 | 01 02 00
    VID 0x2166 (JVCKENWOOD), PID 0x9023, bcdDevice 0x0100
    bDeviceClass EF / SubClass 02 / Protocol 01  = IAD composite
    interfaces: CDC-ACM control (02/02/01), CDC data (0A), Audio control (01/01),
                Audio streaming (01/02)

**1. ONE CDC-ACM function. The vendored patch cannot fix this radio.** With a single
control interface, "first vs last" is the same interface. The patch remains a correct
fix for a real library bug and stays in the tree, but PATCHES.md is now scoped to say
so explicitly, and UPSTREAM_ISSUE.md's TH-D75 reproduction reference must be corrected
before filing -- it should cite a generic dual-CDC device instead.

**2. COM vs KISS changes NOTHING on the bus.** The two dumps differ only in sessionID,
registry ids, retain counts and IOKitDiagnostics counters. Same descriptors, same
interfaces, same endpoints. So the radio's mode setting alters only what the firmware
does with the bytes -- it is not a USB-level difference, and no amount of USB work will
distinguish the modes.

**3. `iSerialNumber = 0` -- the D75 reports NO serial string.** CardSat's makeKey() is
serial-first, so this radio can only ever be keyed VID:PID@address. Adapter pinning
across a replug is therefore address-based for the D75 specifically, which is exactly
the case the finding-A tombstone work makes safe. Worth knowing before anyone tries to
"pin" a D75.

**4. NEW LEADING HYPOTHESIS, tying BOTH bench symptoms: the USB AUDIO interfaces.**
The D75 is the first radio CardSat has met that presents USB Audio alongside CDC.
EspUsbHost claims audio interfaces unconditionally -- there is no "audio enabled" gate
-- and for an Audio Streaming interface with endpoints it sets up ISOCHRONOUS transfers
(AUDIO_ISOC_PACKETS = 8, buffer = wMaxPacketSize x 8). On a no-PSRAM S3, claiming and
running isoc transfers CardSat neither wants nor drains is a plausible cause of both
"CAT does not work" and "the USB host gets stuck and needs a reboot" -- an active,
unserviced isoc pipe is precisely the kind of thing that makes end() time out and latch
the M2 reboot-required state.

CONFIDENCE: moderate, NOT established. Whether the streaming interface is claimed
depends on its alt-setting layout, which the signature does not expose (alt 0 usually
carries zero endpoints and would be skipped by the `bNumEndpoints > 0` test). This is
the third USB hypothesis this cycle; the previous two did not survive measurement, so
nothing is being changed on the strength of it.

**CHEAP DECISIVE TEST:** build once at DebugLevel=info and plug in the D75. The library
logs "USB Audio isochronous OUT endpoint ready: iface=%u ep=..." at ESP_LOGI. If that
line appears, the hypothesis is confirmed and the fix is a second vendored patch gating
audio claiming behind an opt-in. If it does not appear, audio is innocent and the
remaining candidates are protocol-level (the KWHT dialect), not USB.

## 0.9.70 — USB diagnostic build (why ESP_LOG was a dead end, and what replaced it)

Bench needs the descriptor-walk facts, but they are emitted exactly when USB host
takes the PHY and the serial console dies. Hence a build that captures them to the
SD log. Three assumptions failed on the way, each caught by measurement:

1. **`DebugLevel=info` is the wrong lever.** It raises CORE_DEBUG_LEVEL, which only
   feeds LOG_LOCAL_LEVEL when USE_ESP_IDF_LOG is defined -- it is not. Cost: 75 KB
   of flash (99% full) for no benefit.
2. **`-DLOG_LOCAL_LEVEL=ESP_LOG_INFO` is silently wrong.** ESP_LOG_INFO is an enum
   constant, and esp_log.h tests LOG_LOCAL_LEVEL in preprocessor `#if` contexts where
   an enum identifier evaluates to 0. Must be numeric (`=3`).
3. **Even numerically it changed nothing, and the reason was the real find:
   EspUsbHost gates ALL of its own logging behind
   `#if CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR`.** On the stock FQBN
   CORE_DEBUG_LEVEL is 0, so every ESP_LOGI in the library is compiled out at the
   LIBRARY's level regardless of LOG_LOCAL_LEVEL or CONFIG_LOG_MAXIMUM_LEVEL. This
   was found only because the same guard swallowed the CS_DIAG macro and produced a
   compile error pointing straight at it.

Method note: a compile-time `#pragma message` probe reported "ESP_LOGI IS ENABLED"
while the strings were provably absent from the ELF. The probe was telling the truth
about the SKETCH; the library was gated separately. Two sources of truth disagreeing
is what exposed the layering -- neither alone would have.

**What shipped instead.** The vendored EspUsbHost now reports through a plain
`cardsatUsbDiag()` callback (C++ linkage; `extern "C"` collides with Arduino's
auto-prototype generator), compiled out entirely unless CARDSAT_USB_DIAG is defined,
and deliberately placed OUTSIDE the library's CORE_DEBUG_LEVEL guard. Seven call
sites: every interface as walked (number/class/subclass/protocol/endpoint count), the
CDC control and data latches, the serial OUT endpoint, and the audio isochronous
claim. CardSat captures into a 6 KB fill-and-stop ring under a short critical section
(never filesystem I/O from the USB host task) and drains whole lines to Logstore from
the main loop, with dropped bytes counted so a truncated capture cannot pass as
complete. The esp_log vprintf hook is RETAINED because ESP_LOGE *is* compiled in
(CONFIG_LOG_MAXIMUM_LEVEL=1), so IDF's own USB error lines still arrive.

VERIFIED, not assumed: all seven trace strings present in the diagnostic ELF; the
binary grew 572 bytes when they landed; and the NORMAL build is byte-identical to its
pre-diagnostic baseline (3,042,602 flash / 162,176 RAM, 20 gates green), so none of
this can reach a release.

Harness: /home/claude/diagbuild/run_diag.sh -- stock FQBN, so the core cache is shared
and a diagnostic build takes ~5 min rather than the ~20 min a DebugLevel change forces.

## 0.9.70 — TH-D75 diagnostic capture RESULTS (bench log). Three questions settled.

**1. THE AUDIO HYPOTHESIS IS DEAD.** The walk shows the audio interfaces present but
NO isochronous OUT endpoint claimed:
    iface 2: class=0x01 sub=0x01 proto=0x00 eps=0     (audio control)
    iface 3: class=0x01 sub=0x02 proto=0x00 eps=0     (audio streaming, alt 0)
    iface 3: class=0x01 sub=0x02 proto=0x00 eps=1     (audio streaming, alt 1)
No "AUDIO ISOCHRONOUS OUT endpoint CLAIMED" line. Alt 0 carries zero endpoints exactly
as predicted. CAVEAT: only the isoc OUT path was instrumented, so an isoc IN claim on
alt 1 would not have shown -- the D75's audio is radio->host, so IN is the likely
direction. Audio is cleared of causing the CAT failure; it is NOT yet cleared of the
teardown failure (see 3).

**2. THE USB LAYER IS HEALTHY. THE CAT FAILURE IS PROTOCOL-LEVEL.** Binding is exactly
right and the write path completes:
    iface 0: class=0x02 sub=0x02 proto=0x01 -> CDC CONTROL latched on iface 0
    iface 1: class=0x0a sub=0x00 proto=0x00 -> CDC DATA latched on iface 1
                                            -> SERIAL OUT endpoint 0x01
    cat: engaged -> tick: entered -> tick: write freq -> tick: done (tracking)
So CardSat opens the correct interface, asserts DTR/RTS, and writes frequency commands
to the right endpoint. The radio simply does not act on them. Every remaining candidate
is in the KWHT command dialect, NOT in USB. All USB work on this radio should stop.

**3. THE STUCK HOST IS D75-SPECIFIC AND REPRODUCIBLE.** Same firmware, same session,
two devices:
    TH-D75   : end: host stop 71222 -> end: done 72301  = 1079 ms, released=NO
    Prolific : end: host stop 46157 -> end: done 46233  =   76 ms, released=yes
A 14x slower teardown that then fails to release, only on the composite device. This is
the M2 latch firing CORRECTLY -- the library genuinely cannot release this device -- and
it explains the earlier "USB busy - reboot needed (259)" engage failure, which is the
latch from a previous stuck teardown, not a new fault. The remaining structural
difference between the two devices is the claimed AUDIO interfaces, so the audio-skip
patch is still worth doing -- for the TEARDOWN, not for CAT.

**4. NEW, UNRELATED DEFECT, caught by the retained ESP_LOGE hook:**
    E (61626) adc_oneshot: adc_oneshot_new_unit(98): adc1 is already in use   [x16+]
Something is calling adc_oneshot_new_unit() repeatedly on an already-open ADC1 without
freeing it. Bursts of 8 identical errors, twice within 1.5 s. Not USB-related; it is a
CardSat resource-management bug (battery sampling is the obvious suspect). Worth noting
that this vindicates keeping the vprintf hook: ESP_LOGE *is* compiled in, and it caught
a real bug nobody was looking for.

**5. Incidental confirmations.** The D75's adapter key is `2166:9023@1` -- address-based,
exactly as the missing iSerialNumber predicted, versus the Prolific's serial-based
`067b:23a3/FDCKb133812`. And the task-stack report shows Client used=2128 of 4096, so
the "used + 2048" guideline wants 4176 -- marginally OVER the current 4096.

## 0.9.70 — audio-skip A/B RESULT: hypothesis dead. Both symptoms point at the CDC IN endpoint.

**Audio is definitively NOT the cause of the teardown failure.** With
ESPUSBHOST_CLAIM_AUDIO=0 confirmed active in the log ("AUDIE interface 2/3 NOT
claimed"), the same TH-D75 still produced:
    60006 end: host stop -> 61084 end: done   = 1078 ms, released=NO
and, in a different boot with the identical build and device:
    38323 end: host stop -> 38404 end: done   =   81 ms, released=yes
So the failure is NON-DETERMINISTIC on one device, not a property of its audio
interfaces. Across six boots, four first-engages failed to release and two succeeded.
The ~1078 ms is consistent to within 3 ms across all failures -- that is a fixed ~1 s
timeout expiring, not variable work.

**The CAT failure is now precisely characterised.** The console log shows 94 x
"[CAT] VFO-A read: no valid reply" and ZERO successful reads. The self-test:
    [CAT-TEST] Addr 0x0  read=Y sat=N tone=N
    [FAIL] Downlink set     [FAIL] Uplink set
    [PASS] Downlink mode USB (first run only; 0 pass on the second)
CardSat's KWHT frequency path is FO READ-MODIFY-WRITE. It cannot work without a
read, and no read ever succeeds -- so "Downlink set" can never pass. Mode set is
write-only, which is why it is the one thing that sometimes passes. This is exactly
the owner's original report: "the self-test can see when the radio is set to the
correct mode, but nothing else works."

**UNIFYING HYPOTHESIS: the CDC bulk IN endpoint is not delivering.**
  * No reply bytes ever arrive -> every read fails -> FO RMW fails -> no frequency.
  * An IN transfer submitted but never completing is precisely what would make the
    library's end() block for a fixed ~1 s and then fail to release the host --
    including the non-determinism, since it depends on whether a read was in flight.
One cause would explain both remaining symptoms.

WHAT IS NOT YET KNOWN: the 0.9.70 diagnostic instruments the serial OUT endpoint only.
For the IN path we cannot currently see whether the endpoint was claimed, whether
allocateEndpoint() found a slot, whether usb_host_transfer_alloc() succeeded, or
whether a single byte ever arrived. Both failure paths there report via ESP_LOGW,
which is compiled out under the library's CORE_DEBUG_LEVEL guard -- so they are
silent. Next diagnostic: CS_DIAG on the serial IN claim, the slot/transfer allocation
results, and an arrival counter.

## 0.9.70 — receive-path instrumentation (build 3)

Added CS_DIAG to the serial IN endpoint claim, to BOTH of its previously silent
failure paths (no endpoint slot; usb_host_transfer_alloc failure -- each reported only
via ESP_LOGW, which the library's CORE_DEBUG_LEVEL guard compiles out), and an arrival
counter at the dispatch point in handleSerial(). The counter also reports whether any
EspUsbHostCdcSerial port ACCEPTED the bytes, which separates "the radio never replied"
from "the radio replied and CardSat discarded it" -- two very different bugs that look
identical from the CAT layer. Rate-limited (first ten, then every 64th) so a chatty
device cannot flood the 6 KB capture.

Four mutually exclusive outcomes, each with a different fix: IN endpoint never armed
(library/resource bug, and the slot/alloc lines say which); armed but zero arrivals
(radio-side -- command format or a D75 setting); arrivals with port match=NO (CardSat
routing bug); arrivals with port match=yes (KWHT dialect parsing). Verified all four
strings present in the ELF before shipping.

## 0.9.70 — receive-path RESULT: the TH-D75 never sends a single byte

Outcome 2 of the four. Three engages on the new build, all identical:

    -> SERIAL OUT endpoint 0x01 (this is the CAT data path)
    -> SERIAL IN endpoint 0x81 armed, size=64 (replies arrive here)
    cat: tick: entered -> tick: write freq -> tick: done (tracking)
    [zero "SERIAL IN rx #" lines]

So the bulk IN endpoint IS claimed and armed, no slot or transfer-alloc failure, the
transmit path runs to completion -- and **not one byte ever arrives from the radio**.
Combined with 94 x "no valid reply" and 0 successful reads in the console log, the
transport is exonerated end to end: CardSat opens the right interfaces, arms both
directions, and writes to the correct endpoint. The TH-D75 is simply not answering.

**CAVEAT, and it is a real one: this run has NO CONTROL.** All three instrumented
engages were the D75; the Prolific runs in the same log predate the rx counter. So
"zero arrivals" is not yet distinguishable from "the counter never fires". The
instrumentation sits in handleSerial()'s dispatch loop -- the same loop that calls
pushData() into EspUsbHostCdcSerial, which is what CardSat reads from -- so it should
be on the only path bytes can take, but that is reasoning, not measurement, and
reasoning has lost twice this cycle. ONE run with a known-replying radio on a
USB-serial adapter settles it: rx lines appear -> counter works -> the D75 is
genuinely silent.

**Teardown, unchanged:** 57885 host stop -> 58965 done = 1080 ms, released=NO, with
audio not claimed and the IN endpoint armed. Still a fixed ~1 s timeout. Since no IN
transfer ever completes for this device, a submitted-but-never-completing IN transfer
remains the best explanation for the teardown hang -- and would be the SAME root cause
as the silence, exactly as hypothesised, just with the radio (not CardSat) as the
party that never responds.

**Next, in order:** (1) the control run, to make the null result trustworthy; (2) with
the transport cleared, the investigation moves entirely to why the radio does not
answer -- D75 menu state (PC port / USB function), line coding, or command framing --
none of which is a CardSat USB question; (3) the adc_oneshot leak, still spamming
(now seen in bursts of 19).

## 0.9.70 — build 4: IN-transfer counters (removes the need for a control run)

The build-3 result rested on an ABSENT line ("no SERIAL IN rx"), which is the same
weak form of evidence that produced the false-negative risk the DIAG banner exists to
guard against. Build 4 counts raw USB IN transfer completions in handleTransfer() --
BELOW all class dispatch, so the number is independent of CDC routing, port matching
and every other layer -- and prints the total in EspUsbHost::end(), i.e. at EVERY
disengage, whether zero or not:

    ## IN-XFER TOTALS: completions=N bytes=N errors=N lastStatus=N

A printed zero is a measurement. This makes a control run unnecessary: if the counter
prints at all, the counting path ran. The first six completions are also logged
individually with endpoint, status and actual byte count, so a stalling or NAKing
endpoint is distinguishable from a silent one.

Implementation note: the counters had to be hoisted to file scope near the top --
end() is at line ~1666 and handleTransfer() at ~6900, so declaring them next to their
use site put them out of scope for the reporter. Caught by the compiler, not shipped.

## 0.9.70 — build 4 RESULT: the radio DOES reply, and the teardown mystery is solved

    <- IN xfer #1 ep=0x81 status=0 bytes=7    -> rx #1 port match=yes
    <- IN xfer #2 ep=0x81 status=0 bytes=64   -> rx #2 port match=yes
    <- IN xfer #3 ep=0x81 status=0 bytes=9    -> rx #3 port match=yes
    ## IN-XFER TOTALS: completions=5 bytes=153 errors=0 lastStatus=0

**Outcome 4.** 153 bytes arrive over the CDC IN endpoint and are ACCEPTED by the port
(port match=yes). The transport is proven working end to end, both directions. This
overturns build 3's null result -- which was the absent-line evidence I flagged as
weak, and it was indeed misleading. The counters that report positively are why we
know.

**THE TEARDOWN CORRELATION IS THE BIG FIND.** In the run where data flowed:
    88310 end: host stop -> 88390 end: done  =  80 ms, released=YES
In every run where nothing arrived:
    ...   end: host stop -> ...  end: done   = ~1080 ms, released=NO
That is the pending-IN-transfer hypothesis confirmed. A submitted bulk IN transfer
that never completes blocks the library's end(); when the radio sends data the
transfer completes and teardown is instant. The follow-on TOTALS block shows exactly
this: `IN xfer #1 ep=0x81 status=3 bytes=0` -- status 3 = CANCELED, the pending
transfer being torn down. So "USB host stuck - reboot needed" is NOT a library defect
in itself: it is the predictable consequence of disengaging while a read is
outstanding on a quiet device. FIX DIRECTION: cancel/flush the pending IN transfer
before calling end(), rather than letting it time out.

**AND THE CAT FAILURE IS NOW A CONTENT QUESTION.** The radio replies, CardSat receives
and accepts the bytes, yet the console still reports "no valid reply" 94 times. So the
KWHT parser is rejecting what arrives. The packet sizes are suggestive: 7, 64, 9, 64,
9. A Kenwood ASCII answer to `FA;` is ~14 bytes; 64 is exactly wMaxPacketSize, i.e. a
FULL packet in a longer stream. That pattern looks like a data stream (NMEA/GPS or
KISS), not CAT answers -- consistent with the D75's USB port being in the wrong
function, and with the owner's original remark that the self-test behaves differently
"when the radio is set to the correct mode".

NEXT: dump the actual bytes. Counts cannot distinguish "FA00145..." from "$GPRMC..."
or a KISS 0xC0 frame, and that distinction decides whether this is a radio setting or
a dialect bug.

## 0.9.70 — "USB host stuck - reboot needed" is a FALSE POSITIVE (sticky lastError)

The bench log that finally exposed it: a failing teardown completing in **1082 ms**,
then reporting `stack released=NO (reboot needed)`. But end()'s own wait is 3000 ms and
the client-shutdown wait is 2500 ms -- neither had expired, so end() did NOT time out.
It finished cleanly and was still judged a wedge.

Cause: `EspUsbHost::lastError_` is **sticky**. It is cleared in begin() and nowhere
else, while a dozen normal-operation paths set ESP_ERR_TIMEOUT (bulk OUT the device
never drained, control transfers it ignored, MSC Get-Max-LUN, ...). CardSat decides
whether the stack was released with `lastError() != ESP_ERR_TIMEOUT` AFTER end(), so
ONE timeout anywhere earlier in the session poisons the verdict for the rest of it.

This explains the whole symptom, and why it correlated with a silent radio: when the
D75 replies, nothing times out and the verdict is clean (released=yes, 80 ms); when it
stays silent, something times out, the flag sticks, and every subsequent disengage
reports a wedge -- after which CardSat's own latch refuses to re-engage with
"USB host stuck - reboot to reuse USB", which is why reboots appeared to be required.
The stack was almost certainly being released correctly the entire time.

FIX (vendored patch 4 + CardSat): added `EspUsbHost::clearLastError()` and call it
immediately before end() at all three sites that judge the release
(disengage, releaseHostIfIdle, the CAT-B/rotator-aware teardown). The test can now
only see a timeout raised BY end() itself.

NOT claimed: that the ~1080 ms is itself a fault. It is most likely the client task's
event-wait latency when no transfer completes to wake it -- benign, and it is under
every timeout in the path. Only the VERDICT was wrong.

Method note: this was found by disbelieving a number. 1082 ms sitting well inside a
3000 ms timeout is not a timeout, so the "timed out" verdict had to be coming from
somewhere else. Three earlier hypotheses died because they were reasoned rather than
measured; this one came from reading the measurement carefully.

## 0.9.70 — CORRECTION: the "94 no valid reply" lines are NOT the TH-D75

Attribution error, caught by checking which class emits them. "[CAT] VFO-A read: no
valid reply" is KenwoodRig::readSubFreq (kenwood.cpp), which sends "FA;". TH-D75 does
not exist in the main RADIOS table at all -- it is LEG-catalog only (LEGF_KWHT), so it
is driven by PlainCatRig::kwSendFreq, an entirely different reader. Those 94 messages
therefore came from a Kenwood BASE rig in some other part of the session, and say
nothing about the D75. Earlier entries that treated them as D75 evidence are wrong.

**The D75's actual reader is correctly sized.** kwSendFreq() collects into uint8_t
rx[96] and terminates on CR with n>15 -- ample for the ~73-byte FO record, and CR is
right for the D7x family (base rigs use ';'). So the reassembly theory does NOT apply
to the path the D75 uses. Its only real exposure is the read budget
(readBudgetMs, default 300 ms) if the second USB packet is late.

**A genuine latent bug WAS found while checking, and is fixed:** KenwoodRig's reply
reader capped storage at 64 bytes while its terminator test was rx.endsWith(";"). Any
reply longer than 64 bytes had its ';' consumed-but-discarded, so the test could never
fire, the loop burned its full 800 ms ceiling, and the parse ran on a truncated
string. Harmless for FA (14 bytes) which is all it currently sends -- so this is
robustness, NOT the D75 fix -- but it is a trap for any longer Kenwood query added
later. Cap raised to 160 (KW_RX_MAX) with the reasoning recorded at the site.

STATUS: the D75's CAT failure is still uncharacterised. What is needed is the hex dump
of the ~73-byte responses; the byte counts alone cannot distinguish an FO record from
anything else, and every inference drawn from counts this session has had to be
withdrawn.

## 0.9.70 — D75 configuration confirmed, stale evidence discarded, read-budget bug fixed

Owner confirms: CAT type = **Dual rig**, downlink leg = TH-D75 (USB), uplink = None,
single-rig model = IC-821 (inactive while CAT_DUAL is selected).

**Consequence: the 94 "[CAT] VFO-A read: no valid reply" lines are NOT evidence about
this radio and must be discarded.** They come from KenwoodRig::readSubFreq, which is
reachable only via makeRig() when catType != CAT_DUAL and the model is TS-790/TS-2000.
In CAT_DUAL the D75 leg is built by makeLegRig() -> PlainCatRig(LEGF_KWHT), which
sends "FO <band>\r", reads into uint8_t[96] with stop byte CR, and prints nothing of
the sort. The console log is a rolling capture spanning older configurations. Three
entries above cited those lines as D75 evidence; all wrong.

**REAL BUG FOUND AND FIXED: the global read budget starved the one dialect that needs
the most time.** applyRadioFromCfg does
    rig->setReadBudgetMs(constrain(effectiveCatRateMs()/4, 60, 200));
to stop a single CAT read blocking the cooperative loop. But PlainCatRig::kwSendFreq
was written around its own 300 ms default (`readBudgetMs ? readBudgetMs : 300`), and
that default is unreachable whenever the setter has run -- which is always. So the FO
read-modify-write, whose own comment says it "is meaningfully slower than the other
dialects", was capped at 200 ms. Floored at 300 ms in both kwSendFreq() and
PlainCatRig::readFreq() for LEGF_KWHT only; every other family keeps the global cap.

Honest scope: this plausibly explains INTERMITTENT failure (the one bench run where
bytes did arrive showed 64+9 twice = two ~73-byte FO records, i.e. the radio CAN
answer), but it does NOT explain the runs with IN-XFER completions=0, where the radio
sent nothing at all over ~21 s. That silence remains unexplained and is radio-side.

**legKwFoPatch verified by reading, not measurement:** it expects
"FO <band>,<10 digits>,..." with the frequency at offsets 5..14, matching the
documented TH-D74/D75 record and Hamlib's thd74 backend. If a record arrives, it
should patch. Confirming that needs the hex dump.

Self-inflicted note: the first attempt at this fix deleted the `uint8_t rx[96]; size_t
n = 0;` declaration along with the line it was replacing. Compiler caught it; a
reminder that multi-line anchors must be re-read, not just matched.

## 0.9.70 — TH-D75 sets WORK (read-budget floor), and the 5 kHz step is fixed

Owner confirms frequency setting now works after the KWHT read-budget floor. Remaining
symptom: the step was stuck at 5 kHz.

CAUSE, from the authoritative command reference (LA3QMA/TH-D74-Kenwood, commands/FO.md):
the FO record is 21 comma-separated fields --
  p1 band | p2 freq(10) | p3 offset(10) | p4 step | p5 TX step | p6 mode
  p7 FINE MODE | p8 FINE STEP | p9.. tone/CTCSS/DCS/...
legKwFoPatch() wrote p2 and copied everything else back verbatim, so p4 (step size,
code 0 = 5 kHz) and p7 (fine mode, off) were preserved -- and the radio snapped every
exact Doppler frequency to the channel step. Writing the right number and having the
radio round it is exactly the reported behaviour.

FIX: the patch now also forces p7 = 1 (fine mode ON) and p8 = 0 (20 Hz, the finest the
family offers; tables/finestep.md). 20 Hz is ~0.05 ppm at 435 MHz -- below the radio's
own oscillator drift, so it does not limit Doppler. Fields are located by COUNTING
COMMAS, not by fixed offsets, and each is rewritten only when it is exactly one
character wide, so an unexpected record is left alone rather than corrupted. Every
other field -- offset, mode, tone, CTCSS, DCS, URCALL -- is preserved byte-for-byte,
because those carry the operator's own setup.

NEW HARNESS tools/host_kwfo (11th): extracts legKwFoPatch() from src/rig.cpp and runs
it against a realistic TH-D75 record. Checks the frequency is written exactly, fine
mode and fine step are forced, ALL other fields survive byte-for-byte, malformed input
is refused rather than half-patched, and a command echo followed by the real record
patches the LAST match. All pass.

ALSO WORTH REVISITING (not changed): the reference documents an **FQ** command --
"Set frequency: FQ p1,p2" with a 10-digit Hz value -- i.e. a direct single-frame set.
src/rig.cpp currently asserts "There is no 'FQ' command on this family; that was the
single biggest error in the 0.9.68 encoder". That comment appears to be WRONG, and the
0.9.68 encoder may have failed for the read-budget reason found this cycle rather than
because FQ does not exist. FO read-modify-write now works and is left alone, but FQ
would be one frame instead of a round trip per set -- materially faster on a handheld.
Worth testing once the current fix is confirmed on the bench.

## 0.9.70 — TH-D74/D75 command-table PROVENANCE, and FQ withdrawn

**Sources, stated plainly.**
  1. LA3QMA/TH-D74-Kenwood -- community reverse-engineered reference (LA3QMA, with
     WM8S, M1HOG, AG6IE, KK4VCZ, DG6OBE). NOT official Kenwood documentation; Kenwood
     publishes no CAT reference for this family. Documents the D74; D75 compatibility
     is an assumption.
  2. Hamlib rigs/kenwood/thd74.c -- an INDEPENDENT implementation tested against real
     hardware. This is the stronger source and it corroborates the layout exactly.

**Corroboration is exact, and it validates our fix.** Hamlib patches the FO record by
ABSOLUTE CHARACTER OFFSET; CardSat counts commas. They land on the same bytes:

    offset  27 = p4 normal step   (Hamlib thd74_set_ts writes 27)
    offset  33 = p7 fine mode     (Hamlib: "thd74_set_freq_item(rig, vfo, 33, 1);
                                   // Turn fine mode on")
    offset  35 = p8 fine step     (Hamlib writes 35)

Derivation: "FO b," = 0..4, freq 5..14, ',' 15, offset 16..25, ',' 26, p4 at 27, ','
28, p5 29, ',' 30, p6 31, ',' 32, p7 33, ',' 34, p8 35. Two independent methods, same
characters. Hamlib's set_freq is also byte-identical in approach to ours --
`memcpy(buf + 5, fbuf, 10)` over a fetched record.

**FQ: SUGGESTION WITHDRAWN.** "FQ" appears ZERO times in Hamlib's thd74.c (it appears
3 times in th.c, the generic backend for OTHER radios). Hamlib's authors, working
against real hardware, chose FO read-modify-write for the D74 despite FQ being in the
LA3QMA table. That is a deliberate choice by the better-tested source, so switching
CardSat to FQ on the strength of a community table alone would be trading a working,
now-corroborated path for an untested one. If FQ is ever tried it should be as an
experiment behind the diagnostic build, not a replacement.

**NEW ITEM FOUND IN THE SAME READ -- worth acting on.** Hamlib ROUNDS the frequency to
the current step before writing (thd74_round_freq: `r = ts * round(f/ts)`), on every
set. CardSat does not: it writes the exact Doppler Hz and relies on the radio. With
fine mode now forced to 20 Hz, an arbitrary Hz value is not generally a multiple of
20, and how this radio treats a non-multiple is unknown -- it may round, truncate, or
reject. Hamlib rounding client-side suggests it matters. Cheap and safe to match:
round to 20 Hz before patching (max error 10 Hz, ~0.02 ppm at 435 MHz, far below the
oscillator drift). NOT changed yet -- the current build is at the bench and one
variable at a time.

## 0.9.70 — fine mode is NOT valid in FM (owner bench report); patch made mode-aware

Owner: "fine mode is not supported in FM" on the TH-D75. That matters a great deal,
because FM is exactly what FM satellites use -- forcing fine mode there would have
broken the case the fix was meant to help, on the birds most likely to be worked.

NEITHER reference documents the restriction. Hamlib's thd74_set_ts() selects fine vs
normal purely from the requested step size, with NO mode awareness at all, so it would
hit the same wall. This is an operator observation that is not in the community table
and not in the best independent implementation -- worth recording as such.

FIX: whitelist, not blacklist. Fine mode is forced only when p6 (mode) is one of
LSB(3), USB(4), CW(5), R-CW(9) -- the SSB/CW family, where fine tuning is meaningful
and works, and which is exactly the linear-transponder case that needs 20 Hz. For
every other mode (FM 0, DV 1, AM 2, NFM 6, DR 7, WFM 8) the fine fields are left
untouched and p4 is set instead to the finest NORMAL step (code 0 = 5 kHz), so Doppler
is as good as the mode permits. On a 435 MHz FM bird that leaves at most 2.5 kHz of
error -- inside FM's own bandwidth, so no practical loss.

A whitelist was chosen deliberately: the bench reported FM, but DV/DR/AM/WFM are
equally likely to reject fine mode and nobody has tested them. Naming the four modes
that are known good fails safe; blacklisting only FM would have guessed about five
others.

HARNESS EXTENDED (tools/host_kwfo): eight mode vectors, one per code, asserting fine
mode ON with step untouched for LSB/USB/CW/R-CW, and fine fields untouched with p4
forced to 5 kHz for FM/NFM/AM/DV. It also caught a stale expectation of my own -- the
original base vector carried p6=0 (FM), so after this change it correctly stopped
forcing fine mode and the old assertion failed. Vector corrected to USB. That is the
harness doing its job on the author.

## 0.9.70 — USB wedge made RECOVERABLE; adc_oneshot leak fixed; Mac-side probe added

**The D75 remains unreliable. These changes do not claim to fix it** -- they stop the
failure being terminal, and add the one measurement that can separate the two
candidate causes.

**1. The wedge no longer demands a reboot.** Two gates refused outright and latched
until power-cycle: begin()'s s_hostTeardownStuck check and the s_hostReleased check.
Both are now ONE RETRY. Justification: the verdict that sets these flags was
frequently a false positive (the sticky-lastError finding), and even a genuine
teardown timeout may have completed in the seconds before the operator tries again.
usb_host_install() refuses over a live stack and reports 259 on its own, so the
hardware answers instead of a latch set a minute earlier. Flags clear on a successful
engage; on a second consecutive failure the message says the retry was already spent
("USB stack still held after retry - reboot to clear"). Worst case is a clear error
instead of a forced reboot; best case it simply works.

**2. adc_oneshot spam fixed -- and it was producing nothing.** batteryMilliVolts()
looped analogReadMilliVolts() EIGHT times. When ADC1 is already claimed (M5Unified
holds it) each call fails, logs "adc1 is already in use", and returns 0 -- so the loop
logged eight errors to produce the same 0 that `if (!n) return 0` returns anyway. Now
bails on the first failure (one line, same answer) and caches for 2 s, which also
removes the repeat traffic from the several callers that hit it in one frame (charge
screen, About, /api/status, BASIC BATTMV). That accounts for both the bursts of 8 and
the two bursts 1.5 s apart in the bench log.

**3. tools/thd75_probe.py -- the measurement that has been missing.** Drives the D75
from a Mac over its known-good CDC-ACM stack using EXACTLY CardSat's sequence:
"FO <band>\r", patch offsets 5..14, mode-aware fine fields, write back, CR-terminated.
Verified against CardSat's own host-harness vectors: identical output for both a USB
record (fine ON, 20 Hz) and an FM record (fine untouched, normal step -> 5 kHz).
Reports read latency (to test the 200 ms starvation theory directly), a single exact
write, and N consecutive writes -- the actual reported failure. Restores the original
record when done, touches no memories or tones, transmits nothing.

The three outcomes are mutually exclusive and each names the next step:
  all pass on the Mac        -> commands correct; fault is CardSat's USB transport
  reads/writes fail there    -> command set or radio config; USB layer exonerated
  intermittent on the Mac    -> the radio is marginal at this rate on any host

## 0.9.70 — MAC PROBE RESULT: transport exonerated, and TWO of my conclusions overturned

Owner ran tools/thd75_probe.py on a MacBook against the TH-D75. Result is unambiguous
and it invalidates work I shipped.

**1. READS ARE 1 ms.** Not 200, not 300 -- one millisecond, 5/5, every time, for a
72-byte record. The read-budget starvation theory was WRONG, and the "budget floor"
change (kwSendFreq/readFreq floored to 300 ms for LEGF_KWHT) was addressing a problem
that does not exist. The single bench session that appeared to work after it was
coincidence, and I treated it as confirmation. That floor is now unjustified; it is
harmless but should be reverted once the real cause is fixed, so the code does not
carry a fix for a fiction.

**2. EVERY WRITE RETURNS "N" -- a Kenwood NAK.** The radio understands the command and
REFUSES it. 0/10 and 0/60 exact on two runs. Decisively: writing back the COMPLETELY
UNMODIFIED record that the radio had just returned is ALSO refused. So this is not
CardSat's patching, not field widths, not fine mode, not the frequency value, and not
the transport -- the radio is declining FO writes outright.

**3. THE USB TRANSPORT IS EXONERATED.** Same commands, same record, same CR framing,
over a known-good CDC-ACM stack, fail identically. Every USB theory this cycle --
audio claiming, packet reassembly, endpoint selection, read budget -- is now dead, and
so is the premise that CardSat's EspUsbHost path is what breaks this radio.

**LIKELY CAUSE, to be confirmed by the updated probe.** Band A sits on 144.390 MHz --
the North American APRS frequency -- with a 600 kHz offset. FO sets the *VFO channel*.
Two states would make the radio refuse that, and both are queryable:
  * VM <band> reports VFO(0) / MEMORY(1) / CALL(2). In memory or call mode there is
    no VFO to write.
  * TN reports TNC off(0) / APRS(1) / KISS(2) and which band it owns. A TNC holding
    band A would hold its frequency.
BC (control band) is a third candidate.

Probe extended: it now dumps ID/BC/VM(A)/VM(B)/TN/DL before writing, flags the
suspicious state in plain language, and -- when a write is refused -- tries the
recoveries in order (force VFO mode, make it the control band, turn the TNC off and
restore it, then the other band) reporting which one unblocks it. That names the exact
precondition CardSat must satisfy, e.g. "send VM <band>,0 before FO".

METHOD NOTE, the important one: the Mac probe answered in two minutes what six
firmware iterations could not. Every one of those iterations reasoned from CardSat's
own logs, where the radio's behaviour and CardSat's behaviour are confounded. Removing
CardSat from the loop was the step that should have come first.

## 0.9.70 — probe round 2: the D75 refuses FO WRITES, and state is not the reason

Owner's second run, with the state dump. The picture is now precise and it kills the
memory-mode theory as well.

    ID TH-D75        BC 1 (control band = B)
    VM 0,1  -> band A is in MEMORY mode        VM 1,0 -> band B is in VFO mode
    TN 0,0  -> TNC OFF on band A               DL 1
    band B: FO 1,0029399860,...,4,1,...  (29.3998 MHz, mode USB, fine mode ON)

Results:
    FO <band> READ        works, 1 ms, both bands
    BC 0      SET         ACCEPTED (71 ms, replies "BC 0")
    VM 0,0    SET         "N"  -- the radio will not even leave memory mode
    FO 0,...  SET         "N"  -- band A, memory mode
    FO 1,...  SET         "N"  -- band B, ALREADY IN VFO MODE
    unmodified record     "N"  -- writing back exactly what was read

**So the refusal is NOT about state.** Band B is in VFO mode, the TNC is off, and its
FO write is refused identically. And BC proves the radio does accept *some* set
commands over this port, so it is not in a blanket "reads only" condition. What fails
is specifically the FO write -- on either band, in either mode, even when the payload
is byte-identical to what the radio just emitted.

Working conclusion: **this radio does not implement FO as a SET**, whatever the D74
reference and Hamlib's D74 backend do. That is a model difference, not a state
problem, and it means CardSat's entire KWHT frequency path -- FO read-modify-write --
cannot work on a TH-D75 no matter how it is tuned.

NEXT TEST (probe updated): try **FQ** -- "Set frequency: FQ p1,p2", 10 digits -- which
I previously talked the owner OUT of on the grounds that Hamlib's thd74.c does not use
it. That reasoning was weak: Hamlib targets the D74, this is a D75, and the direct
set command is exactly what a radio without FO-write would need. The probe now does
FQ get, FQ set, verified readback, plus MD get, and reports which writes the radio
accepts at all. If FQ works the fix is large and simple: one frame per set, no round
trip, and the read-budget/fine-mode/patching machinery becomes irrelevant.

Also noted for later: band B shows mode=4 (USB) with fine mode ALREADY 1 -- consistent
with fine mode being valid in USB and not in FM, exactly as the owner reported.

## 0.9.70 — TH-D75 KWHT path REBUILT on measured behaviour (FQ), band B confirmed

The Mac probe produced a complete specification, and the KWHT path is rebuilt on it.
Three preconditions, every one measured on the owner's radio:

  1. VFO mode           "VM <band>,0"   -- memory mode refuses everything
  2. CONTROL band       "BC <band>"     -- this is what finally unblocked the write.
                                           VFO mode alone was not enough, and BC alone
                                           was not enough while the band was in memory
                                           mode. Both together worked.
  3. ON-GRID frequency  -- verified 10/10 against the bench data: every ACCEPTED write
                           was a 5 kHz multiple, every REFUSED one was not. The radio
                           does not round; it rejects and echoes the old record. What
                           looked like "intermittent writes" was entirely this.

And "FQ <band>,<10 digits>" is accepted with a matching readback -- a single-frame set,
no round trip. The owner pushed for FQ and was right; my reason for dismissing it
(Hamlib's thd74.c does not use it) was weak, since the D74 is not this radio.

CHANGES: legBuildFreqFrame emits FQ for LEGF_KWHT; sendFreq rounds to the grid
(kwGrid(): 20 Hz fine, else 5 kHz) and calls kwEnsureSession() for VM+BC once per
attached stream (~70 ms each, far too slow per set); sendMode calls
kwApplyStepForMode() to set FT/FS, fine mode in SSB/CW only; setExternalStream clears
_kwSession so a USB re-engage re-sends the preconditions. The FO read-modify-write
(kwSendFreq, legKwFoPatch) and its harness are DELETED, not left as dead code.

REVERTED: the 300 ms KWHT read-budget floor. The probe measured those reads at 1 ms,
5/5. That fix addressed a problem that never existed and I had taken one coincidental
working session as confirmation of it.

BAND: CardSat already targets band B (LEG_KWHT_BAND = '1'), which is correct -- band B
is the all-mode receiver and can cover SSB/CW linear birds AND FM, where band A is
FM-only. The probe defaulted to band 0, which is why it exercised the FM side; both
scripts now default to band B.

SELF-INFLICTED, recorded because the gates earned their keep: a cleanup regex matched
legKwFoPatch as a DECLARATION in the concatenated .ino, brace-matched forward and
deleted the whole PlainCatRig class; parity caught it and the class was spliced back
from src/rig.h. The same edit left an UNTERMINATED "#if 0" in rig.h that would have
silently commented out the class in the PlatformIO build -- invisible to the Arduino
build. And killing a build with pkill corrupted the sketch cache into a bogus RadioLib
link error, costing a full rebuild.

NEW: tools/thd75_verify.py -- validates the new command set on a Mac BEFORE flashing.
Sweeps modes (FM/NFM/USB/LSB/CW/AM/DV) on band B, reports for each whether MD is
accepted, whether FT 1 (fine mode) is accepted, which FS steps are accepted, and
MEASURES the real frequency grid by probing 20/100/500/1k/5k/6.25k/10k offsets rather
than assuming. Flags any disagreement between the radio and CardSat's fine-mode
whitelist, and can simulate an N-step Doppler run at the measured grid. Saves and
restores mode, frequency and control band.

## 0.9.70 — verify sweep RESULT: 60/60 Doppler, and two mode-mapping bugs found

Owner ran tools/thd75_verify.py on band B. The new command set works:

    Doppler simulation: 60/60 exact at a MEASURED 20 Hz grid, 33 ms per step

Per-mode sweep on band B (the all-mode receiver CardSat drives):
    FM  (0)  MD NAK              -- band B refuses plain FM outright
    NFM (6)  ok, fine mode NAK   -- 5 kHz grid
    USB (4)  ok, fine yes        -- 20 Hz, all four FS steps accepted
    LSB (3)  ok, fine yes        -- 20 Hz
    CW  (5)  ok, fine yes        -- 20 Hz
    AM  (2)  ok, fine yes        -- 20 Hz
    DV  (1)  MD NAK              -- band B has no D-STAR

**BUG 1 -- RM_FM would have failed on every FM satellite.** CardSat mapped RM_FM to
'0' (FM), and band B answers "N" to "MD 1,0". Band B calls narrow FM "NFM" (6) and
accepts it. Now mapped there. Consequence to know: NFM refuses fine mode, so FM birds
tune on the 5 kHz grid while linear birds get 20 Hz.

**BUG 2 -- AM/DV transposed, and my earlier "fix" caused it.** CardSat sent AM as '1'.
The references disagree and one contradicts itself: LA3QMA's tables/mode.md says
1 = DV, 2 = AM, while Hamlib's thd74.c contains BOTH -- thd74_mode_table[] has
[2] = RIG_MODE_AM but its set_mode() switch sends '1' for AM. CardSat copied the
switch. The sweep settles it: code 2 is accepted and takes fine mode, code 1 is
refused -- correct for a band with an airband AM receiver and no D-STAR. AM is '2'.
The comment in the source claimed 0.9.68/0.9.69 had these transposed and "fixed" them;
that change WAS the transposition, and 0.9.68 was right. Corrected, with the
measurement cited so the next reader does not undo it from a table.

RM_DATA also remapped: DV ('1') is refused on band B, and satellite DATA transponders
are overwhelmingly FM packet, so it goes to NFM rather than a mode the radio rejects.

AM added to the fine-mode whitelist (kwApplyStepForMode) on the same measurement.

METHOD: every one of these came from a sweep against the radio, not from a datasheet.
Two independent written references were available for the mode table and BOTH were
wrong or self-contradictory in the same place. The 33 ms/step figure also settles the
CAT-rate question that six firmware iterations could not.

## 0.9.70 — charge indicator REMOVED; the ADC contention was self-inflicted

Owner: charge/sleep never recognises battery vs charging, and suggested removing the
indicator. Correct on both counts, and the investigation found we caused the
underlying breakage.

**THE ADC CONTENTION IS OURS.** M5Unified's _getBatteryAdcRaw() calls
adc_oneshot_new_unit() and `if (adc_handle == nullptr) { return 0; }`. CardSat read the
battery pin directly with analogReadMilliVolts(), which claims the SAME ADC1 through
Arduino's layer -- so M5Unified's unit creation failed, its handle stayed null, and
M5.Power.getBatteryVoltage() returned 0. A comment in our source blamed M5Unified for
that zero. We produced it, then "worked around" it with the very call that caused it,
and whichever of the two lost the race logged "adc1 is already in use" -- the bursts of
8 in the bench log. batteryMilliVolts() now calls M5.Power.getBatteryVoltage(): one
owner of ADC1, no contention, no spam, and a voltage that reads.

**CHARGE STATE IS NOT KNOWABLE ON THIS BOARD.** Evidence, recorded so it is not
re-implemented:
  * The Cardputer ADV has NO PMIC -- M5Unified classifies it pmic_adc, a bare
    battery-sense ADC on GPIO10 with a 2:1 divider.
  * It has no charger status line. M5Unified reads CHG_STAT for boards that do
    (StickS3 PM1_G0, PaperDIY PM1_G3, PaperS3 dedicated pin); board_M5CardputerADV
    appears NOWHERE in Power_Class::isCharging().
  * The voltage-trend inference could never work: its only input was
    batteryMilliVolts(), which returned 0 because of the contention above, so it held
    its initial latch and reported "not charging" permanently -- exactly the symptom.
    And even with a working voltage the test is unsound: a full battery on the charger
    is flat, indistinguishable from idle on battery.
  * USB VBUS presence was considered and REJECTED: CardSat SUPPLIES VBUS in USB host
    mode, so it would read "charging" while driving a radio.

batteryCharging() now returns false unconditionally, with the evidence at the
definition. The "Charging / On battery" line is gone from the charge screen, the
"(charging)" suffix from About, and /api/status reports false rather than a guess.
The charge screen shows terminal VOLTAGE instead -- a real measurement that does rise
on the charger, so the operator gets the same information honestly. Battery PERCENTAGE
is unaffected: getBatteryLevel() uses a separate internal path and works.

A confident wrong answer is worse than none: the old readout said "On battery" while
plugged in.

## 0.9.70 — vendored EspUsbHost updated 2.5.2 -> 2.7.0

Owner confirms the teardown work IS in 2.5.2, so being two versions behind was not the
cause of the surviving wedge -- but the library is under very active development and is
worth tracking, so the vendored copy is now upstream master (2.7.0).

Delta 2.5.2 -> 2.7.0 (surveyed before updating, so the change is not a blind bump):
  * 244 functions common, 21 changed, 20 added.
  * Added: async vendor bulk-OUT queue (vendorWriteAsync/vendorWriteAcquire/
    submitVendorOutSlot/releaseVendorOutQueue/vendorAutoZlp...) and multi-listener
    callbacks (addDeviceConnectedListener/addDeviceDisconnectedListener/
    addMidiMessageListener).
  * Teardown-adjacent: drainClientTransfers and releaseEndpoints gained
    usb_host_endpoint_halt/flush coverage for audio-out and the new vendor-OUT queue.
    Both versions already halt/flush ordinary endpoints on shutdown.
  * end() is UNCHANGED between the two versions.
  * 2.7.0 still does NOT contain the CDC first-control-interface guard, so that patch
    remains ours and remains needed for true dual-CDC devices.

All three local patches re-applied to clean 2.7.0 and verified present in the built
binary: the CDC control-interface guard, ESPUSBHOST_CLAIM_AUDIO (default 0), and
clearLastError(). Build clean, 0 warnings, 20 gates green.

PLACEMENT TRAP, HIT AGAIN: the CardSat patch block must sit ABOVE the library's
"#if CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR". I inserted it after `TAG` -- which is
INSIDE that guard -- while the block's own comment claimed it was outside, and the build
failed with "ESPUSBHOST_CLAIM_AUDIO was not declared". Same guard that made the library's
ESP_LOG unreachable earlier in this cycle. Now documented at the top of PATCHES.md as a
re-application instruction.

STILL OPEN: the wedge and the D75 silence both persist as of the last bench report, and
nothing here claims to fix them. The next useful step is a diagnostic build against 2.7.0
so the FQ exchange can be read on the wire -- the CS_DIAG hooks were not re-applied in
this update and would need re-adding.

## 0.9.70 — USB CODE AUDIT (CardSat + vendored EspUsbHost 2.7.0)

Scope: why simple USB-serial adapters work while the TH-D75 (CDC-ACM + audio
composite) does not. Static review of CardSat's usbserial engage/bind/teardown and the
library's CDC path, followed by an instrumented build for the questions the review
could not settle from source.

**FRAMING FINDING (explains "simple adapters work"):** the Prolific is vendor-class
0xff -- it is driven by the library's vendor-serial path and NEVER exercises the
CDC-ACM class-request machinery. The TH-D75 is a true CDC-ACM device, so it is very
likely the FIRST device to run that code here. "Works with simple adapters" validates
enumeration, bulk IN/OUT and teardown -- it validates nothing about CDC class requests.

**A (top structural finding). The CDC configure path is fire-and-forget and its
failures are invisible.** configureCdcAcm() submits SET_LINE_CODING and
SET_CONTROL_LINE_STATE with completion handled by controlTransferCallback, which on a
non-COMPLETED status does only ESP_LOGD -- compiled out on our FQBN -- and
`device.cdcConfigured = true` latches even if the SUBMIT fails. Nothing verifies DTR
ever took effect. A CDC device that never got DTR typically never transmits, which is
precisely the observed failure shape (OUT completes, IN silent). CardSat's bind
sequence (setConfig -> setDtr -> setRts) re-runs the configure three times, papering
over transient submit failures but never verifying the final state either.
[VERIFIED correct in the same read: bmRequestType/bRequest/wValue bit layout,
wIndex = control interface number, 7-byte line coding payload, transfer freed in the
callback -- the requests are well-formed; only their FATE is unknown.]

**B. The CDC notification (interrupt IN) endpoint is never armed** -- endpoint claiming
arms interrupt endpoints for HID only. Consequence 1: no pending interrupt transfer, so
it is NOT the teardown blocker. Consequence 2: serial-state notifications from the
radio are ignored, which is acceptable for CAT.

**C. The receive path is structurally sound.** Serial bulk IN: claimed, armed
(resubmitPending), submitted by the client loop; on COMPLETED -> dispatch ->
resubmit; on error -> recoveryPending -> usb_host_endpoint_clear -> resubmit; on
NO_DEVICE/CANCELED -> stop. No silent-death hole found in review.

**D. Teardown review:** end() unchanged 2.5.2->2.7.0; drainClientTransfers halts and
flushes pending endpoints in both. The measured ~1080 ms constant on failing teardowns
is NOT localized by review -- phase stamps added (finding F below).

**E. CardSat side:** bind order and the 2.5 s connected() wait are sound; engage/
teardown stage breadcrumbs are good; the recoverable-wedge change and clearLastError
are in and correct. No CardSat-side defect found that would silence a CDC device.

**F. Instrumented build produced** (EspUsbHost 2.7.0 + full CS_DIAG set + two NEW
probes from this audit): (1) every EP0 control-transfer FAILURE decoded --
bmRequestType/bRequest/wValue/wIndex/status -- which directly tests finding A on the
wire; (2) end() phase timestamps (begin / unblocked / host task exited / client stop /
client exited) to localize the 1080 ms. Plus the re-applied set: interface walk, CDC
latches, serial IN/OUT arming, rx hex dump, IN-XFER totals. All 12 diagnostic strings
verified present in the ELF. Diag app d0073f52..., merged af2f9f47....

Decision table for the bench run is in the diag README: EP0 FAILED on 0x22 -> DTR
stall confirmed, fix is verify-and-retry in configureCdcAcm; clean EP0 + FQ replies in
the hex dump -> transport exonerated on 2.7.0; clean EP0 + zero arrivals -> compare
enumeration against the Mac's (config / alt-setting choices).

Release-invisibility PROOF for the audit hooks: normal build rebuilt against the fully
instrumented library. Same size to the byte (3,043,856), exactly 64 differing bytes
(the esp_app_desc ELF SHA + trailing image SHA -- the same ~65-byte signature the
0.9.68 comment-audit rebuild documented), and the sorted `strings` output of the two
binaries is IDENTICAL. Every hook is compiled out of release; 20 gates green. The
staged release binary stays d88183a7... (functionally identical; not churned for a
hash-only delta).

## 0.9.70 — BENCH LOG SOLVES IT: _kwSession was never reset (my regression)

Four engages, and the first one WORKED COMPLETELY -- which is what made the rest
readable:

  ENGAGE 1 (34 s, fresh install): 24 IN completions, 333 bytes, 0 errors, teardown
  clean in 3 ms. The rx hex shows the radio ECHOING every command CardSat sent:
      MD 1,4 | VM 1,0 | BC 1 | FT 1 | FS 0
      FQ 1,0145950060 | ...1100 | ...2100 | ...3100 | ...4100
  i.e. session setup, mode, fine mode, fine step, and Doppler steps on the 20 Hz grid.
  NO "EP0 FAILED" lines anywhere.

**Audit finding A is DISPROVEN.** The CDC control path (SET_LINE_CODING /
SET_CONTROL_LINE_STATE) works on this radio; the fire-and-forget submit is a real
robustness gap but is NOT the cause. Recorded as withdrawn.

**ACTUAL CAUSE: `_kwSession` is set true in kwEnsureSession() and cleared NOWHERE.**
Engage 1 starts with it false, sends VM 1,0 + BC 1, and the radio works. Disengage does
not clear it. Engage 2 therefore SKIPS the session setup entirely -- the band is never
made the CONTROL band, which the Mac probe proved is mandatory -- so the radio refuses
every command and reports zero IN completions. That is indistinguishable from a dead
transport, which is why this looked like a USB fault for so long.

This is MY REGRESSION, and worse, a re-introduced one: the reset was written when the
FQ path was built (setExternalStream clearing _kwSession) and was LOST during the
repair of the .ino damage, when the PlainCatRig class was spliced back from src/rig.h
in its pre-fix form. Parity gates compare the two representations against EACH OTHER,
so a change lost from BOTH is invisible to them.

FIX: setExternalStream() clears _kwSession UNCONDITIONALLY -- on any attach, any
detach, and notably when the new stream lands on the same heap address as the old one,
which is the normal case since the CdcSerial object is freed and immediately
reallocated. (The earlier version compared `s != _stream`, which would have failed in
exactly that common case even if it had survived.)

NEW GATE tools/audit_session_latches.py (21st): any bool member named as a one-shot
latch (...Session/Inited/Configured/Applied/Armed/Latched/Done) that is assigned true
must also be assigned false somewhere other than its declaration. Validated BOTH ways:
passes on the fixed tree, and FAILS with the precise line when the fix is reverted in a
scratch copy.

**TEARDOWN LOCALIZED at last.** The phase stamps put the whole delay in ONE place:
    data flowed   : client stop wait t=0 -> client exited t=2 ms     (released=yes)
    radio silent  : client stop wait t=0 -> client exited t=1001 ms  (released=NO)
    and in both, "IN xfer #1 ep=0x81 status=3 bytes=0" (CANCELED) appears during it.
So the ~1 s is the CLIENT task draining ONE pending bulk-IN transfer that never
completed, exactly as hypothesised. It is a consequence of the silence, not an
independent fault -- with the session fix the radio answers, transfers complete, and
teardown should return to the 2-3 ms path. Worth re-measuring before treating it as a
separate bug.

STILL OPEN: engage 4 reported "stack released=yes" and then failed the next install
with 259. The release verdict remains unreliable in at least that case; note engage 4
also showed heap fragmentation (largest block 19444 vs the usual 31732).

## 0.9.70 — BASIC immediate mode could not type 'b' or 'h'

Owner report, confirmed in source. Two globals fire on a BARE letter -- 'b'
(screenshot) and 'h' (help) -- suppressed on typing screens via the `lettersFree`
predicate. SCR_BASICIMM was never in that list, yet keyBasicImm() takes any printable
character as text (its own comment: "Anything printable is text here"). So at the
immediate-mode prompt 'b' screenshotted, 'h' opened Help, and neither letter could be
entered at all -- fatal for a BASIC prompt, where they appear in ABS, BASE, CHR$, HEX$,
and any identifier containing them.

Checked the whole class rather than the one report: derived screen -> handler from the
key dispatch switch, tested each handler body for printable-character accumulation, and
compared against the predicate. SEVEN screens accept free text; SCR_BASICIMM was the
ONLY one missing. Every other (EDIT, NOTEEDIT, BASIC, CALC, LOTWSUB, TELNETTERM) was
correct -- which is why the omission survived review: the pattern was right everywhere
one would think to look. Fn+b / Fn+h still reach the globals, as on every other text
screen.

NEW GATE tools/audit_text_screens.py (22nd): derives the mapping from source each run,
so a NEW text-entry screen is covered automatically with no list to maintain here.
Validated both ways -- and the FIRST version was WRONG: it counted screen names found
anywhere in the predicate block, including the explanatory comment naming SCR_BASICIMM,
so it passed a scratch tree with the real fix removed. A gate a comment can satisfy is
worse than no gate. Now strips // comments before extracting the excluded set, and
correctly FAILS the reverted tree with the exact screen and handler named.

## 0.9.70 — BASIC immediate mode: print added, and an LPRINT lifecycle bug fixed

Owner asked whether the prompt's output can be printed. It could not: printBasicOut()
prints `basicOut`, the last PROGRAM RUN's console output, bound to bare 'p' in
keyBasicRun. The immediate-mode prompt had no print action at all.

ADDED: PR_BASICIMM ("basic_prompt") + printBasicImm(), bound to **Fn+p**. Deliberately
NOT bare 'p' -- SCR_BASICIMM is a text screen and 'p' must remain typeable (PRINT, PI,
POKE). Fn+p matches the note editor, the other text screen with a print action. The
transcript is printed INCLUDING the "> " prompt lines, because a column of answers with
no expressions is not a working note, and the sheet states that only the most recent
2 KB survives (basImmExec trims the scrollback from the front). Also reachable from the
serial console as `print basicimm`. Footer updated to advertise Fn+p.

BUG FOUND WHILE IMPLEMENTING IT -- LPRINT at the prompt never produced output. The VM
object PERSISTS across prompt lines (a program run builds and destroys one), and
basImmExec cleared `basLprOpen = false` at the START of each line WITHOUT closing
anything, and never called the op=2 close that basicRun() performs. So the first LPRINT
opened the printer, nothing ever flushed or closed it, and the next line silently
abandoned the handle: output lost, connection leaked. Same shape for FOPEN/basFileOpen.
Fixed: clear vm->lprUsed/fileUsed per line, then close whatever THAT line opened
(`if (vm->lprUsed) basHookLpr(this, nullptr, 2);` and the file close), plus a
belt-and-braces close in basImmClose() so leaving the prompt cannot strand a sink.

## 0.9.70 — footer overflow (mine), and TWO pre-existing ones the widened gate found

Owner: the immediate-mode footer now exceeds the screen. Correct -- the string I added
was 49 characters against a hard limit of 39 (footer draws at setCursor(2,127), size 1,
6 px/char, 240 px screen; nothing truncates, the tail is silently clipped).

audit_status_width covered setStatus() ONLY, which is why it passed the overflow.
Extended to footer() -- same geometry, same budget, same silent clipping -- and it
immediately found two PRE-EXISTING overflows that had been shipping:
  * app.cpp:24912  "ENTER bearing  f reconn  g grid  p print"     40 cols -> "prin"
  * app.cpp:29025  "ENT e1 2 e2 t trc z zero m mk b tbl c csv"    41 cols -> "cs"
The second matters most: the clipped key is the graph's CSV export. Both fixed.
The gate now checks 797 strings (up from ~480) and evaluates ternary branches
independently, which is how the second one was caught -- it is the false branch of a
conditional footer. Validated by reverting my own footer in a scratch tree: FAILS with
the line, the measured width and the budget.

New immediate-mode footer states the Fn prefix ONCE rather than four times:
"ENTER run  Fn r=recall p=print ;/.=scr" (38).

## 0.9.70 — backspace past the start no longer exits the BASIC prompt

Owner: backspacing beyond the beginning of the typed line exits immediate mode. It did:
DEL on an empty line set screen = SCR_BASIC. That makes over-backspacing -- holding DEL
one keystroke too long, which is exactly what happens when clearing a mistyped line --
drop the operator out of the prompt, and the scrollback is not visible from the editor.
Now a no-op. The documented exits are the backtick and Fn+Back, neither reachable by
overrunning a backspace. Since DEL no longer advertises itself as the way out, the
opening banner now states "` returns to the editor."

## 0.9.70 — ROOT CAUSE of "USB busy after teardown": halted endpoints are never cleared

Owner's requirement, and it is the right one: the USB host must be tearable down at any
time -- switching the radio off to change satellites is normal operation -- and
restartable without a reboot. Traced end to end in the library.

THE CHAIN, every step visible in the source and matching the bench timings exactly:
  1. drainClientTransfers() HALTS every submitted endpoint
     (usb_host_endpoint_halt + flush) and waits for the CANCELED completion. That part
     works -- the bench log shows "IN xfer #1 ep=0x81 status=3 bytes=0" arriving.
  2. It never CLEARS the halt.
  3. releaseClientResources() then calls releaseEndpoints(device, FALSE), which
     explicitly SKIPS usb_host_endpoint_clear(), frees the transfer, and calls
     usb_host_interface_release().
  4. IDF refuses to release an interface whose pipes are still HALTED.
  5. The caller loops on that failure for its full 1000 ms, gives up, and leaves
     clientHandle_ set -- so the client stays registered and the host is NEVER
     uninstalled. It also sets ESP_ERR_TIMEOUT, which is the "released=NO" CardSat
     reports.
  6. The next usb_host_install() therefore returns 259 (ESP_ERR_INVALID_STATE):
     "USB busy - reboot needed", unrecoverable without a power cycle.

This explains the ENTIRE measured signature, including the part that looked like noise:
teardown took 1001 ms and failed ONLY when the radio had gone quiet (one bulk-IN
transfer outstanding, therefore halted), and 2-3 ms with a clean release when data had
just flowed and nothing needed halting. The correlation with radio silence was real but
incidental -- silence is simply what leaves a transfer pending to be halted.

FIX (vendored patch 5): releaseClientResources() now calls releaseEndpoints(device,
TRUE), so usb_host_endpoint_clear() un-halts each pipe before the interface release.

FIX (vendored patch 6): if the release still stalls, clear every endpoint outright and
retry for 500 ms before declaring a timeout. A teardown that cannot complete must not
strand the stack for the rest of the boot.

Both are library-level and squarely upstreamable -- the halt-without-clear is a defect
in the shutdown path for ANY device that goes quiet, not something specific to a D75.

NOT YET CONFIRMED ON HARDWARE. The reasoning is grounded in the source and matches
every measured timing, but this cycle has taught that source-level reasoning about this
library needs bench proof. The signature to look for: teardown after a quiet radio
completing in milliseconds with "stack released=yes", and a following engage that does
NOT report 259.

## 0.9.70 — the port was never CLOSED: DTR left asserted at teardown

Owner: "do the Kenwood HTs need a command to turn off CAT? The HT can't re-enable CAT
after it's been turned off on the Cardputer -- it requires a power cycle."

ANSWER: there is no Kenwood "CAT off" command. On a CDC-ACM device the port state IS
DTR -- asserting it is what tells the device the host has the port open, and CDC has no
other close notification. CardSat asserted DTR at bind (cat: bind: set DTR) and NEVER
de-asserted it: EspUsbHostCdcSerial::end() only removes the object from the host's
callback array, and no teardown path anywhere calls setDtr(false). So from the radio's
side the host opened the port and then vanished without closing it, and the CAT session
stayed open until the battery was pulled. That is exactly the reported symptom, and it
is OUR defect, not a radio quirk.

FIX: cdcClosePort() sends setDtr(false) then setRts(false) -- bind order, reversed in
sense -- before every CDC detach. Applied at all SIX detach sites (CAT-A x4 including
the failed-engage paths, CAT-B, and the rotator port), because the operator can leave
by any of them. Failures are ignored deliberately: if the radio is already switched off
the control transfer cannot land, and that is precisely the case where nothing needs
saying.

## 0.9.70 — why only "a couple" of disengages: the 19 KB is a SYMPTOM, not the disease

Heap across the bench log makes the mechanism plain:
    engage 1 start  76092 free / 31732 largest
    after teardown  76088 free / 31732 largest   clean cycle -- everything returned
    engage 2 start  76100 free / 31732 largest
    after teardown  56828 free / 31732 largest   19 KB retained (release reported stuck)
    engage 4 start  66832 free / 19444 largest   largest block halved -> next alloc fails
A failed release makes CardSat RETAIN the ~20 KB host object (deleting it would be a
use-after-free while zombie tasks hold pointers into it), so each stuck teardown costs
20 KB and fragments what remains. Two or three cycles exhaust the contiguous block the
host object needs, and only a reboot clears it. The leak is therefore downstream of the
halted-endpoint bug (patches 5/6); it is not an independent defect and needs no separate
fix -- if the release succeeds, nothing is retained.

## 0.9.70 — the residual leak: uninstall was gated behind a clean client release

Owner after the DTR fix: disengage/re-engage now works once or twice, but a few cycles
still leak and end in "USB busy" -- and the loss is ~10-11 KB per cycle, NOT the ~19 KB
seen before. That change in size is the diagnostic.

~19 KB was the retained host OBJECT (CardSat holding it because the release reported
stuck). ~10-11 KB with the object correctly freed is the IDF HOST STACK still resident
-- i.e. usb_host_uninstall() never ran. And an installed host stack is exactly what
makes the next usb_host_install() return 259. One symptom, one cause.

CAUSE: in the daemon's shutdown, the uninstall was an **else if**:
    if (clientTaskHandle_ || clientHandle_) { setLastError(ESP_ERR_TIMEOUT); }
    else if (!uninstallHostLibrary(2000))   { ... }
so ANY client-release failure meant usb_host_uninstall() was never attempted at all.
The host stayed installed for the rest of the boot. A single transient failure was
therefore permanent, which is precisely the "requires a reboot" the owner reported --
and it is why patches 5 and 6 improved the odds without removing the wall: they make
the release succeed more often, but any remaining failure still stranded the stack.

FIX (vendored patch 7): if the client is still registered at this point the tasks are
already stopped and nothing can still be using it, so force it out -- clear endpoints,
release interfaces, close devices, deregister -- and then attempt the uninstall
REGARDLESS. usb_host_uninstall() refuses safely if clients genuinely remain, so trying
costs an error code; not trying costs the operator a power cycle.

Note this is upstreamable and not D75-specific: any device that goes quiet leaves a
halted endpoint (patch 5), and any release that then fails strands the stack (patch 7).

## 0.9.70 — DIAGNOSTIC RESULT: usb_host_client_deregister() itself returns 259

The instrumented run names the failing call. Two teardowns, same build, same device:

  ENGAGE 1 -- radio ANSWERED (6 IN completions, echoes of MD/VM/BC/FT/FS/FQ):
      end(): client exited          t=4 ms
      teardown: clientTask=gone clientHandle=clear -> uninstall WILL RUN
      uninstall: device_free_all -> 268 (not finished)
      uninstall: usb_host_uninstall -> 0 (OK, host released)
      DISENGAGED: stack released=yes    heap fully returned (76080)

  ENGAGE 2 -- radio SILENT (0 IN completions):
      <- IN xfer #1 ep=0x81 status=3 bytes=0
      E USB HOST: Get EP handle error: ESP_ERR_NOT_FOUND
      end(): client exited          t=1506 ms
      forced client_deregister -> 259 (STILL REGISTERED)
      teardown: clientTask=gone clientHandle=SET -> uninstall SKIPPED
      DISENGAGED: stack released=NO     heap 57336 (~19 KB retained)

**The wall is usb_host_client_deregister() returning ESP_ERR_INVALID_STATE.** IDF
refuses to deregister a client that still has a device OPEN, so patch 7's forced path
cannot get past it either -- and with the client still registered the uninstall is
correctly skipped, leaving the host installed. The preceding IDF error ("Get EP handle
error: ESP_ERR_NOT_FOUND") shows endpoint operations being issued against a pipe IDF
has already dropped, i.e. our endpoint/interface bookkeeping and IDF's have diverged by
this point. releaseInterfaces() ignores the return value of
usb_host_interface_release() and zeroes interfaceCount regardless, so a failed release
is indistinguishable from a successful one and the device can never afterwards be
closed. That is a plausible mechanism for the divergence but is NOT yet proven.

**What IS proven, and it is the important part:** teardown succeeds completely and
returns every byte of heap when data had flowed, and fails at deregistration when it
had not. Seven library patches have improved the odds without removing the wall,
because the wall is inside IDF's own client-shutdown contract, not in the code paths
patched so far.

SECOND, SEPARATE PROBLEM: engage 2's radio silence. Install, enumeration and CDC bind
all succeed (usb_host_install -> 0, full descriptor walk, DTR/RTS set), CardSat writes,
and NOTHING comes back -- not even the command echoes that engage 1 showed. So after a
DTR-closed session the radio does not answer on the next connection. The DTR close
improved matters (owner: "does seem to work") but has not made re-engagement reliable.
These are two independent failures that compound: the silence leaves a transfer
pending, and the pending transfer is what strands the stack.

RECOMMENDATION RECORDED: given the wall is IDF's deregistration contract, the resident
host design (0.9.58's approach -- keep the host installed for the firmware lifetime,
detach only the CDC port) sidesteps it entirely and delivers the owner's actual
requirement: unlimited engage/disengage without a reboot. Cost: ~20 KB held after first
USB use, and the serial console stays down. Offered as a decision rather than applied,
because the owner explicitly asked for real teardown.

## 0.9.70 — patch 8: QUIESCE before teardown (owner's suggestion, and the right one)

Owner asked whether the pending transfer can be allowed to finish -- traffic stopped
and completed -- before USB is shut down. That is the correct framing, and it inverts
the approach that had failed seven times: instead of trying to force a teardown past an
outstanding transfer, make sure none is outstanding when teardown begins.

WHY IT SHOULD WORK, from the measurements rather than from theory. The instrumented run
showed teardown completing in 4 ms and returning every byte when a transfer had just
completed, and failing at usb_host_client_deregister() with 259 when one was still in
flight. The library's bulk IN pump re-arms the instant a transfer completes, so on a
radio that is not talking there is ALWAYS exactly one outstanding -- which is why the
failure tracked radio silence so perfectly. drainClientTransfers() does try to cancel
during shutdown, but by then the client loop has stopped its normal event pumping and
IDF's view of the pipes has already diverged from the library's (the bench log's
"Get EP handle error: ESP_ERR_NOT_FOUND" is that divergence). Quiescing EARLIER, with
the tasks alive and pumping, reproduces the exact state the successful teardowns were
in.

IMPLEMENTATION: EspUsbHost::quiesce(timeoutMs = 400), called by CardSat at all three
host-teardown sites before end():
  1. quiescing_ = true -- the client loop stops RE-ARMING the read pump, so the next
     completion is the last one.
  2. Halt + flush every submitted endpoint, so what is in flight completes as CANCELED.
  3. Pump client events until no endpoint reports transferSubmitted, or the budget
     expires.
  4. usb_host_endpoint_clear() every pipe, because usb_host_interface_release() -- the
     next step of the teardown the caller is about to run -- will not accept an
     interface whose pipes are still halted.
  Returns false if anything is still submitted at the deadline; advisory only, end()
  runs either way. begin() clears quiescing_ so a re-engage re-arms normally.

The diagnostic build reports "## QUIESCE: idle, nothing in flight after N ms" or
"STILL BUSY", which is the single line that says whether the premise held.

NOT CONFIRMED ON HARDWARE. Eight patches deep, that caveat is the important part of
this entry: if the QUIESCE line says idle and the deregister STILL returns 259, then
the outstanding transfer was never the mechanism and the resident-host design is the
remaining answer.

## 0.9.70 — QUIESCE RESULT: the outstanding transfer was NOT the mechanism

Bench run with patch 8. The decisive line appears on BOTH teardowns:

    ## QUIESCE: idle, nothing in flight after 0 ms

and the failing one STILL ends:

    forced client_deregister -> 259 (STILL REGISTERED)
    teardown: clientTask=gone clientHandle=SET -> uninstall SKIPPED

So the premise is disproven. Quiesce did its job -- in engage 2 the CANCELED
completion ("IN xfer #1 status=3") arrives and the endpoint list reports idle -- and
deregistration refuses anyway. Seven patches were aimed at clearing the in-flight
transfer; that was never what IDF was objecting to.

**WHAT THE 259 ACTUALLY MEANS.** usb_host_client_deregister() returns
ESP_ERR_INVALID_STATE when the client still has a DEVICE OPEN, not when a transfer is
pending. The forced path calls usb_host_device_close() immediately before the
deregister and DOES NOT CHECK ITS RETURN -- so the one call whose failure would explain
everything is the only one not instrumented. releaseInterfaces() likewise discards
usb_host_interface_release()'s result and zeroes interfaceCount regardless, so a failed
interface release is indistinguishable from a successful one, and a device whose
interfaces are still claimed can never be closed.

**AND OUR "idle" IS OUR BOOKKEEPING, NOT IDF'S.** The IDF errors bracketing every
teardown -- "EP command error: ESP_ERR_INVALID_STATE" and "Get EP handle error:
ESP_ERR_NOT_FOUND" -- say IDF has already dropped endpoints the library still holds
EndpointState entries for. quiesce() reports idle by scanning OUR entries. The two
views have diverged, which is exactly why patching our side keeps failing.

**SECOND, INDEPENDENT AND UNFIXED:** the radio answers fully on the first engage
(26 completions, 767 bytes, FO records and FQ echoes -- note it is now running NFM mode
6 with the 5 kHz grid, as designed) and is SILENT on the second, with enumeration and
CDC bind both succeeding. That silence is what leaves the transfer to cancel; it is not
caused by the teardown bug and will not be fixed by fixing it.

**HEAP IS ALSO DEGRADING WITHIN A SESSION**, independent of the stuck teardowns:
engage 1 started at 72408 free / 31732 largest (not the 76308 of a fresh boot), and by
engage 2's bind the largest block was 21492. Fragmentation is accumulating across
cycles even when the release succeeds.

DECISION POINT. Two honest options:
  (a) ONE more measurement -- instrument usb_host_device_close()'s return in the forced
      path. It is the only unreported call in the chain and it is the one IDF's
      contract points at. Cheap, and it either names the blocker or eliminates the last
      candidate on our side.
  (b) RESIDENT HOST (0.9.58's design): keep the host installed for the firmware
      lifetime, detach only the CDC port on disengage. Never calls the deregistration
      that is failing, so the wall cannot be hit. Delivers the stated requirement --
      unlimited engage/disengage, switch satellites freely, no reboot. Costs ~20 KB
      held after first USB use and the serial console stays down. It would also
      sidestep the re-engage silence, since the device is never re-enumerated.
Recommendation: (b), with (a) first only if the owner wants the root cause named for an
upstream fix.

## 0.9.70 — option (a): instrumenting the release chain to name the blocker

Owner chose to name the root cause before deciding on the design, which is the right
call given the patches would go upstream.

Every previously-unreported call in the client release chain now reports its esp_err_t:
  * usb_host_endpoint_clear()   -- forced path
  * usb_host_interface_release() -- BOTH the normal path (releaseClientResources, which
    silently retained a failed interface) and the forced path (releaseInterfaces, which
    discarded the result entirely and zeroed interfaceCount regardless)
  * usb_host_device_close()      -- the call IDF's contract points at, and the only one
    in the chain never checked anywhere
  * plus a count of how many devices/endpoints the library still marks in use at the
    instant deregistration is attempted

That last one is the part that distinguishes "we failed to release something" from "we
released everything and IDF still disagrees". If the counts are zero and every call
returned ESP_OK and deregister still returns 259, then IDF is tracking a resource the
library never recorded -- unreachable from this side, and the resident-host design
becomes the answer rather than a preference.

Diag build f29f1fdf... / merged ef88ea35..., 8 probes verified present in the ELF.

## 0.9.70 — ROOT CAUSE NAMED: usb_host_endpoint_clear() RESUMES transfers (my bug)

The instrumented run names it exactly:

    iface_release 1 -> 259 (FAILED, retained)     [interface 1 = CDC DATA]

usb_host_interface_release() refuses interface 1 with ESP_ERR_INVALID_STATE
("interface currently can not be freed"), so the device can never be closed, so
usb_host_client_deregister() returns 259, so the host is never uninstalled. Every
symptom in this saga hangs off that one call.

AND THE CAUSE IS MY OWN PATCH. From IDF's usb_host.h:
    usb_host_endpoint_clear(): "If the endpoint has any queued up transfers, clearing
    a halt will RESUME their execution"
So clearing re-activates the pipe -- and can put the very transfer just cancelled back
in flight. Patch 5 (releaseEndpoints(device, true)) and patch 8's quiesce BOTH called
usb_host_endpoint_clear() immediately before the interface release, on my assumption
that release wants un-halted pipes. IDF documents the opposite: halted-and-flushed is
the releasable state. I introduced the blocker I then spent three rounds patching
around, and the "it got better then worse" pattern across builds was me toggling it.

CORRECTED throughout: halt + flush, and LEAVE HALTED.
  * patch 5 reverted to releaseEndpoints(device, false)
  * quiesce() no longer clears after waiting
  * the patch 6 retry and patch 7 forced path use halt+flush instead of clear
Note the ORIGINAL upstream code passed false here and was right to; the bug I "fixed"
in patch 5 was not a bug.

SECOND DEFECT, from the same log: the retry loop printed
"iface_release 1 -> 259" hundreds of times and overflowed the 6 KB diagnostic capture
("## DIAG: 8026 byte(s) LOST"), destroying the part of the log that would have shown
what followed. Now rate-limited to three lines with an explicit suppression notice. A
diagnostic that drowns its own evidence is worse than none.

STILL OPEN and untouched by this: the radio is silent on the second engage (enumeration
and CDC bind succeed, zero IN completions). That silence is what leaves a transfer to
cancel in the first place.

## 0.9.70 — ROOT CAUSE, confirmed by elimination: the CDC SERIAL OUT transfer

With the halt-not-clear correction in place, the instrumented run isolates it exactly:

    iface_release 1 -> 259 (FAILED, retained)        [x3, then suppressed]
    forced iface_release 1 -> 259 (FAILED - blocks device close)
    forced device_close -> 259 (STILL OPEN - blocks deregister)
    forced client_deregister -> 259 (STILL REGISTERED)

**ONLY INTERFACE 1 EVER FAILS.** Interface 0 (CDC control) releases cleanly on every
run, including the failing ones. Interface 1 is the CDC DATA interface, and it owns two
endpoints: IN 0x81 and OUT 0x01. The IN side is demonstrably clean -- quiesce reports
idle and the CANCELED completion arrives. That leaves OUT 0x01 by elimination.

CONFIRMED IN SOURCE: sendSerial() allocates a transfer with usb_host_transfer_alloc(),
submits it, and forgets it -- the transfer is NEVER recorded in endpoints_. So neither
drainClientTransfers() nor quiesce() could see it, because both iterate endpoints_. If
the device stops accepting data (a radio switched off, or one that simply stops reading
its port) that transfer remains enqueued in the pipe indefinitely, and IDF refuses
usb_host_interface_release() for the interface that owns the endpoint -- documented as
"interface currently can not be freed".

This is ALSO an upstream gap and a clear one: drainClientTransfers() already halts and
flushes audio-out AND vendor-out by address. Serial-out was simply missed.

FIX (patch 9): halt + flush the serial OUT endpoint by address, in BOTH
drainClientTransfers() and quiesce(). Left halted, not cleared, per the correction
above.

Why the failure tracked "radio went quiet" so perfectly, in full: a silent radio means
(a) an IN transfer pending, which was the visible symptom and the wrong lead, and (b)
an OUT transfer the radio never drained, which was invisible and was the actual
blocker. Every measurement in this saga is consistent with that, including the very
first observation that teardown succeeded whenever data had just flowed -- data
flowing means the OUT transfer completed.

Release build be3bc7be..., 0 warnings, 22 gates green.

## 0.9.70 — TEARDOWN FIXED, confirmed on hardware

Five consecutive engage/disengage cycles, no reboot:
    cycle 1  released=yes  heap 82384
    cycle 2  released=yes  heap 82384
    cycle 3  released=yes  heap 82384
    cycle 4  released=yes  heap 82384
    cycle 5  released=yes  heap 82364
Every cycle returns to the same figure; largest block steady at 31732 throughout. No
259, no retained host object, no fragmentation, no reboot required. The owner's
requirement -- tear the host down whenever the radio is switched off and restart it
without a reboot -- is met.

The fix that mattered was patch 9: halting and flushing the CDC SERIAL OUT endpoint by
address. sendSerial() submits fire-and-forget and never records the transfer in
endpoints_, so a device that stops reading its port left that transfer enqueued
forever, and IDF then refused to free interface 1 (which owns OUT 0x01), which blocked
the device close, the deregistration and the uninstall in turn.

UPSTREAM CANDIDATES, in priority order:
  1. patch 9 -- drainClientTransfers() halts audio-out and vendor-out by address but
     NOT serial-out. A one-loop omission that strands the host for any CDC device that
     stops reading. This is the real bug and it is not CardSat-specific.
  2. patch 4 -- lastError_ is sticky (cleared only in begin()), so callers cannot tell
     a fresh failure from an old one.
  3. patch 1 -- CDC-ACM binds every control interface it sees rather than the first,
     which misbehaves on true dual-CDC composites.
Patches 5, 6, 7 and 8 should NOT be upstreamed as-is: 5 was actively wrong (clearing a
halt RESUMES queued transfers, blocking the very release it was meant to enable), and
6/7/8 were built to work around the damage 5 did plus the real bug 9 fixes. They are
retained locally only where they now do no harm; a clean upstream patch is 9 alone.

METHOD NOTE. Seven patches were aimed at the pending IN transfer because it was the
visible symptom of radio silence. The OUT transfer -- invisible, untracked, and the
actual blocker -- was found only by instrumenting the release chain call by call and
noticing that interface 0 NEVER failed while interface 1 ALWAYS did. Elimination on the
measured data, not reasoning about the source, is what closed it.

REMAINING: the TH-D75 is still silent on the second engage.

## 0.9.70 — Mac reopen test: 6/6 answered. The radio is NOT the problem.

    python3 thd75_probe.py --cycles 6   ->  all six cycles: ID ok, FO ok

Six close/reopen cycles on a known-good host, each dropping DTR/RTS exactly as CardSat
now does, and the radio answered every time. So the TH-D75 has no difficulty starting a
fresh session after a previous one ends, and "the radio needs a power cycle" is
eliminated. The difference is on the Cardputer side.

(Script bug found and fixed first: xchg() returns a (text, ms) tuple and the reopen test
assigned it to a single name. Every other call site was audited for the same mistake --
none -- and BOTH verdict branches were then exercised against a stubbed serial device
before shipping, which is what should have happened the first time.)

**THE BLIND SPOT, and it is a significant one.** Every byte counted in this entire
investigation arrived on the IN endpoint, and on this radio those bytes are ECHOES OF
COMMANDS CARDSAT SENT. The write path reports failures only via ESP_LOGD, compiled out
on this FQBN. So a write that never left the Cardputer is INDISTINGUISHABLE from a radio
that declined to answer -- both produce exactly the "zero IN completions" that has been
read all along as "the radio is silent". That reading may have been wrong from the
start.

Diag build f5c4466f... instruments serialOutTransferCallback (status, bytes, endpoint)
and sendSerial()'s submit failure. Four mutually exclusive outcomes for engage 2:
  * OUT status=0, no IN -> the write reached the radio and it did not answer.
  * OUT status!=0 (3 CANCELED / 4 STALL / 5 NO_DEVICE) -> the write failed on the wire;
    a STALL would indicate a data-toggle mismatch after re-enumeration, which has a
    standard remedy (CLEAR_FEATURE ENDPOINT_HALT at bind).
  * OUT submit FAILED -> never left the host stack.
  * NO OUT lines at all -> CardSat never called the write path, and the fault is above
    USB entirely, in the rig/Doppler layer.
The last of those is worth taking seriously: it would mean the bug was never a USB bug.

## 0.9.70 — THE RADIO STOPS READING ITS PORT. It was never "silent".

Engage 2, with the OUT path finally visible:

    -> OUT xfer #7  ep=0x01 status=0 bytes=7     "MD 1,4\r"   accepted
    -> OUT xfer #8  ep=0x01 status=0 bytes=7     "VM 1,0\r"   accepted
    [nothing more completes -- BC/FT/FS/FQ never go out]
    ...at teardown, four writes come back CANCELED:
    -> OUT xfer #9..#12 ep=0x01 status=3 bytes=0  <-- never left the pipe

Compare engage 1: OUT #1-#6 all status=0, every one echoed back.

**READING IT.** The radio accepted exactly TWO bulk-OUT packets and then NAKed
everything after -- transfers #9 to #12 sat queued in the pipe until teardown cancelled
them. Two packets is a double-buffered endpoint FIFO filling up. The device's USB
hardware took them; the radio's CAT APPLICATION never drained them. So the radio is not
declining to answer, and it is not silent: it has stopped SERVICING its CDC port
altogether, and the four "missing" commands were never delivered to it.

That also retires the framing this whole hunt has used. "Zero IN completions" was read
as "the radio won't talk". It actually meant "the radio never received the commands to
reply to" -- because our own writes were stuck in a pipe the radio would not drain.

**WHY, AND WHY THE MAC DISAGREES.** The Mac reopen test passed 6/6, and the difference
is now sharp: closing and reopening a port on the Mac does NOT re-enumerate the device.
The D75 stays enumerated, its CAT application keeps running, and a new session just
works. CardSat uninstalls the whole host stack on disengage, so re-engaging RE-ENUMERATES
the radio (SET_ADDRESS, SET_CONFIGURATION) -- and the D75's CAT application does not come
back after that. Its USB hardware still enumerates perfectly (descriptor walk, CDC bind,
DTR/RTS all succeed, and the endpoint accepts two packets into its FIFO), but nothing
behind the endpoint is reading.

This is a radio firmware behaviour, not a bug we can patch on our side.

**TWO WAYS FORWARD, in order of cost:**

 (1) FREE, NO BUILD -- test whether the D75 just needs time. Engage, disengage, wait
     30-60 s, then re-engage. If it works with a delay, the fix is a settle period
     before re-engagement, which is trivial.

 (2) RESIDENT HOST BETWEEN ENGAGES -- keep the USB host installed across CAT
     disengage/engage and detach only the CDC port, which is EXACTLY what the Mac does
     and what the Mac proves works 6/6. Note this is no longer the blunt "hold the host
     until reboot" design that was rejected earlier, because the teardown now works
     correctly and is confirmed on hardware: a full release still happens when the
     operator genuinely finishes with USB. And switching the radio OFF still works as
     desired -- that physically disconnects the device, and powering it back on
     restarts the radio's CAT application, which is the case the D75 handles fine.

RECOMMENDATION: try (1) first because it costs nothing and would settle it; implement
(2) if the delay does not help.

## 0.9.70 — RESIDENT HOST BETWEEN ENGAGES, on every USB path

Owner: waiting 1 minute and 5 minutes did not help, so re-enumeration -- not settle
time -- is what kills the TH-D75's CAT application. Decision: keep the host resident
between engages on ALL USB paths, and make full release an explicit UI action.

DESIGN. Detaching the last port no longer uninstalls the host stack; the device stays
ENUMERATED and only the CDC port is detached. This reproduces exactly what a Mac does
when it closes and reopens a port -- which the probe proved the radio survives 6/6,
where re-enumeration fails every time.

ONE CHOKE POINT, DELIBERATELY. The change sits in releaseHostIfIdle(), which CAT-A,
CAT-B and the rotator already shared... except the rotator did NOT: rotEnd() carried an
open-coded copy of the teardown, which meant it had silently missed every fix the CAT
path received this cycle (quiesce, clearLastError, and now residency). That duplicate
is deleted and rotEnd() now calls the shared function. Any device on any path gets the
same treatment by construction rather than by remembering to copy changes across --
nothing about this failure is specific to a radio, and a rotator whose firmware does
not re-initialise would behave identically.

Two direct s_host->end() calls remain and are CORRECT to keep: both are failed-bring-up
cleanups (inside `if (!s_host->begin(...))`), where nothing was ever established and
there is no session to preserve.

THE TEARDOWN IS NOT ABANDONED. It was fixed and hardware-confirmed earlier in this
cycle (five consecutive cycles, every byte of heap returned). It is simply no longer
run on every disengage, because doing so costs the device its session. Full release
still happens on demand, and switching the radio OFF still behaves as the operator
wants: that physically disconnects the device, and powering it back on enumerates it
from cold, which restarts its CAT application -- the case the D75 handles fine.

UI: **Fn+u** on the tracking screen fully releases USB, which is also when the serial
console returns. It refuses with a clear message while any port is still open ("Turn
radio/rotator off first") and says so if already released. Behind Fn because it is a
rare, deliberate operation and bare letters are scarce there; bare 'u' still toggles
the fixed leg, and the Fn form is checked first so it cannot be shadowed.
API: UsbSerial::releaseUsbNow() and UsbSerial::usbHostResident().

COST: ~12 KB and the serial console stay claimed while the host is resident, until the
operator releases it or reboots.

## 0.9.70 — RELEASE

FW_VERSION bumped 0.9.70-wip -> 0.9.70 on the owner's instruction after the resident-host
fix was confirmed working on hardware.

Build: 3,045,402 flash (96.8%), 162,112 static RAM (49%), 0 warnings, 22/22 gates.
Verified rather than assumed: packaged CardSat.ino is byte-identical to the compiled
sketch; firmware/CardSat-app.bin is byte-identical to the build output; the shipped
binary contains the string "0.9.70" and the patched-library markers.

Companion (M5StickS3 DualRig) BUILT for the first time this cycle: 1,280,738 flash
(38%), 61,244 RAM (18%), 0 warnings, with the new usbReleaseControlLines() confirmed
present in the ELF and called from reconfigureAndRebind(). Marked EXPERIMENTAL --
NEVER TESTED ON HARDWARE in the sketch header, companion/README.md,
CardSatDualRig/README.md and the firmware README, because that is the honest status:
its USB fixes were applied by inspection from defects proven on CardSat's hardware,
which is well founded but not verification.

Upstream: 0002 (serial-OUT drain, the important one) and 0003 (clearLastError) drafted
as patches with UPSTREAM_ISSUE_2_SERIAL_OUT.md ready to file, alongside the existing
0001. PATCHES.md rewritten as the build-it-yourself reference, including the placement
trap and an explicit warning not to repeat the endpoint-clear mistake.

Self-build documentation: the patching requirement is now stated in README.md (banner +
doc table), docs/BUILD_AND_FLASH.md, MANUAL.md ch5, firmware/README.md, and both
companion READMEs.

NOT regenerated: CardSat_Fun_Guide.pdf has no generator script in the tree (it is
hand-maintained) and was left as-is.

## 0.9.70 — release zip rebuilt: I had excluded 89 files that ship with the repo

Owner spotted that docs were missing from the package. Cause was my zip command:
`-x 'docs/design/*'` (I assumed those were internal working notes; they are part of the
repository) and `-x '*.git*'`, which is over-broad and also caught `.gitignore`.
89 files dropped: all 87 docs/design/*.md, docs/design/postmortems/*, and .gitignore.

Rebuilt excluding ONLY genuine noise -- `.git/`, `__pycache__/`, `*.pyc`. Verified by
diffing the file list against the owner's previous repo snapshot: **zero files from the
snapshot are missing**, and every addition is accounted for (the vendored EspUsbHost,
the new gates audit_session_latches/audit_text_screens, the thd75 probe scripts, the
release notes, and the host harnesses).

Method note: I verified the zip's CONTENTS (the .ino matched, the binary matched, the
new artefacts were present) but never verified its COMPLETENESS against a known-good
file list. Checking that a package contains what you put in it is not the same as
checking it contains everything it should. The previous snapshot was the right
reference and it took the owner to point at it.

## 0.9.71 — IC-705 LAN leg never began (mixed-bus dual config)

Owner: IC-705 LAN CAT does not work from the dual-rig screen.

FOUND, and it is exact. applyRadioFromCfg()'s CAT_DUAL branch ended:

    #if CARDSAT_HAS_USBCAT
        if (aU || bU) { return; }          // returns if EITHER leg is USB
    #endif
        rig->begin(0, CIV_UART_NUM, ...);  // never reached

So in a MIXED config -- USB on one leg, LAN (or Grove) on the other -- the early
return skipped begin() for the whole composite, and the non-USB leg was never begun.
For an Icom LAN leg that is fatal in a quiet way: IcomNetRig::begin() is what ARMS the
connect state machine (resetSession + allow an immediate first attempt). Without it the
leg sits at NS_IDLE forever; service() runs every loop, finds nothing pending, and does
nothing. The radio looks fully configured and simply never connects -- no error, no
retry, no trace.

FIX: call begin() unconditionally. DualRig::begin() already skips USB legs
INDIVIDUALLY (`if (_down && !legIsUsb(0)) ...`), so the blanket return was not only
wrong, it was redundant -- the per-leg guard it duplicated is the correct one, and
calling begin() cannot double-begin the USB side. A LAN+LAN or LAN+Grove dual worked
because neither aU nor bU was set; only a mix with USB hit it.

## 0.9.71 — audio gate placement corrected (regression from 0.9.70 patch 4)

While investigating the second report, found that ESPUSBHOST_CLAIM_AUDIO was applied to
the isAudioControlInterface / isAudioInterface PREDICATES -- which also guard
`device->hasAudioInterface = true` and the audio interface-number bookkeeping. That
conflated CLAIMING with DETECTING: with audio off, an audio device's interfaces were no
longer recorded, so info.supported could come back false and the descriptor picture lost
the audio interfaces entirely. We only ever wanted to stop SERVICING an isochronous pipe
CardSat has no use for.

Gate moved to the endpoint-claim site; recognition is now unconditional. Not the cause
of the IC-705 report (dispatchDeviceConnected fires before the `supported` test, and
CardSat's onDev registers any non-hub device regardless), but wrong on its own terms.

## 0.9.71 — IC-705 USB: not yet diagnosed, facts needed

"No USB devices found" with an IC-705 attached. Established so far by reading, not
guessing:
  * CardSat's onDev() registers EVERY non-hub device, whatever its class, and
    dispatchDeviceConnected() fires before the library's `supported` test. So an
    unrecognised radio would still APPEAR in the picker -- an empty list means
    enumeration itself is not completing, not that the device was filtered out.
  * The library's vendor-serial table covers FTDI 0x0403, Silicon Labs 0x10c4,
    CH34x 0x1a86 and Prolific 0x067b. Icom's own VID (0x0C26) is absent -- which would
    prevent CAT from working, but NOT prevent the device being listed.
Candidate causes not yet distinguished: VBUS/power (the IC-705 charges over USB and may
draw more than the Cardputer can source, collapsing the bus before enumeration
completes), or the radio's USB function needing a menu setting. Next step is a
descriptor dump from a Mac plus a diagnostic-build enumeration trace -- measurement
before another firmware change, per this project's hard-won practice.

## 0.9.71 — IC-705 USB: the hub message narrows it sharply

Owner: with a powered hub, the scan said "no adapters found".

That is the more informative of the two messages scanAdapters() can produce. The other
is "hub seen but NO adapters behind it", emitted when s_sawHub is set. Getting the
FORMER means the HUB ITSELF never enumerated. So:
  * this is not the IC-705 being filtered out of the picker (CardSat registers every
    non-hub device regardless of class or `supported`);
  * nothing at all is completing enumeration while the 705 is attached;
  * and the power theory is dead -- a powered hub supplies its own downstream current.

Checked and ELIMINATED: external hub support IS compiled in
(CONFIG_USB_HOST_HUBS_SUPPORTED=y, CONFIG_USB_HOST_HUB_MULTI_LEVEL=y).

LEADING HYPOTHESIS, not yet confirmed: Arduino's prebuilt IDF sets
**CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256**. A device whose CONFIGURATION
descriptor exceeds 256 bytes cannot be fetched during enumeration and never appears,
with no error visible to the application. A CDC + USB Audio composite is exactly the
shape that overruns it -- and the TH-D75 (also CDC + audio) fits and works, while the
IC-705 carries more audio interfaces. If true this is an IDF build-configuration limit,
NOT a CardSat bug, and cannot be fixed without a custom IDF build -- which is worth
establishing before anyone spends more effort on the firmware.

Diagnostic build 82ac9372... reports every device's wTotalLength against that limit, on
BOTH enumeration paths. Three-run bench procedure in the diag README, chosen to separate
"hubs do not work here" from "the 705 breaks enumeration" from "the descriptor is too
large", with a control run first so a silent log cannot be misread as a result.

## 0.9.71 — mixed USB+LAN: the begin() fix WORKS; the remaining failure is RAM

Owner's bench log with the 0.9.71 build, TH-D75 on USB + IC-705 on LAN:

    [NET] connecting to 10.0.0.100:50001 (CI-V A4)
    [NET] login sent
    [NET] reconnect: handshake stalled

**The begin() fix is confirmed.** Those lines cannot appear unless begin() armed the
connect state machine, and before the fix a mixed config never reached it. The LAN leg
now starts, resolves, opens its sockets and sends the login. The IC-705 is being talked
to. That was the reported bug and it is fixed.

**What remains is a resource ceiling, not a protocol fault.** Heap through a mixed
engage, straight from the log:

    LAN leg only, before USB engage    free=58128  largest=31732
      after allocating USB host        free=45100  largest=31732
      after host tasks start           free=33884  largest=22516
      after CDC bind                   free=26424  largest=13812
      during the NET retry loop        free=23708  largest= 7412
    reference, USB-only engage:        free=76304  largest=31732

The LAN leg costs ~18 KB (the Wi-Fi/lwIP stack itself -- IcomNetRig's own state is two
WiFiUDP sockets and a few small buffers, nothing), the USB host ~32 KB. Together they
leave ~26 KB free with the largest contiguous block collapsed from 31,732 to ~7,000.
Wi-Fi cannot reliably allocate receive buffers in that state, so the login reply never
lands and the 4-second handshake timeout fires. Corroborating detail: on the third
attempt "login sent" took 1,318 ms after "connecting" versus 151 ms on the first -- a
system under allocation strain, not a radio declining to answer.

Instrumented so the next run CONFIRMS or REFUTES this rather than resting on inference:
the stall now logs the state it was in, free heap, largest free block, and how long
since the last RX. If largest-block is in the low thousands the diagnosis holds; if the
heap looks healthy at the stall, this is CPU starvation or a protocol issue and I have
been wrong.

DELIBERATELY NOT DONE: reducing ESP_USB_HOST_MAX_DEVICES from 4 to 3. It would save one
DeviceState, which is ~1-2 KB against a ~26 KB shortfall -- marginal -- while risking a
legitimate hub-with-three-devices setup. That is exactly the speculative change this
project has been burned by; it waits for the measurement.

## 0.9.71 — memory hypothesis REFUTED; the stall is at NS_CTL_LOGIN with a healthy heap

The instrumented stall settles it, and against my own theory:

    stalled in state 3 after 4001ms | heap=80612 largest=31732 rx=51ms ago

**heap=80,612 with a 31,732-byte largest block is a completely healthy heap** -- that is
the same figure a USB-only engage starts from. So the resource ceiling I diagnosed from
the previous log was WRONG. The earlier run did show a squeezed heap, but that was a
consequence of repeated failed reconnects, not the cause of them: after a reboot, with
memory untouched and USB never engaged, the LAN leg stalls identically.

WHAT THE NUMBERS ACTUALLY SAY:
  * **state 3 = NS_CTL_LOGIN** -- the login was sent and the 96-byte token reply never
    arrived (or never matched).
  * **rx=51ms ago** -- packets ARE flowing. But isPing() also refreshes _tLastRxMs, so
    recent RX may be nothing more than pings we are dutifully answering. "Traffic
    exists" is not "the reply exists", and the log could not tell those apart.

TWO CANDIDATES, both now instrumented rather than argued:
  1. **Empty per-leg credentials.** dualUser/dualPass are PER LEG. Moving the IC-705
     between the downlink and uplink legs to make room for the D75 leaves its
     username/password behind on the old leg -- easy to do, invisible afterwards, and it
     produces exactly this signature: with no username the radio does not answer at all,
     so there is no 96-byte reply to reject and the handshake times out instead of
     failing cleanly with "invalid username/password". This also explains precisely why
     the same radio works alone on the other leg. startConnect() now logs credential
     LENGTHS (never contents).
  2. **A reply we discard.** Every branch of handleCtl() matches on an exact length AND
     first byte, so a reply of an unexpected size is dropped silently -- indistinguishable
     from no reply. Unmatched control packets in the handshake states are now logged with
     length and leading bytes, capped at 8 lines.

Build 72280c5e. One run distinguishes them: "user=0 ch pass=0 ch" is the configuration
error; an "unhandled ctl pkt" line is a protocol gap on our side.

METHOD NOTE: I inferred a memory ceiling from a heap trace that was real but was an
EFFECT of the retry loop rather than its cause, and I was about to reason further from
it. The instrumentation that disproved it cost one build. Recording this because it is
the same failure mode as the D75 saga -- a plausible mechanism, confirmed by
correlation, wrong.

## 0.9.71 — Icom LAN never adopted the on-demand pattern the rest of the net code uses

Asked to check icomnet against the memory work done elsewhere. Result:

**NOTHING TO TRIM INSIDE IcomNetRig.** Its whole persistent state is two WiFiUDP
sockets, three Strings (host/user/pass), a 24-byte name buffer, a 6-byte auth ID and a
16-byte capabilities copy. No packet history, no receive pool, no oversized buffers.
The TLS setBufferSizes() work in net.cpp has no analogue here -- UDP has no such knob.
Socket open/close is balanced: resetSession() stops both, startConnect() begins the
control socket, teardown() stops both, and the destructor tears down. No leak found.

**THE REAL GAP IS STRUCTURAL, and it is the one pattern everything else here follows.**
Look at what sits immediately below the service call in loop():
    APRS-IS is only alive while its screen is open  (aprsStop() on any other route out)
    the DX / ADS-B feeds free their buffers on the way out
    dxcAlloc/adsbAlloc/feedsFreeExcept enforce one-array-at-a-time
Every socket and buffer owner in CardSat is on-demand -- except the Icom LAN rig, which
ran `if (rig) rig->service();` with no reference to whether radio control was even on.
So from boot it opened a UDP socket, ran a full login handshake, failed, and retried
every 8 seconds forever, holding lwIP state the whole time. That is visible in the
owner's log: [NET] connect/login/stall cycling with nothing engaged, straight through a
reboot.

FIX: `Rig::setSessionWanted(bool)` -- a no-op for wired and USB backends, forwarded by
DualRig to both legs, implemented by IcomNetRig to tear the session down and hold idle
when false. loop() drives it from `radioOut`, which is the right signal because the CAT
diagnostic tool sets radioOut too, so the tool still gets a live rig. Disengaging now
also CLOSES the session properly (teardown sends de-auth + disconnect) instead of
abandoning it -- the same defect class as the USB DTR-never-dropped bug fixed in 0.9.70.

Costs: a few seconds to connect at engage, which is when the operator is ready anyway.
Buys: no sockets or lwIP state held while idle, no endless retry loop, no log spam, and
a clean session close on the radio side.

NOT claimed: that this fixes the NS_CTL_LOGIN stall. It does not -- the credential and
unhandled-packet instrumentation is still what will answer that. This is the answer to
the question actually asked.

## 0.9.71 — IC-705 descriptor dump: it is a TRUE DUAL-CDC device

Mac-side descriptor dump (tools/usb_probe.py) of 0c26:0036:

    CONFIG DESCRIPTOR TOTAL LENGTH  141 bytes      (limit 256 -- comfortably under)
    self-powered, requests 100 mA
    iface 0  CDC control  ep 0x83 interrupt
    iface 1  CDC data     ep 0x01 bulk OUT, 0x82 bulk IN
    iface 2  CDC control  ep 0x86 interrupt
    iface 3  CDC data     ep 0x04 bulk OUT, 0x85 bulk IN

**The descriptor-size hypothesis is DEAD** -- 141 bytes, nowhere near the 256-byte IDF
control-transfer limit. So is the power theory: self-powered, 100 mA, and a powered hub
changed nothing.

**The device is a true dual-CDC composite** -- two independent serial ports, device
class 0xEF/0x02/0x01 (IAD). This is precisely the hardware CardSat's vendored patch 1
was written for: the stock library latches EVERY CDC control interface, so
SET_LINE_CODING and SET_CONTROL_LINE_STATE end up addressed to the second function
while data flows on the first. Patch 1 binds only the first control interface, and the
data latch is guarded the same way, so iface 0/1 pair correctly.

Note the patch was written speculatively in an earlier cycle and explicitly recorded as
"NOT the TH-D75's problem, but real for true dual-CDC composites". The IC-705 is that
composite. Worth recording that the speculative patch turned out to be aimed at a real
device -- and equally, that it was NOT validated until now.

STILL UNEXPLAINED: why nothing enumerates at all ("no adapters found", which requires
BOTH zero devices and no hub seen -- so even the hub went unregistered). Nothing in the
descriptor accounts for that. Diagnostic build b991c138 will place the failure: the
device/config-descriptor line is emitted during parse, and both early-exit paths in
handleNewDevice (usb_host_device_open, usb_host_get_active_config_descriptor) log at
ESP_LOGE, which the capture does receive.

FLAGGED FOR AFTER enumeration is working: which of the two ports carries CI-V. CardSat
binds the FIRST (iface 0/1). If the radio puts CAT on the second, we bind the wrong port
and get silence with every layer apparently healthy -- the same misleading signature as
the D75 saga, and worth checking before drawing conclusions from a quiet radio.

## 0.9.71 — dual CAT works; three follow-on findings from the bench log

Owner: dual CAT working (IC-705 LAN + TH-D75 USB), but (1) frequencies rounded to 5 kHz
outside FM, (2) uplink-based tuning too slow, (3) stops working after a few cycles.

**(1) 5 kHz ROUNDING -- FIXED, and the cause was not in the rig code at all.**
applyTransponderModes() opened with `if (!rig || !rig->ready()) return;`, and
DualRig::ready() requires EVERY leg. In a mixed rig that is the normal case at engage:
the USB leg is ready immediately while the LAN leg needs seconds for its login. So the
modes were silently dropped and only retried on a transponder change. With no MD ever
sent, PlainCatRig::kwApplyStepForMode() never ran, _kwFine stayed false, and kwGrid()
returned 5000 -- every Doppler write rounded to 5 kHz, exactly as reported, in every
mode. Now sets modeApplyPending and the loop re-applies once rig->ready() goes true.

**(3) STOPS AFTER A FEW CYCLES -- FIXED (probable).** The log's shape is the evidence:
    cycle 1 CONNECTED | cycle 2 CONNECTED | cycle 3 stalled at NS_CTL_AUTH
    cycle 4 stalled at NS_CTL_LOGIN, and every retry after
The failure moves EARLIER each cycle, which is not what a transient fault looks like --
it is stale sessions accumulating on a radio that accepts one at a time. Cause:
teardown() only sent the de-auth/disconnect when `_state == NS_CONNECTED`, so
disengaging mid-handshake (cycle 3) told the radio nothing at all. Now any state past
NS_CTL_OPEN gets the close, and the 20 ms settle -- too short to be sure the datagrams
even left before the sockets closed -- became a 120 ms pumped wait.

**(2) SLOW UPLINK TUNING -- MECHANISM IDENTIFIED, NOT YET FIXED.** Measured from the
log: cmd 03 goes out every ~405 ms and an answer appears every 4-12 s, i.e. roughly one
read in thirty succeeds. 405 ms is the signature of a 200 ms timeout plus overhead --
and 200 ms is exactly the ceiling CardSat imposes with
`constrain(effectiveCatRateMs()/4, 60, 200)`. So nearly every read times out.
DELIBERATELY NOT "fixed" by raising the number: whether 200 ms is simply too short for
this transport, or replies are being lost for some other reason, is decided by how long
a SUCCESSFUL read actually takes -- and three theories have already died this cycle from
reasoning past the evidence. Both paths are now instrumented: successful reads log their
latency, timeouts log elapsed/budget every 8th occurrence. One run gives the number.

## 0.9.71 — HUBS DO NOT ENUMERATE AT ALL. This is separable from the IC-705.

Owner's diagnostic run, and the most useful line in it is the one about the D75:
"I can't even see the D75 beyond a connected powered hub, or any hub device."

The log confirms it exactly:
    scan: adapters ... scan: no adapters found          <- with the hub attached
    ...
    ## device 2166:9023 config descriptor = 174 bytes   <- D75 DIRECT, enumerates fine
    scan: adapter[0] addr=1 #1 JVCKENWOOD TH-D75 ...

s_sawHub is set in onDev() the moment any device reports isHub, and the message chosen
was "no adapters found", NOT "hub seen but NO adapters behind it". So the HUB ITSELF
never enumerated -- and a device that works perfectly when plugged direct also
disappears behind it.

**THIS IS A SEPARATE BUG FROM THE IC-705, and a much better one to chase:** it
reproduces with hardware that otherwise works (hub + TH-D75), needs no IC-705, and has
a clean pass/fail. Every earlier conclusion that treated "no adapters found with a hub"
as evidence about the IC-705 was reading two independent faults as one -- including my
own power/VBUS theory, which the powered hub was supposed to have tested and did not,
because the hub was never working in the first place.

Established so far:
  * CONFIG_USB_HOST_HUBS_SUPPORTED=y and CONFIG_USB_HOST_HUB_MULTI_LEVEL=y in the
    prebuilt IDF, so hub support IS compiled in.
  * The library has hub handling (nextHubIndex_, scanHostDevices(), isHub detection).
  * usb_host_install() is called with skip_phy_setup=false, intr_flags=LOWMED, and the
    enum filter compiled out (CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK is not set).
  * No IDF ESP_LOGE appears when the hub is attached -- and ESP_LOGE IS captured by this
    build, so IDF is not reporting an enumeration failure. It looks like the attach
    event never happens at all.

IC-705 direct is a DIFFERENT and still-open failure: no "## device" line, no IDF error,
so again no attach event -- while the same radio enumerates fine on a Mac (141-byte
descriptor, dual CDC, self-powered, 100 mA).

Both share a shape: the device is electrically present and IDF never raises a new-device
event. That is now the question, and it is one question rather than two theories.

## 0.9.71 — OTR read latency measured: the BUDGET WAS NEVER THE PROBLEM

Successful reads answered in **4 ms, 8 ms and 12 ms**. Every one of the 145 timeouts sat
at the full ~300 ms budget. So the radio replies almost instantly WHEN IT REPLIES, and
roughly 1 read in 30 gets any reply at all -- the other 29 receive nothing.

Raising the read budget, which was the obvious "fix" before this measurement, would have
changed absolutely nothing. Recording that plainly: it is the fourth theory this cycle
that measurement killed before it reached the bench.

TWO CANDIDATES REMAIN, and they are opposites:
  * The replies ARE arriving, just after the budget expires, and the next read's
    "drain stale" loop throws them away -- so we run permanently one read behind and
    only occasionally catch one inside the window.
  * The radio genuinely ignores most 0x03 polls at this rate.
Both now instrumented: each timeout reports how many stale packets the pre-read drain
discarded and how many packets arrived DURING the read. Steady non-zero drain counts
mean the first; zeros for both mean the second, and the fix is to stop polling this hard
rather than to wait longer.

## 0.9.71 — HUBS CONFIRMED BROKEN FOR EVERY DEVICE

Owner: the hub does not work with a plain Prolific adapter either, and the same adapter
enumerates immediately without the hub. So external hub support is broken universally on
this build -- not a TH-D75 issue, not an IC-705 issue. The hub itself never enumerates
(s_sawHub is never set, hence "no adapters found" rather than "hub seen but NO adapters
behind it"), and no IDF ESP_LOGE appears, so IDF is not reporting a failure -- there is
simply no attach event.

This now has a clean, cheap test rig: powered hub + Prolific adapter, no radios needed.
Tracked as its own defect rather than as evidence about any particular radio.

## 0.9.71 — rounding narrowed to HF downlinks only

Owner: after the mode-apply fix, the 5 kHz rounding remains ONLY on HF downlinks
(AO-7 mode A, 29.4 MHz on the TH-D75's band B), and full-OTR tuning via the DOWNLINK
radio is smooth. So modes ARE reaching the radio now and fine mode works on VHF/UHF --
the remaining case is band-specific.

Hypothesis to TEST, not to code around: the D75 may not offer fine mode below 30 MHz on
band B. tools/thd75_verify.py already sweeps modes and MEASURES the achievable grid, and
takes --freq, so `python3 thd75_verify.py --freq 29400000` answers it directly against
the radio. If fine mode is refused on HF, 5 kHz there is the radio's limit and CardSat
should stop trying rather than pretend otherwise.

## 0.9.71 — HF rounding: fine mode does NOT survive a band change (CardSat bug)

Owner ran thd75_verify.py --freq 29400000: at 29.4 MHz the radio accepts fine mode in
USB/LSB/CW/AM with a measured 20 Hz grid, exactly as it does on VHF. **So the radio is
not the limit and the "HF" framing was a red herring** -- what matters is not the band
itself but CROSSING into it.

MECHANISM. The TH-D75 holds its tuning step PER BAND. CardSat sends MD/FT/FS first and
the frequency second, so on a bird whose downlink lies in a different band from wherever
the radio was sitting -- AO-7 mode A, downlink 29.4 MHz, with the radio previously on
UHF -- the fine step is applied to the OLD band and then discarded by the move. Nothing
downstream notices: kwGrid() still returns 20 Hz, CardSat still sends exact frequencies,
and the radio quantises them to that band's 5 kHz step. Hence "rounding on HF downlinks
only, VHF/UHF perfect".

FIX: PlainCatRig remembers the last mode (_kwMode) and the coarse band of the last
frequency (_kwBand, split at 30 MHz and 300 MHz). After a successful KWHT frequency
write that changes band, FT/FS are re-asserted for the current mode. Cheap, idempotent,
and harmless if the radio ever stops needing it.

VERIFICATION ADDED rather than asserted: thd75_verify.py gains a band-change test --
set fine mode on UHF, measure the grid, cross to 29.4 MHz, measure again WITHOUT
re-applying, then re-apply FT/FS and measure a third time. If the middle number is
coarser than the third, the mechanism is confirmed on the owner's hardware and the
CardSat fix is the right one; if they are equal, the step survived and this fix is
harmless but not the cause. The script says which in plain words.

## 0.9.71 — band-change theory REFUTED; HF rounding source still unidentified

Owner's band-change run: 20 Hz on UHF, 20 Hz on HF after crossing WITHOUT re-applying,
20 Hz after re-applying. The step survives a band change. My fix is harmless and
idempotent so it stays, but it is NOT the cause, and the entry above is corrected.

Two theories now dead on this one symptom (band-specific fine mode; step lost on band
change), both killed by a two-minute script run rather than a firmware cycle.

WHAT READING THE CODE ESTABLISHES, and where it runs out:
  * kwGrid() is the ONLY quantiser in the tree -- grep for 5000 across rig/civ/icomnet
    finds nothing else. So a 5 kHz result means either _kwFine was false on the KWHT
    leg, or the rounding is not CardSat's at all.
  * modeFromString() defaults to RM_USB, so no transponder-mode string can land the
    leg on FM by accident; and applyTransponderModes() sets the downlink to USB on
    every linear bird regardless of band.
  * Which means reading further is guessing. It has now failed twice on this symptom.

INSTRUMENTED INSTEAD, on both legs, so the next run says which side rounds:
  * KWHT (TH-D75 over USB): logs "want X -> sent Y (grid G, fine=N)" but ONLY when the
    rounding actually moved the frequency. If this line is absent while the operator
    sees 5 kHz steps, CardSat is sending exact values and the radio is quantising.
  * Icom LAN (IC-705): logs the exact Hz handed to CI-V, rate-limited to every 16th
    write. CI-V carries 1 Hz resolution and this path applies no rounding, so a 5 kHz
    result there is an Icom TUNING STEP setting on the radio, not CardSat.

The distinction matters because the two have completely different fixes, and AO-7 mode A
is the one configuration where the HF leg might be EITHER radio depending on which leg
the operator assigned -- something worth confirming alongside the log.

## 0.9.71 — OTR uplink: we were DISCARDING the dial frames (CI-V transceive)

The counters added last round answer it outright:

    freq read TIMEOUT after 300ms -- drained 0 stale, saw 12 pkt(s) this read
    freq read TIMEOUT after 300ms -- drained 0 stale, saw 13 pkt(s) this read

12-13 packets arrive during EVERY read and NONE of them matches. Nothing is being
discarded as stale (drained 0), so the "we are one read behind" theory is dead too --
the packets are arriving inside the window and being rejected by the parse.

CAUSE: the frequency parse required `f[2] == 0xE0` (destination = controller) and
`f[4] == 0x03`. An Icom with CI-V Transceive enabled emits a frame every time the dial
moves, addressed to **0x00 (broadcast)** with command **0x00**. Those are exactly the
frames that carry the operator's dial position -- and we threw every one of them away,
catching only the occasional direct poll reply. Hence uplink-based OTR feeling
unresponsive while the downlink path, a different transport entirely, felt smooth.

FIX: accept FE FE <E0|00> <addr> <03|00> <5 BCD> FD. Both forms carry the frequency in
the same place, so one widened check covers them.

VERIFIED BY CONSTRUCTION rather than by hope: the same timeout line now prints the
leading bytes of the last CI-V frame that did NOT match. If the theory is right those
read FE FE 00 A4 00 -- and the widened match consumes them, so the timeouts should
largely stop. If they read something else, the bytes say what is really arriving and
the theory is wrong. (Anchor check worth recording: the capture site appears twice in
the file, in readPtt and readFreqNet; placement in readFreqNet was confirmed by
extracting the function body, not assumed from a successful build.)

Note this also means polling at ~2.5 reads/second was never necessary for a radio with
Transceive on -- the dial position arrives unsolicited. Reducing the poll rate is a
follow-on worth making once the fix is confirmed.

## 0.9.71 — leg assignment clarified

Owner: "Kenwood HTs can only be the downlink and is in this case." So for AO-7 mode A
the HF 29.4 MHz DOWNLINK is the TH-D75 over USB, and the rounding therefore sits on the
KWHT path -- kwGrid() -- not on the IC-705's CI-V path. The KWHT instrumentation
("want X -> sent Y (grid G, fine=N)") is in place but this log was a VHF bird, so the
line could not appear. An AO-7 mode A run is what will show it.

## 0.9.71 — USB enumeration: a BOOTSTRAP BUG in the enumeration window

Explored the hub + IC-705 failures. The strongest finding is in CardSat's own code:

    inline uint32_t enumCapMs() { return s_sawHub ? 9000 : 2500; }

The long budget exists SPECIFICALLY FOR HUBS -- and it is gated on s_sawHub, which is
only set once a hub HAS ALREADY ENUMERATED. So a hub that needs more than 2500 ms can
never be seen, and the generous window written for hubs is unreachable by the very
devices it was written for. The same 2500 ms also applied to a radio with a slow USB
stack.

This is a strong candidate for BOTH symptoms:
  * a hub must power its own controller, after which IDF applies port reset recovery
    (CONFIG_USB_HOST_EXT_PORT_RESET_RECOVERY_DELAY_MS=30) and a power-on delay per
    downstream port before anything behind it can appear;
  * the IC-705 is a self-powered radio whose USB stack comes up on its own schedule,
    not the host's.
Both work on a Mac, which waits indefinitely.

FIX: cap is now 9000 ms normally and 12000 ms once a hub is seen. Note the cap is a
CEILING, not a wait -- the loop exits as soon as the bus goes quiet with at least one
device present, so a USB-serial adapter that appears in 400 ms still returns in 400 ms.
Raising it costs time only when something slow is attached or nothing is attached at
all, and in that second case a few extra seconds is far cheaper than reporting "no
adapters found" for a device that was still waking up.

RULED OUT ALONG THE WAY, so they are not revisited:
  * Configuration descriptor size -- IC-705 is 141 bytes against a 256-byte limit.
  * Hub support missing -- CONFIG_USB_HOST_HUBS_SUPPORTED=y, HUB_MULTI_LEVEL=y.
  * A board-level USB host power switch CardSat fails to enable -- M5Unified has no
    such GPIO for the Cardputer ADV; VBUS is not under firmware control here.
  * Device class/descriptor exotica -- the 705 is an ordinary dual-CDC composite, and
    its descriptor is smaller than the TH-D75's, which works.
NOTED, not pursued: Arduino's IDF ships CONFIG_USB_HOST_HW_BUFFER_BIAS_PERIODIC_OUT=y
rather than BALANCED, which shrinks the non-periodic FIFO used by control and bulk
transfers. It affects throughput rather than attach detection, so it does not explain a
missing attach, but it is a real deviation from the IDF default and worth remembering.

STILL POSSIBLE if the window fix does not do it: inrush/VBUS current limiting on attach
(a hub and a radio both present far more bulk capacitance than a USB-serial adapter),
or USB-C CC configuration. Neither is diagnosable from firmware, which is why the
diagnostic below reports whether IDF sees an attach AT ALL.

DIAGNOSTIC d1097a2c: logs every client event (NEW_DEV / DEV_GONE) and every host-library
event flag. Silence on plug-in means IDF never detected an attach -- electrical, and no
amount of firmware work will help. Any event means it was seen and the library lost it,
which is ours to fix.

## 0.9.71 — OTR responsive; and the same early-return bug found in a WIDER form

Owner: uplink OTR with the IC-705 on LAN is now responsive. The log shows long runs of
successful reads tracking the dial smoothly (145897260 -> 145898550 -> 145901210 ...).

HONEST NOTE ON THE MECHANISM. The timeout lines all report
"last CI-V 00 00 00 00 00", i.e. during a timed-out read NO CI-V frame arrives at all --
the 12-13 packets are pings and keepalives, not data. So the widened match is consistent
with the improvement (broadcast frames that were previously rejected are now accepted
and counted as successes) but the "we were discarding dial frames" story is NOT directly
confirmed by the bytes, because unmatched CI-V frames never appear during failures.
Recording that distinction rather than claiming a proven mechanism.

MEASURED latency of 121 successful reads: p50 23 ms, p75 36 ms, p90 94 ms, p95 121 ms,
p99 188 ms, max 259 ms. A 200 ms budget would still catch 99%.

WHICH EXPOSED A BIGGER BUG. The log says "budget 300", but CardSat's intended value is
constrain(effectiveCatRateMs()/4, 60, 200) -- capped at 200. 300 is IcomNetRig's own
fallback, used when readBudgetMs is still 0. It was 0 because initializeEngagedRig()
opens with `if (!rig || !rig->ready()) return;` -- the SAME early-return defect fixed
earlier for applyTransponderModes(), but on a function that also carries:
  * enableSatMode()
  * the MAIN/SUB band assignment
  * setReadBudgetMs()
In a mixed dual rig none of those ever ran, because the LAN leg is not ready at engage
and DualRig::ready() requires every leg. The mode fix retried only the modes; sat mode,
band assignment and the read budget stayed lost.

FIX: initializeEngagedRig() sets rigInitPending when it bails, and the loop re-runs the
WHOLE function once the rig reports ready. That also subsumes the mode retry, which is
kept for the paths that call applyTransponderModes() directly.

Consequence worth noting: with the budget now actually applied (<=200 ms instead of the
300 ms fallback), a timed-out read blocks the cooperative loop a third less. At ~625
timeouts in a six-minute session that is a meaningful reduction in loop stalling for
everything else -- USB CAT ticks, the UI, the rotator.

## 0.9.71 — the window fix WORKS, and exposed a stack that is too small for hubs

Owner's log, the good news first:

    43761 scan: adapter[0] addr=2 Prolific ... key=067b:23a3/FDCKb133812
    43840 scan: hub present - extended enumeration window used

**A hub enumerated and a device behind it was found.** That is the first time either has
happened, and it confirms the bootstrap bug: the long window really was the blocker, and
gating it on "have we already seen a hub" made it unreachable. The IC-705 also worked
once behind the hub.

**BUT IT IS UNRELIABLE, AND THE STACK NUMBERS SAY WHY.** The high-water mark printed at
every disengage:

    no hub:  EspUsbHost task  used 1156 of 4096  free 2940
    HUB:     EspUsbHost task  used 2684 of 4096  free 1412

kTaskStack = 4096 was sized from END_CDC headroom logs taken with a single directly
attached device. IDF's multi-level external hub support recurses through port
enumeration and more than doubles the peak. This file's own stated rule -- safe =
used + 2048 -- wants 4732, MORE than was allocated. 1412 bytes of headroom on a task
whose overflow reboots the device is not a margin, and the log shows repeated
"## REBOOTED - reset reason=3" around hub scans.

FIX: kTaskStack 4096 -> 6144. That is 3460 bytes of headroom against the measured hub
peak, still below the library's 8192 default, and costs ~4 KB of heap across the two
tasks. The high-water mark is printed at every disengage, so this remains a measurement:
if a hub-with-devices peak ever exceeds ~4 KB, raise it again.

Worth noting the sequence: the enumeration-window bug HID the stack problem, because a
hub could never get far enough to use the stack. Fixing the first exposed the second --
which is the usual shape, and a reason not to read one fix working as the end of a
problem.

STILL OPEN: two radios (TH-D75 + IC-705) on USB together did not work. That needs its
own look once hub enumeration is stable, since it is the case that needs the hub, three
devices, and ESP_USB_HOST_MAX_DEVICES=4 all at once -- and the earlier note about
reducing MAX_DEVICES to 3 would have broken exactly this configuration.

## 0.9.71 — full USB review (CardSat + vendored EspUsbHost)

APPLIED THIS ROUND
  1. **Host task stack 4096 -> 6144** (previous entry). The measured hub peak is 2684
     versus 1156 without a hub; this file's own rule wants 4732. Done.
  2. **Adapter-registry exhaustion is no longer silent.** onDev() dropped a device with
     a bare `return` when the 4-slot s_serDev[] array was full. The operator saw one
     fewer adapter with NOTHING in the log -- indistinguishable from "it did not
     enumerate", which is a different problem with a different fix. Now sets a flag and
     the scan reports "MORE devices than the 4-slot registry - some are not listed".

REVERTED, and worth recording as a lesson: I added a shorter settle window for a
non-fresh host, believing the 9 s cap was being paid on every scan. The owner's own
timings disprove it -- first scan after boot 9111 ms, every later scan 79-80 ms --
because hostUpForRotator() returns early when s_host exists, so the settle loop only
ever runs on a cold host. The change was unnecessary AND would not have compiled
(`freshHost` was undefined at the use site). Reverted; a comment now records the
measured timings so nobody re-derives the same wrong premise.

REMAINING FINDINGS, ranked, NOT yet acted on
  A. **ESP_USB_HOST_MAX_DEVICES = 4 is exactly at the limit** for a real station:
     hub + radio A + radio B + USB rotator = 4, with nothing spare -- and many
     "4-port powered hubs" are internally two chained 2-port hubs, which needs 5.
     Exhaustion logs at ESP_LOGW, compiled out on our FQBN, so the device vanishes
     silently. Raising it costs heap in the ~13 KB EspUsbHost object; the cost per slot
     has NOT been measured, so this needs a number before a decision. This is the most
     likely cause of "could not get both a TH-D75 and IC-705 working over USB".
  B. **Endpoint-slot exhaustion** ("No endpoint slots available", 6 sites) also logs at
     a compiled-out level. ESP_USB_HOST_MAX_ENDPOINTS = 16; two CDC radios plus a hub
     plus a rotator is comfortably inside it, so this is a diagnosability gap rather
     than a live limit.
  C. **Address-based adapter keys are unstable behind a hub.** A device with no
     iSerialNumber keys as VID:PID@address (the TH-D75 is exactly this). Hub port order
     and enumeration order decide the address, so a replug or a different power-up order
     can silently rebind a leg to the wrong radio. Worth a warning in the picker when a
     configured key is address-based AND a hub is present.
  D. **The IC-705 exposes TWO CDC functions and CardSat always binds the first.**
     Vendored patch 1 makes that deterministic, which is right, but if the radio puts
     CI-V on the second function we bind a working port that never answers -- the exact
     silent-failure shape that cost this project days on the D75. A per-leg "which CDC
     interface" setting, or trying the second when the first is mute, would remove the
     guess.
  E. **"scan: releasing temporary host" now lies** -- with the host resident by default
     releaseHostIfIdle() returns immediately and nothing is released. The message
     predates residency. Cosmetic, but this file already carries a comment saying "a log
     that lies is worse than no log", so it should be fixed.

## 0.9.71 — the hub/direct INVERSION explained: address-based adapter keys

Owner: the IC-705 now works reliably behind a hub but NOT directly; the TH-D75 works
directly but NOT behind a hub. An inversion like that is not a bus fault -- a bus fault
does not prefer one radio when hubbed and the other when not.

CAUSE, for the D75 half: NEITHER radio reports an iSerialNumber, so both key as
VID:PID@ADDRESS (the D75 is literally logged as key=2166:9023@1). The USB address is
assigned by ENUMERATION ORDER, so it is not a property of the radio at all. Add a hub
and the hub takes an address and the radio gets a different one; the configured key then
never matches, because every lookup was an exact strcmp. Nothing is wrong with the
radio, the hub, or the bus -- the leg simply finds no adapter and reports none. The
IC-705 works behind the hub because it was CONFIGURED there, and would fail the same way
if moved back to a direct connection.

FIX: findAdapter() -- exact match first, always; then, only if the wanted key is the
address form, fall back to the VID:PID part, and ONLY when exactly one live device
carries it. That last condition preserves the entire reason the address is in the key:
two identical adapters (the likely radio + rotator case) stay ambiguous and are never
guessed between. When the fallback fires it says so, naming the wanted and found keys,
so a silent rebind is impossible. Routed through all four match sites (CAT-A, CAT-B,
rotator, and the presence check).

This should make a configured radio survive being moved between a direct connection and
a hub, between hub ports, and across a different power-up order -- none of which change
the radio, and all of which changed the key.

NOT claimed: that this fixes the IC-705's failure to enumerate when connected DIRECTLY.
That is a separate matter and still looks electrical -- it is the half of the inversion
that a key cannot explain, since a device that never enumerates has no key to match.

## 0.9.71 — two misleading messages fixed, and a third found while fixing them

**1. "Only adapter is the rotator's" was wrong in a dual-USB config.** The picker
excludes up to TWO keys: for a RADIO leg those are the rotator's adapter AND the other
radio leg's (passed in as alsoTaken). A single `skippedTaken` flag lost which one
matched, so the message always blamed the rotator -- confusing exactly when the operator
is working out which port owns what. Now tracked separately and worded accordingly:
"Only adapter is the rotator's" / "Only adapter is the other leg's" for a radio picker,
"Only adapter is the radio's" for the rotator picker (both excluded keys are radio legs
there, so that one was already right).

**2. "scan: releasing temporary host" no longer lies.** Since the host became resident
by default, releaseHostIfIdle() returns without releasing, so the line claimed something
that never happened -- and an operator reading "releasing" would reasonably expect the
serial console back. Now reports "scan: host stays resident (Fn+u on Track releases it)"
when residency is on. (The rotator's equivalent trace had already been written this way.)

**3. FOUND WHILE CHECKING THE OTHERS: the disengage report had the same defect, and it
is the line the operator reads most.** "## DISENGAGED: stack released=yes" was printed
even though residency means the stack is NOT released -- because s_hostReleased is a
latch about whether the last teardown SUCCEEDED, not about whether the host is up now.
The bench logs show the contradiction plainly: "released=yes" beside heap that never
returns (17 KB rather than 82 KB). Reading that as a leak is the obvious inference and
the wrong one. Now three-way: RESIDENT / released=yes / released=NO (reboot needed).
The failed-engage report carried the identical two-way test and got the same fix.

Worth noting the pattern: all three are the same bug -- a message written when
teardown-on-disengage was the only behaviour, left unrevised when residency changed what
actually happens. Adding a mode without auditing what the existing messages assert about
that mode is how a log starts lying.

## 0.9.71 — 0.9.7x USB work carried into the CardSatDualRig companion

Reviewed the companion against every USB fix from 0.9.70/0.9.71 rather than assuming.

ALREADY PRESENT / NOT APPLICABLE, verified rather than skipped:
  * **Library-level fixes** (serial-OUT drain, halt-not-clear, always-uninstall,
    quiesce, CDC first-control guard): the companion links the SAME vendored
    EspUsbHost, so it inherits all of them provided it is built against
    third_party/EspUsbHost/. That requirement is stated in its README and sketch header.
  * **Resident host**: the companion already calls gUsb.begin() once in setup() and
    never end()s, so it has always behaved the way CardSat only now does.
  * **Task stack**: the companion uses the library default (8192), already above the
    6144 CardSat needed for hub enumeration. No change.
  * **Enumeration window**: N/A -- it binds from onDeviceConnected callbacks and never
    polls with a deadline, so the bootstrap bug that hid hubs has no analogue.

APPLIED THIS ROUND:
  * **DTR de-assert on leg release** (earlier in 0.9.70): usbReleaseControlLines().
  * **VID:PID leg pinning.** The companion could pin a leg only by USB SERIAL, falling
    back to enumeration order. Both of the owner's radios report NO serial (TH-D75 and
    IC-705 are both iSerialNumber = 0), so both legs could only ever be order-bound --
    and order is exactly what a hub changes. Downlink and uplink could swap silently
    with both radios working. legPinMatches() now accepts a "vvvv:pppp" VID:PID as well
    as a serial, which is deterministic whenever the two radios are different models.
    Applied to BOTH bind paths (bindDevice and bindSeen) -- the second is the one that
    runs on a live reconfigure, and missing it would have made the fix work only at
    first attach.
  * **Registry overflow is no longer silent.** registerSeen() dropped a device with a
    bare `return` when gSeen[6] was full -- the same trap CardSat's adapter registry
    had, where a dropped device is indistinguishable from one that never enumerated.

Build: 1,280,526 flash (38%), 61,244 RAM (18%), 0 warnings, both new strings verified
present in the ELF. Still EXPERIMENTAL and untested on hardware, and still labelled so
in all four places.

## 0.9.71 — REGRESSION from residency: the adapter list stopped being rebuilt

Owner: the IC-705 is never seen, and the TH-D75 still shows even with a Prolific
attached. Both are one bug, and it is mine.

    78692  D75 present                   -> listed
   108603  D75 UNPLUGGED                 -> "(unplugged)", still listed
   133092 .. 205343  same stale entry across six more scans,
                     while a Prolific attached in that window is NEVER listed

CAUSE: the registry is cleared exactly once -- inside hostUpForRotator()'s CREATE path
("fresh host: clear the registry"). Before residency every scan created a host, so every
scan rebuilt the list from a real enumeration. With the host resident that never happens
again: the list only accumulates whatever the callbacks happened to report, so a
tombstone lives forever and anything attached later can be missing entirely.

This is the second time residency has broken something that silently depended on
"every scan starts a new host" -- the first was the disengage report claiming
"released=yes". Adding a mode is not just new behaviour; it invalidates assumptions
that were never written down.

FIX: scanAdapters() now forces a genuine release BEFORE enumerating, but ONLY when no
port is open. Residency exists to stop a re-enumeration killing a live radio session; a
manual scan with nothing engaged has no session to protect, and the operator asked for
the truth. The host is then left resident afterwards, so an engage moments later does
not pay the enumeration again. Cost: the cold-start window on an explicitly requested
scan -- exactly what it cost before residency.

NOT explained by this, and still open: why the IC-705 does not enumerate at all when
connected directly. A stale registry cannot hide a device that never attaches, and the
earlier direct-connection failures predate residency. That remains the electrical
suspicion (VBUS/inrush), and it is the one thing the powered hub reliably works around.

## 0.9.71 — companion checked against the stale-registry regression

Asked to make sure the CardSat registry bug does not affect CardSatDualRig. Checked
rather than assumed, and the answer is two-part.

**THE SPECIFIC BUG DOES NOT APPLY.** CardSat's failure was that the adapter registry is
cleared only when a host is CREATED, so a permanently resident host means it is never
rebuilt. The companion does not share the pattern:
  * it never clears gSeen wholesale, so it does not depend on a fresh host to do so;
  * a disconnect FREES the slot (unregisterSeen sets used = false) rather than leaving a
    tombstone, so entries cannot accumulate;
  * onUsbDisconnected is wired, and it also unbinds the leg.
Its host has always been permanently resident, so if it shared the pattern the symptom
would have been visible long ago.

**BUT THE SAME FAILURE CLASS HAS ANOTHER ROUTE IN, and that one was real.** A leg is
unbound only by onUsbDisconnected() or a live reconfigure, and every bind path refuses a
leg that is already `bound`. pollRadioOnline() would set online = false when the device
vanished, but leave `bound` set. So a MISSED disconnect event -- device yanked
mid-transfer, host confused, or the radio reappearing at a new address before the
callback lands -- left the leg pinned to an address that no longer exists, permanently
offline, with nothing able to recover it. CardSat escapes this via re-enumeration on a
scan; the companion has no equivalent, because its host is created once in setup() and
never torn down.

FIX: pollRadioOnline() now releases a binding that has been offline continuously for
10 s (about five rigctld poll cycles -- long enough not to react to a hiccup) and
re-binds from the live registry. Re-binding is exactly what a clean
disconnect/reconnect would have done, so this restores the normal path rather than
inventing one, and it cannot bind a radio that is not present because bindSeen() walks
gSeen. lastOnlineMs is armed at all four bind sites and cleared at both unbind sites.

Self-inflicted detail worth recording: the first application of that timer used two
overlapping search patterns and double-assigned it at two of the four sites
(`p.lastOnlineMs = 0; p.lastOnlineMs = 0;`). Harmless, but it was found by checking the
result rather than trusting the replace count -- the counts themselves (1+2+1+2 for four
sites) were the tell.

Build: 1,280,770 flash (38%), 61,244 RAM (18%), 0 warnings, the new string verified in
the ELF. Still EXPERIMENTAL and untested on hardware.

## 0.9.71 — partition scheme enlarged, and item 1 (MUF to a DXCC entity)

**PARTITIONS.** Moved from stock `huge_app` to a custom partitions.csv. huge_app is a
4 MB-part layout (3 MB app, 896 KB FS) and left HALF of the Cardputer ADV's 8 MB chip
unused, while the app sat at 96.9% with 97 KB free.

    app0      4 MB    (was 3 MB)      -> 1,142,800 bytes free, 11.8x the headroom
    spiffs    1.5 MB  (was 896 KB)    -> LittleFS for operators with no microSD
    coredump  64 KB   (kept)
    total 5.62 MB, leaving 2.38 MB of the part unallocated

The ceiling is deliberate, not an accident: most users install through Launcher, which
lives in flash alongside the app and writes its own partition table sized to the binary
it installs. Letting the app expand to fill the chip would squeeze the tool people use
to install it. Two constraints preserved and documented in the file: the FS partition
must stay NAMED "spiffs" (LittleFS.begin(true) looks that label up -- a rename silently
mounts nothing and loses every setting), and the coredump partition must stay (the panic
backtrace is read back on the next boot).

NEW GATE tools/check_app_fits.py (23rd). With PartitionScheme=custom, arduino-cli reports
usage against the scheme's DECLARED 16 MB ceiling in boards.txt, NOT against the 4 MB
app0 we actually define -- "Sketch uses 3048390 bytes (18%)... Maximum is 16777216" is
meaningless and the compiler would happily emit an unflashable binary while the log
called it healthy. huge_app reported the true limit, so this check was not needed before;
it is needed precisely BECAUSE the scheme changed. It also validates the layout itself
(ordering, overlap, 8 MB bound, the spiffs label, the coredump partition). Validated both
ways: passes as shipped, and fails with the exact reason on a renamed FS partition and on
an oversized binary.

**ITEM 1: MUF to a DXCC entity.** The tool answered "how are paths generally"; the
question an operator has is "can I work THAT entity now", which no region row answers.
Press `d` on the MUF screen to pick an entity, `D` to clear; the path appears as a
pinned row above the region table, same model and units so it is directly comparable.

Data: no DXCC coordinates existed anywhere in the tree. Generated src/dxcc_geo.h from
AD1C cty.csv -- 340 entities, 6 bytes each (~2 KB), int16 hundredths of a degree
(~1.1 km, far finer than MINIMUF's own accuracy). Coverage verified: the 62 entities
without coordinates are EXACTLY the 62 flagged deleted in DXCC_LK, so nothing current is
missing, and dxccGeoFind() returns false for them so the UI says "no location" rather
than plotting 0,0 -- a real place in the Atlantic that would look like a plausible answer.
Host-tested against known positions before wiring anything.

SIGN CONVENTION, the trap here: cty.csv stores longitude WEST-positive; CardSat is
east-positive; minimufMHz() wants west-positive (MUF_REGIONS stores it that way and
passes it straight through). The table is stored east-positive to match the rest of the
firmware and negated at the MUF call. Getting this backwards puts every path on the far
side of the planet, so it is stated in the header and at the call site.

The picker REUSES the existing type-to-search screen (SCR_DXLK) via a dxPickFor flag
rather than duplicating prefix/name matching; back from the picker returns to MUF, not
Tools, so the operator is not stranded somewhere they never chose.

TWO SELF-INFLICTED ISSUES, both caught by the toolchain rather than shipped:
  * CL_DKBLUE does not exist -- the palette has 16 fixed slots. Used CL_BLUE, and
    deliberately NOT the green CL_SELBG, so a pinned target does not look like a second
    list cursor.
  * Inlining dxcc_geo.h early in the .ino broke the build with "'VoiceMemo' has not been
    declared". dxccGeoFind() is a FUNCTION, and Arduino inserts its generated prototypes
    immediately before the first function definition in the sketch -- which my block had
    just become, putting prototypes ahead of VoiceMemo's declaration. Moved the block
    next to its use in the MUF section. Worth remembering: in the concatenated .ino,
    WHERE a function lands changes where every prototype lands.

## 0.9.71 — items 3 and 4: string variables and Microsoft-style text functions

BASIC had NO string type at all: 26 numeric variables A-Z, string literals usable only
as PRINT items. Items 4, 6 and 7 all depend on this, so it went first.

ADDED
  * **A$ .. Z$**, 26 string variables. The table is ALLOCATED ON DEMAND -- 26 Arduino
    Strings cost real heap and most programs never touch one, so it appears the first
    time a program mentions a string variable and dies with the VM. That is the same
    rule the feeds, notes and BASIC program buffers already follow.
  * **LEFT$ RIGHT$ MID$ CHR$ STR$ UCASE$ LCASE$ TRIM$** (string-returning) and
    **LEN ASC VAL INSTR** (number-returning).
  * Concatenation with '+', and **string comparison in IF** (= and <> only).
  * Strings work in PRINT, and in LPRINT/FPRINT via the shared emitLine() grammar --
    otherwise the report sinks would silently disagree with the screen.

DESIGN NOTE. Strings are a SEPARATE evaluator, not a variant type threaded through the
numeric one. The numeric path is the hot path (Doppler loops, plot points) and tagging
every value would cost speed and memory on the 99% of expressions that are numbers.
BASIC has always separated the two syntactically with '$', so the parser does too:
isStrStart() decides which evaluator a caller needs without consuming input.

Two deliberate restrictions, both stated in the code:
  * Comparison is = and <> only. Ordering strings raises collation questions this
    interpreter cannot answer, and a confidently wrong answer is worse than a refusal.
  * fmtNum() is shared by PRINT and STR$. Separate formatting would let PRINT X and
    PRINT STR$(X) disagree -- a difference that surfaces months later in someone's
    report output.

NEW HARNESS tools/host_basicstr (12th). Text functions are where a silent off-by-one
lives: MID$ is 1-BASED and INSTR returns a 1-based position with 0 meaning "absent",
and getting either wrong produces a program that runs, prints something plausible and
is wrong -- which the operator would blame on their own code. 16 vectors check the
index arithmetic against Microsoft BASIC's actual behaviour, including the clamping
cases (LEFT$ beyond end, negative counts, MID$ past the end, MID$ start 0). All pass.

Flash after: 3,055,680 of 4,194,304 (72.9%), 1,138,624 free. The partition work paid
for itself immediately -- this would not have fitted the old 3 MB app.

## 0.9.71 — item 5: named arrays

BASIC had exactly ONE array, `@()`, capped at 256 doubles. Added named arrays A()..Z()
alongside it; `@()` is untouched so existing programs keep working.

  * `DIM A(n)` and `DIM A(10), B(20)` -- several per statement, as in MS BASIC.
  * `A(i)` reads and assigns; re-DIM is allowed and clears, which is what a programmer
    expects from DIM inside a loop.
  * `ERASE A` gives the heap back early rather than waiting for the program to end.

MEMORY DISCIPLINE, and why the budget is shared. Each array is allocated ON DEMAND when
DIMmed and freed with the VM. The cap is on TOTAL elements across every array
(ARR_TOTAL_MAX = 2048 doubles, 16 KB), not per array: 26 arrays of 1024 would be 208 KB
of doubles on a device whose whole free heap is ~76 KB, so a per-array limit would let a
program that reads perfectly reasonably line by line starve the radio, the display and
the USB host. re-DIM releases the old allocation from the budget before charging the new
one, so a DIM in a loop cannot creep.

BOUNDS ARE ENFORCED, not traditional. arrCell() refuses a subscript outside the array
and stops the program. Classic BASIC's silent out-of-range write is not survivable here:
this interpreter shares an address space with a live CAT session and a USB host, and a
stray write would show up as something else entirely, days later.

PARSING NOTE. `A(i)` is treated as an array element only when A has actually been
DIMmed. Checking the DIM state rather than merely the '(' keeps `A (3+1)` -- a scalar
followed by a parenthesised term -- working for programs written before arrays existed,
and keeps an undimensioned `A(` as the old clear error instead of a mysterious no-op.

Harness tools/host_basicstr extended with 13 array vectors covering the bounds refusals,
the shared budget, and that re-DIM releases rather than accumulates. All pass alongside
the 16 text vectors.

## 0.9.71 — item 6: pre-run input form (SCR_BASICASK)

A program declares what it needs:

    10 INPUT "Downlink MHz"; F
    20 INPUT "Callsign"; C$

Fn+R scans the source, and if any INPUT is declared it shows ONE form -- all fields at
once -- then runs the program to completion with the variables already set. A program
that asks nothing runs immediately, exactly as before.

WHY A FORM RATHER THAN A RUN-TIME PROMPT, which is what BASIC traditionally does: this
interpreter executes inside a single key handler, to completion, with the watchdog fed
by a statement budget. Stopping mid-run to wait for a keystroke would mean re-entering
the event loop with a live VM on the stack -- the design this interpreter deliberately
avoids. Collecting first preserves the run-to-completion property, which is the whole
reason it is safe to run BASIC on a device that is simultaneously flying a radio.

Details that matter:
  * The scan is a TEXT pass over basicBuf, not an interpreter pass. Building a VM to
    discover its questions would mean running the program to find out what to ask it.
  * Duplicate targets collapse to one field -- two boxes writing the same variable is a
    UI that cannot be right.
  * An empty field leaves the variable at its default (0 / "") rather than erroring: a
    program asking for an optional value should not be stopped by declining to give one.
  * At run time INPUT steps over its own arguments and continues, because the value is
    already in place.
  * DEL edits the value and never exits the form -- over-backspacing must not discard a
    half-filled form, the same trap the immediate-mode prompt had until 0.9.70.

## 0.9.71 — the text-screen gate had a blind spot, and it was hiding a shipped bug

audit_text_screens reported "all excluded" while SCR_BASICASK -- a screen that takes
typed callsigns and grids -- was NOT excluded. Cause: the gate matched `c >= 32 && c <
127`, and the new handler used `c > 32 && c < 127`. One character of difference, and the
gate silently approved the exact bug it exists to prevent.

Widened to accept both spellings, and it immediately found a SECOND, PRE-EXISTING
offender: **SCR_TGTSEARCH**, which has always taken typed text and has always lost `b`
and `h` to the global screenshot and help hotkeys. That one has been shipping.

Both now excluded; the gate covers 10 text-entry screens (was 7). Recorded because the
lesson is not "add the screen" -- it is that a gate matching one spelling of what it
looks for is worse than no gate, because it is believed. This is the second time this
particular gate has needed hardening: the first version could be satisfied by a comment
mentioning a screen name.

## 0.9.71 — items 2 and 7: constants, functions, and example programs

ITEM 2 -- constants and functions BASIC lacked:
  * Constants (bare, no parens): PI TWOPI DEG RAD CLIGHT KBOLT REARTH. Writing 3.14159
    instead of PI is a 5-arcsecond pointing error at the horizon; these exist so a
    program does not retype them subtly wrong.
  * Maths: ATN2(y,x) ASN ACS LOG10 ROUND FRAC HYP. ATN2 matters most -- hand-rolling a
    bearing with ATN(y/x) loses the quadrant and divides by zero due east, and every
    geometry program needs it.
  * Geometry/radio: GCDIST GCAZ DXCCLAT DXCCLON, and the string-returning GRID$ DXCC$
    TIME$ DATE$. All reuse the FIRMWARE'S OWN routines (greatCircle, Location::toGrid,
    the DXCC tables, the same sys snapshot). A second hand-written copy is how a program
    comes to disagree with the tracker it is running on, and TIME$ built from anything
    other than the snapshot could print a timestamp contradicting the program's own
    UTCH/UTCM.

ITEM 7 -- three example programs, in the REPOSITORY (not flash, per the owner):
  DXPATH.BAS    the input form + DXCC path work + GRID$/TIME$/DATE$
  CALLPARSE.BAS string variables and every text function, with the MS index rules
  PASSTATS.BAS  named arrays, DIM of several, ERASE

NEW GATE tools/audit_basic_examples.py (24th). Examples ship in the repo, so NOTHING
compiles or runs them: a typo, or a function renamed in app.cpp, produces an example
that fails on the operator's device -- and they cannot tell whether the mistake is
theirs or ours. An example that does not run is worse than none, because it teaches that
the interpreter is unreliable.

THE GATE'S FIRST VERSION WAS WRONG IN THREE WAYS, and it flagged 112 "errors" in the 17
EXISTING examples, all of them false:
  * it did not strip trailing ": REM note" comments, so it judged prose as code;
  * it tokenised scientific notation, turning the perfectly valid `1E8` into an unknown
    name `E8`;
  * its keyword list omitted PSET and MOD, both real (MOD is an infix operator at
    app.cpp:29672).
Fixed, and only then validated in both directions: 19 programs pass, and injecting
`GCDISTX` into DXPATH.BAS fails with the exact file, line and token. Recording this
because the pattern keeps recurring in this project -- a new gate's first job is to
prove it is right about code already known to be good, and 112 confident failures
against working examples is what "wrong gate" looks like.

## 0.9.71 — item 8: calculator gaps

Surveyed both calculators first. The scientific one is already strong -- trig and
hyperbolics, atan2/hypot/mod/ncr/npr/fact/cbrt/log2, and a real radio set
(swr2rl/rl2swr/mml/fspl/nf2t/t2nf/dbd/dbi/dop) plus orbital porb/vorb/fpr. So this was
about GAPS, not volume; several obvious candidates (footprint radius, orbital period,
Doppler) already existed and were deliberately not duplicated.

ADDED, each one something an operator was computing on paper beside the device:
  lam(mhz)         wavelength in metres
  dipole(mhz)      half-wave dipole length, 0.95 velocity factor (the figure every
                   antenna book uses -- a free-space half wave cuts elements long)
  dbm2w / w2dbm    power conversions
  aorb(minutes)    altitude for a given orbital period -- the INVERSE of porb, and the
                   question actually asked when identifying an orbit
  slant(el, alt)   slant range from elevation and altitude
  dgain(d, mhz)    parabolic dish gain, dBi, 55% efficiency

Why slant() earns its place: the naive substitute -- treating altitude as range -- is
5.6x wrong at the horizon, which is exactly where a link budget is tightest and where
the error is least visible.

VERIFIED against independently computed values before shipping, not merely compiled:
lam at 145 and 435 MHz, dipole at 14.1 MHz, dbm2w/w2dbm at 30/60 dBm and 5 W, a
porb->aorb round trip at 420 km, slant at zenith (= altitude) and at the horizon, and
dgain for a 3 m dish at 10 GHz (47.7 dBi).

Worth recording: the slant() horizon check FAILED first, and the code was right --
my expected 2292 km was wrong, the true value being sqrt((Re+h)^2 - Re^2) = 2352.5 km.
Checking the disagreement rather than adjusting the code is the only reason that ended
correctly; had I "fixed" the function to match my number, every horizon link budget
would have been quietly 2.5% optimistic.

NOTE FOR ITEM 9: the calculator's function list is NOT shown anywhere on-device -- it
exists only in MANUAL.md and FEATURES.md. Item 9 must add these six there, or they are
invisible to everyone who does not read the source.

## 0.9.71 — item 9: help, references, and two silent documentation failures

ON-DEVICE
  * **SCR_CALCREF**, reached with **Fn+f** from BOTH calculators. The entry screen can
    show two lines of hints; there are 65 names. Behind Fn because every bare letter on
    those screens is expression text -- 'f' is needed for floor, fact, fspl and fq.
    audit_key_conflicts confirms no collision. Both calculators share ONE reference
    because they share one evaluator; a second list would drift from it.
  * The BASIC reference screen gained CONSTANTS, TEXT, ARRAYS, STATION AND GEOMETRY and
    ASKING FOR INPUT sections.

A FOOTER DECISION worth recording: the grapher's footer is already 39 of 39 columns of
keys that DO something. Fn+f is deliberately NOT advertised there -- hiding the table or
CSV export to advertise a help screen is the wrong trade. It is on the calculator's
footer and in the manual instead.

PRINTABLE
  * NEW **CardSat_CalcCard_4x6.pdf**, generated by tools_make_calccard.py which shares
    the refcard's LAYOUT verbatim and changes only content, so the two cards cannot
    drift in style. Includes worked examples with real numbers and a short "why some of
    these exist" panel (atan2 keeps the quadrant; slant is 5.6x off if you use altitude).
  * NEW GATE tools/audit_calc_card.py (25th): every `name(` on the card and in the
    on-device CALCREF must be a function the evaluator actually has. A reference listing
    a function the firmware lacks is worse than none -- the operator types it, gets an
    error, and assumes the mistake is theirs. Validated both ways (a planted `vesc(`
    fails with the name).
  * **Version numbers removed from the cheat and reference cards**, per the owner: they
    now describe the firmware as it is. "Added in 0.9.59" tells a reader nothing
    actionable and goes stale the moment the feature stops being new.

TWO SILENT FAILURES FOUND WHILE DOING THIS, BOTH THE SAME BUG:
  1. All three card generators read the version with `FW_VERSION\s*=\s*"([0-9.]+)"`,
     which matches NOTHING once the version carries a suffix. Every card had been
     printing **v0.0.0** without complaint.
  2. tools/build_manual.sh extracts the version the same way, and runs under
     `set -euo pipefail` -- so the failed grep ABORTED THE SCRIPT ON LINE 10. The manual
     had not been rebuilt at all; the PDF on disk was eight hours old, from the last
     release when the version had no suffix. I nearly shipped it, and only caught it by
     grepping the PDF for content I had just written and finding it absent.
Both fixed to accept any version string, and build_manual.sh now FAILS LOUDLY if the
version ever comes back empty rather than silently doing nothing.

Recording this because the lesson is not the regex. It is that "the command exited 0"
proved nothing: the script had exited 0 after doing no work, and the only thing that
caught it was checking the OUTPUT for something that should be in it.

Manual rebuilt: 165 pages, new content verified present.

## 0.9.71 — MUF DXCC row hid the last region row (my bug, and the gate slept through it)

Owner: with a DXCC target pinned, the last row of the region list is hidden behind the
footer.

Correct, and the arithmetic is unambiguous. The pinned row pushes the table down --
TOP 38 -> 51 -- but ROWS stayed at the literal 8, so the last row started at y=128 and
painted to y=139. footer() prints at y=127, over the top of it. The operator sees a list
that is silently one row short with no indication anything is missing, which is worse
than an obviously broken layout.

FIX: derive the count instead of writing it down twice.
    const int ROWH = 11, LAST_ROW_Y = 116;   // lowest y a row may START at
    const int ROWS = (LAST_ROW_Y - TOP) / ROWH + 1;
LAST_ROW_Y = 116 because a row paints fillRect(0, y-1, 240, ROWH), so y=116 fills to 126
and just clears the footer. That gives 8 rows at TOP=38 -- EXACTLY the original layout,
not a row fewer -- and 6 with a target pinned. A first attempt used ROW_BOTTOM=124 and
silently cost a row that had always displayed correctly; caught by computing both cases
before building rather than after.

WHY audit_screen_geometry MISSED IT. The analyzer evaluates literal coordinate
arithmetic. `int TOP = 38; ... TOP = 51;` is a mutable local with no single value at
analysis time, so every bound derived from it was UNVERIFIED -- and the gate passed the
function silently. Reporting success about something it never checked is the worst
failure mode a gate has, and this is the third time in this cycle a gate has done it.

GATE EXTENDED: a draw function whose list-layout base is a mutable int assigned more
than once now FAILS, with the fix spelled out. Refined once during development, because
the first version also flagged the CORRECTED code -- if the row count is derived from
the moving variable the layout adapts and there is nothing to warn about, so only a
moving base paired with a FIXED row count is flagged. Exactly one function in the tree
matched when written (this one), so it is signal rather than noise. Validated both ways
by exit code, not by eye: the pre-fix source exits 1, the fixed source exits 0.

## 0.9.71 — my re-enumerate change broke re-scanning; and a VBUS theory retracted

**REGRESSION, MINE.** The "re-enumerate from cold" fix released the host so the next
scan would rebuild the list. The bench log shows what actually happens:

    scan: re-enumerating from cold for an accurate list
    scan: host would not start

Every time. The IDF host cannot be reinstalled the instant it is uninstalled, so the
scan left the operator with NO host at all -- worse than the stale list it was meant to
fix, and it is the direct cause of "devices never re-enumerate when I re-scan".

FIX: rebuild the registry from the LIVE host instead of tearing it down. The library
already knows what is attached -- getDevices() reads its device table -- so a scan now
tombstones every entry, re-publishes whatever is really there, and reports the count.
No teardown, so nothing can fail to restart.

**A THEORY I HAD NO BUSINESS SHIPPING.** From one line of Mini-FT8's README I concluded
the Cardputer ADV's OTG port carries no VBUS without 5 V on PORTA, and I added an
operator-facing hint saying so. The owner had already MEASURED VBUS; an IC-705 charges
from the port, and a bus-powered serial adapter enumerates with no external supply. The
hint was removed the moment that was pointed out. It would have sent operators to
re-wire hardware that was never at fault -- a worse outcome than saying nothing.

**WHAT MINI-FT8 ACTUALLY OFFERS, checked in its source rather than its prose.** It does
not use the Arduino/IDF-bundled USB host: it vendors `espressif__usb` as a managed
component, and it explicitly REPARTITIONS the ESP32-S3 FIFO because "built-in Kconfig
biases do not cover" its case -- reserving lines specifically for NON-PERIODIC OUT,
which is the FIFO that carries control transfers (enumeration) and CDC bulk.

That matters here, because Arduino's prebuilt IDF ships
**CONFIG_USB_HOST_HW_BUFFER_BIAS_PERIODIC_OUT=y** -- biased toward isochronous OUT,
shrinking exactly that non-periodic FIFO. It is a plausible mechanism for the pattern
seen all cycle: a simple adapter enumerates, a bigger composite is unreliable, and a hub
(which needs control traffic to every downstream port) fails.

BUT IT IS NOT ACTIONABLE FROM ARDUINO, and that is worth stating plainly rather than
implying a fix exists: our `usb_host_config_t` has no `fifo_settings_custom` field --
that is newer than the component Arduino bundles -- and the bias is compiled into the
prebuilt library, not settable at runtime. Changing it needs either the standalone
espressif__usb component or a custom IDF build.

RULED OUT along the way: external hub support IS compiled into Arduino's library
(ext_hub.c.obj and ext_port.c.obj are present, 37 symbols), so "hubs are not built in"
is dead. And root_port_unpowered defaults false, so the root port is powered -- matching
the owner's measurement.

## 0.9.71 — native ESP-IDF build: assessed and written up, NOT started

Owner asked whether a non-Arduino build is possible. Assessed against the actual
container rather than from memory; written up in
**docs/design/IDF_NATIVE_BUILD_PLAN.md** so it does not have to be re-researched.

Short answer: yes, and the Arduino core is designed for it (arduino-esp32 3.2.1 ships
CMakeLists.txt, Kconfig.projbuild and idf_component.yml), so every Arduino library
CardSat depends on would keep working.

Two facts worth having here as well:
  * **Disk looked like the blocker and is not.** 1.8 G free of 252 G, but 2.3 G of what
    is already installed is unused by this project -- esp-rv32 (2.1 G, RISC-V, and
    CardSat is Xtensa only), both GDB builds, and OpenOCD. Pruning yields ~4.1 G.
  * **The real cost is per-session, not one-off.** The container filesystem resets
    between sessions, so an IDF install is 10-20 minutes of setup EVERY session before
    any build runs. This cycle's USB work needed many short build/inspect rounds; a
    native build would have made each slower.

The plan front-loads the cheap failure modes: prove IDF builds at all with a throwaway
hello_world, MEASURE the session tax, then test the actual hypothesis (a non-periodic
FIFO bias enumerating a powered hub) in a minimal project -- and only consider porting
CardSat if that experiment succeeds. If it fails, the FIFO theory dies for the cost of
one experiment and the correct outcome is to document the hardware limit and stop
spending on it.

Also recorded there: esp32-arduino-lib-builder as the middle option (custom sdkconfig
while keeping the Arduino workflow, and therefore the 18 gates and the release flow
intact), and the fact that NOTHING about a native build reduces the hardware
verification the owner still has to do.

## 0.9.71 — "one device behind a hub works, two do not": the FIFO split, quantified

Owner: one device behind a hub enumerates; two do not. That is the signature of a
resource ceiling, not a hub fault, and the numbers are now exact rather than suspected.

Read from the USB host driver's own source (hcd_dwc.c, obtained via Mini-FT8's vendored
espressif__usb component). The ESP32-S3 has the full-speed PHY, so otg_dfifo_depth is
256 and the port has 200 usable FIFO lines of 4 bytes each. The Kconfig bias decides the
split:

    PERIODIC_OUT (what Arduino ships)
        RX               34 lines   136 B
        NON-PERIODIC TX  16 lines    64 B     <-- control + BULK OUT
        periodic TX     150 lines   600 B
    BALANCED (the IDF default)
        RX              104 lines   416 B
        NON-PERIODIC TX  64 lines   256 B     <-- 4x more
        periodic TX      32 lines   128 B

**The non-periodic TX FIFO carries CONTROL transfers and BULK OUT** -- that is
enumeration itself, and every CDC write. Arduino's setting leaves it **64 bytes:
exactly one full-speed 64-byte packet.**

That fits the whole cycle's evidence without straining:
  * one CDC device: fits, works;
  * a second device: its control transfers must share a single-packet FIFO with the
    first device's traffic -- enumeration of the second fails;
  * a hub: needs control traffic to the hub AND to each downstream port;
  * a lone simple adapter direct: always fine, which is why nothing looked wrong until
    two devices were asked for.
Arduino presumably chose PERIODIC_OUT for USB audio and MIDI, which is the opposite of
what CAT over CDC wants.

NOT FIXABLE FROM ARDUINO, and worth stating flatly rather than implying a workaround:
the split is computed inside the prebuilt library at usb_host_install() time from a
compile-time Kconfig. Our usb_host_config_t has no fifo_settings_custom field (that is
newer than the bundled component), so there is no runtime override. Nothing in
EspUsbHost, and nothing in CardSat, can change it.

This is now the strongest single argument for the native-IDF work, and it converts the
plan's step 3 from "test a theory" into "confirm a computed figure": build a minimal IDF
project with BALANCED (or a custom split) and see whether two devices behind a hub
enumerate. If they do, the cause is settled. If they do not, the FIFO theory dies for
the cost of one experiment -- which is exactly what that step exists for.
