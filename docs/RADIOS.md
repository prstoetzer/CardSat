# CardSat — Radios: Bands, Satellite Mode, and Read-back

How CardSat drives each supported radio family, what it sets over CAT versus what
you set on the rig, and the per-model specifics. See also
**[interfaces/RADIO_SETTINGS.md](interfaces/RADIO_SETTINGS.md)** for the settings
chart and **[interfaces/CIV_INTERFACE.md](interfaces/CIV_INTERFACE.md)** /
**[interfaces/ICOM_LAN_PROTOCOL.md](interfaces/ICOM_LAN_PROTOCOL.md)** for protocol detail.

CardSat drives **two independent VFOs** and Doppler-corrects both. The default
convention is **"Sub" = downlink/RX, "Main" = uplink/TX**; the **VFO Type** setting
swaps the roles (*Main Dn/Sub Up*) when your rig's satellite-mode band layout needs
it. The **CAT rate** setting sets how often updates are sent (default **500 ms**,
adjustable in 10 ms steps, soft-floored to what the CAT baud can service). How that
maps to each family:

- **Icom** — CardSat drives MAIN/SUB directly. By default it leaves the rig's own
  satellite mode **off**; the **Sat mode** setting commands it on/off when you
  engage CAT (a no-op on rigs without one). MAIN/SUB select uses CI-V `0x07 D0/D1`
  (verified vs the IC-821H manual). If a radio mistunes the wrong VFO, edit
  `selMain[]`/`selSub[]` in `radio_profiles.h`. Network-capable Icoms can run this
  same control over WiFi/Ethernet — see **Icom over the network** under
  [CAT (radio)](#cat-radio).
- **Yaesu** — the FT-847 sat opcodes set the SAT-RX (downlink, `0x11`) and SAT-TX
  (uplink, `0x21`) VFOs directly, and read the downlink back with `0x13` (firmware-
  dependent). CAT is enabled at startup (`00 00 00 00 00`).
- **Kenwood** — downlink on **VFO A** (`FA`), uplink on **VFO B** (`FB`).

> **Shared limitation of the Yaesu/Kenwood sat rigs:** CAT **cannot switch the band
> pair**. The operator selects the uplink/downlink bands and engages the rig's own
> satellite / full-duplex mode **by hand**; CardSat only Doppler-tunes within that
> setup. (This is exactly how SatPC32 drives these radios.) Icom is the exception —
> CardSat manages its MAIN/SUB bands over CI-V.

**Frequency read-back** powers the radio-knob *One True Rule* mode (Icom and Kenwood,
plus the FT-847 on updated firmware). Where it isn't available, the device **TUNE** keys
move the passband instead:

| Family | Radios | Protocol | Interface | Read-back | Knob tuning |
|---|---|---|---|---|---|
| Icom | IC-820/821/910/970/9100/9700 | CI-V (binary) | CI-V 5 V single-wire | ✅ `0x03` | ✅ |
| Yaesu | FT-847 | 5-byte (binary) | serial (TTL/RS-232) | ✅ `0x13` ¹ | ✅ ¹ |
| Yaesu | FT-736R | 5-byte (binary) | serial (TTL/RS-232) | ❌ | ❌ (TUNE keys) |
| Kenwood | TS-790, TS-2000 | ASCII `;` | RS-232 (MAX3232) | ✅ `FA;` | ✅ |

¹ **FT-847 read-back** uses the "read freq & mode" command (`0x03`, patched to `0x13`
for the SAT-RX/downlink VFO): 4 big-endian BCD bytes + mode. It works only on
**firmware-updated** FT-847s — early units have no read capability and stay silent
(CardSat times out and holds steady). In satellite mode the radio occasionally returns
the uplink VFO instead (Hamlib #1286); CardSat rejects any read that jumps > 1 MHz from
the commanded downlink, so a stray wrong-VFO reply holds the passband rather than jerking
it. The **FT-736R** cannot report frequency at all, so it uses the device **TUNE** keys.
Both Yaesu rigs track fully under software control regardless.

### CAT serial trace

Every frame the firmware sends is printed to the **serial monitor at 115200
baud**, decoded, so you can watch exactly what reaches the radio. The Icom (CI-V)
trace looks like:

```
[CI-V TX] FE FE A2 E0 07 D1 FD  sel-band SUB
[CI-V TX] FE FE A2 E0 05 00 60 58 45 14 FD  set-freq 145456000 Hz
[CI-V RX] radio ACK (FB)
[CI-V TX] FE FE A2 E0 07 D0 FD  sel-band MAIN
[CI-V TX] FE FE A2 E0 05 00 00 56 35 14 FD  set-freq 435356000 Hz
[CI-V RX] radio ACK (FB)
[CI-V TX] FE FE A2 E0 03 FD  read-freq req
[CI-V RX] FE FE A2 E0 03 FD FE FE E0 A2 03 00 60 58 45 14 FD
[CI-V] SUB freq read: 145456000 Hz
```

Set-freq frames are decoded to Hz, modes to LSB/USB/CW/FM, band selects to
MAIN/SUB, and the radio's reply is reported as **ACK (FB)** or **NAK (FA)** — the
quickest way to confirm wiring, address, and baud. Set `CIV_DEBUG 0` at the top of
`civ.cpp` to silence the trace.

The Yaesu and Kenwood backends emit the same kind of trace tagged **`[CAT TX]`**
(decoded set-freq/mode and CAT-on for Yaesu; the literal `FA…;`/`MD…;` strings and
the `FA;` read reply for Kenwood). The **Icom LAN** backend adds **`[NET]`** lines for
its connect/auth handshake and keepalives, carrying the same CI-V frames. Silence any
of them with `CIV_DEBUG` / `YAESU_DEBUG` / `KW_DEBUG` / `ICOMNET_DEBUG 0` at the top of
`civ.cpp` / `yaesu.cpp` / `kenwood.cpp` / `icomnet.cpp`.
