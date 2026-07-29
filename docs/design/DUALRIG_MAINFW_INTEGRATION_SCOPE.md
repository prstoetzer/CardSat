# Dual-rig capability in the main firmware — scope

Status: **implemented — Model A, Phases 1 + 2 + 4** (0.9.68 cycle). CAT type
`CAT_DUAL` composes two native legs (`DualRig` over `PlainCatRig` /
`IcomNetRig`-in-plain-mode, `src/rig.{h,cpp}` + `src/icomnet.*`), configured per
leg on the Dual-Rig screen; the leg catalog and all four CAT dialects are ported
from the companion and byte-verified by `tools/host_dualrig/`. **Phase 3
(two USB radios on the one PHY) is implemented as well** — heap headroom for the
second port was confirmed by the project's hardware owner, so the gate came off:
a second CAT CDC (`cat2*` in usbserial) binds on the shared host through a hub,
each leg nominated to its own adapter, with dual-USB + a USB rotator refused
(channel budget). Hub enumeration behavior is on the bench matrix. The IC-705-over-LAN
caveat below is resolved: the leg rides the existing RS-BA1-family backend in
plain-VFO mode (the protocol note in this document's caveat section proved
pessimistic; the transport is the same UDP trio). Original scope follows.

This document scoped bringing the
two-radio (dual-rig) capability currently provided by the external **CardSatDualRig**
companion (M5StickS3) directly into the main CardSat firmware on the Cardputer ADV.

## What "dual rig" means and what already exists

A full-duplex satellite rig (IC-9700, FT-847, TS-2000) transmits and receives at once, so
CardSat drives it directly. A linear-transponder pass worked with **two half-duplex or
receive-only radios** — one on the downlink, one on the uplink — needs something to present
those two radios to CardSat as a single full-duplex VFO pair.

Today that "something" is the **CardSatDualRig companion**: an M5StickS3 (8 MB PSRAM, native
USB OTG) that hosts both radios on USB, speaks each one's native CAT dialect, and exposes a
single **rigctld** server (over Wi-Fi/TCP or Grove UART) that CardSat drives as one rig.

**CardSat already owns the entire client half of this today:**
- `RigctlRig` / `RigctlGroveRig` CAT backends (`rig.h`) — CardSat steers two logical VFOs
  over rigctld, TCP or Grove.
- The **Dual-Rig setup screen** `SCR_DUALRIG` (`app.h`), which configures the Stick over the
  active transport via its `\csdr_*` vendor escape (live USB enumeration, per-leg model/
  CI-V/baud/serial binding) — no phone or captive portal needed.
- The CAT-type selector already lists `rigctl (net)` and `rigctl (Grove)`, and treats them
  as full-duplex (`H9`, `app.h:1307`).

So the dual-rig *feature* works now. What lives on the Stick and is **not** in the main
firmware is the part that requires hosting two USB radios at once and translating each
radio's native CAT: the **radio catalog + per-dialect CAT encoders**, the **dual-USB host
+ device registry**, the **two-leg VFO state machine**, and the **rigctld server** that ties
them together.

## Why the companion exists (the constraint that shaped it)

The companion was built on the StickS3 specifically because the Cardputer ADV is a
**no-PSRAM** ESP32-S3, and hosting two USB radios plus two CAT dialect stacks was judged too
heavy for its heap. That judgement is worth re-examining now (see next section): the RAM
picture has improved materially since the companion was written.

## Feasibility re-assessment (updated)

The historical blocker was heap. Two facts change the calculus versus when the companion was
designed:

1. **USB device budget is already sufficient.** The build sets
   `ESP_USB_HOST_MAX_DEVICES=4`, and CardSat **already runs two USB devices at once** — a USB
   CAT radio and a USB rotator on two adapters, in either engage order (`app.cpp:5805`). The
   USB host stack, device registry, and adapter-binding UI for "two USB things at once"
   therefore already exist and ship.
2. **Heap headroom has improved.** The `.bss` / heap-on-demand optimization work moved large
   buffers (raster pipeline ~16.2 KB, voice-memo buffers, function-static scratch) off the
   permanent budget; static RAM now sits at ~156.9 KB (47%), leaving ~170 KB for the heap.
   The old "~17 KB free / ~7 KB largest block under USB CAT" figure in `audioAcquire()` is
   annotated "when first measured" and predates that work; it is stale. Two USB radios plus a
   second CAT dialect stack is now a **plausible** target rather than a non-starter.

The one thing that cannot be settled from source is the **contiguous-block** behavior with
two radios enumerated *and* a TLS fetch or audio path live — heap fragmentation on the
no-PSRAM part is the real risk, and it can only be proven on hardware. So this scope treats
"two USB radios on the Cardputer directly" as feasible-pending-bench, not as certain.

