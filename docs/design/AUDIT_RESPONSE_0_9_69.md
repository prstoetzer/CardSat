# Response to the external audit of v0.9.68

An external functional review of the v0.9.64 → v0.9.68 range was received after
0.9.68 shipped. **Its two P0 findings are correct, both are real, and both were
introduced by the 0.9.68 dual-radio work.** This document records the evaluation
of every finding and what was done about it.

## P0-1 — CAT_DUAL discarded on every load. CONFIRMED. Fixed.

`Settings::load()` validated `catType` with `if (catType > CAT_USB) catType =
CAT_WIRED;`. `CAT_DUAL` is 5 and `CAT_USB` is 4, so a saved native dual-radio
configuration was silently reset to wired CI-V on every boot — working until
reboot, then reverting, and potentially seizing the Grove UART from a Grove GPS or
rotator on the way.

The finding is worse than it looks in isolation: **the comment block immediately
above that clamp documents the previous occurrence of the same bug**, when the
clamp read `> CAT_RIGCTL` and discarded `CAT_USB`. The lesson recorded there was
"bounds-check against the LAST enumerator" — advice that a range check cannot
keep, because it re-breaks every time the enum grows.

Fixed by replacing the range check with an **explicit switch whitelist**, as the
audit recommended: a new enumerator either appears in it or lands in `default`,
and there is no bound to fall out of date.

**New gate: `tools/audit_settings_clamps.py`** (16 static gates). It parses the
enums in `settings.h`, finds `field > ENUM_MEMBER` comparisons in `settings.cpp`,
and fails when the right-hand side is not that enum's last member. Validated
against the real defect: reintroducing the 0.9.68 line makes it fail with the
exact diagnosis, and it passes on the fix.

## P0-2 — CAT-B not torn down on settings re-apply. CONFIRMED. Fixed.

`applyRadioFromCfg()` tested only `UsbSerial::active()` (CAT-A) and called only
`UsbSerial::end()`. The disengage path in the reconciler did the right thing
(`cat2End()` then `end()`); the two paths had diverged. Consequences were as the
audit described: changing an engaged uplink leg's model/baud/adapter kept the old
CAT-B session, because `cat2Begin()` returns success when already active; and
switching away from dual-USB stranded CAT-B holding a CDC, an adapter and the
shared host, so the console never returned.

The audit also identified a **third defect implied by the same root cause**, which
was verified and fixed: the reconciler's outer gate read
`catUsesUsb() || UsbSerial::active()`, so after switching to a non-USB
configuration the block was skipped entirely and the teardown branch inside it
could never run.

Fixed by introducing **one** teardown, `App::usbCatTeardown()` — detach rig
streams, `cat2End()`, `end()` — used by both paths, and by widening the
reconciler gate to include `cat2Active()`. `cat2Begin()`'s early return now
documents that callers must end the port to change its parameters.

## Medium findings

| finding | verdict | action |
|---|---|---|
| Web battery/charging bypasses the ADV fix | **Confirmed** | `/api/status` and the About page now call `batteryPercent()`/`batteryCharging()`, the same accessors the charge screen uses. `batteryCharging()` was made cheap (ADC read rate-limited to its own 30 s latch window) so any consumer can call it. No `M5.Power.isCharging()` call sites remain outside that accessor. |
| "Telnet" is a raw TCP terminal | **Confirmed** | Added a minimal IAC state machine rather than renaming. It is a spec-compliant *refuser*: DO/DONT → WONT, WILL/WONT → DONT, subnegotiation swallowed to SE, `IAC IAC` unescaped to a literal 0xFF. That is the correct way to say "no options supported", and it fixes both symptoms — negotiation bytes no longer print as garbage, and a server that waits for a reply before offering a login no longer hangs. |
| RX-only radios permitted as the uplink leg | **Confirmed** | Now **refused** at the same choke point as the bus conflicts, with the radio named. The Dual-Rig screen's warning went from yellow to red and states the configuration cannot transmit. The audit's reasoning was decisive: a transient status message leaves no standing explanation for why uplink Doppler does nothing. |
| Warning coverage weaker than the release notes imply | **Confirmed — and the release-notes claim was wrong** | See below. |
| USB registry race; enumeration stops at the first adapter | **Confirmed** | The adapter wait now uses a **bounded quiet period** (keep waiting until nothing new has enumerated for 400 ms, same overall 2.5 s cap) instead of returning at the first device — which made the adapter list depend on enumeration order, exactly the dual-USB/hub case. `s_serDevN` is now `volatile` with a **release fence** before the count is published, so a reader can no longer observe a count that includes a half-written entry. Full mutual exclusion is still not present; adds are append-only and the array is reset only at host start, which makes publication ordering the actual hazard, but this is documented as a partial fix. |
| Flash headroom ~4% | **Confirmed** | No code change. The 8 MB partition work is now on the critical path rather than optional; tracked in ROADMAP_TO_1.0. |
| Heap fragmentation / `drawSettings()` String array | **Confirmed, pre-existing** | Unchanged this pass; remains a tracked deferral. |

