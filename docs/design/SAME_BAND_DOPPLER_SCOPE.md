# Scope: Same-Band / Half-Duplex Doppler Compensation

**Status: design scope only — not implemented.** This document scopes adding
Doppler compensation for satellites whose uplink and downlink share one frequency
(e.g. ISS packet, 145.825 FM) or sit in the same band (e.g. an in-band V/V FM
repeater), where the radio can only operate **half-duplex** (it cannot transmit
and receive at the same time on one band). It reviews how Hamlib, Gpredict,
SatPC32/SatPC32ISS, and OscarWatch solve this, proposes a CardSat design that
works for **all supported radios**, and assesses the risks of building it.

---

## 1. The problem

CardSat's whole Doppler engine assumes **cross-band full-duplex**: uplink on one
band (MAIN), downlink on another (SUB), both tuned continuously, the operator
hearing their own downlink while transmitting. That is how every satellite rig
behaves — but only when uplink and downlink are in **different** bands.

When both legs are in the **same** band (or on the *same frequency*), the radio
is only **half-duplex**: you cannot hear yourself while transmitting. The classic
cases:

- **ISS packet** — 145.825 MHz FM, uplink *and* downlink on the **same**
  frequency (a digipeater).
- **ISS / PCSAT-style APRS** and some **V/V FM** repeaters — both legs in 2 m.
- Any "in-band" bird where the rig's satellite/full-duplex mode can't be used
  because that mode requires the two legs to be cross-band.

Two things change versus CardSat's normal operation:

1. **No simultaneous RX+TX.** Tuning must follow **PTT**: drive the *receive*
   Doppler frequency while receiving, and the *transmit* Doppler frequency while
   transmitting, switching on the PTT edge.
2. **The rig's sat mode is unusable.** The radio stays in normal VFO/split (or
   even single-VFO) operation, not satellite mode.

A practical note worth stating up front, because it shapes how much value this
adds: on 2 m the total Doppler swing is only about ±3.5 kHz across a pass. For FM
that is within the channel, so **many operators simply tune the nominal frequency
and ignore Doppler entirely** for same-band FM. Doppler tuning still helps at the
pass extremes and is essential for SSB/CW birds, but the FM same-band case is the
*least* impactful and the *most* forgiving of step size.

---

## 2. How the established tools do it

### 2.1 SatPC32 / SatPC32ISS (DK1TB)

"Normal" SatPC32 puts the radio into satellite mode for full-duplex, which forces
the two legs cross-band. For same-band birds it ships a **separate program,
SatPC32ISS**, providing "In-Band" frequency control. Key mechanisms:

- A **"uplink + downlink" Doppler mode** that **holds both frequencies constant at
  the satellite** while tracking — the same hold-at-satellite idea CardSat already
  implements as the One True Rule.
- Explicit **PTT buttons** and an **"Autom. RX/TX Change"** option added precisely
  so the controller can switch the radio between the RX-corrected and TX-corrected
  frequency under remote control. Same-band operation is fundamentally PTT-driven.
- A documented reliability caveat: SatPC32ISS originally **could not steer the
  FT-736R reliably when uplink == downlink** (the 145.825 case), and the IC-910H
  needed an extra hardware wire (later removed). Same-band CAT is finicky on older
  rigs.

### 2.2 Gpredict — "Simplex TRX" controller (the closest match)

Gpredict's design wiki (oz9aec.net, "Doppler Tuning Algorithm for Gpredict")
describes a **Simplex TRX** mode for exactly this: one radio, half-duplex,
PTT-switched. Prerequisites it lists: the radio is in **split** mode; Gpredict
**ignores VFO A/B** and just sends the uplink or downlink correction to the
**active VFO**; the radio must accept `SET_FREQ` in both RX and TX states.

Its published control loop (paraphrased):

