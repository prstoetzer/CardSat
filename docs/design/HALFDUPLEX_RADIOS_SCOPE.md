# Scope: Half-Duplex Split-VFO Satellite Support on All-Mode VHF/UHF Radios

**Status: design scope only — not implemented.** This scopes adding half-duplex,
split-VFO satellite operation (same-band or cross-band, PTT-switched) for a set of
**all-mode VHF/UHF transceivers** that are not currently CardSat radio profiles.

The hard constraint, set by the platform: **CardSat runs on an ESP32; it cannot
host a PC bridge.** It can talk to a radio only over (a) **wired CI-V/CAT**, or
(b) **native network CAT** (the RS-BA1 UDP backend already in `icomnet.cpp`).
**A radio that exposes CAT only over a USB-CDC port — requiring a host PC and a
driver — is OUT OF SCOPE**, because there is nothing on the Cardputer to enumerate
USB-CDC and run that driver. (The Cardputer Zero port may change this — see
`CARDPUTER_ZERO_PORT_SCOPE.md`.)

This document therefore splits the requested radios into **in scope** and **out of
scope by interface**, then scopes the half-duplex operating mode itself (which
builds directly on `SAME_BAND_DOPPLER_SCOPE.md`).

---

## 1. Interface triage of the requested radios

| Radio | All-mode V/U? | Control interface to CardSat | Verdict |
|-------|---------------|------------------------------|---------|
| **IC-705** | Yes (2 m/70 cm + HF) | **Built-in WiFi → RS-BA1 UDP** (no CI-V jack) | **IN SCOPE** via existing LAN backend |
| FT-817 / FT-818 | Yes | Serial CAT (TTL-level via ACC) | Wired-CAT possible, see §3 |
| FT-857 / FT-897 | Yes | Serial CAT (CAT jack) | Wired-CAT possible, see §3 |
| FT-991 / FT-991A | Yes | **USB-CDC only** (no TTL CAT jack) | **OUT OF SCOPE** (USB host needed) |
| IC-7000 | Yes | CI-V (wired) | Wired-CAT possible, see §3 |
| IC-7100 | Yes | **USB-CDC** (CI-V tunneled over USB) | **OUT OF SCOPE** (USB host needed) |
| IC-706MkIIG | Yes | CI-V (wired) | Wired-CAT possible, see §3 |
| FTX-1F | Yes (new) | TBD — verify CAT jack vs USB-only | **VERIFY** before scoping |
| IC-9700 (already supported) | Yes | CI-V + LAN | already done |

**Key results:**

- The **IC-705 is the headline IN-SCOPE addition.** It has no CI-V jack, but its
  built-in WiFi speaks the **RS-BA1 UDP protocol CardSat already implements** for
  the IC-9700 (`icomnet.cpp`, `ICOM_LAN_PROTOCOL.md`). Adding the 705 is mostly a
  profile/capability exercise, not new transport code.
- **FT-991/991A and IC-7100 are OUT OF SCOPE** as you specified: their CAT is
  **USB-CDC only**, which needs a USB host + driver CardSat can't provide.
- The **wired-CAT all-mode rigs** (FT-817/818, FT-857/897, IC-7000, IC-706MkIIG)
  are technically reachable over CardSat's existing wired Yaesu/Kenwood/CI-V
  paths, but each is a **single-VFO HF/VHF rig**, so satellite use is inherently
  the **same-band / half-duplex** case from `SAME_BAND_DOPPLER_SCOPE.md` (they have
  no MAIN/SUB sat mode). They're in scope only if that half-duplex mode is built.
- **FTX-1F** needs its interface confirmed (Yaesu's newest); if USB-only, it's out.

---

## 2. The IC-705 (the real new capability)

### 2.1 Why it's feasible now

CardSat's `icomnet.cpp` already does the RS-BA1 UDP handshake (Are-You-There /
Are-You-Ready), passcode auth, control + serial (CI-V) streams on ports
50001/50002, keepalives, and **deliberately never opens the audio stream** — the
exact CAT-only subset the 705 needs. The 705's wire format is the same RS-BA1 UDP;
the differences are at the CI-V layer (address, capabilities), not transport.

### 2.2 What adding the 705 needs

- **A radio profile** with the 705's CI-V address (`0xA4`) and capability flags.
  The 705 is a **single-receiver HF/V/U rig** — it has **no satellite mode and no
  MAIN/SUB**, so `hasSatMode=false`, `canAssignBand=false`, like the 820/821.
- **Half-duplex operation only.** With one receiver, the 705 can't do full-duplex
  cross-band sat; it works ISS/same-band and cross-band-split *half-duplex*
  (PTT-switched). So the 705 depends on the **same-band/half-duplex mode**.
- **LAN-only.** The 705 has no CI-V jack, so its profile must force `CAT_NET`
  (Icom LAN) — wired CI-V is not an option for this model.
