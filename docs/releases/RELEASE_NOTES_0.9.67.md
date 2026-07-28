# CardSat v0.9.67 — release notes

A field-tools release. The headline is a **Telnet client** in the Tools menu — a line-oriented
network terminal for reaching another host from the same handheld while you're out operating: a
rotator controller, an APRS box, a DX-cluster node, or any raw-TCP service on a network you
trust. Alongside it, a round of bench-driven fixes to the live feeds and the weather and
overhead screens, and a corrected DX-cluster spot layout that now shows the spotter and the spot
comment the right way round.

Everything here is built and gate-checked (13 static gates plus the host harnesses). The Telnet
terminal, its printer path, and the DX/weather/overhead fixes were exercised on the device across
several rounds this cycle.

A note on scope, stated plainly: an **SSH** client was designed alongside the Telnet client and
then **shelved on memory grounds**. An SSH handshake needs a large single contiguous heap
allocation that does not fit reliably next to CardSat's resident display and state on this
no-PSRAM part (the largest contiguous block measured ~31 KB on the bench; SSH wants 16–32 KB in
one piece). Telnet needs no crypto, no handshake, and no large contiguous buffer, so it runs with
margin. The terminal is transport-agnostic, so SSH can be added later behind a runtime memory
guard if the contiguous-heap picture improves. See
`docs/design/TELNET_SHIPPED_SSH_SHELVED_0_9_67.md` for the full reasoning, including why the
8 MB partition (which frees flash, not DRAM) is not the lever that unblocks SSH.

# New

### Telnet client (Tools › Calculators & programming › Telnet)

A line-oriented terminal for a rotator box, APRS box, DX node, or any raw-TCP/Telnet host on a
trusted LAN. It deliberately is **not** a full VT100 emulator: remote ANSI/CSI/OSC escape
sequences are stripped rather than interpreted, and output is drawn as a scrolling character
grid, so shells, `ls`, config edits and log reads are readable while full-screen TUIs (vi, top,
tmux) are not the target.

- **Saves up to 10 connections** on the SD card (or internal flash if no card), each with a
  label, host, port, and an output mode. Add/edit/delete from the connection list; **ENTER**
  opens the highlighted connection.
- **Setup** walks label → host → port → output (screen / printer / both). Output defaults to
  screen only; the printer is opt-in.
- **In the terminal:** type to send; **ENTER** sends the line (CR LF). **Ctrl+key** sends control
  characters (Ctrl-C/D/Z, etc.). **Fn+;./,/** send arrow keys, **Fn+1..0** send F1–F10, and
  **Fn+`** sends Escape — all to the remote host. **Opt** is the local modifier: **Opt+;/.**
  scroll the buffer, **Opt+c** clears, **Opt+r** reconnects, **Opt+1/2/3** switch the output
  between screen / printer / both, and **Opt+`** exits. No Fn combo ever leaks to a CardSat
  global while the terminal is open.
- **Printer output** streams sanitized lines to your configured printer (Settings › Network)
  through every sink **except IPP/raster** — a live line stream can't rasterize a page, so IPP
  transport and the PWG/URF raster formats fall back to screen-only with a notice. **Opt+2/Opt+3**
  open the printer on demand mid-session.
- **Plaintext and unauthenticated:** Telnet carries no encryption and no login. Use it only on
  networks you trust. The connection list carries a plaintext-trust banner, in the same spirit as
  the mobile-web-control and LoRa features.

### DX cluster: comments, and the spotter shown correctly

Each DX spot now shows its **comment** on a second line beneath the spot (mode, signal report,
notes), with the cursor stepping spot-to-spot so scrolling skips the comment lines. The spot line
itself now reads correctly: the **DX** (spotted station) and the **spotter (de)** were swapped in
0.9.66 — the live HamQTH `dxc_csv.php` feed puts the spotter first and the spotted DX in the third
column, which the feed's own Country column confirms. Both are now mapped the right way round, and
the comment prints on the paper report too.

# Fixed

- **Weather times are now UTC**, consistent with the rest of CardSat. The local-weather fetch
  requested the site's local timezone, so sunrise/sunset and the hourly cloud/pressure series
  read in local time while every other clock in the app was UTC. Sun times now show and print
  with a `UTC` label.
- **Overhead-now** no longer overlaps its last row with the "N up / M scanned" count line — the
  visible list is eight rows, leaving the count and footer clear.
- **DX cluster spotter/DX swap** (see New, above) — corrected and verified against the feed's
  Country column as ground truth.

# Documentation

- The printable **cheat card** moves from a 4×6 to a **5×7** landscape index card
  (`CardSat_CheatCard_5x7.pdf`). The reference had grown past what fits legibly on two 4×6 sides;
  the larger card restores a clean **two-page** front/back layout with slightly larger type.
  Regenerate with `python3 tools_make_cheatcard.py`.

# Known limitations

- The Telnet terminal is line-oriented; full-screen TUIs won't render correctly (by design).
- SSH is not included this release (memory; see above).
- Weather remains an F-region-free surface forecast from Open-Meteo; nothing here changes its
  accuracy, only its timezone.
