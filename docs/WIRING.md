# CardSat — Wiring Guide

Connection details for CAT (radio control), GPS, and antenna rotators. For the
firmware-side radio settings that go with this wiring, see
**[interfaces/RADIO_SETTINGS.md](interfaces/RADIO_SETTINGS.md)**; for the
electrical interface specifics see the documents under **[interfaces/](interfaces/)**.

### CAT (radio)

All three CAT families share the **3.3 V hardware UART** — `UART1`, **RX = G1,
TX = G2** by default — but the **interface hardware between the radio and G1/G2
differs by family**, because the electrical layer is different:

- **Icom CI-V** — single-wire half-duplex bus idling near 5 V. Use a 3.3 V-safe
  CI-V interface (the common one-transistor circuit or a ready-made board) on the
  radio's REMOTE jack. Full build guide: **[CIV_INTERFACE.md](interfaces/CIV_INTERFACE.md)**.
  CardSat also has a **single-pin CI-V** mode (one shared open-drain GPIO for the
  whole bus) selectable in *Settings → Radio / CAT → CI-V wiring* — **confirmed
  working on an IC-821** (full bidirectional exchange, Doppler, knob tuning), but it
  needs correct open-drain 5 V/3.3 V interfacing; the TX/RX path stays the simplest.
  See **[CIV_SINGLE_PIN.md](interfaces/CIV_SINGLE_PIN.md)**.
- **Kenwood (TS-790, TS-2000)** — true **RS-232** on a DB-9 COM port (±12 V). Use a
  **MAX3232-class level shifter** between the DB-9 and G1/G2; do **not** use the CI-V
  circuit. On the TS-2000, a straight 3-wire cable with **CTS/RTS bridged** (or the
  "RTS +12 V" handshake) is the usual fix for the rig's handshake quirk. Build
  guide: **[RS232_INTERFACE.md](interfaces/RS232_INTERFACE.md)**.
- **Yaesu (FT-847, FT-736R)** — 5-byte serial CAT. Verify **TTL vs RS-232** for your
  unit from the CAT manual and use the matching level interface. The FT-736R is most
  reliably driven through an **FT-847-emulating** CAT interface (KA6BFB / HS-736USB);
  select **FT-847** in Settings in that case. Build guide (MAX3232):
  **[RS232_INTERFACE.md](interfaces/RS232_INTERFACE.md)**.

**No-solder alternative — CAT over USB (0.9.58; on by default since 0.9.59).**
Instead of any of the interfaces above, a **USB↔serial adapter** (FTDI / CP210x /
CH34x, or any CDC-ACM device) on the Cardputer's **USB-C port** can carry CAT for
**every radio family** — set **CAT type → USB serial**. The adapter's TTL/RS-232 side
still has to match the radio's CAT jack levels (an FTDI *cable* built for the rig, or
adapter + the matching level interface), but the Cardputer end is plug-in: no G1/G2
wiring, no level shifter at the Cardputer. Bench-proven on an IC-821 + FTDI. Engaging
USB takes the S3's one USB PHY, so the **serial console is gone for the session** —
diagnostics land in `/CardSat/Logs/usb.log`. G1/G2 stay free, which is what lets a
**Grove rotator** (below) coexist with CAT.

Set the **model** and **CAT baud** in **Settings** to match the radio's menu (the CI-V
**address** field applies to Icom only). Yaesu is 8N2; Icom and Kenwood are 8N1 —
the firmware sets this automatically per backend.

**Icom over the network (no wiring).** The **IC-9700** can be driven over
**WiFi/Ethernet** with no CI-V interface at all — CardSat speaks the radio's RS-BA1
UDP protocol directly. (The transport is proven — it has controlled an **IC-705** —
but the LAN feature is *intended for the IC-9700*: the other networked Icoms
(IC-705/7610/785x) speak the protocol yet lack the dual-receiver satellite
architecture, and the IC-9700 itself has not been LAN-tested yet.) On the radio enable **Network
Control**, set a **Network User1** id + password, keep the **Control port** at 50001,
and turn **CI-V Transceive ON**. In **Settings** set **CAT type → Icom LAN** and fill in
**LAN host / port / user / pass**. Only CAT is carried (the audio stream is not opened);
everything else — MAIN/SUB, Doppler, sat mode, CTCSS — works exactly as on a wired Icom.
Protocol details: **[ICOM_LAN_PROTOCOL.md](interfaces/ICOM_LAN_PROTOCOL.md)**.

