# Building a CI-V ↔ Grove interface and cable for CardSat

A short, buildable guide to connecting an Icom radio's **CI-V (REMOTE)** jack to
the **M5Stack Cardputer ADV** Grove port for use with CardSat. It covers the
level-shifting interface circuit (required — do **not** wire CI-V straight to the
GPIOs) and the cable. This is the **Icom** interface specifically; Kenwood and
Yaesu radios use RS-232 / serial CAT and a different cable (see the README and
MANUAL §16).

> **Why you can't connect directly.** CI-V is a single-wire, half-duplex bus that
> idles near **5 V**. The Cardputer's ESP32-S3 GPIOs are **3.3 V and not 5 V
> tolerant** — putting 5 V on G1 will damage the pin. The little circuit below
> level-shifts both directions and keeps the bus's open-collector behavior.

---

## Interfacing options

There's more than one way to get an Icom CI-V (REMOTE) jack onto CardSat's Grove
UART. Pick whichever suits the parts you have — all of them end at the same CardSat
settings (the model picker fills in the CI-V **address** and **baud**):

1. **Build the discrete level-shifter** in this guide (sections 3–5). Cheapest and
   self-contained: two transistors and a few resistors, powered from the Grove
   **5 V** (the Grove port provides **5 V, not 3.3 V**). The rest of this document
   covers this option.
2. **Use a ready-made logic-level CI-V interface** (a small board, or an M5/Grove
   "CI-V" unit). Bring its logic side to **G1/G2 + GND**; make sure it presents
   **≤ 3.3 V** to the ESP32 — a 5 V-only board still needs the RX divider from
   section 4. See section 8.
3. **Use an RS-232 → CI-V cable/adapter** you may already own (an Icom **CT-17**
   level converter, or a generic RS-232 CI-V cable). CardSat's UART is **3.3 V
   TTL**, not RS-232, so insert a **MAX3232** between the Grove UART and the cable:
   `Grove UART → MAX3232 → RS-232 → RS-232/CI-V cable → radio`. Build the MAX3232
   stage per **[RS232_INTERFACE.md](RS232_INTERFACE.md)** (note its power section —
   the Grove port is **5 V**, so the MAX3232 is fed from a small 3.3 V LDO). See
   section 8.

---

## 1. How CI-V behaves (the 60-second version)

- **One wire** carries both directions (TX and RX share it); the controller hears
  its own transmissions echoed back. *(CardSat's firmware already swallows that
  echo, and the CI-V serial trace shows every frame — see §6.)*
- It is **open-collector, idle-high**: the radio holds the line at ~5 V through an
  internal pull-up; any device sends a `0` by **pulling the line low**, and a `1`
  by releasing it.
- Levels on these radios (IC-820/821/910/970/9100/9700) are **5 V logic**. *Measure
  yours to be sure (see §6); if a radio's CI-V idles near 3.3 V, you can reduce or
  omit the RX divider.*

So the interface must: (a) let the 3.3 V UART **pull the 5 V line low** without
inverting the data, and (b) feed the line's level back to the 3.3 V RX pin
**clamped to ≤3.3 V**.

---

## 2. What connects where (CardSat defaults)

CardSat uses **UART1** with **G1 = RX** (into the ESP32) and **G2 = TX** (out of
the ESP32). The Grove port carries those two signals plus power:

| Grove pin | Typical wire color | Net | Use in this build |
|---|---|---|---|
| Signal 1 | yellow | **G1** | ESP32 **RX** ← interface output (clamped) |
| Signal 2 | white | **G2** | ESP32 **TX** → interface input |
| VCC | red | **5 V** | powers the interface + bus pull-up |
| GND | black | **GND** | common ground (also the CI-V sleeve) |

