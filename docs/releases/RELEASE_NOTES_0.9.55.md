# CardSat v0.9.55 — release notes

*July 2026. Theme: printing — CardSat puts satellite data on paper.*

## Receipt printing over WiFi (TCP:9100 ESC/POS)

CardSat can now print text reports to a **network thermal receipt printer** — the
pocket, battery-powered kind hams already carry to the field. It speaks **ESC/POS
over raw TCP port 9100**, the JetDirect standard virtually every receipt printer
implements. The reference target is an **Epson TM-P20II (Wi-Fi model)**: current,
IP54, 58 mm, USB-C charging.

Design, true to this project's constraints: printing is a **thin streaming
transport** (`print.cpp`). No report is buffered whole — each opens a `WiFiClient`,
streams 32-column lines straight to the socket, and closes. The only resident cost
is a transient socket during the print, the same heap class as an ordinary fetch.
**Zero steady-state memory.**

### The reports (sixteen)

- **Passes day-sheet** — your favorites' passes for the next 24 h: satellite, AOS,
  max elevation, rise azimuth (compass octant), duration. One line each. Tear it off,
  tape it to the rig. This is the one you'll use.
- **All-favorite passes** — the full schedule across every favorite, sorted by AOS.
- **Outreach ticket** — a personalized slip for the active satellite: rise time,
  direction, the **selected transponder** (the one chosen on Track — down/up frequency
  and mode, not just the first one found), and a friendly "you can hear a satellite with
  a handheld — ask me how!" line. Print one per visitor at an AMSAT demo table.
- **Satellite card** — the active bird's transponders plus its next three passes. For
  a **linear** transponder the full passband prints (downlink and uplink low–high), not
  just the low edge, so you can see the whole slice you have to work in. A shack crib.
- **Keplerian elements** — the active satellite's elements, printed. Keps once
  *arrived* on paper; now they fit in your pocket.
- **QSO log** — your recent contacts as a paper backup (Field Day runs on paper).
- **Mutual windows** — co-visibility windows versus a remote DX grid.
- **DX Doppler** — the RX/TX table across a selected mutual window.
- **Equator crossings (EQX)** — ascending-node (or descending-node) times and longitudes.
- **Target search** — every chance to work a chosen grid/target, time-ordered.
- **Pass sky-track (polar)** — an **ASCII sky map** of a single pass's arc, drawn in
  printer-safe characters (`+` zenith, `.` horizon, `*` track) for the widest printer
  compatibility.
- **Rove plan** and **Workable horizon** — printed from the export files those
  features already write.
- **Support AMSAT** — an outreach/donation page for AMSAT tables and classrooms.
- **Operator contact card** — your callsign/name/email plus a plain-language explanation
  of amateur radio and ham satellites, to hand to a curious member of the public.
- **Note** — the note currently open in the editor or highlighted in the browser.

All times are **UTC** and labeled as such (the firmware runs in UTC). **Paper width is a
setting** — 58 mm (32 columns, default) or 80 mm (48 columns) — so an 80 mm desktop
printer and a pocket 58 mm both format correctly. On 80 mm the reports that can use the
room do: the day-sheet adds an LOS column and full-width satellite names, and the QSO log
fits each contact (sat, call, grid, RST) on a single line.

### How to print

Printing is **contextual**: press **`p`** on the screen that shows the data. The Passes
screen prints the day-sheet; **Mutual windows**, **DX Doppler**, **EQX**, and
**Target-search** each print their own table with `p`; the schedule prints **all
favorites' passes** with `P`; the rove-plan viewer's `p` prints the plan being read; and
the **Notes** browser prints the highlighted note (`p`), as does the editor (`Fn+p`). About also hosts a **Print submenu** (**`p`**) — a scrollable list of **every** report, so
the ones without a natural home screen (ticket, satellite card, Keplerian elements, QSO
log, workable horizon) are reachable on-device alongside the contextual `p` keys. Two
entries also have direct About shortcuts: **`a`** a "Support AMSAT" outreach page and
**`c`** an operator **contact card** (your callsign/name/email plus a plain-language
"what is amateur radio, and radio from space?" explainer for the public). The old
central print-menu is retired in favor of this per-screen model.

New reports since the first cut: mutual windows (with an **ASCII sky-track map** drawn
in printer-safe characters), the DX Doppler RX/TX table, equator-crossing / descending-node
tables, all-favorite passes, target-search results, notes, the AMSAT pitch, and the
operator contact card.
- **Serial console** (115200): `print passes | ticket | card | keps | log | rove |
  horizon`.
- **Settings → Network / data → Printer IP** (port defaults to 9100; blank = off).

### Print to a printer, the serial console, or a file

You do not need a printer to use printing. Every report fans out to any combination of
three sinks, chosen in Settings → Network: the **TCP:9100 printer**, the **USB serial
console** (copy-and-paste the report from your serial monitor), and an **80-column text
file** under **/CardSat/Reports** on the SD card (named per report). An operator with no printer
turns on serial and/or file and pulls the report that way; if a configured printer is
unreachable, the serial/file copies still succeed and the status line flags the printer.

