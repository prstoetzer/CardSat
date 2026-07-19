# CardSat — What's Verified, and What to Check on the Air

*Companion document: **[ROADMAP_TO_1.0.md](ROADMAP_TO_1.0.md)** tracks what stands between the
current release and 1.0 — the hardware-verification gap below is the largest single blocker.*

CardSat is developed and tested host-side (x86 logic simulations plus brace/parity
checks); the firmware author flashes and confirms behavior on real hardware. This page
records what is confirmed on the Cardputer ADV versus what still needs verification
against real radios and rotators.

## Confirmed working on hardware

Display and keyboard, GP download + streaming parse, SGP4 pass prediction, the polar /
pass-detail / mutual-window screens, GPS (auto-refresh on fix / satellite-count
change), the AOS alarm and speaker, deep sleep, the visual-pass / decay /
Sun-Moon-transit / per-satellite-note features, and the offline GP/transponder caches.

**v0.9.52–0.9.53 additions confirmed:** on-demand speaker power (audio buffers up only while
sound plays, released after — including game exit via any path), the **4bpp display sprite**
(colors verified unchanged on hardware), and the **multi-batch LoTW upload fix** — three
back-to-back signed uploads with zero send stalls, confirmed via the on-device heap log
(largest block recovering to its ceiling before every batch). The one 0.9.53 addition **not**
yet exercised on hardware is multi-file download from a phone browser (the Files page's
sequenced downloads; browser behavior varies by platform).

**Added in 0.9.54, not yet on hardware:** favorites-first loading and the "Loaded X of
Y" truncation status with an oversized CelesTrak group; the storage preflight refusal
("file too big for storage") on an internal-flash unit; the USB serial console
(115200: help / ver / heap / sats / fav / next / net); and the Tools → CubeSatSim C2C
reference screen (render, scroll, backtick exit); and the Help → `a` AMSAT Fox
anatomy animation (spin smoothness, callout cycling, leader tracking, Orbit-zoo
regression on the shared 66 ms tick), plus its two companion text screens (`i`
primer from the anatomy; Help `c` Simulator intro).

**Printing (0.9.55, updated):** the **PWG/URF raster + IPP (port 631) path is
confirmed on a real AirPrint printer** — on-device raster generation, the chunked
IPP transport, and the capability probe all work against real hardware. Still to
verify: the **raw TCP:9100 receipt path** (ESC/POS and the other page languages)
against a physical receipt printer — the report *content* (now **29 reports**) is
host-validated and prints over serial/file, but no physical 9100 printer has
confirmed the wire path yet — plus the Settings printer IP/port persistence and the
error paths (unreachable printer fails fast; blank IP reports "No printer set").

**Added in 0.9.57, not yet on hardware:**

