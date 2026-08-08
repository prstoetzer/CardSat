# CSUH — the CardSat USB Helper protocol

The wire protocol between CardSat (Cardputer ADV) and the **CardSatUsbHelper**
companion (M5StickS3) over a Grove cable. Introduced in 0.9.73.

The authoritative definition is `src/csuh_proto.h`, which is shared **byte-identically**
with `companion/CardSatUsbHelper/csuh_proto.h`. This document explains the design;
the header is what the two firmwares compile. `tools/check_csuh_parity.py` gates
the two copies against each other.

---

## 1. Why the helper exists

The ESP32-S3 has `OTG_NUM_HOST_CHAN` = **8 host channels for the entire USB bus**,
one per open pipe — including every device's default control pipe.

| device | channels |
| --- | --- |
| hub | 2 |
| CDC radio (TH-D75, IC-705's CDC function) | 3 |
| vendor-serial adapter (Prolific) | 4 |
| FTDI FT232R | 3 |

The **IC-705 contains its own internal TI TUSB2046 hub**, fanning out a CDC device
and a Burr-Brown CODEC, so the radio costs **5** by itself.

```
hub + IC-705             =  7   works
hub + TH-D75 + FTDI      =  8   works, no headroom
hub + TH-D75 + IC-705    = 10   cannot be made to fit
```

`usb_host_interface_claim()` allocates a pipe for **every** endpoint of an
interface, all-or-nothing, so only endpoints sitting on an interface of their own
can be skipped — true for CDC control, not for vendor-serial adapters. There is no
arrangement of eight channels that holds two USB radios when one is an IC-705.

A second microcontroller brings its own eight. That is the whole idea: CardSat
keeps one USB device on the Cardputer and hands the other to a Stick over Grove.

## 2. What the helper is not

**It is a byte pipe.** It speaks no CAT dialect, holds no radio catalogue, and
knows nothing about Doppler, VFOs or rotator grammar. CardSat owns every protocol
decision exactly as it does for an adapter plugged into the Cardputer directly.

That is a deliberate reversal of its predecessor. `CardSatDualRig` owned CAT state
and carried its own radio table, so every radio fix had to be made twice and kept
honest across two release cycles — a standing cost that bought nothing once
`CAT_DUAL` landed natively. It was retired in 0.9.73.

The helper also stores **nothing** across reboots. That is what makes recovery
simple: there is no configuration to lose, so rebooting is always a safe answer,
and `CSUH_T_RESCAN` uses exactly that.

## 3. Physical layer

| | |
| --- | --- |
| Wire | Cardputer Grove (G1 = RX, G2 = TX, UART1) ↔ Stick Grove (GPIO9 = RX, GPIO10 = TX) |
| Levels | 3.3 V both ends, no shifter |
| Framing | 8N1 |
| Rate | 230400 default; 115200 and 460800 also scanned |

Cardputer TX (G2) → Stick RX (GPIO9); Cardputer RX (G1) ← Stick TX (GPIO10).
**A reversed pair is silent, not noisy** — it looks exactly like a dead cable or a
wrong baud, which is worth remembering before suspecting anything else.

Because the helper claims UART1, it is mutually exclusive with wired CI-V, the
Grove GPS, a Grove rotator, `CAT_RIGCTL_GROVE` and any `LEGBUS_GROVE` leg. CardSat
enforces this through `catUsesGroveWire()` / `rotTransportConflict()`.

### Power

The Stick does **not** source USB VBUS — M5Stack frame its USB-C port as a power
input, and its 5 V boost feeds the Grove / Hat2 EXT_5V rail instead. **A
self-powered hub is required for the radio**, exactly as on the Cardputer.

The Stick itself can be fed 5 V from the Cardputer's Grove rail over the same
cable that carries the data. The firmware clears `cfg.output_power` before
`M5.begin()` and calls `M5.Power.setExtOutput(false)`, forcing EXT_5V to **input**,
so the Stick can only ever receive 5 V on Grove. Never call `setExtOutput(true)`
while anything feeds that pin — two supplies on one wire is the short-circuit case
M5Stack explicitly warn about.

## 4. Framing

Every frame is **COBS-encoded** and terminated by a single `0x00`, so `0x00` never
appears inside a frame and a receiver can always resynchronise at the next
delimiter.

That property is the reason for COBS rather than a magic byte plus escapes: the
helper is stateless and may reboot mid-session, and whatever its bootloader emits
on the UART must not be able to desync the host permanently. Verified: noise
landing on a frame boundary loses nothing at all; noise landing mid-frame costs
exactly the frame it corrupts (`tools/host_usbhelper/csuh_frames_test.sh`).

Decoded frame:

```
[0]        TYPE          one of the CSUH_T_* codes
[1]        PORT          reserved; always 0 in v1
[2..n-3]   PAYLOAD       0..128 bytes
[n-2..n-1] CRC16         CCITT-FALSE over [0..n-3], LITTLE-endian
```

Minimum decoded frame 4 bytes; maximum 132; worst-case encoded 134 including the
delimiter. Host→helper types are `0x01..0x7F`, helper→host `0x80..0xFF` — the
direction is implicit in who received it, so the split buys nothing at runtime. It
buys a byte trace you can read without knowing which end dumped it, and both ends
reject a frame arriving from the wrong direction (which is what a looped-back
Grove pair would produce).

## 5. Frame types

### Host → helper

| code | name | payload |
| --- | --- | --- |
| `0x01` | `HELLO_REQ` | — |
| `0x02` | `ENUM_REQ` | — |
| `0x03` | `OPEN` | baud32, bits, parity, stop, dtr, rts, keylen, key[] |
| `0x04` | `CLOSE` | — |
| `0x05` | `DATA_OUT` | bytes → the USB device |
| `0x06` | `MODEM` | dtr, rts |
| `0x07` | `PING` | token16 |
| `0x08` | `CREDIT_OUT` | frames8 |
| `0x09` | `STAT_REQ` | — |
| `0x0A` | `RESCAN` | — (the helper reboots) |
| `0x0B` | `LINE` | baud32, bits, parity, stop |
| `0x0C` | `WAKE` | on8 (light the Stick's screen) |

### Helper → host

| code | name | payload |
| --- | --- | --- |
| `0x81` | `HELLO` | ver, epoch32, maxPayload, credits, fwlen, fw[] |
| `0x82` | `ENUM` | index, count, flags, keylen, key[], lablen, label[] |
| `0x83` | `OPENED` | ok8, err8, namelen, name[] |
| `0x84` | `DATA_IN` | bytes ← from the USB device |
| `0x85` | `EVENT` | code8, detlen, detail[] |
| `0x86` | `PONG` | token16 |
| `0x87` | `CREDIT_IN` | frames8 |
| `0x88` | `STAT` | 36-byte counter block |

Unknown types are ignored by both ends, so a newer peer can add frames safely.

A v1 `OPEN` **replaces** any port already open — that is what the host means when
it re-opens after a settings change, and requiring a `CLOSE` first would add a
state the two ends could disagree about.

## 6. Flow control

Credit-based, symmetric, counted in **frames**. Each side may have at most
`CSUH_CREDIT_INIT` (8) `DATA` frames outstanding.

Credit is granted from **receive-ring space**, not from frames consumed:

```
peerCredit * CSUH_MAX_PAYLOAD  <=  receiveRing.freeSpace()
```

so every frame the peer is allowed to send is guaranteed somewhere to land. The
obvious alternative — return credit for each frame consumed — overflows the moment
the reader stalls.

The direction that matters is helper → host: an Icom in CI-V transceive mode emits
unsolicited frames, and CardSat's main loop can stall for tens of milliseconds
inside a screen redraw. Under credit that becomes back-pressure into the helper's
ring (counted, reported via `CSUH_EV_OVERRUN`) instead of bytes vanishing.

**A dropped CI-V byte does not look like a link fault. It looks like a radio
fault, and would be chased as one.** That is the whole justification for carrying
flow control on a link that is, most of the time, almost idle.

A `HELLO` resets both windows, so the two ends cannot disagree about what is in
flight after a reconnect.

## 7. Link baud and auto-baud

The helper keeps no persistent configuration, so it cannot remember the link rate.
It scans `CSUH_BAUDS[]` — 230400, 115200, 460800 — for `CSUH_BAUD_TRY_MS` (400 ms)
each until a frame passes CRC, then locks. After `CSUH_BAUD_RELOCK_MS` (5 s) with
no valid frame it unlocks and scans again, so a host that changes rate is picked up
without a reboot.

This is only safe because the framing is CRC-checked: at the wrong rate the decoder
sees noise, and noise does not pass a CRC.

CardSat clamps `cfg.helperBaud` to that list. A rate outside it could never link,
and a link that silently never comes up is the hardest fault here to read.

**230400 is the default** rather than something faster: the wire is a short 3.3 V
Grove cable, but it runs beside a transmitting radio, and the failure mode of
pushing it harder is corrupted CAT rather than a clean error. `CSUH_T_STAT` reports
CRC and framing error counts, so "is the link clean at this rate" is a number the
operator can read on the USB helper screen rather than a thing to guess at.

## 8. Device identity

Keys are built with logic byte-identical to CardSat's `usbserial.cpp makeKey()`:

```
vvvv:pppp/SERIAL      when the device reports an iSerialNumber
vvvv:pppp@ADDRESS     when it does not
```

Serial-first matters because two adapters of the same model — the likely
radio + rotator case — are indistinguishable by VID:PID alone.

The address form exists because the TH-D75 (`2166:9023`) and IC-705 (`0c26:0036`)
report no serial at all. But a USB address is assigned by **enumeration order**, so
it is not a property of the radio: plug it into a different hub port, or power
things up in a different order, and an exact match fails. Both ends therefore fall
back to the VID:PID part — **and only when exactly one live device carries it**.
That last condition is what keeps two identical adapters ambiguous rather than
guessed between; the helper answers `CSUH_ERR_AMBIG` and CardSat tells the operator
to nominate one.

## 9. Liveness and recovery

The helper generates a fresh random **epoch** each boot and reports it in `HELLO`.
When CardSat sees the epoch change it knows the far end restarted: it drops its
device list, resets both credit windows, and re-issues the `OPEN` itself. A helper
that browns out or is unplugged mid-pass therefore recovers with **no operator
action** — which is the main practical argument for a stateless helper over one
that remembers its configuration.

CardSat pings every 1.5 s when the link is otherwise idle and declares it dead
after 5 s of silence. A wanted-but-unopened port is retried every 1.2 s, which
covers plugging the radio in after CardSat has already engaged.

`CSUH_T_RESCAN` reboots the helper. That is the only recovery when a USB host
stack has wedged (`usb_host_install()` returning `ESP_ERR_INVALID_STATE` for the
rest of a boot is a real failure mode — see `third_party/EspUsbHost/PATCHES.md`),
and unlike an in-place teardown it cannot itself get stuck.

## 10. Verification

| tool | what it proves |
| --- | --- |
| `tools/host_usbhelper/csuh_frames_test.sh` | CRC against its published check value; COBS against the Cheshire/Baker reference vectors including the 254/255-byte rollover; round-trip of every frame type at every payload length; every single-bit corruption caught; stream resynchronisation |
| `tools/host_usbhelper/csuh_link_test.sh` | the **actual** `src/usbhelper.cpp` run against a mock helper: handshake, enumeration, every `OPEN` failure code, byte round-trip, the credit invariant under an 8 KB transceive flood, helper reboot mid-session, link death and recovery, version mismatch, write back-pressure |
| `tools/check_csuh_parity.py` | the two copies of `csuh_proto.h` are byte-identical |
| `tools/helper_probe.py` | bench: speak CSUH to a Stick from a Mac, with CardSat out of the loop |

The link test compiles the shipped client against a minimal Arduino shim, so what
is exercised is the code that ships — not a re-implementation of it. Its credit
test was confirmed to have teeth by deliberately breaking the grant and checking
that it fails.

## 11. Reserved for later

* **`PORT`** is always 0 in v1 and receivers reject anything else. It exists so a
  later revision can carry more than one device without a framing change.
* **`CSUH_ERR_BUSY`** is defined and never sent, for the same multi-port future.
* Both ends ignore unknown frame types.