## Transport-agnostic dual rig — the general design

The original framing above ("two USB radios") is too narrow. CardSat already has **five CAT
transports**, and each one is instantiated through a single factory into a uniform `Rig*`
object. The right design is therefore not a bespoke dual-USB host but a **dual-rig driver
that owns two independent `Rig` legs, each configured with any of the existing transports**,
so *every permutation of existing connections* works in dual-rig mode.

### The five existing transports (from `settings.h`)

| Type | Enum | Physical resource |
|------|------|-------------------|
| Wired CI-V | `CAT_WIRED` | Grove UART1 on **G1/G2** (TTL) |
| Icom LAN (RS-BA1 UDP) | `CAT_NET` | **Wi-Fi/TCP-UDP** (host:port) |
| rigctld (Hamlib NET) | `CAT_RIGCTL` | **Wi-Fi/TCP** (host:port) |
| rigctld over Grove | `CAT_RIGCTL_GROVE` | Grove UART1 on **G1/G2** (TTL) |
| USB serial adapter | `CAT_USB` | **USB-C host PHY** (FTDI/CP210x/CH34x) |

### The composition seam already exists

`makeRig(model, catType, host, port, user, pass, groveBaud)` (`app.cpp:605`) builds **one**
`Rig` from a transport tuple and returns a uniform `Rig*`. The dual-rig driver simply calls
it **twice**, with two independent config tuples — leg A (downlink) and leg B (uplink) — and
runs each leg's Doppler write against its own `Rig`. No transport needs to know it is part of
a pair; the abstraction is already uniform. This is the single most important consequence of
the request: **"all permutations" is a config-model problem, not a per-transport-driver
problem.** Duplicate the per-rig config into an A/B pair and the existing backends compose for
free.

### Requested permutations, and which are physically possible

The request explicitly asks for: USB + Grove serial; rigctl(net) + USB/serial; and an
IC-705-over-LAN + serial/USB. Generalizing to *all* pairs of the five transports, the gating
question is **physical-resource contention**, not protocol — two legs cannot share one
physical bus. The resource classes are: the single **Grove UART (G1/G2)**, the single
**USB-C host PHY**, and **Wi-Fi/IP** (which is shareable across many sockets). CI-V, Grove
rigctl, and the Grove-wired path all collide on G1/G2.

Compatibility matrix (leg A × leg B); ✓ = physically independent buses, ✗ = same physical
resource, so not simultaneously usable:

| A ＼ B | Wired (G1/G2) | LAN (Wi-Fi) | rigctl-net (Wi-Fi) | rigctl-Grove (G1/G2) | USB (PHY) |
|--------|:---:|:---:|:---:|:---:|:---:|
| **Wired (G1/G2)**       | ✗ | ✓ | ✓ | ✗ | ✓ |
| **LAN (Wi-Fi)**         | ✓ | ✓* | ✓* | ✓ | ✓ |
| **rigctl-net (Wi-Fi)**  | ✓ | ✓* | ✓* | ✓ | ✓ |
| **rigctl-Grove (G1/G2)**| ✗ | ✓ | ✓ | ✗ | ✓ |
| **USB (PHY)**           | ✓ | ✓ | ✓ | ✓ | ✓** |

- **✗ (G1/G2 collisions):** any two of {Wired CI-V, rigctl-Grove} cannot coexist — there is
  one Grove UART. This is the same mutual-exclusion CardSat already enforces between wired
  CI-V and the Grove rotator/GPS. So "USB + Grove serial" (the requested pair) is ✓, but
  "wired-CI-V + Grove-rigctl" is ✗.
- **✓\* (both Wi-Fi):** two IP-based legs (LAN+LAN, net+net, LAN+net) are fine — they are
  independent sockets to independent host:port targets. Needs a **per-leg** host/port/user/pass
  in the config model (today those are single fields).
- **✓\*\* (USB+USB):** two USB serial radios on the one PHY — possible only through a hub and
  within the `ESP_USB_HOST_MAX_DEVICES=4` / 8-channel budget (hub + 2 serial). This is the
  Phase-0 heap/enumeration proof from the earlier section; treat as feasible-pending-bench.
- The **requested pairs all land on ✓:** USB+Grove-serial ✓, rigctl(net)+USB ✓,
  rigctl(net)+serial ✓, LAN+serial ✓, LAN+USB ✓.

### The IC-705-over-LAN caveat (must be surfaced)