- ~~**The BASIC heap fix**~~ — **CONFIRMED ON HARDWARE (0.9.57).** A runaway program's output
  buffer is released on leaving BASIC; the largest contiguous block returns to its pre-run value
  and the LoTW upload this was breaking now succeeds. (Worth knowing for the future: the *first*
  attempt at this fix shipped and did nothing, because it was written against a hand-authored
  model of Arduino's `String` rather than the real one. Only destruct + placement-new frees a
  `String`'s buffer — every assignment form keeps the capacity.) Still worth a glance when
  convenient: that the buffer is *not* dropped where it shouldn't be — run a program, scroll the
  console, `` ` `` back to the editor, and the output should still be there.
- **The keyboard changes** — `h` and `b` now type on the BASIC editor, scientific calculator and
  LoTW passphrase prompt; **`Fn`+`h`** / **`Fn`+`b`** reach Help and screenshot from *every*
  letter-consuming screen (including Tools and the DXCC/char lookups, which previously could not
  screenshot at all); `Fn`+`shift`+`b` still types a capital B; and `Fn`+DEL no longer fires the
  emergency stop while editing in BASIC or the calculator.
- **BASIC system data** — the bare names (`SATAZ`, `AOSIN`, `SFI`, `MOONEL`, …) on real data, the
  `…OK` flags, and that a missing-data read halts with the right message. Host-validated against
  the shipped resolver; the on-device check is that the snapshot matches what the screens show.
- **The nine new reports on paper** — EME (`Fn`+`p`), EME 30-day plan, EME mutual Moon, QRZ,
  readiness, awards, workable states/DXCC lists, visible passes. Widths are checked against 32-col
  (58 mm) paper on the host, but the real test is the printer.
- **LTAN in the orbit report** — cross-checked to the minute against an independent solar-position
  implementation; confirm it matches the Nodal page on the device.
- **The Help screen corrections** — the Satellites topic previously named the wrong key
  ("ENT toggle favorite"; ENTER opens Passes, `f` is the toggle). Worth a read-through on the
  device against the footers.

**Added in 0.9.56, not yet on hardware:** the **Tiny BASIC** editor and console (typing a program
on the Cardputer keyboard, `Fn`-modified commands, cursor movement including `Fn`+up/down by line,
save/load to `/CardSat/basic/*.bas`, the run → console → back flow, and the empty-editor hint);
the **graphing calculator**'s rendering (curve density at 240×105, axis and window-readout
legibility, pan/zoom feel); the **location converter** screen (field navigation, scrolling the
derived list, `s`-to-QTH, and whether the widest lines — UTM and USNG — fit the 240 px width);
the **four new printable reports** (orbital analysis, illumination, 10-day, 6-hour timeline) on a
real printer, especially the **ASCII renderings**, whose character-cell aspect ratio can render
differently across printer fonts (the 80 mm layouts were sized by calculation, not by eye); the
**tool printing** keys (`p` / `Fn`+`p`) behaving correctly — nothing should type when it should
print; the **global emergency stop** (`Fn`+back) disengaging control from each operating screen;
and the **memory diagnostics** (`mem`, `memtrace`) output.

The interpreter *logic*, the coordinate projections, and the report *content* are host-validated
(see [ROADMAP_TO_1.0.md](ROADMAP_TO_1.0.md) §4 for what that does and doesn't mean); what needs
hardware is the **display, input, and printer output**.

**EspUsbHost v2.3.1+ compiles CardSat clean, unpatched (verified 0.9.59)** — the
`peripheral_map` fix landed upstream in v2.3.1; a pristine v2.3.2 checkout builds
the full monolith on arduino-esp32 3.2.1 (85% app partition), the 2.3.0→2.3.2 diff
audit found the whole API surface CardSat uses unchanged, and `end()` is untouched
(the resident-host design still applies). Still worth one bench engage after a
library *update*: the changed serial-path behaviors — deferred unplug recovery and
interface-number-pinned binding — should be invisible on a single-interface FTDI,
but "should" is the word this file exists for.

**CAT over USB is confirmed on an IC-821 + FTDI adapter (0.9.58)** — engage,
disengage, re-engage, and full Doppler tracking, over many cycles. The console does
not return while USB is engaged (resident host, by design); diagnostics land in
`/CardSat/Logs/usb.log` and, when enabled, `console.log`. Since **0.9.59 USB CAT is
part of the default build** (`CARDSAT_HAS_USBCAT=1`).

**Single-pin CI-V is confirmed on an IC-821** — the full bidirectional CI-V exchange
(frequency reads and ACKs) works over one shared open-drain GPIO, including **Doppler
compensation and full radio-knob tuning**. See
**[interfaces/CIV_SINGLE_PIN.md](interfaces/CIV_SINGLE_PIN.md)**.

**LoRa text messaging is confirmed on hardware** — two-way messaging between CardSat and a
LilyGo T-LoRa unit running the companion CardSat Pager firmware works (on-air frame format,
sync word, and CRC interoperate). See
**[design/LORA_MONITOR_SCOPE.md](design/LORA_MONITOR_SCOPE.md)**.

**The Icom LAN (RS-BA1 UDP) CAT path is confirmed able to control an Icom radio** — CardSat
successfully controlled an **IC-705** over the network once the CI-V address was set
correctly (connect / auth / keepalive handshake and CI-V framing all work). Practical
satellite use needs a radio with proper satellite mode: on the IC-705 the two VFOs just
swap back and forth (single-RX limitation), so it isn't usable for live satellite work, but
the path is proven and **should work with an IC-9700**, which has true satellite (dual-RX)
operation. Still unverified specifically against an IC-9700.

## Network commands verified, but not yet tested against a physical device

These send correct, protocol-accurate traffic on the wire (confirmed by the author), but
have **not** been driven against a real rotator or radio on the far end:

- **rotctl / rigctl network clients.** The **rigctl** client and the **rotctld**-protocol
  rotator client send accurate Hamlib TCP commands over the network. Exercise the client
  against `rigctld -m 2` / `rotctld -m 1`; the on-air commands look correct but the
  device side (a real rig or rotor actually moving) is untested.
- **PstRotator (UDP).** Sends accurate PstRotator UDP commands over the network
  (host-verified against the PstRotator manual). Not yet tested driving a real rotator
  through PstRotator.
- **Icom LAN against an IC-9700 specifically.** The LAN path is confirmed controlling an
  IC-705 (see above); the IC-9700 — the radio it's actually intended for, with real
  satellite mode — has not been tested directly.

For all of these, keep the network servers/clients on a trusted LAN (no auth).

## Still to verify on real equipment

- **Tool printing (0.9.59) — untested on paper.** `p` on any form tool should print
  the inputs, a rule, and the complete output list (past what fits on screen); `p` on
  conjunction/neighborhood/debris/link-curve prints their reports (the link curve
  includes an ASCII plot — check it survives the 42- and 48-column printer widths).
  Confirm no `p` fires while a form field is being edited (it types instead).
- **LAN line buffers (0.9.60).** With rigctld/rotctld enabled, drive the radio and
  rotator from WSJT-X/GPredict/Hamlib `rigctl` over the network: frequency-set and
  rotator commands still parse and act correctly, long commands are bounded (no
  overrun), and a sustained session runs without heap issues. Load the web-dashboard
  page and click through its controls: requests route, the page serves. (Behaviour
  identical; this is a heap change.)
- **CAT fixed-buffer stores (0.9.60).** Open the CAT monitor during live CI-V
  traffic: hex lines still render (up to 36 chars) with T/R colouring, the ring
  scrolls, nothing truncates oddly. Run the CAT self-test: PASS/FAIL/INFO lines
  colour correctly and scroll. (Behaviour identical to before; this is a heap
  change, not a visible one.)
- **Menu-order audit (0.9.60) — spot-checks.** Home: Overhead now launches from its
  new slot beside World Map; Weather from the station column; all twenty land right.
  Tools: every band opens the correct tool (the display permutation vs canonical ids
  — a mistake here opens the *wrong tool*, so click through a few per band); form
  tools still print with the right title. Settings: each category shows its new
  order and every row still toggles/edits the right thing.
- **List wraparound (0.9.60) — spot-checks.** Satellite list: up at the top lands
  on the last bird with the window following; empty catalog doesn't crash. Target
  search pick list: wrap in both directions with an active filter. Transponder
  editor: wrap doesn't leave delete armed. DX Doppler table wraps to the last row.
- **Star layer + QTH presets (0.9.60) — bench items.** Sky sources: `c` cycles four
  states; Orion/Big Dipper recognizable and correctly placed vs a planetarium app;
  names legible; redraw pace unchanged. Presets: save/name/recall round-trip; recall
  updates passes immediately and shows GPS off; slots survive reboot; empty-slot
  recall gives the hint; `1`–`5` jump.
- **Voice memos under USB CAT (0.9.60).** With USB CAT engaged: recording a memo
  works (mic path, ~4 KB) and shows REC; playback of a memo works IF the heap has
  room, otherwise a clear "needs more free RAM" message rather than silent
  no-audio; disengaging USB CAT restores normal audio. Games/alarm beeps still
  degrade silently under a tight heap.
- **High-orbit pass length was double-bugged (0.9.60).** The real cap was in
  buildSchedule's in-progress LOS scan (hardcoded 60 min), not just the formatter.
  Verify on the Schedule screen: a GEO favorite currently in view reads ~24h, a
  Molniya reads its true multi-hour length, and a LEO pass still reads its normal
  few-to-fifteen minutes (didn't regress).
- **High-orbit pass duration — now including the Schedule screen (0.9.60).** The
  multi-sat upcoming-pass list (Schedule) and the pass-detail LOS line were the
  remaining `%2ldm` sites; a GEO/Molniya favorite there now reads h:mm, not ~60m.
- **High-orbit pass duration (0.9.60).** Select a GEO or Molniya favorite and open
  Next Passes: durations read as h:mm (e.g. `4h00`, `24h`), not a capped ~60m, and
  the column doesn't overrun elevation. Check the pass-detail Dur, the sat-detail
  dur line, the Length row, and the web dashboard match.
- **KESSLER over LoRa (0.9.60) — needs TWO devices.** Host with `n`, confirm the
  guest auto-joins and the title shows "guest joined"; both build the SAME terrain
  and wind (compare screens); a fire on one plays identically on the other; you can
  only aim on your turn ("waiting for peer" on the other); scores stay in sync
  across rounds; the match ends on both at the same score; a dropped peer (power one
  off mid-match) leaves the other showing the last state rather than hanging; other
  LoRa messaging traffic on-channel is ignored by the game and vice versa.
- **KESSLER game (0.9.60) — bench items.** Flight animates smoothly at the 15 fps
  tier and the trajectory pace matches muscle memory (45°/60 on the Moon should
  clear mid-screen modules); craters carve and a station over an undermined column
  drops correctly; direct hit scores for the *other* player; Earth shock face
  triggers once per shot; velocity < 2 self-hit gag; `;`/`.` nudge and 3-digit
  entry both work; `` ` `` mid-flight exits cleanly and frees the heap (re-enter
  and check free-heap is unchanged); Fn+b screenshots the battlefield. Off-top return: a shot fired near-vertical (high angle/velocity) that leaves the
  top of the screen and comes down on the enemy base now explodes and scores (was
  passing through). Self-hit fix: a normal 45/60 shot flies clear
  (no instant round-end); a steep lob back onto your own roof still loses; v=1
  wobbles ~1.6 s then detonates on the pad. Rev B: backspace deletes digits (game
  survives), `,`/`/` swap fields, every turn shows both fields, off-screen and
  loitering shots end within ~8 s, title centred, aim form sits on the aiming player's side (P1 left, P2 hugging the right edge, clear of the wind arrow), star names selectable with the
  diamond marker and panel readout. Rev D: a fast direct hit at high velocity
  connects (no tunneling); a crater beside a station kills it; after any impact
  the next player's form appears with no keypress; the flight footer names the
  actual shooter; the aim line clears the Earth on both sides.
- **Six-task batch (0.9.59) — bench items.** High-orbit passes: pick a real HEO or
  GEO GP (period > 225 min) and confirm the Next Passes list populates, a
  geostationary-in-view bird shows one horizon-long pass, and the scan cost feels
  acceptable on-device. GP throttle: press `k` twice within 2 h — second run must say
  courtesy-skip and still reload; reboot between to prove persistence; change source
  and confirm an immediate fetch. USB `#N`: two identical adapters must show distinct
  leading ids matching what binding stores. hams.at: a favorite's activation rows
  tint green with a CelesTrak-sourced catalog. LoTW: a QSO logged under a CelesTrak
  name exports without a prompt. Graph screen: `b` toggles the table; `Fn+b`
  screenshots.
- **BASIC + calculator expansion (0.9.59) — none exercised on hardware.** Gate-checked and
  compiled; the `lookFor` math behind `SATSEL` is host-verified against `look()`, but every
  on-device path needs a bench pass:
  - `SATSEL` in a loop (e.g. scan all `NSAT` birds for the highest `SATEL`) completes without
    a watchdog trip and errors cleanly at the 2,000-call budget.
  - `TXSEL`, `PASSAOS/LOS/MAX(k)`, `LSTHR`, GPS names (with and without a fix), `HEAPFREE`.
  - `LPRINT` opens the sinks lazily, prints, and closes at run end — including on a runtime
    error mid-program; `no print output` with no sink configured.
  - Graphics: `SHOW` holds the frame after the run, any key returns to the console; colors
    0–9 render; drawing off-screen coordinates is harmless.
  - File gate: `FOPEN` refuses with the setting OFF; ON, it appends under `/CardSat/basic/`,
    rejects path characters, closes at run end; `FILES` lists; the grapher's CSV mode plots
    a file `FPRINT` produced.
  - Grapher: trace readout, `z`/`Z` roots and intersections on something known
    (`sin(x)` zeros at 0/±180; `sin(x)`=`cos(x)` at 45°), Simpson `S=` vs a known integral,
    table view, CSV decimation on a large file.
  - Calc: spot-check `atan2(1,1)=45`, `ncr(49,6)`, `fspl(435.5,800)`, `nf2t(0.5)`,
    `porb(400)` ≈ 92.6 min, and the `f` suffix.
- **CelesTrak catalog search + auto-refreshed extras (0.9.59) — network paths untested.**
  Host-verified (gates + full compile) but no on-air run yet. To confirm on hardware:
  - `/` search by NAME and by CATNR both return and render results; a query with no
    match surfaces CelesTrak's error body as a status rather than a blank screen.
  - Adding an out-of-source object creates `/CardSat/ctx.json`, the satellite appears
    with a favorite mark, and survives a reboot.
  - Running Update re-fetches the extras (watch for the "CT extra i/N" statuses) and a
    second Update within 2 h shows "CT extras fresh (Nm ago)" instead — including
    across a power cycle (the throttle timestamp is persisted in `/CardSat/ctx.ts`).
  - The 10 s search spacing and the 2 h same-query cache statuses appear when provoked.
  - `x` on a `/`-added satellite deletes it, removes the ctx line, and it does NOT
    come back on the next Update.
- **The twenty new tools (0.9.59) — none exercised on hardware.** All were verified
  host-side (balance/parity gates, a full arduino-cli firmware compile, and the orbital
  math cross-checked against skyfield), but none has been run on the Cardputer yet.
  Specifically worth confirming on-device:
  - **Conjunction screener & debris-group screen runtime.** Each propagates the fitter's
    pairwise SGP4 forward model many thousands of times. On the ESP32-S3 (no PSRAM) confirm
    the 6 h conjunction scan and the 3 h debris screen finish in a tolerable time and that
    the progress redraws (`conjPct` / `dgPct`) keep the UI responsive rather than tripping
    the watchdog. If a scan is too slow, widen the coarse step or cap the candidate count.
  - **Debris-group network path.** The debris screen fetches a CelesTrak GROUP as **GP JSON**
    (`FORMAT=JSON`, like the rest of the program — the legacy TLE format can't represent newer
    objects) to a temp file over TLS on the no-PSRAM heap, then stream-parses it with the shared
    allocation-free GP parser. Confirm the fetch succeeds for each of the four groups, the temp
    file is parsed and then deleted, and the resident 150-satellite database is genuinely
    untouched afterward. **Note the size ceiling:** GP JSON is ~800 bytes per object, so a large
    historical cloud (Fengyun-1C is thousands of objects, >1 MB) may exceed the download's
    free-space budget and stop with StorageFull — the smaller groups (last-30-days, Iridium-33)
    are the ones expected to fit comfortably. RAM stays flat regardless of file size.
  - **Transponder planner print.** `p` prints the dial-pair table through the same sink block
    as the report menu; confirm it reaches the field receipt printer / serial / file like the
    other reports.
  - **Sun-noise G/T against a real measurement.** Sanity-check the computed G/T on the bench
    (sun vs cold sky Y-factor) — and remember the seeded 10.7 cm flux is an upper bound; enter
    the flux at the operating frequency for the truest number.
  - **Number spot-checks.** The closed-form tools were checked against hand calculations and,
    for the Pi/T matching networks, an ABCD round-trip; a couple of real-world cross-checks on
    the bench (a known toroid turn count, a known microstrip line) would still be worthwhile.

- **CAT radio control (other paths).** Separate-pin CI-V, Yaesu, and Kenwood encoders
  are host-tested but not yet confirmed against those specific radios. Watch the CAT
  serial monitor to confirm the rig ACKs (`FB`) rather than NAKs (`FA`), that the
  correct VFO tunes, and that model / baud / address match. For radio-knob (One True
  Rule) tuning, each cycle reads the dial back after a set and only re-sends a leg when
  it actually moved, so coarse tuning steps don't masquerade as knob moves; while the
  rig reports PTT it skips the knob read. The knob-move threshold is **mode-aware**
  (≈30 Hz SSB/CW, 250 Hz FM, floored at the rig's tuning step), with a short grace
  window that holds off downlink writes while you're turning — tune
  `KNOB_MOVE_SSB_HZ` / `KNOB_MOVE_FM_HZ` / `TUNE_GRACE_MS` in `app.h` if the feel needs
  adjusting. (Single-pin CI-V and CAT-over-USB — both on the IC-821, above — are
  the CAT paths that **are** hardware-confirmed.)
- **92% of the code has never been compiled with warnings on.** The Arduino build ships
  `-w`, which silences every warning GCC has. `tools/check_compiles.py` is the only place
  that turns them back on, and it reaches **~3.5k of ~46.5k lines (8%)** — `usbserial`,
  the serial rotator path, `logstore` and `consolelog`. `app.cpp` (30k lines), `net.cpp`
  and `civ.cpp` are **unaudited**, and that gap already cost one shipped bug: 0.9.58's
  non-virtual-destructor defect was in covered code and *still* only surfaced when someone
  thought to run the audit. Extending the gate was attempted and abandoned deliberately —
  `rig/yaesu/kenwood` pull `radio_profiles.h`, then `String::toUpperCase`, then stand-ins
  for `IcomNetRig`/`RigctlRig` and the network stack behind them: a growing fake modeling
  code the change never touched. **The honest fix is to drop `-w` from the real build** and
  triage what falls out, rather than to grow a parallel fake. That is a 1.0 item.
- **`Rig` and `Rotator` base-pointer deletes are safe** — both declare `virtual ~`
  (`rig.h:25`, `rotator.h:114`), checked after the 0.9.58 transport bug. The defect was
  confined to deleting a transport through Arduino's `Stream*`, which has no virtual dtor.

- **Rotator serial transports (0.9.58).** The three serial protocols now run over the I²C
  bridge, **Grove G1/G2**, or a **USB adapter**. Only the transport plumbing is verified —
  no protocol has been driven by a real controller over Grove or USB. The Grove conflict
  rules (a Grove rotator refuses while wired CI-V or the Grove GPS hold G1/G2; CAT/GPS
  claiming Grove makes the rotator yield to the bridge) are logic-tested, not bench-tested.
- **Two USB adapters — radio and rotator at once (0.9.58).** Built, guarded, never run with
  two adapters plugged in. The hazard is real and specific: `EspUsbHostCdcSerial` defaults
  to `ANY_ADDRESS`, which the library resolves to *the first enumerated device with a
  bulk-OUT endpoint*, so two default-bound ports take the **same** adapter and the radio's
  Doppler writes land on the rotator. CardSat binds explicit device addresses to prevent
  it, and refuses rather than guessing when two adapters are present and neither is
  nominated. **Verify:** scan, assign each port, confirm each drives the right device, then
  replug and confirm the assignment survives (it persists by adapter serial number where
  one is reported — CH340s often report none, and those can only be told apart by address).
- **IC-9100 / IC-9700 over USB (0.9.58).** Never tested. Those radios present an internal
  hub with both a serial interface and a **USB Audio** device. The claim that audio cannot
  be mistaken for the CAT port is *structural* — a serial candidate needs a **bulk** OUT
  endpoint and audio streams are **isochronous** — and drawn from the library and IDF
  sources, not from a radio. The claim that a 9700 is **one** device slot (a composite
  device: one USB address, two interfaces) rather than two is the same kind of inference.
  **Verify:** does it enumerate, does CAT work, and does `/CardSat/Logs/usb.log` list the
  adapters you expect? If it reports slot exhaustion, that is the evidence to raise
  `ESP_USB_HOST_MAX_DEVICES` from 4 to 5 (+2,048 B — measured, not estimated).
- **Console-capture cost (0.9.58).** The ~0.5%-of-loop figure is **modeled**, not measured:
  ~6 ms per LittleFS write is an estimate, and the absolute could be off 2× either way.
  **Verify:** turn on Settings → Station / logging → *Console to file*, track a pass, and confirm tracking
  stays smooth and `console.log` stops growing at the cap. Every line is timestamped, so the
  log profiles itself.

- **Antenna rotator (hardware paths).** The motor-driving backends are host-tested only.
  For **GS-232**, the I²C pins (G8/G9) are confirmed from the Cap LoRa-1262 pinmap, but
  the SC16IS750 I²C→UART bridge and command path are host-tested for baud math and
  framing only — confirm the bridge address (`ROT_I2C_ADDR`) and controller baud
  **before keying real motors**. The **direct-Yaesu** I²C backend (ADS1115 feedback +
  PCF8574 direction) is host-tested only. (The network rotator surfaces — rotctld and
  PstRotator — are covered in the section above: their commands are verified, only the
  physical rotor is untested.)
- **Network *server* surfaces (CardSat as the server).** Separate from the client
  commands above: CardSat can also *act as* a **rigctld server** and a **rotctld server**
  for Gpredict / `rigctl` / `rotctl` to connect into. Those inbound paths are host-tested
  only — exercise them with `rigctl` / `rotctl` or Gpredict pointed at CardSat, on a
  trusted LAN (no auth).
- **TLS** uses `WiFiClientSecure::setInsecure()` (no cert validation) — fine for public
  GP data; pin a CA root if you care.

## Implemented in 0.9.36

- **DONE & CONFIRMED WORKING ON A REAL LoTW ACCOUNT — LoTW `.tq8` rewritten to LOTW V2.0.**
  A satellite QSO built by CardSat was uploaded and **successfully posted to the operator's
  LoTW account** (N8HM, the FO-29 QSO). Getting there took several fixes beyond the initial
  signing rewrite, each found from LoTW's server-side processing log:
  - `signData()` emits the V2.0 normalized string (station VALUES + contact VALUES, no adif
    tags, sigspec order, UPPERCASED, worked CALL included, station CALL/DXCC excluded).
    Host-verified the byte string and that it signs+verifies with `openssl dgst -sha1`.
  - Records carry `CERT_UID`/`STATION_UID` linkage; signature field is `<SIGN_LOTW_V2.0:LEN:6>`.
  - **Station record field names:** the tSTATION uses TQSL's internal names `US_STATE` /
    `US_COUNTY`, NOT the ADIF `STATE`/`CNTY`. A bare `CNTY` is rejected ("data length
    overflow"), which discards the whole tSTATION and orphans the tCONTACT.
  - **County value:** `US_COUNTY` is the county NAME ALONE (`Arlington`), not the combined
    `VA,Arlington` (which LoTW rejects as "Invalid value"). CardSat stores `ST,County` and
    strips the prefix when building both the record and the signed data. The state is in
    `US_STATE`.
  - **Date/time format:** the tCONTACT uses TEXT forms `YYYY-MM-DD` and `HH:MM:SSZ` under the
    field name `QSO_TIME` (not the compact `20260628`/`011800`, not `TIME_ON`) — otherwise
    "Invalid Date/Time in tCONTACT record". The signed data uses the same text forms.
  - **Response parser:** keys off LoTW's REAL marker `<!-- .UPL. accepted -->` (dots + space),
    not `<!-- UPL_ -->` (underscore) which never matched — that mismatch made an accepted
    upload report as a failure. Reports "Queued N at LoTW" on success (acceptance = queued
    for processing; per-QSO results are server-side).
  - Upload transport verified against tqsl 2.8.6 apps/tqsl.cpp: endpoint
    `https://lotw.arrl.org/lotw/upload`, multipart field `upfile`, **no login/cookie/auth**.
  - Opt-in **re-send** toggle (`a` on the LoTW screen) re-uploads QSOs already marked
    uploaded -- needed because pre-fix QSOs were flagged uploaded but never posted.
  - Full, corrected format spec: `docs/design/LOTW_TQ8_FORMAT.md`.

## Reference: how the root cause was found (kept for context)

- **ROOT CAUSE CONFIRMED + FULL FIX SPEC (reverse-engineered from tqsl-2.8.6 source).**
  The user's `.tq8` uploaded via the LoTW website was accepted/queued but the QSO never
  posted. Direct inspection proved the file is cryptographically self-consistent (sig
  verifies against the cert with `openssl dgst -sha1`), cert valid 2026-2029, all fields
  well-formed, date in range. **The problem: CardSat builds the SIGNED DATA completely
  differently from real TQSL, so LoTW re-derives a different hash and silently drops the
  QSO.** CardSat implements ARRL's *developer-tq8* doc, but that doc is STALE (documents
  only `SIGN_LOTW_1.0` and is wrong about what's signed). The authoritative definition is
  tqsllib's `make_sign_data()` + `tqsl_getGABBItCONTACTData()` in `src/location.cpp` and
  the `<sigspecs>` in `src/config.xml` (downloaded from SourceForge, tqsl-2.8.6).

  **The correct LOTW V2.0 signed-data algorithm (what LoTW actually verifies):**
  1. Build a single string = concatenation of STATION field VALUES then CONTACT field
     VALUES — **values only, NO `<adif:tags>`**.
  2. **tSTATION** fields, in this exact `config.xml` sigspec order, non-empty only:
     `AU_STATE, CA_PROVINCE, CA_US_PARK, CN_PROVINCE, CQZ(as int), DX_US_PARK, FI_KUNTA,
     GRIDSQUARE, IOTA, ITUZ(as int), JA_CITY_GUN_KU, JA_PREFECTURE, RU_OBLAST, US_COUNTY,
     US_PARK, US_STATE`. For a US station that's effectively: CQZ, GRIDSQUARE, ITUZ,
     US_COUNTY (value `ST,County`), US_STATE. **NOTE: CALL and DXCC are NOT signed.**
  3. **tCONTACT** fields appended, in this exact order (alphabetical, per sigspec /
     `tCONTACT_sign` loop): `BAND, BAND_RX, CALL, FREQ, FREQ_RX, MODE, PROP_MODE,
     QSO_DATE, QSO_TIME, SAT_NAME`. **The worked station's CALL IS included here** (CardSat
     currently omits it from signdata). Required: BAND, CALL, MODE, QSO_DATE, QSO_TIME.
  4. **UPPERCASE the entire concatenated string** (`string_toupper`) — CardSat keeps mixed
     case. e.g. `145.9500`->unchanged, `FO-29`->`FO-29`, `fm18lu`->`FM18LU`.
  5. SHA-1 hash that, RSA-sign (PKCS#1 v1.5) -> base64. (CardSat's sign primitive is fine;
     only the INPUT bytes are wrong.)
  Worked example (N8HM, this QSO), as understood at this *initial* stage — signed string =
  `5FM18LU8VA,ARLINGTONVA2M70CMN9EAT/VE3145.9500435.8500SSBSAT20260628011800FO-29`
  **(SUPERSEDED — this intermediate string is wrong: later real-account testing showed the
  county must be name-only and the date/time must be text-formatted. The corrected,
  confirmed-working string is `5FM18LU8ARLINGTONVA2M70CMN9EAT/VE3145.9500435.8500SSBSAT2026-
  06-2801:18:00ZFO-29`. See `docs/design/LOTW_TQ8_FORMAT.md` §6.4 for the authoritative
  version.)**

  **File STRUCTURE changes (also required):**
  - tCERT: add `<CERT_UID:n>1` after the Rec_Type (before CERTIFICATE).
  - tSTATION: add `<STATION_UID:n>1` and `<CERT_UID:n>1`.
  - tCONTACT: add `<STATION_UID:n>1` right after `<Rec_Type:8>tCONTACT`.
  - The signature field tag is `<SIGN_LOTW_V2.0:N:6>base64sig` where N = base64 length
    and `6` is the ADIF "type 6" annotation (verified: `tqsl_adifMakeField` emits
    `<name:len:type>value`, so type '6' -> trailing `:6`). NOT CardSat's
    `<SIGN_LOTW_1.0:N>`. The sigspec name string is built as `SIGN_` + name + `_V` +
    version = `SIGN_LOTW_V2.0`. (The wavelog reference file's `<SIGN_LOTW_V2.0:1:6>` had
    a 1-char placeholder value; real value is the ~172-char base64 sig.)
  - The SIGNDATA field stored in the file is the SAME uppercased station+contact value
    string from step 1-4 (not CardSat's tagged contact-only blob).
  - tSTATION record itself still lists the human fields (CALL, DXCC, GRIDSQUARE, US_STATE
    as `STATE`, US_COUNTY as `CNTY`, CQZ, ITUZ) as it does now — those are separate from
    the signed string.

  **Implementation plan:**
  1. Rewrite `signData()` in `src/lotw.cpp` to emit the station+contact UPPERCASED
     values-only string in the orders above (this is the load-bearing change).
  2. Add CERT_UID/STATION_UID to the three records; change the sig tag to
     `SIGN_LOTW_V2.0` with the `:6:` type annotation.
  3. Include the worked CALL in the signed data; drop the adif tags from signdata.
  4. Verify against tqsl: ideally sign the same one-QSO ADIF with desktop TQSL 2.7.2+ and
     byte-compare; at minimum confirm a CardSat file's stored SIGNDATA matches the
     step-1-4 reconstruction and the signature verifies, THEN test-upload ONE QSO and
     confirm it posts before shipping.
  5. Update the stale references in code comments/docs that cite the developer-tq8 page.

  Source refs in /tmp (this session): tqsl-2.8.6/src/location.cpp lines ~752 (make_sign_data),
  ~3760 (tqsl_getGABBItCONTACTData), config.xml <sigspecs>. The string_toupper at
  location.cpp:3827 is the easy-to-miss key step.

- **`.tq8` vs `.tq7` (DEMOTED — do NOT switch yet).** Earlier theory: our stored
    (uncompressed) gzip should use `.tq7` (the documented uncompressed extension) rather
    than `.tq8` (documented as compressed). Re-examined and the evidence now argues
    *against* switching: our `.tq8` got "Accepted: 1", and a genuine format/compression
    rejection produces an upload error (`400 Bad Request` / "bad file format"), not an
    acceptance — so the file was NOT rejected for being uncompressed. There is also
    *conflicting information* on whether LoTW still accepts `.tq7` at all, so switching
    risks trading a file that demonstrably uploaded for one that may not. Keep `.tq8`.
    Only revisit if the serial response specifically shows a compression/format error.
    (A stored gzip is a valid gzip stream — gunzip-verified — and "stored" is a legal
    DEFLATE method, so `.tq8` is defensible.)

## Investigated and intentionally left as-is — do NOT "fix"

- **Two `SAT_NAME` occurrences in a `.tq8` tCONTACT record are correct.** A grep of a
  signed `.tq8` shows `<SAT_NAME:n>` twice in one QSO record, which looks like a
  duplicate field but is not. One is the QSO's own ADIF field at the record level; the
  other is *inside* the `<SIGNDATA:n>` value, where `n` is the byte length of the whole
  normalized-and-signed blob. A length-tag-driven ADIF parser (LoTW's) consumes exactly
  `n` bytes of SIGNDATA as one opaque value, so at the record level there is exactly one
  `SAT_NAME`. The copy inside SIGNDATA is load-bearing: SIGNDATA is the normalized field
  sequence that gets SHA-1-hashed and signed, and SAT_NAME is field 9 of that sequence
  (per the ARRL developer-tq8 sigspec) — removing it would break signature verification.
  This is exactly how TQSL builds the file, and LoTW's "Accepted: 1" confirms the
  signature verified. Verified by reparsing a generated record host-side (one record-level
  `SAT_NAME`). Leave both emitters in `src/lotw.cpp` (`signData()` and `contactRec()`) as
  they are.

## Source file map

```
platformio.ini          board, libs, build flags
CardSat.ino             single-file Arduino build (generated from src/)
src/main.cpp            entry point (instantiates App)
src/app.{h,cpp}         UI state machine, rendering, Doppler service loop
src/config.h            URLs, UART/pin assignments, limits, file paths
src/storage.{h,cpp}     filesystem: microSD (/CardSat) first, internal LittleFS fallback
src/settings.{h,cpp}    persisted config (WiFi, location, radio, rotator, alarm, calibration, notes)
src/satdb.{h,cpp}       GP/OMM element store + TLE rebuild + streaming parse + transponder cache
src/net.{h,cpp}         WiFi, NTP, HTTPS GET, GP stream-to-file, SatNOGS fetch
src/location.{h,cpp}    manual / grid / GPS position, Maidenhead conversion
src/predict.{h,cpp}     SGP4 wrapper: look angles, passes, Doppler, Sun/eclipse, polar path, mutual windows
src/rig.{h,cpp}         abstract Rig interface + rigctl (rigctld) network client backend
src/civ.{h,cpp}         Icom CI-V framing, freq/mode set + read, MAIN/SUB select, single-pin
src/icomnet.{h,cpp}     Icom LAN (RS-BA1 UDP) CAT backend — confirmed controlling an IC-705; intended for the IC-9700
src/yaesu.{h,cpp}       Yaesu 5-byte CAT (FT-847 / FT-736R)
src/kenwood.{h,cpp}     Kenwood ASCII CAT (TS-790 / TS-2000)
src/rotator.{h,cpp}     rotator backends: GS-232 / Easycomm / SPID (I²C→UART), rotctl (TCP), PstRotator (UDP), Yaesu direct (I²C)
src/voicememo.{h,cpp}   SD-card voice memo recorder + playback (ADV ES8311 mic via M5Unified)
src/irbeacon.{h,cpp}    optional IR-LED pass beacon (38 kHz carrier, per-event flash counts)
src/lora.{h,cpp}        optional LoRa text messaging (Cap LoRa SX1262 via RadioLib; CARDSAT_HAS_LORA)
src/radio_profiles.h    per-model address, baud, band-select, capabilities
tools_make_cheatcard.py generates the printable 4×6 key-reference card (front + back)
```
