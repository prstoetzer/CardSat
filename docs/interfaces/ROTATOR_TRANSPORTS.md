# Rotator transports (0.9.58)

**Hardware interface — untested, at your own risk, no liability.** Rotator wiring drives
a motor with mass and stops. Verify limits and calibration before letting CardSat
point anything. Nothing here has been tested against every controller.

## What this adds

The three serial rotator protocols — **GS-232**, **Easycomm I/II/III**, **SPID Rot2Prog** —
now run over any of three wires. Protocol and wire are separate settings, so any
protocol works on any transport:

| Setting | Values |
|---|---|
| `Rot type` | GS-232, rotctl (net), PstRotator (net), Yaesu (direct), Easycomm I/II/III, SPID Rot2Prog |
| `Rot wire` | I2C bridge (default), Grove G1/G2, USB adapter |

`Rot wire` reads `n/a` for the network backends (they carry their own socket) and for
Yaesu direct (I2C ADC + expander, no UART).

## The transports

**I2C bridge** (`ROT_XPORT_BRIDGE`, default) — SC16IS750/752 on Wire1. What every
pre-0.9.58 config meant; unchanged, and still the default.

**Grove G1/G2** (`ROT_XPORT_GROVE`) — UART1 on the Cardputer's Grove port.
*The Cardputer has one Grove port.* Wired CI-V CAT and the Grove GPS use the same two
pins, so a Grove rotator requires **CAT on USB or LAN** and **GPS not on Grove**.
CardSat enforces this both ways:
- Selecting a Grove rotator while Grove is taken → refused, with the reason on screen.
- Selecting Grove CAT/GPS while a Grove rotator holds it → the **rotator yields** to the
  I2C bridge and says so. CAT and GPS are primary; a rotator transport is an accessory
  choice, and blocking the CAT row would strand you on a setting that silently refuses
  to move.

**USB adapter** (`ROT_XPORT_USB`) — a USB↔serial adapter on the resident USB host, the
same host USB CAT uses. Rotator-only works (the rotator brings the host up itself);
radio-and-rotator together is supported but **experimental** — see below.

## Radio + rotator on USB at once — EXPERIMENTAL

This could not be bench-tested before release. It is built, guarded, and believed
correct; treat it as unproven.

The host has 4 device slots (`build_opt.h`) and 4 CDC slots, so two adapters are
supported by design. The hazard is *which adapter each port binds*:
`EspUsbHostCdcSerial` defaults to `ANY_ADDRESS`, and the library resolves that to
**the first enumerated device with a bulk-OUT endpoint** — so two ports left at the
default both grab the *same* adapter, and the radio's Doppler writes can land on the
rotator. "First" is enumeration order, which can change across a replug.

CardSat therefore **never leaves a port at ANY_ADDRESS**. Each binds an explicit device
address, re-resolved on every engage. With two adapters plugged in, pick which is which
in Settings; the choice is persisted by a stable key (serial number when the adapter
reports one — FTDI/CP210x usually do, CH340 usually does not — else VID:PID + address).
Two adapters of the *same model with no serial numbers* can only be told apart by
address: if you replug, re-check the selection.

## IC-9100 / IC-9700 internal hub (with USB Audio)

Those radios present an internal hub carrying **both** a serial interface and a USB
Audio device. CardSat does not use USB audio, and the audio device cannot be mistaken
for the CAT port — that is structural, not luck:

- The library claims only the CDC-data / vendor-serial interface it selected.
- A device is a serial candidate only via a **bulk OUT** endpoint. Audio streaming
  endpoints are **isochronous**, never bulk.

What audio *does* cost is **device slots**. A 9700 can present hub + serial + audio =
up to 3 of the 4. Add a rotator adapter (and its own hub, if any) and the slots can run
out. That failure is reported explicitly rather than as a vague "no device", and every
enumerated adapter is listed in the Settings picker so you can see exactly what the
radio presented.

If you run out of slots: use Grove or LAN for the radio and leave USB to the rotator.

## Two adapters: which is which

With one adapter, nothing to do — whichever port engages takes it, and the other is
refused rather than double-binding.

With two (e.g. an IC-9700 on a hub plus a rotator adapter), assign them explicitly:

1. **Settings → Radio → Scan USB adapters** (or the same row under Rotator). This brings
   the USB host up and enumerates. It is a deliberate keypress, not automatic: the host
   claims the S3's one USB PHY, so **the serial console closes and stays closed until
   reboot**, and the host holds ~11.8 KB for the session. Making that a side effect of
   opening a menu would be a nasty surprise.
2. **Radio USB:** cycle to the adapter driving the radio.
3. **Rot USB:** cycle to the other one. The radio's adapter is **skipped** — it is visible
   in the list but not selectable, so you can see it is taken rather than wonder where it
   went. The same applies in reverse.

Selections persist by a stable key (serial number where the adapter reports one — FTDI and
CP210x usually do, CH340 usually does not — else VID:PID + address), so they survive a
replug. Two adapters of the *same model with no serial numbers* can only be told apart by
address: re-check the selection after replugging those.

`Auto` (the default) means "the only adapter that is not the other port's". With two
adapters and neither nominated, engage **refuses** rather than guessing — enumeration
order is not a decision.

### Device slots

`build_opt.h` sets `ESP_USB_HOST_MAX_DEVICES=4`, and a slot is a USB **address**, not an
interface. An IC-9700 is a *composite* device — one address presenting both a CDC interface
and a USB Audio interface, which share the single `DeviceState`'s `interfaceInfos[16]`. So:

| Configuration | Devices |
|---|---|
| USB-serial adapter only | 1 |
| IC-9700 (hub + composite radio) | 2 |
| IC-9700 + rotator adapter on the same hub | 3 |
| IC-9700 + rotator adapter on its own hub | 4 |

Each additional slot costs **2,048 B** (measured: 8 slots → 20,040 B at ALLOC, 4 slots →
11,848 B). Raise it in `build_opt.h` if a deeper hub tree ever needs it; slot exhaustion is
reported as its own error, not a vague "no device".

## Diagnosing a USB rotator

Everything the rotator port does is traced to the **same log as USB CAT**
(`/CardSat/usbcat.log`), appended, so one file tells the whole USB story in order:

    rot: begin
    rot: starting host (rotator-only)
    rot: host up
    rot: adapter[0] addr=1 FTDI FT232R 0403:6001 key=0403:6001/A50285BI
    rot: one adapter present, using it
    rot: binding addr=1 baud=9600
    rot: port open
    rot: ENGAGED FTDI FT232R 0403:6001

Every refusal writes its reason (`no adapters enumerated`, `want key=... but no adapter
matches`, `That adapter is the radio's`, ...). The adapter list is dumped on every bind, so
you can see what enumerated and copy the exact `key` to persist.

The **Rotator** row on the status screen shows protocol, wire and link state at a glance —
green when talking, red with the reason when not (`GS-232/USB: FTDI FT232R 0403:6001` when
bound; `GS-232/USB: No USB serial adapter detected` when not).

## Notes

- One rotator is live at a time; its transport is created with it and freed with it
  (`freeRotator()`), so switching wires cannot leak a UART or a CDC port.
- The Grove UART is a single reused instance — the peripheral is not churned on every
  settings change.
- **Fixed in passing:** `rotType` was clamped to `ROT_PST` on load, silently resetting
  Yaesu / Easycomm / SPID configs to GS-232 at every boot. The clamp predated those
  types. If your rotator setting kept reverting to GS-232, that was why.