The request names the **IC-705 via LAN**. CardSat's `CAT_NET` backend implements Icom's
**RS-BA1 UDP** scheme, which is validated against the **IC-9700**; the current guidance is
that Icom LAN support is IC-9700-oriented and the **IC-705 is not a supported LAN target**
(it exposes CI-V-over-network differently, via its WLAN/Bluetooth gateway, not the RS-BA1
control/serial/audio port trio). So "IC-705 via LAN + another radio via serial/USB" is
architecturally a normal ✓ pairing *once* the IC-705 LAN transport itself is supported — but
that support is a **separate prerequisite** (extend `CAT_NET`, or add an IC-705-specific LAN
mode) and should not be assumed to exist. In the meantime the IC-705 pairs cleanly as the
**USB or wired-CI-V** leg. This is called out as an open item rather than silently scoped as
working.

## Two integration models (pick one)

### Model A — A native two-leg dual-rig driver over the existing transports

Give CardSat a **local dual-rig mode** in which it drives two `Rig` legs itself, each leg
using any of the five existing transports (subject to the compatibility matrix above). This
subsumes the earlier "two USB radios" idea as one cell of the matrix and delivers all the
requested permutations.

**The core change is the config model, not the transports.** Today Settings holds a single
rig's transport tuple (`catType`, `catHost`, `catPort`, `catUser`, `catPass`, `catGroveBaud`,
`catUsbKey`, `civAddr`, `model`). Dual-rig needs an **A/B pair** of that tuple:
`legA{model,catType,host,port,user,pass,groveBaud,usbKey,civAddr}` and `legB{…}`, plus which
leg is downlink vs uplink (CardSat already has `vfoType`/`rxOnlyVfo` concepts to reuse). The
engage/teardown path calls `makeRig()` once per leg and holds two `Rig*`s instead of one; the
Doppler loop writes each leg's computed frequency to its own rig. Because `makeRig()` and all
five backends already exist and return a uniform `Rig*`, **no new transport code is required
for the net/LAN/Grove/wired/USB permutations** — only the second config tuple, the two-rig
ownership, and a conflict guard.

**What must be added:**
- **Per-leg config + UI.** Extend the settings model to an A/B pair and give the existing
  `SCR_DUALRIG` screen a local-mode variant that assigns a transport + model to each leg
  (it already knows how to pick model/CI-V/baud/serial per leg for the Stick; the same rows
  drive local legs).
- **A conflict guard** that enforces the matrix: reject/disable a leg-B transport that
  collides with leg A on G1/G2 or (without a hub) on the USB PHY, reusing the existing
  Grove/CI-V/USB mutual-exclusion logic CardSat already has for the rotator+radio case.
- **A two-leg Doppler/engage driver** that begins both rigs, writes both per cycle honoring
  each leg's per-transport cadence (`catRateMs`, `catDelayMs`), and tears both down cleanly
  (including the USB-CAT heap-on-demand release) when disengaged.
- **Optional: the local dual-USB host** (two USB radios on one PHY via hub) — only this cell
  needs the Phase-0 heap proof and the companion's dual-USB host logic; every other
  permutation avoids it entirely.

