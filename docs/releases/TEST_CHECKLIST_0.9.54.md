# CardSat v0.9.54 — on-device test checklist

*Flash, then work top to bottom. Items marked (regression) guard 0.9.53 behavior.*

## Boot & regression

- [ ] Boot log shows `CardSat v0.9.54`; About screen agrees.
- [ ] AMSAT source: satellite count unchanged from 0.9.53 (no truncation message —
      the AMSAT list is well under 150).
- [ ] (regression) Workable horizon and Target search both run to completion.
- [ ] (regression) One LoTW upload batch completes; `[lotw] after POST` heap line
      shows the largest block recovering as in 0.9.53.

## Favorites-first loading & truncation status

- [ ] Settings → GP source → CelesTrak → pick a group known to exceed 150
      (e.g. **SatNOGS** or **Amateur Radio**; **GNSS** if you want a big one).
- [ ] Update: status line shows **"Loaded 150 of Y (cap 150)"**; serial log shows
      `[gp] parsed 150 of Y satellites (truncated; favorites kept)`.
- [ ] `sats` on the serial console reports the same loaded/seen numbers.
- [ ] Favorite any loaded bird (Satellites → `f`), refresh the same source,
      confirm it is still present after the reload.
- [ ] **Past-150 survival**: from the full `gp.json` on the SD card, find a NORAD id
      that appears *after* the 150th object; add it to `favs.txt` (one id per line, or
      favorite it if visible); refresh; confirm it now loads and is tracked.
- [ ] Switch back to AMSAT; confirm the truncation message disappears and the
      catalog reloads normally.
- [ ] Reboot with the oversized source cached: boot status shows
      **"Cached GP: 150 of Y"** and favorites are present (offline path).

## Download preflight

- [ ] On an **internal-flash (no SD) unit**: select a very large group
      (e.g. **Active** or **Starlink**) and Update. Expect
      **"file too big for storage (XKB > YKB free)"**, no partial write, and the
      previous catalog still loading on reboot.
- [ ] On an SD unit: the same group downloads (or fails only on link quality),
      i.e. the preflight does not false-positive with SD free space.

## USB serial console

- [ ] Serial monitor at **115200**: `help` prints the command list.
- [ ] `ver` shows 0.9.54 + build date; `heap` shows plausible free/largest values.
- [ ] `sats`, `fav`, `net` all report sensible values; `next` returns the active
      satellite's next pass (or a clear message when no sat / clock unset).
- [ ] Unknown input (e.g. `zzz`) answers `unknown: zzz (try 'help')`; console
      does not interfere with normal UI operation while connected.
- [ ] `time`, `gps`, `bat`, `fs`, `up` all answer sensibly; `pass ao-91` (name
      fragment) and `pass <NORAD>` both resolve; `pass` alone prints usage.

## CubeSatSim C2C reference

- [ ] Tools hub shows **32** entries; **CubeSatSim C2C ref** sits after
      *State vector → GP*.
- [ ] Screen renders (orange section heads, grey detail), `;`/`.` scroll through
      all lines, `` ` `` returns to Tools.
- [ ] The form tools *after* the new entry still open correctly (dispatch indices
      shifted by one — spot-check **Coax loss / power** and **RC/RL time constant**,
      the first and last forms).

## AMSAT Fox anatomy

- [ ] Help (`h`) lists **a  AMSAT Fox anatomy**; `a` opens it.
- [ ] Cube spins smoothly (~15 fps); whip antennas rotate *with* the body.
- [ ] Callouts auto-advance ~4.5 s; `,`/`/` cycle them; leader line tracks the
      rotating anchor; all seven render inside the right column.
- [ ] Space pauses/resumes the spin; `;`/`.` rotate manually (and stop auto-spin).
- [ ] `` ` `` returns to Help; Orbit zoo (`o`) still behaves as before (shared tick).
- [ ] `i` on the anatomy opens the **Fox & CubeSats** primer; scrolls fully;
      `` ` `` returns to the (still-spinning) animation.
- [ ] Help `c` opens the **CubeSat Simulator** intro; scrolls fully; `` ` ``
      returns to Help.

## Global hotkeys

- [ ] DXCC lookup: typing **HB9** lands all three characters in the query (no
      Help / screenshot hijack); the character lookup accepts `h` and `b`.
- [ ] Tools list: `b` jumps to the **Battery** tool; neither `b` nor `h` fires the
      global action there.
- [ ] `h` still opens Help from Home, Track, and Passes; `b` still screenshots there.
- [ ] Help hub: GLOBAL block lists only true globals; the HELP TOPICS block follows;
      WORKABLE HORIZON / TARGET SEARCH / ROVE PLANS / CAT MONITOR sections are
      present and their key lines match the on-screen footers.

## Documents

- [ ] `CardSat_Manual.pdf` opens at v0.9.54; §14 has the *Oversized sources*
      paragraph; §20 has the preflight and serial-console entries.
- [ ] Cheat card still 2 pages, stamped 0.9.54.