### On the "zero warnings" claim

The release notes for 0.9.68 stated the build was clean with zero warnings. That
claim was **not meaningful**: `arduino-cli` defaults to `--warnings none`, which
the ESP32 platform maps to `-w`. Nothing was being reported, so nothing being
reported proved nothing.

Measured properly with `--warnings all`: **103 warnings, 70 of them in our own
code.** By category (ours): 34 `-Wformat-truncation=`, 16
`-Wmissing-field-initializers`, 10 `-Wextra`, 5 `-Wdeprecated-declarations`,
3 `-Wreorder`, 1 `-Wunused-but-set-variable`, 1 `-Wint-in-bool-context`.

Triaged this pass — the ones that could indicate a defect:
- **`-Wreorder` (3)** in `IcomNetRig`'s leg constructor: `_plain` was initialized
  out of declaration order. Ours, from 0.9.68. Fixed.
- **`-Wint-in-bool-context`**: `abs(w) * 2 ? ... : ...` in the Gorillas wind
  indicator. Correct logic, misleading form. Rewritten.
- **`-Wunused-but-set-variable`**: a `bTop[24]` array in the building generator,
  filled and never read (roof heights already live in `K.sky[]`). Removed.

The remaining 67 are being tracked, not silently accepted. The
`-Wformat-truncation=` group is mostly `snprintf` into fixed buffers where
truncation is intentional, but each needs reading before it can be called benign.

**Process change:** the honest statement is "builds clean at the project's default
warning level", and warning counts are only quotable from a `--warnings all`
build. The current baseline (103 total / 70 ours) is recorded here so a regression
is visible.

## Not accepted as stated

Nothing in the audit was rejected. The one framing worth qualifying is the
conclusion that the branch is "not yet suitable for declaring native dual-radio
and dual-USB CAT generally reliable" — that is exactly what
`docs/THINGS_TO_VERIFY.md` already says, and what the 0.9.68 release notes and
firmware README say. The audit and the project agree: the feature is
protocol-verified at host level and has never driven a radio. That remains true
after this pass; these fixes remove defects found by reading, and do not
substitute for the bench.

---

## Also in 0.9.69: single-leg dual rig, and single-wire CI-V on a leg

Two follow-ups from the project owner, both affecting the same dual-rig feature.

### A leg may be "None"

`makeDualRig()` returned nullptr if EITHER leg failed to construct, and `LEG_NONE`
constructs to nullptr -- so a one-radio dual configuration was simply refused. It
is now a first-class configuration: the composite drives whichever legs exist,
`ready()` requires only the legs that are present, and the absent half's Doppler
goes nowhere. Only "both legs None" is refused.

The subtle part was that an absent leg still carries a **stale bus field** from the
UI. Every place that treats a bus as a claimed resource had to stop counting it:
the two-Grove and two-USB engage guards, `catUsesGroveWire()` (which otherwise
would have taken the UART away from a Grove GPS or rotator on behalf of a leg that
does not exist), the reconciler's USB-leg selection and its dual-USB flag, and the
Grove-vs-GPS arbitration. `catUsesUsb()` already tested the model and needed no
change.

### Single-wire CI-V now works on a Grove leg

This did **not** work before 0.9.69 and would have been found on the bench the
first time an Icom leg was wired up. Single-pin CI-V is a delicate, bench-verified
sequence -- clear UART signal inversion, pull-up, open-drain set at the PAD
REGISTER so the output matrix stays attached, then re-assert the RX input on the
shared pad -- and it lived entirely inside `CivRig::begin()`. `PlainCatRig`, which
every dual-rig leg uses, called `Serial1.begin(rx, tx)`: two-wire only. A one-wire
radio would simply never have answered.

That sequence is now one shared function, `civUartOpen()` (declared in `rig.h`,
body lifted verbatim from `CivRig::begin()`), used by both backends. Sharing it
rather than copying it also makes the "release the previously bound pads"
bookkeeping global, which is what it has to be: there is one UART, and now two
backends that can open it. `PlainCatRig` gained a `setPinMode()` override, `DualRig`
forwards it to both legs, and the engage path applies `cfg.civPinMode` before
`begin()` whenever a leg is on Grove -- mirroring the wired path exactly.

This matters more than it sounds: the IC-705, IC-7100, IC-706MKIIG, IC-275/475 and
all nine IC-R receivers in the leg catalog present CI-V on a single 3.5 mm jack.
One-wire is the common case for these radios, not an exotic option.

### A contradiction found while doing it

`docs/interfaces/CIV_SINGLE_PIN.md` carried a "✅ confirmed working on hardware
(verified on an IC-821)" banner at the top and a stale "this single-pin mode in
particular is unverified on hardware" line at the bottom; `civ.cpp` still said
UNVERIFIED too. The specific, radio-named claim is the current one; both stale
statements were corrected to match, with a note in the document recording the
change rather than quietly rewriting history.
