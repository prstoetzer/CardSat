# CardSat v0.9.55 — on-device test checklist

*Flash, then work top to bottom. Printing needs a real ESC/POS TCP:9100 printer on
the same WiFi (an Epson TM-P20II Wi-Fi, or any 9100 receipt printer), or a host-side
`nc -l 9100` capturing the byte stream to eyeball the ESC/POS.*

## Regression (nothing printing-related touched these)

- [ ] Boot shows **v0.9.55**; normal AMSAT boot, favorites, and manual sats intact.
- [ ] One LoTW upload batch still completes (heap sentinel line recovers).
- [ ] Passes screen still tracks, opens pass detail, and the other letter keys
      (`d`/`g`/`w`/`e`/`v`/`x`) behave — `p` prints the day-sheet directly; contextual print keys are the new bindings.

## Print output setup (Settings -> Network / data)

- [ ] **Printer IP** and **Printer port** rows present; IP entered, port shows **9100**;
      both persist across reboot.
- [ ] **Printer paper** cycles **58mm (32) -> 80mm (42) -> 80mm (48) -> Font B (64)** and
      persists. On 80 mm the rule lines widen; the **day-sheet** gains an LOS column with
      full-width names; the **QSO log** puts each contact on one line.
- [ ] **Print to serial** and **Save to /CardSat/Reports** toggle on/off and persist.
- [ ] Both SSID rows show the **`[s=scan]`** hint. `s` on **WiFi 2 SSID** scans and stores
      into the WiFi-2 slot (header reads "WiFi scan (net 2)"); `s` on WiFi 1 stores WiFi 1.

## The three sinks (item 5/6)

- [ ] With **only serial** on (no printer IP): a print streams the report to the USB serial
      console (115200) and the status does **not** say "unreachable".
- [ ] With **only file** on: a print writes `/CardSat/Reports/<report>-<n>.txt` (80 columns);
      confirm it appears and reads correctly when pulled off the card.
- [ ] With a printer **and** serial/file: all enabled sinks receive the report. A quick
      printer emulator: `while true; do nc -l 9100 | cat -v; done` (a bare `nc -l 9100`
      serves one job then exits).

## The reports -- print each and eyeball it (item 9/10/11)

Contextual `p` (or `P`) on the screen, plus every report from **About -> `p` (Print submenu)**
and from the serial `print <name>` command. Times are UTC.

- [ ] **Passes day-sheet** -- `p` on Passes / `print passes`: station grid + UTC window,
      one line per favorite pass (SAT / UTC / EL / AZ octant / MIN), sorted by AOS.
- [ ] **All-favorite passes** -- `P` on the schedule / `print allpass`.
- [ ] **Outreach ticket** -- `print ticket` (active sat): rise time/direction, then the
      **selected** transponder (cycle it on Track, reprint, confirm the ticket follows);
      linear transponders show the down/up passband as a low-high range.
- [ ] **Satellite card** -- `print card`: transponders (linear = low-high passband) + next
      three passes.
- [ ] **Keplerian elements** -- `print keps`.
- [ ] **QSO log** -- `print log` (empty log prints "(log empty)").
- [ ] **Mutual windows** -- `p` on Mutual / `print mutual`: window table.
- [ ] **DX Doppler** -- `p` on DX Doppler / `print dxdopp`: RX/TX table across the window.
- [ ] **EQX** -- `p` on EQX / `print eqx`: crossing times + longitudes.
- [ ] **Target search** -- `p` on Target-hits / `print target`.
- [ ] **Pass sky-track (polar)** -- open a pass -> its **polar screen** -> **`P`** /
      `print polar`: an ASCII sky map (`+` zenith, `.` horizon, `*` the arc) plus a
      "Peak el N at az M" line. **This is the newest/most experimental -- check first.**
- [ ] **Rove plan** -- `p` in the rove-plan viewer prints the viewed plan; `print rove`
      prints a fresh survey export.
- [ ] **Workable horizon** -- `print horizon`.
- [ ] **Support AMSAT** -- `a` on About / `print amsat`.
- [ ] **Operator contact card** -- `c` on About / `print contact`: your callsign/name/email
      (set Operator name/email in Settings) + the ham/satellite explainer.
- [ ] **Note** -- `p` in the Notes browser (highlighted note) or `Fn+p` in the editor /
      `print note`.
- [ ] Every report ends with a paper feed so the last line clears the tear bar; a
      cutter-equipped printer cuts (cutter-less mobiles ignore it).

## Error paths & status (item 2 fix)

- [ ] No output enabled at all: a print says **"No print output on (Settings>Network)"**.
- [ ] Printer IP wrong/absent but serial or file on: the report still goes there and the
      status notes the printer without calling it a failure; the status **auto-clears**
      after a few seconds (no lingering "unreachable" until a key press).
- [ ] `print` with an unknown name prints the usage lines over serial.
- [ ] A report needing an active satellite, with none selected, prints a one-line note
      rather than misbehaving.

## Footers

- [ ] No footer overflows the screen edge (all <= 39 chars). Spot-check About, the report
      screens, and the pass polar screen.

## Documents

- [ ] Manual PDF opens at **v0.9.55** with the Printing section; cheat card is 2 pp / 0.9.55.
