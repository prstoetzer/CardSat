# Printing from CardSat — report candidates, printers, and interfaces

> **STATUS (v0.9.55): SHIPPED, and expanded well beyond the Phase 0 below.** Printing is
> implemented as a three-sink engine (network **ESC/POS printer** over TCP:9100, **USB
> serial console**, and 80-column **/CardSat/Reports/*.txt** file), with **sixteen reports**,
> contextual per-screen `p` keys plus an About print submenu, selectable paper width
> (58/80mm/Font B), and ASCII polar sky maps. This document is retained as the original
> design rationale — the Bluetooth analysis in particular still governs why WiFi/serial/file
> were the right sinks. See RELEASE_NOTES_0.9.55 and the manual for the shipped behavior.*

*Original scoping note. Evaluates what would be worth printing on a
receipt-class thermal printer, which portable battery printers fit, and — the part
that actually decides everything — how each would interface with a no-PSRAM
ESP32-S3 whose UART belongs to the radio.*

## Verdict up front

Worth doing, phased. **Phase 0** is ESC/POS *text* over **WiFi (raw TCP port
9100)**: it reuses the existing network stack, costs essentially zero resident
heap, and works with real, current, battery-powered hardware (Epson TM-P20II
Wi-Fi). **Bluetooth is the popular path but the constrained one**: the ESP32-S3
has **no Bluetooth Classic** — BLE only — which silently disqualifies most $30
"Android receipt printers" (they're Classic SPP), and a BLE stack costs tens of
KB of heap on a part where that number is the project's whole history. BLE gets
the SSH treatment: **an on-device heap spike before any promise.** A wired TTL
printer module is the maker path, with honest pin-conflict and power caveats.

---

## 1. What's worth printing (ranked)

The receipt format — narrow, sequential, tear-off, pocketable — fits some CardSat
data uncannily well. At 58 mm, ESC/POS Font A gives **32 columns**, so every
report needs a 32-col formatter (CardSat's screen text runs 38–39; a small
dedicated formatting layer, not a reuse).

1. **Today's passes day-sheet** — the killer app. The favorites schedule for the
   next N hours: sat, AOS local time, max el, AOS→LOS az, duration. Tear it off,
   tape it to the rig, take it to the hill. One-line-per-pass fits 32 cols
   naturally; a crude elevation bar in block characters costs nothing.
2. **Rove plan sheet** — the saved what-if schedules already live as files
   (`/CardSat/rove/`); printing one is the paper artifact a rover actually wants
   in a pocket at a park with no phone dependence.
3. **Outreach pass ticket** — a personalized slip for demos and classrooms:
   *"AO-91 tonight 19:42, rises SW, 6 minutes, 145.960 FM — you can hear a
   satellite with a $30 radio."* Printed per visitor at an AMSAT table. Small
   firmware, outsized education value; pairs with the Learn corner.
4. **Workable-horizon checklist** — the 10-day states/DXCC/grids union as a
   tick-box list. Receipt printers and pen-checkable lists are soulmates.
5. **Satellite card** — one bird: transponder lines + next three passes. A
   per-satellite shack crib.
6. **QSO log backup** — last N QSOs, or one pass's QSOs, as a paper trail
   (Field Day culture runs on paper). ADIF stays the real export; this is
   belt-and-suspenders.
7. **Target-search hit list** — every chance to work one place, in time order.
8. **Charm tier** — a Keplerian-elements printout (Keps used to *arrive* on
   paper; pure nostalgia) and copies of the CubeSatSim C2C card as classroom
   handouts.

Graphics (the polar plot as an ESC/POS raster) are genuinely possible — 384-dot
lines, a small 1-bit renderer — but they belong in **phase 2**: text covers the
top candidates, and raster is where the cheap-printer protocols get weird.

## 2. Interfaces, honestly ranked for THIS device

**(a) WiFi, raw TCP port 9100 — recommended Phase 0.** Open a `WiFiClient` to
the printer, send `ESC @`, plain ASCII lines, feed, close. Transient socket only
— the same heap class as an existing fetch, no new libraries, no new pins.
Requires a printer that joins the WLAN (or Wi-Fi Direct). This is the path where
"interfaces well" is simply true today.

**(b) BLE — the decisive constraint stated plainly.** The **ESP32-S3 has no
Bluetooth Classic (BR/EDR)** — BLE only. The ubiquitous $25–40 58 mm printers
that say "Android/Windows, not iOS" are saying *Classic SPP*, and **cannot pair
with this device at all.** A BLE path needs either (i) a printer exposing
ESC/POS over a BLE GATT serial service — some do; it is a per-unit,
verify-before-buying property — or (ii) the BLE "cat printers"
(GB01/Phomemo-class), which pair fine but speak **raster-only**
reverse-engineered protocols (no text mode → needs the phase-2 bitmap engine).
And NimBLE itself costs tens of KB of heap: on this no-PSRAM part, **an
on-device spike (stack up + connected + largest-block measured, WiFi in its
usual state) gates the whole path** — the SSH rule. Mitigation if tight: print
is a modal moment; a print mode that briefly parks other consumers is
acceptable UX.

**(c) Wired TTL printer module (QR701/CSN-A2, Adafruit-mini lineage) — the
maker path.** Real caveats, stated: the Grove UART **is CAT's** (G1/G2), so a
printer there means unplugging the radio, and the alternate UART2 pins
(G15/G13) are the GPS options' home; these modules want **5–9 V at ~1.5–2 A
print bursts**, which the Cardputer cannot source — a separate pack is
mandatory; and levels need shifting — pleasingly, the **same ProtoSupplies
module** from the new interface docs does it. Workable for a builder, not the
recommendation.

**(d) USB host — out.** The USB-C port is the flashing/CDC port; host-mode
printing is complexity without a constituency.

## 3. Printer candidates (verified July 2026)

| Printer | Power | Link | ESC/POS | Fit |
|---|---|---|---|---|
| **Epson TM-P20II, Wi-Fi model** | battery, ~15 h (Wi-Fi) | Wi-Fi 5, TCP 9100 / ePOS | text + raster | **The anchor.** Current, IP54, 58 mm, USB-C charging, drop-rated. The one where Phase 0 just works. Pro-priced (~$250–300 class). |
| **Epson TM-P20II, BT model** | battery, ~27 h | Bluetooth 5.0 **classic *and* BLE** (explicitly) | text + raster | The credible BLE target if that path is built — BLE is documented, not luck. |
| **PT-210-class 58 mm portables** (GOOJPRT etc.) | battery, 2000 mAh | usually **Classic SPP** ⚠ | text | ~$25–40. **Only compatible if the specific unit exposes BLE serial** — many don't. Buy-with-verification, or pair via nothing at all. |
| **"Cat printers" / Phomemo-class** | battery | BLE ✓ | **raster only** ⚠ | Pair fine; no text mode. Phase-2 material once a bitmap engine exists. |
| **Bixolon SPP-R210** | battery | BT/WLAN | text | **Discontinued June 2025** — noted so nobody buys one new; successors exist but weren't evaluated. |
| **QR701/CSN-A2 TTL modules** | external 5–9 V pack | UART TTL | text-ish | The soldering path; §2(c) caveats apply. |

## 4. Firmware scope sketch (for whenever it's built)

A small `print.cpp`: report formatters (32-col builders for the ranked list) +
a trivial ESC/POS text helper (`init`, bold, feed) + **one transport for Phase
0**: `printTcp9100(host, text)` on the existing `WiFiClient`. Settings: printer
host (+port). UI: a print action on Passes and on a saved rove plan covers
candidates 1–2; the serial console gets `print passes` for free scripting. Paper width
is a setting (58 mm/32 col, 80 mm/48 col) so both printer classes format correctly. Flash
cost small; **resident heap cost zero** (transient socket + a line buffer).
Phase 1 = BLE *after and only after* the heap spike passes. Phase 2 = raster
(polar plot; cat-printer protocol) if ever.

## 5. Open questions

Which screens get a print key (key budget is real estate); 80 mm / 48-col
support (trivial flag if a TM-P80II-class shows up); whether the outreach
ticket wants a QR code (ESC/POS QR is one command on real ESC/POS printers —
charming for linking AMSAT.org); and the BLE heap number, which only the device
can answer.

*Printer facts verified against Epson/Bixolon product pages and current
marketplace listings, July 2026 (TM-P20II specs incl. per-model battery life and
BLE support; SPP-R210 discontinuation notice). The ESP32-S3's lack of Bluetooth
Classic is an Espressif hardware fact and the load-bearing constraint of §2(b).*
