# Scope: dual-radio CAT support (two FT-817/818, split uplink/downlink)

Status: **design only — no code written.** This document scopes the first dual-radio
case: two Yaesu FT-817/818 radios, one as the uplink (TX) and one as the downlink
(RX), each on its own CAT link, so a full-duplex satellite station can be run from
two single-VFO mobiles. It is grounded in the current single-radio architecture as
it actually exists in `rig.h`, `yaesu.h`, and the engage/Doppler paths in `app.cpp`.

## Why this case, and why the FT-817/818 specifically

The FT-817/818 is a **single-VFO** radio with no satellite mode. CardSat's existing
Doppler engine already thinks in two legs — `setMainFreq()` = uplink/TX,
`setSubFreq()` = downlink/RX — but today both legs are commanded on **one** rig
object, which only works on radios with a SAT dual-VFO (IC-9700, FT-847, TS-2000,
etc.). To work the birds full-duplex with 817/818s you need two of them, and that
is the smallest, cleanest dual-radio case: no shared VFO, no split, no cross-band —
just "this radio does the uplink, that radio does the downlink." It is the right
first target precisely because it forces the core architectural change (two rig
objects) without any of the harder sub-problems (shared PTT, common-oscillator
offset, one-radio-two-bands).

## What exists today (the starting point)

- **One rig object.** `App::rig` is a single `Rig*`, created in the engage path by
  `makeRig(model, catType, host, port, user, pass)` and destroyed on disengage.
- **The two Doppler legs are methods on that one rig.** `setMainFreq/​setMainMode`
  (uplink) and `setSubFreq/​setSubMode` (downlink) are commanded on `App::rig`. For
  Yaesu they patch the CAT opcode (MAIN 0x0-, SAT-RX 0x1-, SAT-TX 0x2-); for the
  817/818 there is no SAT VFO, so those opcodes don't apply.
- **Transport is already abstracted.** Every wired backend talks through a
  `Stream*`; `begin()` binds either the on-board G1/G2 UART or (via
  `setExternalStream`) a USB<->serial adapter. This is the key enabler: a second
  radio needs a second `Stream`, and the backend is indifferent to what's under it.
- **CAT settings are single-valued.** `radioModel`, `catType`, `catHost/Port`,
  `catBaud`, CAT-delay/rate — one of each. There is exactly one radio's worth of
  configuration.
- **The FT-817/818 is not yet in the radio table.** `radio_profiles.h` has FT-847
  and FT-736R but no 817/818 entry; its CAT set (5-byte frames, opcode 0x01 freq,
  0x07 mode, single VFO) is close to the existing Yaesu backend but not identical.

## The core change

A second rig. Concretely, `App` grows a second `Rig* rigDown` (or an array
`rig[2]`), and the Doppler push routes the uplink leg to one and the downlink leg
to the other:

- Uplink leg  → `rigUp->setFreq(hz)` / `rigUp->setMode(m)` (radio in plain VFO)
- Downlink leg → `rigDown->setFreq(hz)` / `rigDown->setMode(m)`

Because the 817/818 has one VFO, this uses **plain freq/mode set** (a new
`setFreq()/setMode()` path, opcode 0x01/0x07), not the SAT-patched
`setMainFreq/setSubFreq`. The two rigs are independent objects with independent
transports, service ticks, and CAT pacing.

## Work breakdown

1. **FT-817/818 backend + table entry.** Add the model to `radio_profiles.h` and
   implement its plain-VFO CAT in the Yaesu backend (or a thin subclass): freq set
   (0x01, 4-byte BCD, 10 Hz), mode set (0x07), optional read-back, CAT-on preamble.
   Independently useful even for single-radio 817 users.
2. **Second rig object + lifetime.** `rigUp`/`rigDown` created and destroyed
   together in the engage/disengage paths. Both must honour the same
   setExternalStream lifetime discipline that the 0.9.58 fix hardened — clear the
   Stream before tearing it down, per rig.
3. **Two transports at once.** The two radios need two wires. Options, in rough
   order of effort:
   - Two USB<->serial adapters on the hub (the USB-rotator work already established
     multi-adapter binding by device address — `#N` — so the plumbing for "which
     adapter is which radio" partly exists).
   - One on-board UART (G1/G2) + one USB adapter.
   - Two on-board UARTs if pin-count allows (the S3 has the peripherals; the
     Cardputer's exposed pins are the constraint).
   The address-first USB device strings (0.9.60) and explicit-binding model are the
   foundation here.
4. **Doppler router.** The one place that currently calls `setMainFreq/setSubFreq`
   on `App::rig` splits into "uplink → rigUp, downlink → rigDown". A single-radio
   SAT rig remains the default; dual is a mode.
5. **Settings surface.** A "dual radio" toggle plus a second radio's worth of
   config: model, transport, adapter binding, baud, and which radio is uplink vs
   downlink. This roughly doubles the CAT settings block; it should be a distinct
   sub-screen so the single-radio path stays uncluttered.
6. **Self-test, rigctld, and the web/monitor surfaces** all assume one rig and need
   a second instance or a rig selector: the CAT self-test, the CI-V/CAT monitor, and
   the rigctld/​web-dashboard control paths.

## Hard problems and open questions (flagged, not solved)

- **RAM.** Two rig objects, two transports, and (if both USB) two resident
  EspUsbHost clients. USB CAT already runs the heap tight (~17 KB free, ~7 KB
  largest block, per the worst-case analysis); a *second* USB host client may not
  fit at all. **Likely constraint: dual-USB-CAT is infeasible without freeing heap
  first;** the realistic first implementation is one UART + one USB adapter, or two
  UARTs. This needs measuring before committing to a transport combination.
- **Full-duplex timing.** Two radios means uplink and downlink Doppler must both be
  serviced each cycle without one starving the other; the CAT cycle-rate budget is
  currently sized for one rig. Two slow Yaesu links at 4800/38400 could exceed the
  loop's time budget.
- **PTT / TX-RX coordination.** With two radios there is no shared PTT. Whether
  CardSat needs to coordinate (e.g. mute downlink Doppler updates during uplink
  transmit to avoid CAT contention) is an open behavioural question.
- **Which radio is which.** Binding "uplink = adapter #2, downlink = adapter #1"
  must survive re-plugging; the `#N` device-address model helps but a user-facing
  assignment step is needed.
- **Transport enumeration on the Cardputer.** How many simultaneous CAT wires the
  hardware can physically carry (UART pins vs USB hub) bounds the whole feature and
  should be established first.
- **Generality later.** This scope is deliberately the two-817 case only. A general
  dual-radio framework (mixed models, IC-9700 uplink + FT-817 downlink, cross-band)
  is a superset to design *after* the concrete case proves the two-rig plumbing.

## Recommended staging (when built)

- **Phase 0:** FT-817/818 single-radio backend + table entry (useful alone; proves
  the plain-VFO CAT).
- **Phase 1:** second rig object + Doppler router, on **two on-board UARTs or
  UART+USB** (avoid dual-USB until RAM is proven), with a minimal dual-radio
  settings sub-screen. Uplink/downlink split only.
- **Phase 2:** dual-USB transport *iff* a RAM audit shows two host clients fit;
  otherwise document it as unsupported.
- **Phase 3:** extend the self-test / monitor / rigctld / web surfaces to two rigs.
- **Later:** generalise beyond the two-817 case.

## Non-goals for this scope

Cross-band single-radio tricks, shared-oscillator offset handling, transponder
inversion across two radios beyond what the existing Doppler engine already
computes, and any mixed-model matrix. Those are explicitly out until the concrete
two-817 uplink/downlink case is working and measured.
