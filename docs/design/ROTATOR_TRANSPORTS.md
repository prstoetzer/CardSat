# Rotator Transports: Serial (G1/G2) and USB

> **SUPERSEDED (0.9.58).** This design shipped. The as-built reference is
> **[docs/interfaces/ROTATOR_TRANSPORTS.md](../interfaces/ROTATOR_TRANSPORTS.md)**;
> this file is kept as the design-time record.

**Status: DESIGN (historical) — written before implementation.** Phase 0 (proving
single-adapter USB CAT on real hardware) is still in progress; this design exists so
the rotator work lands on an architecture that was planned, not bolted on.

## Goal

Radio control offers three transports today: wired serial (G1/G2), LAN (Icom
UDP / rigctld), and USB (a USB↔serial adapter on the USB-C port). Rotator control
should reach parity, with four:

| | I2C (Wire1) | Serial (G1/G2) | USB | LAN |
|---|---|---|---|---|
| **Radio** | — | CAT wired (today) | USB CAT (0.9.58-wip) | Icom LAN / rigctld (today) |
| **Rotator** | SC16IS750 bridge / Yaesu direct (today) | **this design** | **this design** | rotctld / PstRotator (today) |

## What exists, precisely

The three *serial-protocol* rotator backends — `Gs232Rotator`, `EasycommRotator`
(I/II/III), `SpidRotator` — each weld the SC16IS750/752 I2C→UART bridge into the
class: `putc_()/getc_()/flushIn()` are bridge register reads and writes on Wire1,
duplicated per backend. The `RotType` enum bakes the transport into the protocol
name ("GS-232A/B **via the SC16IS750 I2C->UART bridge**"). `RotctlRotator` and
`PstRotator` are inherently network; `YaesuRotator` is inherently I2C-direct
(ADS1115 + PCF8574). This is exactly the shape `Rig` had before
`Rig::setExternalStream()` decoupled the CI-V/Yaesu/Kenwood *dialects* from the
wire underneath them — and the same refactor applies.

On the USB side, EspUsbHost v2.3.0 already supports what dual-adapter operation
needs (verified in source, **not on hardware**): hub enumeration
(`EspUsbHostHubInfo`, hub descriptor parsing, `nextHubIndex_`) and multiple
concurrent CDC ports (`cdcSerials_[ESP_USB_HOST_MAX_CDC_SERIALS]`, each instance
bindable to one device address via `setAddress()` and filtered by `accepts()`).

## Design

### 1. Decouple protocol from transport

A new orthogonal setting for the serial-protocol family only:

```cpp
enum RotConn : uint8_t {
  ROT_CONN_I2C    = 0,   // SC16IS750 bridge on Wire1 (today's behavior, default)
  ROT_CONN_SERIAL = 1,   // G1/G2 UART directly
  ROT_CONN_USB    = 2,   // USB<->serial adapter (FTDI/CP210x/CH34x)
};
```

`RotConn` applies to GS-232 / Easycomm I-III / SPID. rotctld and PstRotator remain
LAN-only; Yaesu-direct remains I2C-only. Existing configs migrate silently:
`RotConn` defaults to `ROT_CONN_I2C`, which is byte-identical to today.

### 2. Stream extraction (the heart of the refactor)

- **`Sc16is750Stream : public Stream`** — the bridge register code, written once,
  wrapping today's `putc_/getc_/flushIn` logic behind `write()/read()/available()`.
  The three protocol backends lose their private bridge members.
- The serial-protocol backends gain **`setTransport(Stream*)`**, mirroring
  `Rig::setExternalStream()`. Protocol bytes are unchanged; only the carrier moves.
- Transports supplied by `applyRotatorFromCfg()`:
  - `ROT_CONN_I2C` → a `Sc16is750Stream` (constructed with `rotBaud`).
  - `ROT_CONN_SERIAL` → the G1/G2 `HardwareSerial` at `rotBaud`.
  - `ROT_CONN_USB` → a `Stream*` from `UsbSerial` (§4).

The protocol classes become transport-blind, which also means the read loops in
them inherit the **inactivity + absolute-ceiling** discipline from civ.cpp
(0.9.58-wip lesson: a USB adapter is a byte source that never has to go quiet;
every read loop must be adaptive to slow links *and* bounded against chatty ones).

### 3. G1/G2 sharing rules

One UART, two possible owners. Enforced in `applyRadioFromCfg()` /
`applyRotatorFromCfg()` and validated in the Settings UI:

| Radio transport | Rotator `ROT_CONN_SERIAL` allowed? |
|---|---|
| CAT wired (G1/G2) | **No** — refused with a status message naming the conflict |
| CAT USB / LAN / rigctld | Yes — the UART is free |

The refusal is loud, not silent: silently falling back is how the `catType` clamp
bug hid for a whole session. A rejected combination sets a persistent status and
leaves the rotator disabled.