### GPS (optional)

GPS runs on **UART2**, so it never collides with CAT on UART1. The source is
selectable at runtime on the **Location** screen (press `s`):

| Source | UART | RX | TX | Baud |
|---|---|---|---|---|
| Grove 9600 | 2 | G1 | G2 | 9600 |
| Grove 115200 | 2 | G1 | G2 | 115200 |
| Cap LoRa868 | 2 | G15 | G13 | 115200 |
| Cap LoRa1262 | 2 | G15 | G13 | 115200 |

Both Cap LoRa modules carry the same AT6668 GNSS at **115200 8N1** on G15/G13;
the two Grove rows differ only in baud, to match whatever receiver you plug in.

> ⚠️ The **Grove** GPS option uses **G1/G2 — the same pins as the default CAT
> port and the Grove rotator wire.** Only one of the three can own those pins at a
> time; CardSat enforces it, but wire accordingly. The two Cap LoRa sources use
> G15/G13 and coexist with all of it fine.

---

### Antenna rotator (GS-232 / Easycomm / SPID over three wires, Yaesu direct, or rotctld / PstRotator over WiFi)

CardSat drives an az/el rotator through one of **eight interchangeable backends**,
chosen in **Settings → Rot type**: the three serial protocols **GS-232**,
**Easycomm I/II/III**, and **SPID Rot2Prog**; the two network clients **rotctl
(net)** and **PstRotator (net)**; and the controller-less **Yaesu (direct)** build.
For the serial protocols, **which wire they run on is a second setting — `Rot
wire`**: the **SC16IS750 I²C→UART bridge** (the wiring below), the **Grove port
(G1/G2)** driven directly, or a **USB↔serial adapter** on the USB-C port. Protocol
and wire are independent; conflict rules and the guardrails for radio+rotator on
USB are in **[interfaces/ROTATOR_TRANSPORTS.md](interfaces/ROTATOR_TRANSPORTS.md)**.
Only one rotator is active at a time, and the pointing logic — alignment offsets,
deadband, flip mode, park-on-LOS — is shared, so switching backends or wires
doesn't change behavior. Enable the rotator in Settings and
press **`o`** on the Track screen to start/stop pointing; it parks at the
configured azimuth on LOS. With **Rot pre-point** set (default 2 min), it also
slews to the next pass's rise bearing that far before AOS, so a slow rotator is
already aimed when the satellite appears. **Rot az range** matches your rotator's
azimuth travel — `0..360`, `-180..+180` (centered on North, like Gpredict), or
`0..450` (90° overlap, so a pass crossing North runs into the overlap instead of
unwinding a full turn). **Rot el range** picks `90` or `180`°; at 180° a high pass
**flips over the top** rather than swinging azimuth 180° at culmination.

**Wired, `Rot wire` = I2C bridge (the default).** With wired CAT on G1/G2 and a
Grove GPS both possible, the bridge is the one serial wire that coexists with
everything — the rotator gets its own UART on the expansion I²C bus. Chain
(GS-232 shown; Easycomm and SPID use the identical chain):

**Wire1 → SC16IS750 → MAX3232 → DB-9 → GS-232 controller.**

- The bridge runs on the Cardputer-ADV expansion I²C bus — **G8 = SDA, G9 = SCL**
  (`ROT_I2C_SDA`/`ROT_I2C_SCL`), on **Wire1**, separate from the keyboard bus.
  These are confirmed from the Cap LoRa-1262 pinmap and clear of CAT (G1/G2), the
  GPS UART (G13/G15), the LoRa SPI (G3/G4/G5/G6/G14/G39/G40), and SD (CS G12).
- On the Cap LoRa-1262, G8/G9 is the bus broken out to its **HY2.0-4P Grove
  Port.A**, so a Grove SC16IS750 plugs straight in. It's shared with the cap's
  PI4IOE5V6408 expander (~0x43/0x44, LoRa RF switch only); keep `ROT_I2C_ADDR`
  (default `0x4D`) clear of that, or add a TCA9548A mux.
