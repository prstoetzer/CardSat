# Single-Pin CI-V Level Shifter — the ProtoSupplies Module Build

*Companion to [CIV_SINGLE_PIN.md](CIV_SINGLE_PIN.md), which covers the firmware mode,
settings, and protocol behavior. This document is the concrete hardware build: one
small off-the-shelf board that solves the 5 V ↔ 3.3 V interfacing that doc warns
about, powered entirely from the Cardputer's Grove port.*

> ✅ **This build is used and verified by the author (N8HM)** for single-wire CI-V
> on the bench radio. As always with hardware interfacing: **verify every connection
> with a meter before plugging in a radio.** Wrong wiring can damage a GPIO or the
> radio's CI-V jack.

---

## The board

**ProtoSupplies "I2C Logic Level Converter with Regulator Module"** (SKU MOD-140,
$0.79 as of July 2026):
<https://protosupplies.com/product/i2c-logic-level-converter-with-regulator-module/>

- **2 bidirectional channels**, BSS138 N-channel MOSFETs with 10 K pull-ups on both
  sides — the classic MOSFET level-shift topology.
- **On-board 3.3 V regulator** (XC6204, 150 mA) — the whole module runs from **5 V
  only** and generates its own 3.3 V side.
- Tiny (16 × 11 mm, DIP-8 on 0.5″ centers), ships with headers.

Marketed for I2C, but a BSS138 channel is protocol-agnostic — it passes any
open-drain, idles-high signal in both directions. That is *exactly* what CI-V is.

## Why this particular board fits CardSat so well

Three properties line up:

1. **CI-V is a one-wire, open-collector, idles-high 5 V bus.** A BSS138 channel is
   *inherently bidirectional* and only ever pulls the line low — it behaves like a
   transparent open-drain repeater between a 5 V segment and a 3.3 V segment. No
   direction pin, no enable, no timing configuration.
2. **The Cardputer's Grove port supplies 5 V and GND — but no 3.3 V.** Ordinary
   level-shifter boards need both rails, which on the Grove means stealing 3.3 V
   from somewhere else. This module's on-board regulator makes its own 3.3 V
   reference from the Grove's 5 V, so **the Grove cable is the only power you
   need.**
3. **Speed is a non-issue.** The module is comfortable to ~400 kHz on its 10 K
   pull-ups; CI-V runs at 19,200 baud or below — more than an order of magnitude
   of margin.

A bonus over a bare-wire hookup: the module's B-side 10 K pull-up to its regulated
3.3 V parallels the ESP32's weak internal ~45 kΩ pull-up, giving the shared pin a
stiffer idle-high and cleaner rising edges than the internal pull-up alone.

## Wiring

Uses **one** of the module's two channels (SDA here; SCL is identical if you prefer
it). The radio side is the usual 3.5 mm **mono** plug into the rig's CI-V REMOTE
jack: tip = data, sleeve = ground.

| From | To (module pin) | Notes |
|---|---|---|
| Grove **5 V** | **AVCC** | Powers the module *and* its 3.3 V regulator |
| Grove **GND** | **GND** | Either GND pin; they're common |
| Grove **G2** (data) | **BSDA** | The 3.3 V (B) side — CardSat's shared CI-V pin |
| Radio CI-V **tip** | **ASDA** | The 5 V (A) side — the radio's bus |
| Radio CI-V **sleeve** | **GND** | The second GND pin is convenient here |
| **BVCC**, ASCL/BSCL | *(unused)* | BVCC can power a 3.3 V accessory ≤ 100 mA |

Grove cable lead colors vary between vendors — **identify 5 V, GND, G1, and G2 with
a meter against the Cardputer's port before soldering anything.** (The matching
CardSat setting below assumes G2; if your cable makes G1 more convenient, use
"1-pin G1" instead and move the BSDA wire.)

## CardSat settings

**Settings → Radio → "CI-V wiring" → `1-pin G2`** (or `1-pin G1` to match your
wiring). Set the radio model, CI-V address, and baud per
[RADIO_SETTINGS.md](RADIO_SETTINGS.md) — for the author's IC-821 that is address
`4C`. Everything else about the mode — the open-drain pin configuration, echo
behavior, and troubleshooting — is covered in
[CIV_SINGLE_PIN.md](CIV_SINGLE_PIN.md).

## Bench checks before first use

1. **Unpowered:** continuity from Grove GND → module GND → plug sleeve; *no*
   continuity between AVCC and ASDA, or between the tip and sleeve of the plug.
2. **Powered, radio unplugged:** BSDA idles near **3.3 V**, ASDA idles near
   **5 V** (each held up by its 10 K). If either sits at ~0 V, stop and re-check.
3. **With the radio:** open CardSat's **CAT monitor**, send a frequency read, and
   watch the request and the radio's reply scroll past. Seeing both directions on
   one wire is the whole trick working.

## Notes

- **Party line:** CI-V is a multi-drop bus. Additional Icom radios can share the
  A-side (tip-to-tip), each answering its own address — the module doesn't change
  that.
- **Pull-up stacking:** the module's A-side 10 K sits in parallel with the radio's
  internal CI-V pull-up. That's normal for this bus and harmless at CI-V speeds.
- **Regulator budget:** if you use BVCC to power something, stay under ~100 mA to
  keep the XC6204 comfortable.
- Datasheets, per the product page:
  [XC6204 regulator](https://www.torexsemi.com/file/xc6205/XC6204-XC6205.pdf) ·
  [BSS138 MOSFET](http://www.onsemi.com/pub/Collateral/BSS138-D.PDF).

*See also [TTL_CAT_LEVELSHIFTER.md](TTL_CAT_LEVELSHIFTER.md) for using this same
module's two channels as a full-duplex TTL CAT interface (verified-TTL Yaesu ports
only — and why it must never touch RS-232).*

*Board facts verified against the ProtoSupplies product page, July 2026. CardSat
settings and pin names verified against the v0.9.54 source and
[CIV_SINGLE_PIN.md](CIV_SINGLE_PIN.md).*