### Paper widths

Paper width is a setting with four choices — **58 mm (32 col)**, **80 mm (42 col)**,
**80 mm (48 col)**, and **Font B (64 col)** — so full-size 80 mm network printers (48
columns is the ESC/POS Font A standard at 576 dots, e.g. the GZM8022) are supported
alongside the pocket 58 mm. Each sink hard-wraps to its own width, so a narrow printer
stays inside its paper while the file/serial copies keep the full 80-column layout.

### Page languages: eight formats, receipt to raster

The network printer sink speaks one of **eight page languages**, set by **Settings →
Network → Printer format**, so CardSat prints to receipt printers, office printers, label
printers, and driverless/AirPrint printers alike:

- **ESC/POS (receipt)** — the default; thermal receipt printers (TM-P20II, GZM8022), with
  centered/bold titles and a paper cut.
- **Plain text** — no control bytes at all; the universal fallback most raw-9100 printers
  accept as a basic job.
- **PCL (HP)** — HP LaserJet / OfficeJet and other PCL office printers: a reset plus a
  page-eject form-feed so the sheet actually comes out.
- **PostScript** — PostScript office printers: a minimal Courier-monospace document with
  automatic page breaks (validated to render under Ghostscript).
- **ESC/P2 (Epson)** — Epson page / inkjet / dot-matrix printers: reset, plain text, a
  form-feed to eject. *(Untested against hardware.)*
- **Star Line Mode** — networked Star thermal printers (TSP-series): reset, plain text,
  Star feed + cut. *(Untested against hardware.)*
- **ZPL (Zebra label)** — network label printers: each report line becomes a positioned
  `^FO/^FD` field inside an `^XA…^XZ` label, with a new label when one fills — so a pass
  or a QSL can print as a label. ZPL structure validated by host simulation. *(Untested
  against a physical label printer.)*
- **PWG raster (AirPrint)** — driverless / AirPrint / IPP Everywhere printers: CardSat renders
  the report to a 300-DPI grayscale page on the device and streams it over IPP. Validated
  byte-identical to the reference `ppm2pwg` encoder and **confirmed printing on a real AirPrint
  printer**.
- **URF raster (AirPrint)** — Apple Raster, for AirPrint printers that accept only `image/urf`.
  Same on-device rendering as PWG with the Apple container.

All three new languages reuse the same zero-buffer streaming path — no libraries, no
resident RAM — so they cost nothing when unused. They ship untested on real hardware; the
/CardSat/Reports file remains the safe fallback.

This answers the common "my HP has 9100 on but nothing prints" case: an HP expects PCL or
PostScript and silently discards ESC/POS. The serial and /CardSat/Reports file outputs remain plain
text regardless of format.

### And, notably: it prints to ordinary home printers too

CardSat is built first for the **WiFi receipt printer in your field bag** — that is the
intended use, and everything above is optimized for it. But the same firmware can also drive
the **driverless / AirPrint printer on a home or club network**, which is worth calling out
because of how common those are: AirPrint and its siblings (IPP Everywhere, Mopria) are
supported by the overwhelming majority of network printers sold in the last decade — the PWG
estimates IPP-based driverless printing is in **over 98% of printers sold today**. That a
Cardputer — an ESP32 with no PSRAM — can render a full page and hand it to one of these is,
as far as we know, unique among Cardputer projects.

It works because CardSat generates the printer's **raster** itself, on the device, one
scanline at a time (no full-page buffer — a few KB of RAM). Two raster formats are provided:
**PWG raster** (`image/pwg-raster`, the format IPP Everywhere requires — try this first) and
**URF raster** (`image/urf`, Apple Raster, for the AirPrint printers that accept only URF).

