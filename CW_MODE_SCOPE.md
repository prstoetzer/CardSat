# Scope: CW Mode on Linear Transponders

**Status: design scope only — not implemented.** This scopes letting an operator
flip both legs of a **linear (SSB/CW) transponder** to **CW** mode from the Track
and Track-Big screens, so they can work CW through the bird instead of SSB.

---

## 1. The need

Linear transponders carry SSB *and* CW in the same passband. CardSat today sets a
fixed SSB policy on a linear bird (`applyTransponderModes`): uplink LSB (USB if
HF), downlink USB. A CW operator wants **CW on both the uplink (MAIN) and downlink
(SUB)** instead — and wants to toggle it live without leaving the pass.

`RigMode` already includes `RM_CW`, and the per-leg setters
(`rigSetUplinkMode` / `rigSetDownlinkMode` → `setMainMode`/`setSubMode`) already
exist for all three CAT families. So the building blocks are present; what's
missing is an operator-facing **mode override** and the key to drive it.

---

## 2. How others handle it

- **SatPC32 / Gpredict / Hamlib** treat uplink and downlink mode as independent,
  operator-selectable per leg (Hamlib `set_mode` / `set_split_mode`; Gpredict's
  rigctl scripts send `M CW` / `X CW`). None hard-codes SSB; CW is just another
  mode token. CardSat's hard-coded SSB policy is the outlier.
- **Operating reality:** on an **inverting** linear transponder, the *sideband*
  inverts (LSB↔USB) but **CW is CW on both ends** — the operator simply zero-beats
  the downlink. So CW mode is actually *simpler* than SSB for inversion handling;
  there is no sideband to flip.
- One nuance: some operators prefer **CW-R** (reverse) on the inverted leg to keep
  the tuning sense intuitive, but plain CW/CW works for making contacts.

---

## 3. Proposed design

### 3.1 A per-track CW override

Add a small tri-state mode override carried on the Track/Big screens:

```
enum LinModeOverride : uint8_t { LMO_AUTO = 0, LMO_CW };   // (room to grow: LMO_DATA…)
```

- `LMO_AUTO` → today's behaviour (LSB/USB SSB policy).
- `LMO_CW` → set **CW on both legs** for a linear bird.

`applyTransponderModes()` consults the override:

```
if (t.isLinear) {
  if (linModeOverride == LMO_CW) {
    if (t.uplink) rigSetUplinkMode(RM_CW);
    rigSetDownlinkMode(RM_CW);
  } else {
    …existing LSB/USB policy…
  }
}
```

The override is **per-session, reset on satellite/transponder change** (back to
`LMO_AUTO`), so a CW choice never silently persists onto the next bird.

### 3.2 The key

On both `keyTrack` and `keyBig`, add a key — proposed **`m`** ("mode") — that
toggles `LMO_AUTO ↔ LMO_CW` for a linear bird, re-applies modes immediately
(`applyTransponderModes(activeTx[curTx])`), and shows a status line
("CW both legs" / "SSB auto"). For non-linear (FM) birds the key is a no-op with a
brief "FM bird — mode fixed" status. `m` must be checked against the existing
Track/Big key map (`r`/`t`/`c`/`n`/`d`/`h`/`b`…) to avoid a collision — `m` is
currently free on both.

### 3.3 Display

Show the active mode on the Track/Big screens (e.g. "USB/LSB" vs "CW/CW") so the
operator can see the override at a glance. The Big screen has room; Track is
tighter and may need a compact tag.

### 3.4 Calibration interaction

CardSat's `calDl`/`calUl` offsets and the One-True-Rule passband hold are
mode-agnostic — they operate on frequency, not mode — so CW works with them
unchanged. The only subtlety: the **deadband** should stay tight in CW (a CW note
shifts audibly with small Doppler), which the existing mode-aware deadband already
does for non-FM.

---

## 4. Risks & limitations

- **Low risk overall.** This rides entirely on existing, exercised setters and a
  state flag; no new CAT command bytes, no new frame formats. The main exposure is
  the new key + the override-reset discipline.
- **Inversion / CW-R nuance.** Plain CW/CW is correct for contacts; some operators
  may want CW-R on the inverted leg. Scope v1 as CW/CW and note CW-R as a possible
  refinement (it needs an `RM_CWR` that the enum doesn't have yet — a separate
  small addition per family).
- **Per-rig mode-byte coverage.** Each backend must map `RM_CW` correctly:
  - Icom CI-V: `06 03` (CW) is standard and already in the mode table.
  - Yaesu / Kenwood: CW tokens already supported by the existing setters.
  - **Set-only rigs (FT-736R)** will accept the CW set but can't read back — same
    limitation as everywhere else; acceptable.
- **Override-reset bug surface.** Forgetting to reset `LMO_CW` on a sat/transponder
  change would leave a later SSB bird in CW. Mitigation: reset in
  `onTransponderChanged()` and on sat selection, and assert it in review.
- **Dual-apply discipline.** Touches `applyTransponderModes` (one function, two
  files) plus two key handlers — modest, but must stay byte-identical and pass the
  brace/parity suite.
- **Untested on hardware beyond the author's IC-821** for the CW *mode set*
  specifically; the IC-821 can verify CW is set on its legs, which covers the core
  path for CI-V. Yaesu/Kenwood CW remains host-reasoned pending user feedback.

---

## 5. Recommendation

Low-risk, high-value-for-CW-ops. Implement as an **`LMO_AUTO`/`LMO_CW` per-session
override** toggled by **`m`** on Track and Track-Big, consulted in
`applyTransponderModes`, reset on every sat/transponder change, with the active
mode shown on-screen. Treat **CW-R** and other modes (DATA) as later extensions of
the same override enum.

> **No code is changed by this document.** Scoping/design only; all CardSat CAT
> paths remain host-verified, nothing flashed.
