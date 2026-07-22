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

### Deferred to the corrective pass (round 2)

- **Storage transactionality (H12, H13, H14, H19, M29, M32).** A single validated
  `replaceFileTransactionally()` helper (temp write → validate → rotate → promote →
  restore-on-failure) should back GP promotion, transmitter-cache writes, config save, and
  note writes. This is a cross-cutting change touching `net.cpp`, `satdb.cpp`,
  `settings.cpp`, and `notes.cpp`, and needs power-cut fault-injection testing. Highest
  priority for the next pass.
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
- **Input/boundary validation (M16–M20).** Centralize `Settings::validate()` (enum ranges,
  coordinates, grid subsquares, LoRa region/freq/SF/BW bounds) called after load, restore,
  migration, and edit; fix Wi-Fi SSID/pass NUL-termination; bound LoRa RX frequency.
- **Backend health honesty (M22–M27, M39).** Distinguish transport-open from
  device-responding for rigctl and the serial/direct rotator backends; check I2C and stream
  write results; verify Kenwood MAIN/SUB mode writes on hardware.
- **Robustness (M21, M24, M28, M30, M31, M33, M34, M35, M37).** LoRa TX/RX IRQ ordering;
  unchecked `new` on backend/predict allocations; `Notes::list()` row-width hazard; logstore
  per-line open/flush cost; Wi-Fi scan disconnect restoration; IPP drain-after-close;
  removing/guarding the deprecated whole-response `net` APIs; converting long foreground
  operations to cooperative jobs.
- **Wrap-safe timing (M36).** Replace absolute `millis() < deadline` comparisons with signed
  `timeBefore(now, deadline)` helpers across the cited sites. Mechanical but wide; worth a
  focused pass with a wrap unit test.

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
  while reporting a green link. Bound fixed to `DR_MAX_MODEL`; empty catalogue now fails
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