- GS-232 uses RS-232 levels, so a **MAX3232** sits between the bridge's TTL pins
  and the controller's DB-9 (TXD / RXD / GND). Set **Rot baud** to match the
  controller (commonly 9600).

**Wired, `Rot wire` = Grove G1/G2.** The serial protocols can instead drive the
**Grove port directly** — TTL on G1/G2, through a MAX3232 if the controller wants
RS-232 levels. **The Cardputer has one Grove port**: wired CI-V CAT and the Grove
GPS use the same two pins, so a Grove rotator needs **CAT on USB or LAN** and the
**GPS off Grove** (Cap LoRa GNSS is fine — it uses G15/G13). CardSat enforces this
both ways: it refuses a Grove rotator while Grove is taken, and yields back to the
bridge if CAT or GPS later claims Grove.

**Wired, `Rot wire` = USB adapter.** A **USB↔serial adapter** on the USB-C port,
sharing the resident USB host with USB CAT. Engaging USB closes the serial console
for the session. Rotator-only is straightforward; **radio and rotator on USB at
once** (through a hub, each bound to a chosen adapter) is built and guarded but
**untested with two physical adapters** — see
**[interfaces/ROTATOR_TRANSPORTS.md](interfaces/ROTATOR_TRANSPORTS.md)**.

**Wired — Yaesu (direct), no controller box.** A G-5500-class controller can be
wired **straight to CardSat**: an I²C **ADS1115** reads the position-feedback
voltages and an I²C **PCF8574** drives the four direction lines, on the same Wire1
bus the bridge would use. Hardware build + calibration:
**[interfaces/ROTOR_INTERFACE.md](interfaces/ROTOR_INTERFACE.md)** (⚠️ untested; at
your own risk).

**Networked — rotctld.** No extra wiring: set **Rot type → rotctld (net)** and
point CardSat at a **Hamlib `rotctld`** server on the same WiFi network — enter
its **Net host** (an IP is simplest) and **Net port** (Hamlib default
**4533**). CardSat is the TCP client, the same role Gpredict plays: it sends
`P <az> <el>` about once a second to track and `S`/park on LOS. The socket opens
when you enable the rotator and reconnects on its own (throttled) if the server
drops; pressing `o` re-attempts the link on the spot. Because the rotator sits
behind Hamlib, anything `rotctld` can drive works over the LAN. That includes
rotators with a native rotctld network service, such as the **MuseLab AntRunner**
(portable, 360°/180°, WiFi) and **AntRunner-Pro** (fixed-install, 360°/90°,
Ethernet) — point **Net host/port** at the AntRunner, no PC required.

**Networked — PstRotator.** Already running **PstRotator** on a shack PC? Set
**Rot type → PstRotator (net)**, **Net host** to the PC, and **Net port** to
PstRotator's UDP Control port (default **12000** — change it from rotctld's 4533).
CardSat sends `<PST><AZIMUTH>..</AZIMUTH><ELEVATION>..</ELEVATION></PST>` datagrams
to point and `<PST><STOP>1</STOP></PST>` to stop; PstRotator drives whatever
controller it is set up for. UDP is connectionless, so enable **UDP Control** in
PstRotator and confirm the antenna follows on the first pass. This backend also
drives the **WA4MCM PSR-100** portable satellite rotor directly: it speaks the
same `<PST>` UDP az/el protocol, so point **Net host/port** at the PSR-100's WiFi
interface (no PC running PstRotator required).

- Bench-test it end-to-end before trusting motors: run
  `rotctld -m 1 -t 4533 -vvvvv` (Hamlib's dummy rotator) on a PC — the `-vvvvv`
  makes rotctld print every `P`/`S` command CardSat sends, with no motors moving
  (without the verbose flag it stays silent, which can look like nothing is sent).
- `rotctld` has **no authentication** — keep it on a trusted LAN, never exposed
  to the internet.

Full details for both backends: **[MANUAL.md §17](../MANUAL.md)**.
