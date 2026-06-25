# Building an RS‑232 CAT interface for Yaesu / Kenwood radios

A buildable guide to connecting a **Yaesu** (FT‑847, FT‑736R) or **Kenwood**
(TS‑2000, TS‑790) transceiver that uses **RS‑232‑level serial CAT** to the
**M5Stack Cardputer ADV** Grove port for use with CardSat. It covers the
**MAX3232** level‑shifter circuit (required — do **not** wire RS‑232 straight to the
GPIOs) and the cabling. Icom radios use **CI‑V**, which is different — see
**[CIV_INTERFACE.md](CIV_INTERFACE.md)** for those.

> ## ⚠️ UNTESTED — BUILD AND USE AT YOUR OWN RISK
> **This interface has not been verified against a physical radio.** It is a paper
> design from the radios' published CAT behaviour and the CardSat wiring. RS‑232
> carries voltages that will **destroy** the Cardputer's GPIO if mis‑wired.
> **Proceed entirely at your own risk. The author accepts no liability for any
> damage to your radio, your Cardputer, or anything else arising from following this
> document.** Verify every pin, level, and baud against *your* radio's manual, and
> bench‑test the converter before connecting the radio.

> **Why you can't connect directly.** True RS‑232 swings to roughly **±5 V to ±12 V**.
> The Cardputer's ESP32‑S3 GPIOs are **3.3 V and not 5 V tolerant** — putting RS‑232
> levels on G1 will damage the pin. A **MAX3232** (the 3 V‑capable MAX232) translates
> both directions between RS‑232 and 3.3 V logic.

---

## 1. The CardSat side (same port as CI‑V)

CardSat's CAT runs on **UART1** on the **Grove HY2.0‑4P** port:

| Cardputer Grove pin | Signal |
| --- | --- |
| **G1** | UART **RX** (into the Cardputer) — `CIV_RX_PIN` |
| **G2** | UART **TX** (out of the Cardputer) — `CIV_TX_PIN` |
| **5V** | Grove **power** pin — **5 V only**, no 3.3 V rail (see section 2) |
| GND | common ground |

This is the **same** physical port used for the CI‑V interface — you build *either*
an Icom CI‑V interface *or* this RS‑232 interface on it, for whichever radio you run.

## 2. The MAX3232 circuit

A MAX3232 plus its charge‑pump capacitors is the whole interface. Use a **3.3 V**
part (MAX3232, *not* the 5 V‑only MAX232).

> **Powering it — the Grove port gives 5 V, not 3.3 V.** The Cardputer's Grove
> **power pin is 5 V**; the signal pins are 3.3 V logic, but there is **no 3.3 V
> rail on the Grove port**. Power the MAX3232 at **3.3 V** so the level it hands
> back to the ESP32 RX (`R1OUT`) is 3.3 V — running it from 5 V would put ~5 V on
> `R1OUT` and **destroy** the non-5 V-tolerant GPIO. Two ways to get 3.3 V:
>
> - **(Recommended) A small 3.3 V LDO** (HT7333, AMS1117-3.3, MCP1700, …) fed from
>   the Grove **5 V**, powering the MAX3232 `VCC`; then `R1OUT` goes straight to **G1**.
> - **(No LDO) Run the MAX3232 from the Grove 5 V**, but add a **resistor divider**
>   on `R1OUT → G1` (e.g. 22k/33k to ground, as in
>   **[CIV_INTERFACE.md](CIV_INTERFACE.md)** section 4) so the RX pin stays under
>   3.3 V. Driving `T1IN` from **G2** (3.3 V) is fine even at 5 V VCC — the
>   MAX3232's input-high threshold is well below 3.3 V.

- **VCC:** 3.3 V from the LDO above (**not** straight off the Grove 5 V); **GND** common.
- **Charge‑pump / bypass caps:** the usual **four 0.1 µF** capacitors on C1+/C1−,
  C2+/C2−, V+ and V− per the MAX3232 datasheet, plus **0.1 µF** across VCC–GND.
  (Most eBay/Amazon "MAX3232 RS‑232 to TTL" breakout boards already have these — you
  can just use one of those boards.)
- You only need **one transmit** channel and **one receive** channel:

| Direction | TTL side (3.3 V) | RS‑232 side |
| --- | --- | --- |
| Cardputer → radio | **G2 (TX)** → `T1IN` | `T1OUT` → radio data‑in (RXD) |
| radio → Cardputer | `R1OUT` → **G1 (RX)** | `R1IN` ← radio data‑out (TXD) |

Tie **all grounds together** (Cardputer GND ↔ MAX3232 GND ↔ radio GND).

## 3. Radio connector and pinout

> Pinouts below are the common case. **Confirm against your specific radio's
> manual** — Yaesu in particular varies by model and some CAT ports are TTL, not
> RS‑232.

### Kenwood TS‑2000 / TS‑790 (DB‑9 RS‑232)

These use a standard **DB‑9** serial COM port at RS‑232 levels:

| Radio DB‑9 pin | Signal | Connect to |
| --- | --- | --- |
| 2 | radio **TXD** (out of radio) | MAX3232 `R1IN` |
| 3 | radio **RXD** (into radio) | MAX3232 `T1OUT` |
| 5 | **GND** | common ground |

- That mapping is effectively a **null‑modem** crossover: the Cardputer's TX reaches
  the radio's RXD, and the radio's TXD reaches the Cardputer's RX.
- Some Kenwood radios expect **RTS/CTS hardware handshake**. Either set the radio's
  COM port to **no flow control** in its menu, or loop **RTS↔CTS** at the radio's
  DB‑9 (**pins 7 ↔ 8**) so it sees itself as clear‑to‑send.
- **Baud:** match the radio (the **TS‑2000** default is **9600**; it's menu‑selectable).

### Yaesu FT‑847 / FT‑736R

- The **FT‑847** has a CAT jack; **verify whether it is RS‑232 or TTL level and its
  pinout** in the FT‑847 manual. If it is RS‑232 level, use the MAX3232 wiring above
  (radio data‑in ← `T1OUT`, radio data‑out → `R1IN`, common GND). Match the radio's
  CAT **baud** (commonly 4800/57600 — set it the same in CardSat).
- The **FT‑736R** uses an external CAT arrangement; consult its manual for the
  connector and levels, then apply the same RS‑232 ↔ 3.3 V translation.
- A swapped TX/RX won't damage anything (you just get no data) — but wrong **voltage
  levels** can, so confirm RS‑232 vs TTL before powering up.

## 4. CardSat settings

- **Settings → Radio / CAT → CAT type →** select **Kenwood** or **Yaesu** to match
  the radio.
- **CAT baud →** set equal to the radio's CAT baud.

## 5. Bench‑test BEFORE connecting the radio

- **Loopback the converter:** temporarily tie **`T1OUT` → `R1IN`** (RS‑232 side). Now
  anything CardSat transmits should come straight back on its RX. If the firmware ever
  echoes/round‑trips, or you watch with another tool, this proves the MAX3232 + UART +
  caps are working at the right levels before a radio is involved.
- Then connect the radio, **double‑check ground continuity and TX/RX direction**, and
  bring it up at the matched baud.

---

*Reminder: every connection in this document is **untested** and provided **as‑is**.
You assume all risk; the author is **not liable** for any resulting damage.*