Because not every printer advertises every format, a **Test printer (probe formats)** action
in the printer settings asks the printer directly and shows what it accepts (e.g. "PWG URF
PDF JPEG"), so you know which format to choose before you print.

### Suggested settings by printer type

Set these under **Settings → Network** (Printer IP, Printer format, Paper width). When in
doubt on a network printer, run **Test printer** first.

| Printer type | Example | Format | Paper width | Transport / port |
|---|---|---|---|---|
| Pocket WiFi receipt (58 mm) | Epson TM-P20II | ESC/POS (receipt) | 58 mm (32 col) | Raw : 9100 |
| Desktop WiFi receipt (80 mm) | GZM8022 | ESC/POS (receipt) | 80 mm (48 col) | Raw : 9100 |
| HP office laser/inkjet | LaserJet, OfficeJet | PCL (HP) | 64 col | Raw : 9100 (or IPP) |
| PostScript office printer | many workgroup printers | PostScript | 64 col | Raw : 9100 (or IPP) |
| AirPrint / IPP Everywhere home printer | most modern home printers | PWG raster | 64 col | IPP : 631 (automatic) |
| AirPrint printer, URF-only | some HP LaserJets | URF raster | 64 col | IPP : 631 (automatic) |
| Epson page/inkjet (raw) | ESC/P2 models | ESC/P2 (Epson) | 64 col | Raw : 9100 |
| Star thermal (network) | TSP-series | Star Line | 80 mm (48 col) | Raw : 9100 |
| Zebra label printer | ZPL models | ZPL (Zebra label) | 32–48 col | Raw : 9100 |
| No printer at all | — | any | any | Serial and/or /CardSat/Reports file |

Notes: raster formats (**PWG**, **URF**) automatically use IPP on port 631 — you don't need to
change the transport setting. For office printers (**PCL**, **PostScript**) that expose IPP but
not raw 9100, set **Printer transport → IPP**. The **plain text** format is a universal
last-resort for any raw-9100 printer that ignores the others. And every report always also
goes to the **serial console** and an **/CardSat/Reports** text file if you enable those — so
you never actually need a printer to get the data off the device.

### PWG raster -- printing to AirPrint / driverless printers

Many modern home printers (AirPrint / IPP Everywhere) accept **only raster** --
`image/pwg-raster` or `image/urf` -- and have no PCL or PostScript interpreter, so they
silently discard every text format. CardSat can now drive them: **Printer format -> PWG
raster (AirPrint)** renders each report to a 300-DPI grayscale page **on the device**, a
single scanline at a time (~4 KB working RAM, no full-page buffer), and streams it over IPP.

The path was built with strict PC-side validation: the on-device encoder's output is
**byte-identical to the reference `ppm2pwg` encoder**, and round-trips **pixel-perfect**
through the independent `pwg2ppm` decoder. It was then confirmed **printing a legible page
on a real AirPrint printer**. Raster forces IPP transport (port 631); it is heavier than
the text formats, so prefer a text format where the printer supports one. A 16x32 bitmap
font (~6 KB flash) renders the monospace report text. *(On-device, this remains new -- the
encoder is validated but the full firmware path awaits bench confirmation.)*

### IPP transport (for IPP office printers)

Beyond raw port 9100, the printer sink can now use **IPP** (Internet Printing Protocol) —
**Settings → Network → Printer transport → IPP (:631)**. This is an HTTP Print-Job that
carries the selected page language (**PCL** or **PostScript**) as its document, for office
printers that expose IPP but not raw 9100. It streams via HTTP chunked transfer-encoding, so
it keeps the same zero-buffer model — no whole-document buffering. The IPP request byte
layout was validated against a real printer (which returned HTTP 200) before implementation.

**Limitation — raster-only printers are not supported.** IPP here sends PCL/PostScript; it
does not rasterize. Many modern home/AirPrint printers accept *only* `image/urf` or
`image/pwg-raster` and have no PCL/PostScript interpreter — they will **accept an IPP job and
silently discard it**. Rendering a full page to a bitmap on a no-PSRAM ESP32-S3 isn't
feasible (the framebuffer doesn't fit), so CardSat can't drive those printers; the /CardSat/Reports
file remains the fallback. A helper, `tools/ipp_probe.py`, queries a printer's supported
formats and sends test pages so you can tell before relying on it. *(On-device IPP client is
new and untested on hardware beyond the byte-layout validation.)*

### Why not Bluetooth

The obvious question. The **ESP32-S3 has no Bluetooth Classic (BR/EDR)** — BLE only —
and most sub-$40 "Android receipt printers" are Classic-SPP devices that cannot pair
with this chip at all. WiFi/TCP:9100 was therefore the correct Phase 0: it reuses the
existing network stack, costs no resident heap, and works with real current hardware.
A BLE path (for printers exposing ESC/POS over a GATT serial service) remains a
future phase, gated on an on-device heap measurement. Full analysis and the printer
survey are in `docs/design/PRINTING_SCOPE.md`.

### Fixes from bench testing

- **Printer status is accurate and self-clearing.** With serial and/or file output on, a
  print now succeeds even with no printer, and the status auto-clears after a few seconds
  instead of leaving "Printer unreachable" on screen until a key is pressed. (A bare
  `nc -l 9100` serves one connection then exits; use `while true; do nc -l 9100; done` to
  keep catching jobs.)
- **WiFi rows show the `s=scan` hint** on both the primary and secondary SSID rows, so the
  second-network scan is discoverable.
- **Polar sky maps now plot.** The mutual-window report referenced an unpopulated array, so
  it printed blank; the polar sheet moved to where the per-pass data actually lives — a
  pass's **polar screen**, `P` to print (`PR_PASSPOLAR`, also in the About print submenu).

### Scope honesty

Target-search printing was **intentionally not shipped** in Phase 0: the target hit
list is transient `.bss` populated only during a live search, so printing it outside
that flow would print stale or empty data. The `Printer` transport and the report
pattern make it a clean future addition if wanted. Graphics (a polar-plot raster) are
likewise deferred to a later phase — text covers every top report.

## Verification status

Host-side gate green: 48 files balanced (the two new `print.*` files included), full
src↔ino parity, and mirror-identity confirmed on every report builder plus the
shared helpers. **Not yet on hardware** — see `TEST_CHECKLIST_0.9.55.md`: the real
test is a physical (or emulated) TCP:9100 printer receiving each report, plus the
unreachable-printer and no-printer-set error paths. No prior-release behavior changed;
the upload instrumentation remains as a regression sentinel.