> ⚠️ **Verify the G1/G2 positions on your unit** with a meter or the M5 schematic
> before soldering — a swapped TX/RX is the single most common reason a
> home-built interface doesn't talk. (If your Grove maps the signals to other
> GPIOs, just change `CIV_RX_PIN`/`CIV_TX_PIN` in `config.h` to match.)
>
> ⚠️ The Grove **GPS** option in CardSat also uses **G1/G2** — you can't run Grove
> GPS and CI-V on the Grove port at the same time. If you want GPS *and* CI-V
> simultaneously, take GPS from an M5 Cap LoRa (G15/G13) instead.

The radio side is a **3.5 mm (1/8") mono / TS** plug into the **REMOTE** jack:

| Plug contact | Net |
|---|---|
| **Tip** | CI-V data |
| **Sleeve** | GND |

> Use a **mono (TS)** plug, not stereo (TRS). A stereo plug in a mono jack can
> short the ring to sleeve. Confirm the jack type in your radio's manual.

---

## 3. Bill of materials

| Qty | Part | Notes |
|---|---|---|
| 2 | NPN transistor **2N3904** | BC547, 2N2222, or S8050 are fine substitutes |
| 1 | Resistor **10 kΩ** (R1) | TX → Q1 base |
| 1 | Resistor **10 kΩ** (R2) | 5 V → Q2 base (Q1 collector pull-up) |
| 1 | Resistor **22 kΩ** (R3) | RX divider, top |
| 1 | Resistor **33 kΩ** (R4) | RX divider, bottom |
| 1 | Resistor **4.7 kΩ** (R5) | *optional* bus pull-up to 5 V (add only if idle measures low) |
| 1 | Ceramic cap **100 nF** (C1) | *optional* 5 V–GND decoupling |
| 1 | 4-pin **Grove (HY2.0)** cable/pigtail | to the Cardputer |
| 1 | **3.5 mm mono (TS)** plug | to the radio REMOTE jack |
| — | perfboard / Grove proto board, wire, heatshrink, small enclosure | |

All resistors ¼ W, 5 % are fine. Total current draw is a few mA.

---

## 4. The circuit

Two parts share the `CIV` node (the radio's data line) and a common `GND`. Power
(`+5V`) and ground come from the Grove cable.

**TX path — non-inverting, open-collector (so TX-low pulls the 5 V line low):**

```
 G2 (TX, 3.3V) ──[R1 10k]── base ┐
                                 Q1 (2N3904)   emitter → GND
                                 │ collector → node A
        +5V ──[R2 10k]── node A ─┘
                          │
                 node A ──── base ┐
                                  Q2 (2N3904)  emitter → GND
                                  │ collector → CIV  (the data line)
```

- TX **high** (idle): Q1 on → node A low → Q2 off → `CIV` released → pulled to 5 V.
- TX **low** (a `0`): Q1 off → node A pulled to 5 V by R2 → Q2 on → `CIV` pulled low.

That's the correct, non-inverting open-collector behavior with a standard
(idle-high) UART, and it needs only 5 V + GND from the Grove cable.

**RX path — clamping divider (feeds the line level to G1, capped ≤3.0 V):**

```
 CIV ──[R3 22k]──┬──► G1 (RX, 3.3V)
                 │
              [R4 33k]
                 │
                GND
```

- `CIV` at 5 V → G1 sees 5 × 33/(22+33) ≈ **3.0 V** (a valid logic high, under the
  3.3 V limit). `CIV` low → G1 sees ~0 V. Non-inverting, and the 22k/33k divider
  loads the bus only lightly.

**Shared / optional:**

```
 CIV ── 3.5 mm TIP          (data to the radio)
 GND ── 3.5 mm SLEEVE       (common ground to the radio)
 +5V ──[R5 4.7k]── CIV      (optional pull-up; add if idle < ~4 V)
 +5V ──[C1 100nF]── GND     (optional decoupling)
```

### Connection list (netlist) — the unambiguous build reference

```
+5V (Grove red)   : R2(top), R5(top, opt), C1(top, opt)
GND (Grove black) : Q1 emitter, Q2 emitter, R4(bottom), C1(bottom, opt), 3.5mm SLEEVE
G2  (Grove, TX)   : R1 -> Q1 base
G1  (Grove, RX)   : junction of R3 and R4
node A            : Q1 collector, R2(bottom), Q2 base
CIV               : Q2 collector, R3(top), R5(bottom, opt), 3.5mm TIP
```

---

## 5. Assembly

1. Build the circuit on a small perfboard or Grove prototyping board. Keep leads
   short; this is low-speed (≤19200 baud) so layout is forgiving.
2. Bring the **Grove** cable in for `+5V`, `GND`, `G1`, `G2`.
3. Run a short two-conductor lead out to the **3.5 mm mono plug**: tip = `CIV`,
   sleeve = `GND`.
4. House it inline or in a small box; strain-relieve both cables. A tidy option is
   to fit the whole circuit in a slightly oversized 3.5 mm plug shell or a small
   project box near the Grove end.
5. Double-check **G1↔G2 are not swapped** and that the **3.5 mm is mono**.

---

## 6. Bring-up and testing

Do the static checks **before** trusting it on the air.

**a. Voltage check (radio on, CardSat idle, nothing transmitting):**

| Test point | Expected |
|---|---|
| 3.5 mm tip (CIV) idle | ~4.5–5.0 V |
| 3.5 mm tip (CIV) while sending | dips toward ~0.1–0.3 V |
| G1 / RX divider tap idle | ~2.7–3.0 V |
| G1 while line low | ~0 V |

If CIV idle is well below ~4 V, add R5 (4.7 kΩ to 5 V). If G1 idle is below ~2.5 V,
your divider is too low-impedance or the bus idle is low — recheck R3/R4 and the
pull-up.

**b. Set the radio's CI-V menu:** address and baud to match CardSat's **Settings**
(the model picker fills in the standard values — IC-820 `0x42`/9600, IC-821
`0x4C`/9600, IC-910 `0x60`/19200, IC-970 `0x2E`/9600, IC-9100 `0x7C`/19200,
IC-9700 `0xA2`/19200). **CI-V Transceive** can be left **off** — CardSat polls with
its own read command, and off keeps the bus quiet.

**c. Watch the CI-V serial trace.** Connect the Cardputer over USB, open a serial
monitor at **115200**, go to **Track**, and turn radio output on (`r`). You should
see decoded frames and the radio's acknowledgement:

```
[CI-V TX] FE FE A2 E0 07 D1 FD  sel-band SUB
[CI-V TX] FE FE A2 E0 05 00 60 58 45 14 FD  set-freq 145456000 Hz
[CI-V RX] radio ACK (FB)
```

- **`radio ACK (FB)`** → wiring, address, and baud are all good.
- **`radio NAK (FA)`** → the radio heard you but rejected the command (often a
  band/sub-command issue) — wiring is fine, look at settings.
- **No `[CI-V RX]` line at all** → the radio isn't hearing you, or you're not
  hearing it. See troubleshooting.

If read-back works you'll also see `[CI-V] SUB freq read: … Hz`, which is what
powers the radio-knob (One True Rule) tuning.

---

## 7. Troubleshooting

**Nothing happens / no ACK.** In order of likelihood:
1. **G1/G2 swapped.** Swap which Grove signal goes to the interface's TX input vs
   RX output (or swap `CIV_RX_PIN`/`CIV_TX_PIN` in `config.h`).
2. **No common ground.** The 3.5 mm sleeve and the Grove GND must be the same net.
3. **Address/baud mismatch.** Match the radio's CI-V menu to CardSat's Settings.
4. **Mono vs stereo plug**, or the plug not fully seated.

**Radio ACKs but tunes the wrong VFO.** That's a MAIN/SUB band-select detail, not
the interface — adjust `selMain[]`/`selSub[]` for your model in
`radio_profiles.h`.

**IC-821: frequency reads are unreliable (knob-follow jumps or no read).** This is a
known quirk of the IC-821, not a wiring fault. On this radio the **SUB** band (the
downlink/RX in satellite mode) frequently won't answer the read command unless the
SUB band has just been selected. CardSat already re-selects the band immediately
before every read and, when no valid reply comes back within the budget, **falls
back to the last frequency it commanded** rather than acting on a bad read — so the
downlink keeps Doppler-tracking even when reads fail; you just lose live knob-follow
for that cycle. If knob-follow is consistently unavailable on your IC-821, that is
expected; the device **TUNE** keys move the passband instead. Increasing **CAT
Delay** in Settings (giving the SUB band longer to settle) can improve read success.

**Garbled/intermittent frames.** Check the idle voltages (§6a), shorten the cable,
and confirm the baud. The 22k/33k divider and the transistors are good well past
19200 baud, so persistent garbling usually means a wiring/ground issue.

**You see TX frames but never an RX/ACK.** The radio isn't pulling the line where
the divider can see it (RX path), or the radio's TX isn't reaching the bus. Verify
the divider tap actually moves when the radio is keyed/queried, and that R3
connects to the **CIV** node (not after a series block).

---

## 8. Alternatives

- **MOSFET level-shifter module.** A cheap BSS138 bidirectional level-shifter
  board can replace the discrete parts if you also merge TX/RX onto one line: put
  a small Schottky diode (anode at the 3.3 V data node, cathode at G2/TX) so TX can
  pull the node low but not drive it high, tie G1/RX to that node, and run the node
  through one channel of the level shifter to the 5 V CI-V line (3.3 V pull-up on
  the low side, the radio's pull-up on the high side). The discrete circuit above
  is self-contained and needs no 3.3 V rail, which the Grove port doesn't provide.
- **Commercial CI-V interface.** Any 3.3 V-safe CI-V interface works; just bring
  its logic side to G1/G2 and GND with the polarity above. (5 V-only interfaces
  still need the RX divider to protect the ESP32.)
- **RS-232 → CI-V cable (e.g. Icom CT-17).** If you already have a CI-V interface
  that presents an **RS-232** port — the classic **CT-17** level converter, or a
  generic RS-232 CI-V cable — you don't build the discrete circuit at all. Put a
  **MAX3232** between the Cardputer's 3.3 V Grove UART (G1/G2) and the cable's
  RS-232 side, then plug the cable's CI-V plug into the radio's REMOTE jack:
  `Grove UART → MAX3232 → RS-232 → CT-17/CI-V cable → radio`. The MAX3232 stage —
  including the important detail that the **Grove port supplies 5 V, not 3.3 V**, so
  the MAX3232 is powered from a small 3.3 V LDO — is documented in
  **[RS232_INTERFACE.md](RS232_INTERFACE.md)**. This reuses a cable you may already
  own and keeps the CI-V level conversion inside the proven CT-17.

---

## 9. Quick reference card

```
GROVE                         INTERFACE                         RADIO
 5V  ──────────────► +5V ──[R2 10k]──┐        ┌──[R5 4.7k opt]──┐
                                     node A    │                │
 G2(TX)──[R1 10k]──► Q1.base         │         │                │
                      Q1.E→GND  Q1.C─┘         │                │
                                node A→Q2.base │                │
                      Q2.E→GND  Q2.C───────────┴──── CIV ───────┼──► TIP  (3.5mm mono)
 G1(RX)◄──┬───────── R3 22k ───────────────────────────────────┘
          └─ R4 33k ─► GND
 GND ─────────────────────────────────────────────── GND ─────────► SLEEVE
```

Levels: CIV idle ≈ 5 V, low ≈ 0 V · G1 idle ≈ 3.0 V, low ≈ 0 V · TX-low pulls CIV
low (non-inverting) · RX divider keeps G1 ≤ 3.3 V.

*Amateur-radio homebrew — double-check polarity and the mono plug before keying.*