- **PTT over LAN.** RS-BA1 carries PTT; CardSat's existing `readPtt` path over the
  net backend would need verification for the 705 specifically.

### 2.3 Risks (IC-705)

- **RS-BA1 is proprietary/encrypted-ish and reverse-engineered.** CardSat's
  backend is derived from `nonoo/kappanhang`, which targets the 9700. The 705's
  auth/stream details are *believed* identical but are **unverified on a 705**.
  Risk: handshake or passcode differences could prevent connect.
- **WiFi coexistence.** The Cardputer's single 2.4 GHz radio already does WiFi for
  data fetches; talking to the 705's access point means CardSat must join the
  **705's** network (or share an AP), constraining simultaneous internet use.
- **Untestable here** (author has neither a 705 nor any LAN Icom); ships behind the
  untested banner.

---

## 3. Wired-CAT all-mode rigs (FT-817/857/897, IC-7000, IC-706MkIIG)

These are reachable over CardSat's existing wired backends, but all are **single-VFO**
for our purposes, so they only do **same-band / half-duplex** satellite work:

- **Yaesu FT-817/818, FT-857/897:** Yaesu CAT (TTL-level on the FT-817 ACC jack;
  CAT jack on 857/897). The FT-817 famously **cannot be CAT-controlled during
  transmit** — so, exactly like Gpredict's handling, it's an **RX-drive + manual-
  uplink** configuration. FT-857/897 can be set during TX but are still single-VFO.
- **Icom IC-7000, IC-706MkIIG:** wired CI-V, single receiver, no sat mode. Same-band
  half-duplex only; `hasSatMode=false`. The 706MkIIG in particular is a classic
  ISS-packet rig.
- All need the **half-duplex tuning mode** to be useful; without it, CardSat would
  drive one VFO with cross-band assumptions that don't hold.

### 3.1 Risks (wired all-mode rigs)

- **Level shifting.** FT-817 ACC is ~TTL but verify; IC-7000/706 CI-V needs the
  usual 3.3 V-safe single-wire interface (the Grove 5 V rail caveat applies).
- **No PTT read on some** (older Yaesu CAT) → manual-PTT-toggle fallback.
- **Single VFO means no full-duplex** — operators lose self-monitoring; inherent,
  not fixable in software.
- Each adds a **profile + capability row** and per-rig CAT verification, all
  **untestable on the author's IC-821**.

---

## 4. Dependency on the half-duplex mode

Every radio here that isn't already supported is **single-receiver**, so its
satellite use is the **same-band / half-duplex, PTT-switched** mode scoped in
`SAME_BAND_DOPPLER_SCOPE.md`. That mode is the **prerequisite**; these radio
profiles are the consumers of it. Sequence:

1. Build the half-duplex `TM_SIMPLEX` mode (separate scope).
2. Add the **IC-705** profile (LAN-only, half-duplex) — highest value, reuses the
   existing RS-BA1 backend.
3. Optionally add wired all-mode rigs (FT-817/857/897, IC-7000, IC-706MkIIG) as
   half-duplex profiles.
4. Confirm **FTX-1F** interface; include only if it has non-USB CAT.

---

## 5. Out of scope (restated)

- **FT-991 / FT-991A, IC-7100** — USB-CDC-only CAT. No host on the Cardputer →
  cannot drive them. (Would become feasible on a Linux-based **Cardputer Zero**
  with USB host — see `CARDPUTER_ZERO_PORT_SCOPE.md`.)
- Any radio whose only control path is USB-CDC.

---

## 6. Risk summary

| Risk | Severity | Mitigation |
|------|----------|-----------|
| RS-BA1 705 details differ from 9700 | Med | Mark untested; get a 705 user to validate the handshake |
| WiFi coexistence (705 AP vs internet) | Med | Document the trade-off; let the operator choose network |
| No PTT read on older rigs | Med | Manual-PTT-toggle fallback (shared with half-duplex scope) |
| Single-VFO = no self-monitor | Low (inherent) | Document; it's how the radio works |
| Level-shifting / Grove 5 V | Med | Reuse existing CI-V interface guidance + banners |
| All untestable on author's IC-821 | High | Untested banner; user feedback loop |
| Scope creep (many profiles) | Med | Gate on the half-duplex mode first; add radios incrementally |

**Recommendation:** prioritize the **IC-705 over LAN** (best value, reuses the
RS-BA1 backend), but **only after** the half-duplex `TM_SIMPLEX` mode exists, since
the 705 and every other in-scope rig here is single-receiver. Keep FT-991/IC-7100
out until/unless the Cardputer Zero USB-host port lands.

> **No code is changed by this document.** Scoping/design only; all CardSat CAT
> paths remain host-verified, nothing flashed.