```
if (PTT == RX) {                 // receiving
    read RadioRX
    if (RadioRX moved by user)   // dial turned
        update SatRX; if locked, recompute SatTX
    else
        RadioRX += SatRX * Doppler / 100MHz   // apply correction
        write RadioRX
} else {                         // transmitting
    read RadioTX
    if (RadioTX moved by user)
        update SatTX; if locked, recompute SatRX
    else
        RadioTX += SatTX * Doppler / 100MHz
        write RadioTX
}
```

Hard-won lessons recorded there, directly relevant to CardSat:

- The **first version ignored PTT and kept sending RX corrections while the user
  transmitted** — the central bug this mode must avoid.
- A fix was needed to default **PTT = TRUE** so an unknown/unsupported PTT read
  doesn't strand the controller.
- The FT-817 is configured **RX-only** because it **cannot be controlled during
  transmit**; its uplink Doppler is *displayed* for manual use rather than driven.
- Tested working on an IC-765 in simplex "using both split or same VFO."

### 2.3 Hamlib

Hamlib supplies the primitives, not the policy: `set_freq` / `get_freq`,
`set_split_vfo`, and `get_ptt`. Same-band tracking lives in the *client*
(Gpredict, SatPC32). Two Hamlib realities matter here:

- **PTT read isn't universal.** Some rigs report PTT over CAT, some don't; the
  FT-736R has no usable read path at all. A same-band design cannot assume PTT is
  readable.
- The `freq_skip` rigctld option exists precisely to **skip setting freq on the TX
  VFO while in RX and vice-versa** for rigs that don't have targetable VFOs — the
  same half-duplex concern, handled at the Hamlib layer.

### 2.4 OscarWatch

OscarWatch focuses on cross-band full-duplex transponder work and the per-rig
MAIN/SUB/tone setup CardSat already mirrors. It contributes the **deadband and
defer-after-dial-move** behaviours CardSat already uses, but does not add a
distinct same-band half-duplex mode beyond the PTT-awareness principle.

### 2.5 Synthesis

Every tool converges on the same shape: **one frequency source, PTT selects which
Doppler-corrected frequency is driven, hold the chosen point at the satellite, and
degrade gracefully when PTT can't be read.** CardSat already has most of the
ingredients.

---

## 3. What CardSat already has (foundation)

The same-band mode is **not** a rewrite — it's a new tuning mode layered on
existing machinery:

- **PTT read + a `transmitting` state** already exist in the Doppler loop
  (`rig->readPtt(tx)`), today used to *skip* the downlink knob-read while
  transmitting. Same-band mode reuses this as the RX/TX selector.
- **Hold-at-satellite** (One True Rule, KB5MU) already holds a constant satellite
  frequency while the operator tunes — exactly SatPC32ISS's "uplink+downlink" hold.
- **`driveDownlink` / `driveUplinkDeferred`** already drive each leg independently
  with a mode-aware, TCA-adaptive deadband and an uplink defer after a dial move.
- **VFO routing** (`dlOnSub()`, `rigSetDownlinkFreq` / `rigSetUplinkFreq`) already
  abstracts which physical VFO carries each leg.
- **Calibration offsets** (`calDl` / `calUl`) already exist for the per-rig/per-bird
  frequency trim that same-band SSB needs.
- **Receive-only detection** (`txReceiveOnly()`) already routes no-uplink birds to
  a single VFO on MAIN.

---

## 4. Proposed CardSat design

### 4.1 A new tuning mode: `TM_SIMPLEX` (working name)

Add one value to the existing `TuneMode` enum (today `TM_HOLD`, `TM_FULL`,
`TM_DL`, `TM_UL`). `TM_SIMPLEX` means: **single VFO, PTT-gated, half-duplex** —

- **On receive (PTT = RX):** drive the **downlink** Doppler frequency to the
  active VFO. Honor knob moves exactly as `TM_FULL` does (hold the chosen point at
  the satellite).
- **On transmit (PTT = TX):** drive the **uplink** Doppler frequency to the **same**
  VFO.
