# TTL-Level CAT with the Level-Shifter Module — and Why NOT RS-232

*Companion to [CIV_SINGLE_PIN_LEVELSHIFTER.md](CIV_SINGLE_PIN_LEVELSHIFTER.md) (the
same ProtoSupplies module used for Icom CI-V) and to
[RS232_INTERFACE.md](RS232_INTERFACE.md) (the MAX3232 build for RS-232 radios).
This document answers two questions: can the $0.79 module drive a **TTL-level**
Yaesu/Kenwood CAT port (yes — build below), and can it interface **RS-232** (no —
and it matters why).*

> ## ⚠️ UNTESTED — PAPER DESIGN, BUILD AT YOUR OWN RISK
> Unlike the CI-V build (which the author runs on the bench), this TTL arrangement
> has **not been verified against a physical radio**. It follows standard practice
> for BSS138 shifters on UARTs, but you must verify your radio's CAT **levels and
> pinout against its own manual**, and bench-test before connecting. The ESP32-S3
> pins are **not 5 V tolerant**; the radio's CAT jack is irreplaceable. Proceed at
> your own risk.

---

## First: RS-232 is a hard NO for this module

True RS-232 idles at a **negative** voltage and swings roughly **±5 V to ±12 V**.
The BSS138 shifter is a 0-to-VCC logic device:

- Negative swings would be applied across the MOSFET and its pull-ups — outside
  ratings, and the module would pass garbage (or worse) through to the GPIO.
- RS-232's logic sense is **inverted** relative to a UART pin (mark = negative).
  Even a shifter that survived would deliver upside-down data.

Per the repo's radio table, that rules this module **out** for both supported
Kenwoods: the **TS-2000** and **TS-790** present a **DB-9 COM port at RS-232
levels**. For those — and for any Yaesu whose CAT proves to be RS-232 — build the
**MAX3232** interface in [RS232_INTERFACE.md](RS232_INTERFACE.md) instead. This
document exists partly to stop the tempting mistake.

## Which radios *can* use it: verified-TTL CAT ports

Some serial CAT ports are **5 V TTL** at the jack — 0 V/+5 V, idle **high** —
which is exactly what this module shifts. Among CardSat's supported radios that
is Yaesu territory: the **FT-847**'s CAT jack, and the **FT-736R**'s CAT
arrangement, **vary by unit and documentation era — the repo does not certify
either as TTL.** You must confirm from *your* radio's manual before using this
build. (The FT-736R is also often driven through an FT-847-emulating commercial
interface — see [RS232_INTERFACE.md](RS232_INTERFACE.md); those interfaces
handle levels themselves.)

**The decision test.** With the radio on and the CAT port idle, measure the
radio's data-out pin against ground with a meter:

- Idles near **+5 V** → TTL. This build applies.
- Idles **negative** (−5 V or lower) → RS-232. **Stop** — use the MAX3232 guide.
- Idles near **0 V** → something is off (wrong pin, port disabled); investigate
  before proceeding either way.

## Wiring (full duplex — both channels used)

Unlike single-wire CI-V, Yaesu/Kenwood CAT is a normal two-wire UART, so this
build uses **both** of the module's channels: one per direction. Power is still
Grove-only — AVCC takes the Grove 5 V and the on-board regulator makes the 3.3 V
side.

| From | To (module pin) | Notes |
|---|---|---|
| Grove **5 V** | **AVCC** | Powers module + its 3.3 V regulator |
| Grove **GND** | **GND** | Common with the radio's ground |
| Grove **G2** (CardSat TX) | **BSDA** | 3.3 V side, channel 1 |
| **ASDA** | Radio **data-in (RXD)** | 5 V side, channel 1 |
| Radio **data-out (TXD)** | **ASCL** | 5 V side, channel 2 |
| **BSCL** | Grove **G1** (CardSat RX) | 3.3 V side, channel 2 |
| Radio CAT **GND** | **GND** | The second GND pin is convenient |

Radio-side pin numbers come from **your radio's manual** (Yaesu CAT jacks vary
by model). CardSat's side is the default **TX/RX (G2/G1)** wiring — single-pin
mode is a CI-V-only concept and does not apply here.

Two electrical notes worth knowing: a BSS138 channel drives **lows** actively and
lets the 10 K pull-up supply the **highs** — at CAT speeds (4800–57,600 baud)
the rise time through 10 K is a non-issue, which is why these shifters are
routine on UARTs despite the I2C marketing. And a UART idles at **mark (high)**,
which the pull-ups provide on both sides — so both lines should sit at their
rail voltages when quiet (see checks below).

## Bench checks before first use

1. **Loopback first, radio absent:** jumper **ASDA ↔ ASCL** on the 5 V side.
   CardSat's transmitted CAT bytes now come straight back into G1 — open the
   **CAT monitor**, send anything, and confirm the same bytes appear on RX.
   This proves both channels, both directions, and the Grove wiring, with zero
   risk to the radio. Remove the jumper afterward.
2. **Idle voltages:** BSDA and BSCL near **3.3 V**; ASDA and ASCL near **5 V**.
   Anything sitting at ~0 V means a wiring fault.
3. **Radio's own idle:** before connecting, re-verify the radio's data-out idles
   at **+5 V** (the decision test above) — not negative.
4. **First contact:** connect, set the model and baud (per
   [RADIO_SETTINGS.md](RADIO_SETTINGS.md)), and watch the CAT monitor. Note the
   **FT-736R quirk**: its CAT has **no read-back** — CardSat drives it push-only,
   so a working link shows your transmitted frames but no replies. That is
   correct behavior, not a wiring fault. The FT-847 does reply.

## CardSat settings

**Settings → Radio →** select the Yaesu model; set the **CAT baud** to match the
radio (chart in [RADIO_SETTINGS.md](RADIO_SETTINGS.md); Yaesu framing is 8N2 and
the firmware sets that automatically). Leave **CI-V wiring** at its default
**TX/RX (G2/G1)** — that setting only affects Icom single-pin mode.

## Summary table

| Radio | CAT levels (per repo docs) | Interface |
|---|---|---|
| TS-2000, TS-790 | RS-232 (DB-9, ±V) | **MAX3232** — [RS232_INTERFACE.md](RS232_INTERFACE.md) |
| FT-847, FT-736R | *Verify per your manual* | TTL → **this build**; RS-232 → MAX3232 |
| Icom CI-V rigs | 5 V open-collector bus | Same module, one channel — [CIV_SINGLE_PIN_LEVELSHIFTER.md](CIV_SINGLE_PIN_LEVELSHIFTER.md) |

*Board facts per the ProtoSupplies product page (July 2026); radio-level facts per
this repo's [RS232_INTERFACE.md](RS232_INTERFACE.md) and
[RADIO_SETTINGS.md](RADIO_SETTINGS.md); wiring conventions per the v0.9.54 source
(UART1, G1 = RX, G2 = TX).*