**What is NOT needed:** the companion's rigctld *server*, Wi-Fi AP, and captive portal — those
exist only so a remote CardSat can reach the Stick. Driving two legs locally, CardSat's own
Doppler engine writes each rig directly; there is no socket in the middle (except where a leg
*is* itself a rigctl/LAN network transport, which is just that leg's normal backend).

### Model B — Keep the companion, but make CardSat able to *be* it on capable hardware

A lighter framing: factor the dialect encoders + two-leg driver into a shared module compiled
into **both** the companion and CardSat, gated by a capability flag. On the Cardputer the
local-host path is available when the heap proves out; the companion remains for users who
want the radios off the Cardputer entirely (field ergonomics, the Stick's powered-hub story,
keeping the tight Cardputer heap clear). This is more of a refactor-for-reuse than a straight
absorb, and it keeps the companion as a supported deployment.

## Recommended path

**Model A, staged by transport-pair risk.** The decisive insight from the expanded scope is
that *most* permutations need **no new transport code** — they compose the existing five
backends through a duplicated config tuple — so they carry almost none of the heap risk that
shaped the companion. Only the two-USB-radios-on-one-PHY cell needs the hardware heap proof.
Stage accordingly:

- **Phase 1 — the config model + two-leg driver (unlocks most permutations at once).**
  Duplicate the rig config into an A/B pair, add the local-mode `SCR_DUALRIG` assignment UI,
  the conflict guard (matrix enforcement), and the two-leg engage/Doppler/teardown driver.
  With only this, every ✓ permutation that uses *distinct* physical buses works: **USB+Grove-serial,
  USB+wired-CI-V, USB+LAN, USB+rigctl-net, LAN+wired, LAN+Grove, rigctl-net+wired,
  rigctl-net+Grove, LAN+rigctl-net, LAN+LAN, net+net.** No new transport code, no dual-USB
  host, minimal heap exposure (each leg is a transport CardSat already runs single).
- **Phase 2 — per-leg network fields.** LAN+LAN / net+net / LAN+net need independent
  host/port/user/pass per leg (today single fields); small config extension.
- **Phase 3 — the two-USB-on-one-PHY cell (gated).** Only if users want two USB radios with
  no other bus free: port the companion's dual-USB host + hub enumeration, and run the
  **Phase-0 heap proof** (two radios enumerated, one tuning, TLS + audio exercised, real
  free-heap/largest-block captured). If the contiguous block collapses, this one cell falls
  back to the companion; every other permutation is unaffected.
- **Phase 4 — IC-705 LAN prerequisite (separate track).** Add IC-705 network-CAT support to
  the `CAT_NET` path (or a new mode) so the requested "IC-705 via LAN" leg becomes a ✓ like
  any other network leg. Until then the IC-705 pairs as a USB or wired leg. Independent of
  Phases 1–3.

## What must not regress

- **Single-rig behavior on every transport is untouched.** Dual-rig is a new mode; each of
  the five existing single-rig transports (`CAT_WIRED`, `CAT_NET`, `CAT_RIGCTL`,
  `CAT_RIGCTL_GROVE`, `CAT_USB`) and the radio+rotator USB coexistence must behave
  byte-for-byte as today when dual-rig mode isn't engaged.
- **The external companion stays supported.** The `rigctl (net)` / `rigctl (Grove)` client
  backends and `SCR_DUALRIG`'s `\csdr_*` remote-config path remain, so existing Stick users
  are unaffected regardless of which model ships.
- **PTT-manual contract preserved:** the firmware never keys a radio over CAT on the uplink
  leg; the operator keys by hand, exactly as the companion does.
- **Heap discipline:** no permanent `.bss` growth for the dialect tables (flash-resident);
  the second radio's transport buffers are heap-on-demand and released when the dual-rig type
  is disengaged, mirroring the existing USB-CAT teardown.

## Verification plan

- All nine gates + `src`/`.ino` body parity, as always.
- The CAT dialect encoders get a **host-side byte-verification harness** (like the existing
  orbit/wrap harnesses): feed known freq/mode values through each encoder and assert the
  on-the-wire bytes match the companion's already-verified output and the radio specs. This
  validates the encoders without hardware.
- **Phase 0 hardware heap proof is mandatory before merge** — two radios enumerated, one
  tuning, TLS + audio exercised, real free-heap/largest-block captured. This is the single
  unavoidable bench step.

## Effort estimate (Model A)

- Reuse (no new code): all five transport backends, `makeRig()`, the client VFO model, the
  CAT-type framework, the Grove/CI-V/USB mutual-exclusion logic, the Icom CI-V codec, and the
  `SCR_DUALRIG` UI rows — all already in CardSat.
- **Phase 1** (unlocks the bulk of permutations): A/B config-tuple duplication (~60 lines);
  two-leg engage/Doppler/teardown driver (~180 lines); conflict-guard matrix (~50 lines);
  `SCR_DUALRIG` local-mode variant (~80 lines).
- **Phase 2** (network legs): per-leg host/port/user/pass fields + UI (~50 lines).
- **Phase 3** (two-USB-on-one-PHY, gated): port the companion dual-USB host + hub enumeration
  (~200 lines) **plus the mandatory Phase-0 bench heap proof**.
- **Phase 4** (IC-705 LAN): separate CAT_NET extension, sized on its own.
- Verification harness: a host-side two-leg Doppler-write test (assert each leg receives the
  correct uplink/downlink frequency for a given transponder + Doppler) (~150 lines).
- All mirrored `src`↔`.ino`; several compiles. Only Phase 3 needs hardware before it can ship.

## Open items before implementing

1. **Per-leg network config** — LAN/rigctl legs need independent host/port/user/pass; today
   those are single-rig fields. Required before any two-network-leg permutation.
2. **IC-705 LAN support is a prerequisite, not a given** — the requested IC-705-via-LAN leg
   needs `CAT_NET` extended for the IC-705's network-CAT scheme (it is not the RS-BA1 target
   the backend is validated against). Until then, pair the IC-705 as USB or wired CI-V.
3. **Two-USB-on-one-PHY heap proof** — the one cell that still needs the Phase-0 bench
   validation; every other permutation avoids it.
4. **Conflict-guard UX** — decide how the local-mode UI presents an illegal pair (gray out
   leg-B transports that collide with leg A on G1/G2 / USB PHY, matching the matrix).
5. Decide Model A vs B (companion-kept) — Model A now covers all permutations natively; the
   companion remains the answer only for "radios physically off the Cardputer" ergonomics.