- **Switch on the PTT edge**, not every tick — only write when the PTT state
  changes or the Doppler correction exceeds the deadband, to avoid clobbering the
  VFO mid-over.

This reuses `driveDownlink` / `driveUplink…` against a single routed VFO instead
of MAIN/SUB. For the **same-frequency** case (uplink == downlink, e.g. 145.825),
the only thing that changes between RX and TX is the **sign of the Doppler
correction**; the nominal frequency is identical.

### 4.2 Detecting candidates

- **Same frequency:** `uplink != 0 && downlink != 0 && |uplink - downlink| <
  someEpsilon` (e.g. < 1 kHz → effectively one frequency).
- **Same band:** both legs map to the same `civBandCode()` band (already exists
  from the band-assignment work).
- Surface `TM_SIMPLEX` as an option (auto-suggest it, or let the operator pick it)
  when a candidate is active. Do **not** silently force it — the operator may have
  a genuine cross-band setup.

### 4.3 PTT handling (the crux)

- If the rig **can** read PTT: gate RX/TX drive on it, debounced.
- If the rig **cannot** read PTT (FT-736R, and any rig that returns no PTT): fall
  back to one of —
  - **Manual PTT toggle** on the Track screen (a key that says "I am now
    transmitting / receiving"), mirroring SatPC32's PTT buttons; or
  - **RX-only drive** (like Gpredict's FT-817 handling): drive the downlink, and
    only *display* the uplink Doppler for the operator to set manually.
- **Default to the safe state.** Following Gpredict's hard-won fix, if PTT state is
  unknown, treat it as **RX** (do not transmit-tune blindly) — the inverse of
  Gpredict's "PTT default TRUE", chosen because CardSat's risk is driving the VFO
  off the listening frequency, so defaulting to RX keeps the operator hearing the
  bird.

### 4.4 Applicability to all supported radios

The mode needs only `setFreq` + (ideally) `readPtt`, both already abstracted in the
`Rig` interface, so it is **available on every supported radio** in principle:

| Radio | Same-band feasible? | PTT read | Notes |
|-------|--------------------|----------|-------|
| IC-9700 / IC-9100 | Yes | Yes | Cleanest; single-VFO simplex straightforward |
| IC-910 | Yes | Yes | Works; same-band keeps it out of sat mode |
| IC-820 / IC-821 / IC-970 | Yes | Limited | Older CI-V; PTT read may be flaky → manual toggle |
| FT-847 | Yes | Yes | |
| FT-736R | Partial | **No** | No PTT read and no freq read → manual-toggle or RX-only |
| TS-790 | Yes | Yes | |
| TS-2000 | Yes | Yes | |

Where PTT read is absent or unreliable, the **manual-PTT-toggle** path makes the
feature usable without a hardware PTT line, at the cost of operator key presses.

### 4.5 Modes and tone

- FM same-band (ISS packet/repeater): set FM on the single VFO; CardSat's existing
  CTCSS path still applies the uplink PL tone when transmitting.
- SSB/CW same-band (rare): the step size must stay ≤ 100 Hz (the established
  SSB rule) — CardSat's deadband already tightens near TCA.

---

## 5. Risk assessment

**Honest framing: the author operates only an IC-821, so every same-band path
below is host-reasoned and would ship untested on real hardware unless a user with
the relevant rig validates it.** That alone is the largest risk and argues for
shipping this **behind an explicit, off-by-default mode** with a prominent
untested banner.

### 5.1 Functional risks

- **Transmitting on the wrong frequency.** The core hazard: if PTT state is
  misread or laggy, CardSat could drive the VFO to the *uplink* Doppler frequency
  while the operator is actually receiving (or vice-versa), or transmit on a
  drifted frequency. Mitigation: default-to-RX on unknown PTT; only switch on a
  confirmed PTT edge; never transmit-tune speculatively.
- **PTT read latency / unreliability.** CI-V PTT polling adds bus traffic and can
  lag; a half-second-late switch is audible. Mitigation: debounce, and prefer the
  manual toggle on rigs with flaky PTT.
- **Clobbering the operator mid-over.** Writing the VFO every tick during a QSO is
  disruptive. Mitigation: write only on PTT edge or when correction exceeds the
  deadband (reuse existing deadband logic).
- **Half-duplex means no self-monitoring.** The operator can't hear their own
  downlink while transmitting, so they can't aurally confirm Doppler — unlike
  full-duplex. This is inherent to same-band, not a CardSat bug, but it limits how
  well the One-True-Rule knob-follow can work (no live downlink to read while TX).
- **Wrong-entry transmit.** If `TM_SIMPLEX` is engaged on a downlink-only entry
  (beacon) or a cross-band bird, behaviour is undefined. Mitigation: only allow
  the mode for two-way, same-freq/same-band candidates; refuse otherwise.
- **Interaction with existing sat-mode-off logic.** Same-band must keep the rig
  *out* of satellite mode; the engage path already forces sat mode off for
  receive-only birds, but `TM_SIMPLEX` needs its own "sat mode stays off" rule so
  it doesn't fight the Sat Mode setting.
- **Single-VFO vs split ambiguity.** Gpredict supports both "split" and "same VFO."
  CardSat would need to pick one (single active VFO is simplest) and document it,
  since driving the wrong VFO does nothing useful.

### 5.2 Low-value-for-effort risk

For the headline case (2 m FM ISS packet), the Doppler swing is small enough that
**many operators don't bother** — so a complex PTT-gated mode could be a lot of
untestable code for marginal on-air benefit. The SSB/CW same-band case is rarer
but is where the feature would actually matter.

### 5.3 Maintenance / dual-apply risk

A new tuning mode touches the hot Doppler loop, which must stay byte-identical
across `src/*.cpp` and `CardSat.ino` and pass the brace-balance + parity suite.
The PTT-edge logic is stateful (debounce, last-PTT, last-write) — exactly the kind
of code where the dual-apply discipline is easy to get subtly wrong.

### 5.4 Recommendation

If built: scope it as an **explicit, off-by-default `TM_SIMPLEX` mode**, gated to
same-freq/same-band two-way candidates, PTT-gated with a **manual-toggle fallback**
and **default-to-RX** safety, reusing the existing drive/deadband/calibration
machinery, and carrying a clear **untested-on-hardware** banner until a user with
an ISS-capable rig validates it. Start with the **FM same-frequency** case
(simplest: only the Doppler sign flips between RX and TX) and treat SSB/CW
same-band as a later refinement.

---

## 6. References

- Gpredict Doppler Tuning Algorithm (Simplex TRX), wiki.oz9aec.net —
  PTT-gated single-VFO pseudocode; PTT-default and FT-817 RX-only lessons.
- SatPC32 / SatPC32ISS, dk1tb.de ("What's New", manual, FAQ) — "In-Band" control,
  uplink+downlink hold-at-satellite, PTT buttons / "Autom. RX/TX Change",
  FT-736R same-freq and IC-910H wire caveats.
- Hamlib — `set_freq`/`get_ptt`/`set_split_vfo` primitives; `freq_skip` rigctld
  option for half-duplex VFO handling; rigs lacking PTT read.
- AMSAT-BB / AMSAT-UK — 2 m Doppler is small enough that many ignore it for
  same-band FM; manual uplink-while-TX technique.
- CardSat internals — existing `readPtt` + `transmitting` state, One True Rule
  hold-at-satellite, `driveDownlink`/`driveUplinkDeferred`, `dlOnSub()` routing,
  `calDl`/`calUl`, `txReceiveOnly()`, `civBandCode()`.

> **No code is changed by this document.** It is a scoping/design analysis only.
> All CardSat CAT paths remain host-verified; nothing here has been built or
> flashed.