Non-goal for v1: CI-V single-pin mode (G1 only) technically leaves G2 free for a
TX-only "blind pointing" rotator line. Deferred; noted for completeness.

### 4. UsbSerial: from one port to roles

Today `UsbSerial` owns one host + one CDC port, engaged by the radio reconciler.
It grows a role concept:

```cpp
namespace UsbSerial {
  enum Role : uint8_t { ROLE_RADIO = 0, ROLE_ROTATOR = 1 };
  bool    beginRole(Role r, uint32_t baud, uint8_t dataBits, uint8_t parity, uint8_t stopBits);
  void    endRole(Role r);
  Stream* stream(Role r);
  // host lifecycle becomes refcounted: first role up starts the host (console
  // down, core-0 pin, stage trace); last role down stops it (console up).
}
```

- **Single USB role in use** (radio *or* rotator, not both): binding stays
  `ANY_ADDRESS`, exactly today's behavior. No hub needed.
- **Both roles on USB**: a hub is required, and the two adapters must be
  distinguishable. Per-role match settings, `radioUsbId` / `rotUsbId`, hold a
  `VVVV:PPPP` VID:PID string; identical chips are disambiguated by USB serial
  string if present. **Two identical adapters with no serial string are explicitly
  unsupported in v1** (documented limitation; the failure is a clean "cannot tell
  the adapters apart" status, never a guess).
- The engage reconciler generalizes:
  `wantUsbHost = (radioOut && catType==CAT_USB) || (rotOut && rotConn==ROT_CONN_USB)`.
- The stage/breadcrumb/SD-log instrumentation extends per role (`rot bind: ...`
  stages); the core-0 task pin and the loop-WDT arm/disarm are host-level and
  unchanged.

### 5. Settings additions

| Field | Type | Meaning |
|---|---|---|
| `rotConn` | `RotConn` | transport for GS-232/Easycomm/SPID (default I2C) |
| `radioUsbId` | `char[10]` | VID:PID match when both roles are USB ("" = any) |
| `rotUsbId` | `char[10]` | VID:PID match for the rotator adapter ("" = any) |

`rotBaud` is reused unchanged for all three serial transports. Loader clamps must
be written against the **last enumerator** (the `catType > CAT_RIGCTL` clamp
silently discarded `CAT_USB` for an entire bench session; that class of bug is now
a review checklist item).

## Resource matrix (full)

| Radio \ Rotator | I2C bridge | Serial G1/G2 | USB | LAN |
|---|---|---|---|---|
| **Wired G1/G2** | OK | **refused** (UART taken) | OK, no hub | OK |
| **USB** | OK | OK | **hub + ID match required** | OK |
| **LAN / rigctld** | OK | OK | OK, no hub | OK |

Any USB use (either role) takes the S3's one USB PHY: the serial console drops at
engage and returns at disengage, exactly as USB CAT does today. The SD stage log
remains the diagnostic channel.

## Phasing

- **Phase 0 (in progress):** prove single-adapter USB CAT on hardware. Nothing
  below starts until this works.
- **Phase 1 — Stream extraction + G1/G2 serial rotator.** No USB changes at all.
  Smallest diff, immediately useful: anyone running the radio on LAN or USB frees
  the UART for a GS-232/Easycomm/SPID controller today. The `Sc16is750Stream`
  refactor is regression-tested against the I2C path (byte-identical protocol
  output is assertable on the host).
- **Phase 2 — single-USB rotator.** `beginRole()` with refcounted host lifecycle;
  radio not on USB. Reuses the entire proven USB CAT machinery with a role tag.
- **Phase 3 — dual USB via hub.** Per-role VID:PID matching, hub on the bench.

## Bench questions (all unverified on hardware)

1. **Hub enumeration** with EspUsbHost 2.3.0 on the Cardputer: supported in
   source; never powered on this bench.
2. **VBUS budget:** hub + two adapters off the Cardputer's 5 V in host mode.
   Unknown, and battery vs USB-PD may differ. This may be the real Phase-3 gate.
3. **SC16IS750 behind a Stream:** timing identical to the register-direct code?
   (Host-assertable for protocol bytes; bridge FIFO behavior needs the bench.)
4. **Enumeration time behind a hub** — the 2.5 s bind wait may need to grow when a
   hub adds a hop.

## Standing risks

The EspUsbHost error-resubmit spin (audit, 0.9.58-wip: STALL/ERROR completions are
resubmitted with no backoff or stall-clear) scales with endpoint count — two
adapters double the surface. The core-0 task pin contains the blast radius (the UI
survives; the TWDT converts a spin into a diagnosed panic), and the issue should be
reported upstream alongside the `peripheral_map` patch and `end()`-without-
`usb_host_uninstall()`.

---
*All hardware interfaces described here are UNTESTED. As with every CardSat
hardware interface: at your own risk, no liability accepted.*
