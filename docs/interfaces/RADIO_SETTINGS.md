# CardSat — Per-Radio Settings & CAT Capability Reference

Recommended settings for each supported transceiver, and a clear breakdown of
**what CardSat can control over CAT vs. what you must set up by hand on the
radio.** Values come from the firmware's `RADIOS[]` profile table and the radios'
own CAT/CI-V command tables, cross-checked against Hamlib, OscarWatch, SatPC32,
and Gpredict behaviour.

> **Tested coverage.** The author operates an **IC-821** and has verified its
> behaviour on real hardware. The other radios are documented from their command
> tables and from how Hamlib / OscarWatch / SatPC32 / Gpredict drive them, but
> have **not** all been bench-tested with CardSat. Corrections from users with
> other rigs are very welcome — please open an issue with a serial trace.

## The one rule that matters most

**In practice today, CardSat Doppler-tunes within a band pair you have set up by
hand on the radio.** Whether the *radio* can also assign MAIN/SUB over CAT varies:
the IC-910/9100/9700 can (Select MAIN/SUB VFO, or `07 B0`/`07 D2`), but the
IC-820/821/970, FT-736R, TS-790, and TS-2000 cannot — and CardSat currently uses
band-*access* addressing on every Icom regardless. So on the older rigs especially,
selecting the uplink/downlink bands, assigning MAIN vs SUB, engaging the radio's
satellite / full-duplex mode, and setting any uplink CTCSS (PL) tone are **manual,
front-panel operations.** CardSat sends frequency (and, where supported, mode/tone)
updates *into* that layout.

The hard limit to remember: on the **IC-820/821/970** the MAIN/SUB assignment and
satellite mode are manual because **no CAT command exists** for them — not merely
because CardSat doesn't drive it. On the IC-910/9100/9700 the commands exist even
though CardSat doesn't yet use them to build the pair for you.

This is how SatPC32, Gpredict (via Hamlib), and OscarWatch drive these radios too.

## Quick reference

| Radio | Protocol | CI-V addr | Rec. baud | Reads freq? | Sat mode via CAT | CTCSS via CAT | MAIN/SUB band assignment |
|-------|----------|-----------|-----------|-------------|------------------|---------------|--------------------------|
| IC-820H | CI-V | 0x42 | 9600 | Yes* | **No** (manual) | No (manual) | **Manual only** (D0/D1 = access) |
| IC-821H | CI-V | 0x4C | 9600 | Yes | **No** (manual) | No (manual) | **Manual only** (D0/D1 = access) |
| IC-910 | CI-V | 0x60 | 19200 | Yes | Yes (1A 07)† | Yes (16 43) | CAT auto (read MAIN, swap 07 D0) |
| IC-970 | CI-V | 0x2E | 9600 | Yes* | **No** (manual) | No (manual) | **Manual only** (D0/D1 = access) |
| IC-9100 | CI-V | 0x7C | 19200 | Yes | Yes (16 5A) | Yes (16 42) | CAT-capable (07 B0 / 07 D2) |
| IC-9700 | CI-V | 0xA2 | 19200 | Yes | Yes (16 5A) | Yes (16 42) | CAT-capable (07 B0 / 07 D2) |
| FT-847 | Yaesu CAT | — | 57600 | Yes* | Yes (split/SAT) | Yes | Manual on radio |
| FT-736R | Yaesu CAT | — | 4800 | **No** | Yes | No | Manual on radio |
| TS-790 | Kenwood CAT | — | 4800 | Yes | Yes (SAT) | No‡ | Manual on radio |
| TS-2000 | Kenwood CAT | — | 57600 | Yes | Yes (SAT) | Yes | Manual on radio (A=down, B=up in SAT) |

\* Read-back: IC-821 confirmed on hardware; IC-820/IC-970/FT-847 read-back is
supported in firmware but less tested. FT-847 read addresses the known Hamlib
"always returns MAIN VFO" quirk (Hamlib issue #1286) by reading the SAT-RX VFO
explicitly.
† IC-910 sat-mode byte `1A 07` is from the IC-910 manual; **not** corroborated by
Hamlib, so treat as confirm-on-bench.
‡ Many TS-790 units — especially European TS-790E — shipped without the CTCSS
board, so tone is unavailable; CardSat treats TS-790 tone as not-CAT.

> **"MAIN/SUB band assignment" vs how CardSat addresses bands.** The IC-910,
> IC-9100, and IC-9700 *can* set up which band sits on MAIN vs SUB over CAT — the
> 910 via Select MAIN/SUB VFO, the 9100/9700 via Exchange (`07 B0`) and Send/read
> band selection (`07 D2`). The IC-820/821/970 cannot: their `07 D0`/`D1` are band
> *access* (which band a read/write targets), with no command to assign or exchange
> the pair. **CardSat itself currently uses band-access (`D0`/`D1`) addressing on
> all Icoms and expects the operator to have the MAIN/SUB pair set up**, so in
> practice you still configure the band layout on every radio today; the table
> column reflects what the *radio* supports, which matters for future automation
> and for understanding the 820/821/970's hard limit.

## Universal setup (all radios)

- **Do not rely on CAT to enter SATELLITE mode unless the table says it can.**
  On the IC-820/821/970 it cannot — engage SAT/full-duplex on the radio.
- **Set up the band pair and MAIN/SUB on the radio first**, then start tracking.
- **Match the baud rate** on both ends. CI-V address must match for Icom.
- **Leave CI-V Transceive OFF** (Icom) — CardSat polls with its own read command.
- **VFO type** for the Icom sat rigs is Main Up / Sub Down (uplink on MAIN, downlink
  on SUB); this is CardSat's default.

## Per-radio notes

### IC-820H / IC-821H — manual-mostly
CAT does: set/read frequency per band, set mode, address MAIN vs SUB (band-access
`D0`/`D1`), VFO A/B. You must do on the radio: **enter satellite mode** (the CI-V
sat-mode command is a no-op — confirmed on an IC-821), **assign which band is MAIN
vs SUB**, **set CTCSS/PL tone**. The `D0`/`D1` bytes choose which band a read/write
*targets*; they do **not** move a VFO onto MAIN/SUB. The IC-820's band-access bytes
are **reversed** relative to the IC-821 (820: Sub=`D0`, Main=`D1`; 821: Main=`D0`,
Sub=`D1`) — CardSat's profiles encode this.

### IC-970 — same family as 820/821
Frequency/mode/band-access over CAT; satellite mode, MAIN/SUB assignment, and tone
are manual. No CAT satellite-mode command. Least bench-tested of the Icoms here.

### IC-910 — richer CAT
Adds CAT control of **Select MAIN VFO (`07 D1`) / Switch MAIN↔SUB (`07 D0`)**,
satellite mode (`1A 07`), subaudible tone (`16 43`), and RIT (`1A 06`). Recommended
19200 baud. (Sat-mode byte unconfirmed vs Hamlib — see note †.)

**CardSat orients MAIN/SUB automatically** on the 910 when radio control is turned
on for a two-way transponder: it reads MAIN's frequency (`07 D1` then `03`) and, if
MAIN is on the wrong band, issues one **swap (`07 D0`)** — since the 910's MAIN and
SUB can never share a band, that one check fixes both legs. This mirrors how Hamlib
drives the 910. Fired once at engage. **UNTESTED on hardware** (author has no 910).

> **910 profile note.** Earlier firmware had the 910's MAIN-select byte set to
> `07 D0`, which is actually the *swap* command, not "select MAIN" — so band
> addressing could be inverted. This is corrected (`selMain = 07 D1`). The 910's
> addressed-SUB path remains the least-verified part and should be confirmed by a
> 910 owner. As a Hamlib developer put it, "SAT mode on the IC910/IC9100 is very
> different from other Icom CAT philosophy and not straightforward to control."

### IC-9100 / IC-9700 — fullest CAT
Frequency/mode/read-back, **MAIN/SUB band assignment over CAT** — Exchange MAIN/SUB
(`07 B0`) and Send/read main/sub band selection (`07 D2 00`/`01`) — satellite mode
(`16 5A`), CTCSS (`16 42`), RIT. The IC-9700 additionally supports **Icom LAN**
(network CAT, RS-BA1) — the IC-9700 is the intended target among the supported set (the
LAN path is confirmed controlling an IC-705, but only the IC-9700 has true satellite mode).
Least manual intervention.

**CardSat sends the band assignment automatically** on these two rigs: when radio
control is turned on for a two-way transponder, it issues `07 D2 00 <band>` /
`07 D2 01 <band>` once so the uplink and downlink land on the correct bands
(2 m / 70 cm / 23 cm) per the VFO-type setting — you don't have to pre-arrange
MAIN/SUB. It fires once at engage (never per tick) and is a no-op on every other
radio. **This path is UNTESTED on hardware** (the author runs an IC-821, which has
no band-assignment command); confirm it on a 9100/9700 and report back. If a leg is
outside 2 m/70 cm/23 cm the assignment is skipped and you set that band manually.

### FT-847 (Yaesu CAT)
CAT satellite mode (via split/SAT), Main/Sub SAT VFOs, Doppler, and uplink CTCSS.
CardSat reads the SAT-RX (downlink) VFO explicitly to avoid the documented Hamlib
"defaults to MAIN VFO" behaviour (#1286). Recommended 57600 baud. Band pair is
still set up on the radio.

### FT-736R (Yaesu CAT)
CAT can drive frequency and satellite mode, but **cannot report frequency** (no
read-back — `canReadFreq` is false). Operate it push-only / open-loop: CardSat
Doppler-tunes, you trim with the radio. No CAT tone. 4800 baud.

### TS-790 (Kenwood ASCII CAT)
Full-duplex satellite radio; `FA;`/`FB;` read the VFOs, `MD` sets mode. **Band
pair and SAT setup are manual.** CTCSS often absent in hardware (esp. TS-790E), so
treated as not-CAT. 4800 baud. (Note: some TS-790 units mute audio briefly when
CAT switches between the VHF/UHF VFOs — a known radio-side trait.)

### TS-2000 (Kenwood ASCII CAT)
In the radio's satellite mode, **VFO B is always uplink and VFO A is always
downlink**, and the band pair must be configured on the rig before tracking (the
`FR` VFO-select command is rejected in SAT mode — see the Hamlib/Gpredict TS-2000
thread). Full-duplex SSB/CW is only available in the radio's SAT mode. CAT does
Doppler + tone; the layout is manual. 57600 baud.

## How other software treats these radios (cross-reference)

- **Hamlib**: the IC-821 backend (`ic821h.c`) declares no MAIN/SUB or satellite
  mode (`/* FIXME: What about MAIN/SUB mode? And satellite mode? */`); FT-847 has
  the MAIN-VFO read quirk (#1286); TS-2000 `set_vfo` is disabled in SAT mode.
- **OscarWatch**: supports IC-910/9100/9700, FT-847, TS-2000 for full automation;
  lists the IC-821 as "Satellite Main/Sub only (no split CAT) … uplink tone manual
  on radio" — i.e. it does **not** fully automate the 820/821, matching this guide.
- **SatPC32 / Gpredict**: drive all these rigs by Doppler-tuning a band pair the
  operator has set up on the radio; they do not assign MAIN/SUB or engage SAT mode
  on the older rigs either.

## Recommended CardSat-side defaults

| Setting | Default | Notes |
|---------|---------|-------|
| CAT update rate | 500 ms | adjustable in 10 ms steps; ~200 ms works well on 9100/9700 |
| Linear (SSB/CW) write deadband | 50 Hz | mode-aware, tightens near TCA |
| FM write deadband | 300 Hz | mode-aware, tightens near TCA |

CardSat's write deadband is mode-aware and TCA-adaptive — the base values tighten
automatically as Doppler slew increases near closest approach. (For reference,
OscarWatch's independently-chosen defaults are FM 350 Hz / SSB-CW 50 Hz — the
SSB/CW value is identical.)
