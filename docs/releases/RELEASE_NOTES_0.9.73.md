# CardSat 0.9.73 — release notes

A **USB release**, and a deletion. The `CardSatDualRig` companion is gone; in its
place is **CardSatUsbHelper**, a second USB host on the end of a Grove cable.

---

## The headline: a second USB bus

The ESP32-S3 has **eight USB host channels for the entire bus**, one per open pipe
including every device's default control pipe. A hub costs 2, a CDC radio 3, a
vendor-serial adapter 4 — and the **IC-705 contains its own internal TI TUSB2046
hub**, so the radio costs **5** by itself:

```
hub + IC-705            =  7   works
hub + TH-D75 + FTDI     =  8   works, no headroom
hub + TH-D75 + IC-705   = 10   cannot be made to fit
```

`usb_host_interface_claim()` takes every endpoint of an interface or none, so there
is no arrangement of eight channels that holds two USB radios when one is an
IC-705. That ceiling was diagnosed in 0.9.72 and is not a software problem.

A second microcontroller brings its own eight. **CardSatUsbHelper** runs on an
M5StickS3 and acts as a pure byte pipe: CardSat keeps one USB device on the
Cardputer and hands the other to the Stick over Grove. Every CAT dialect, all
rotator grammar and all the Doppler logic stay in CardSat.

Selectable three ways, for one device at a time:

* CAT type **USB helper (Grove)** — a single radio on the helper.
* Dual-rig leg bus **Helper** — one leg on the helper, the other on local USB or LAN.
* Rotator wire **USB helper** — the rotator on the helper, the radio elsewhere.

That last one solves a smaller but equally real ceiling: a USB radio *plus* a USB
rotator on the Cardputer is `hub + 3 + 3 = 8`, the absolute limit with nothing
spare. Moving the rotator to the helper leaves the radio a bus to itself.

**Settings → Radio → USB helper >** is the new screen: link state, firmware
version, the devices the Stick can see, and the link baud. Entering it brings the
link up even before a transport is selected, so you can see what is plugged in
before choosing.

### What the helper is not

It stores nothing across reboots and knows nothing about radios. That is
deliberate, and it is also the recovery story: there is no configuration to lose,
so rebooting is always safe — and a reboot is the only thing that reliably clears
a wedged USB host stack. When the Stick restarts it reports a new epoch, and
CardSat re-establishes the port **by itself**, with no operator action.

### Status — please read

The helper firmware compiles clean and its link layer is verified host-side,
including the *shipped* CardSat client run against a mock helper (handshake,
enumeration, every failure code, flow control under an 8 KB transceive flood,
reboot recovery, link death). **It has not been run on a real M5StickS3 with a
radio attached.** Treat it as ready to bench, not as proven.

`tools/helper_probe.py` speaks the protocol from a Mac over a 3.3 V USB-serial
adapter, so the Stick can be exercised with CardSat out of the loop — the fastest
way to separate a firmware problem from a wiring one.

One question to settle on the first bench session, because it decides whether the
helper's default setting works: run `helper_probe.py --enum` with only an IC-705
attached. If **one** device is listed, auto-select is fine. If **two** appear, the
radio's audio function is enumerating separately, and a device must be nominated
explicitly until that is handled.

---

## CardSatDualRig is retired

The old companion was a rigctld server that owned CAT state for two radios and
carried its own radio catalogue. Once `CAT_DUAL` landed natively in 0.9.68 that
duplication bought nothing: every radio fix had to be made twice and kept honest
across two release cycles. Nobody had ever run it on hardware.

Removed: the sketch and its binaries, its host test harness, its integration scope
document, the Dual-Rig screen's second personality, and the `\csdr_*` configuration
escape (`Rig::vendorLine()`) that existed solely to serve it.

**`rigctl (net)` and `rigctl (Grove)` are unaffected.** They still drive any Hamlib
rigctld, which is what they were always for. The native two-leg `CAT_DUAL` setup is
untouched.

The Dual-Rig settings row now applies only when the CAT type *is* Dual, and says so
rather than opening an editor for a rig that is not configured.

---

## QSL card for a single QSO

`p` on **Edit QSO**, or on the **QSO Log** list to print the highlighted contact.

Aimed at handing someone a paper confirmation on the spot — at a hamfest table,
after a demo pass — not at replacing a desktop logger's card designer. It carries
everything a confirmation needs and nothing else: both callsigns, both grids, date
and time in UTC, the satellite, `PROP SAT`, the mode, uplink and downlink with
their bands, the report **sent**, and the path length. Goes to the printer, the
serial console and `/CardSat/Reports/qsl-*.txt` like every other report.

Two wording choices are deliberate. `RST SENT` is labelled, because a QSL confirms
what *you gave them* and a bare "RST" reads either way round. And the LoTW line
says **Uploaded**, never *Confirmed* — the flag means we sent it, not that it
matched.

## Deorbit — a new game

Breakout, themed as sweeping derelict satellites out of the orbital shells above your
station. Sits in the Games menu directly above KESSLER.

The paddle is your **ground-station dish** and the ball a **capture tug**. The bricks
are dead birds in four colour-coded shells scoring 40 down to 10 a hit, so working
from the top pays. The tug parks on the dish until ENTER launches it; losing it off
the bottom is a re-entry and costs one of three tugs. Clearing all 32 respawns the
shells a step faster. Off-centre dish hits steer the tug, so there is real aim in it
rather than pure reflection.

Steering is tilt (honouring `cfg.gameTilt` and the IMU) with the keyboard always
live alongside — arrows, `t`/`u`, or `a`/`l` — the same pattern Doppler Lock and
Rotor Runner use.
All state is fixed `.bss`: a 4-byte brick bitmask and a few floats, no heap, per the
house rule for games. It cost 2,848 bytes of flash and 32 bytes of RAM.

**It has never been played.** Written and compiled, never run on hardware — paddle
feel, launch angle, collision at speed and above all **tilt direction** are all
unverified. If the dish steers backwards, `gameTiltAxis()` is the first place to
look: it notes that the raw `ax` sign runs opposite the physical roll on this board
and is negated to compensate. The keyboard fallback should be correct regardless.

## MyGrid is editable (and this was a bug)

`MyGrid` was display-only on the Edit QSO screen, so a QSO started before the GPS
had a fix carried an empty own-grid **forever**, with no way to correct it. That
was wrong in the ADIF export too, not just on paper.

It is now the twelfth editable field. The QSL card prints the grid recorded *with
the QSO* and never the current fix — for a contact worked from a rove or a previous
QTH those are different squares, and the recipient may claim grid credit from what
is on the card. If the QSO has no grid the card refuses to print and says where to
set it.

---

## Also

* **`Printer::center()`**, a real centring primitive. `title()` only centred on
  ESC/POS and the raster path; the other seven page formats fell through to
  left-aligned, which no report had noticed because they were all tabular.
* **`docs/design/PRINTING_GAPS.md` is closed.** Every gap it listed — EME, the EME
  plan, workable states, workable DXCC, awards, readiness, visible passes,
  propagation, space weather, Sun/Moon — has since been built.

## USB reliability

An external review of 0.9.72's USB handling was evaluated against the source late
in this cycle; the findings that held up were fixed here rather than deferred.
Full evaluation, including what was overstated and what was wrong, is in
`docs/design/USB_REVIEW_EVAL_0_9_72.md`.

**The helper was linking Arduino's prebuilt USB host stack.** `CardSatUsbHelper`
did not `#include <UsbHostSrc.h>`, so arduino-cli silently skipped the vendored
library and the firmware shipped without *any* of 0.9.72's USB work — no
enumeration-stage retry, no reset hold and recovery timings, no enumeration filter
callback. That is backwards: the helper exists to host the device the Cardputer
cannot, and was doing it with the weaker of the two stacks. Both firmwares now
verify identically in the map — 1,269 `UsbHostSrc` references, `libusb.a` at zero.

**A use-after-free on the failed-engage teardown path.** When `end()` times out the
library documents that it leaves its tasks alive, deliberately, to avoid freeing
in-flight transfers. Two of CardSat's three teardown sites already retained the
host object in that case; the third — reached when `begin()` fails — deleted it and
called `consoleUp()`, handing a live task a dangling pointer and reclaiming the USB
PHY from a stack that might still own it. It now retains and latches, like the
other two.

**Serial OUT transfers are bounded** (EspUsbHost patch 10). `sendSerial()` allocated
a transfer and buffer per call and freed them only on completion, so a radio that
NAKs, stalls or is unplugged mid-transfer accumulated heap with nothing counting
it — on a bus with a hub and two devices the largest free block is around 11 KB.
There is now one reusable transfer per device, with a busy flag and a submit
timestamp; an overdue write is treated as a stalled endpoint (halt, flush, clear)
rather than a slow one.

That change made refusals routine, which exposed a matching defect in the helper:
its drain loop popped a chunk out of the ring *before* checking whether the send
was accepted, so every refusal lost bytes. It now holds the chunk until the host
takes it — which also completes the back-pressure chain, since the ring no longer
drains and credit is correctly withheld. Credit had been bounding the *link* while
the USB side was unbounded.

**Unplugging a USB radio now recovers by itself.** The disconnect callback used to
tombstone the adapter registry and nothing else, so `active()` — which is
`s_active && s_bound` — stayed true for a radio that was no longer there, and the
loop reconciler saw nothing to do. A replug did nothing until a setting changed or
the firmware was rebooted. Disconnects now raise flags the main loop consumes,
detaching within one cycle so the reconciler rebinds by stable adapter key, and
`active()` / `cat2Active()` / `rotActive()` additionally consult the device's own
`connected()`.

A USB **rotator** is freed on unplug but deliberately not re-engaged
automatically. A rotator that starts moving again on its own because a cable was
reseated is not a surprise anyone wants from an antenna.

**Only serial-capable devices appear in the adapter pickers.** Hubs were excluded
and nothing else, so any device that enumerated — an audio function, a HID device,
a composite sibling — could be selected as if it could carry CAT. Both pickers now
require a claimed serial OUT endpoint. This matters most on the helper's *auto*
setting, where a composite radio with a separately-enumerating non-serial function
would make a one-radio helper look ambiguous.

**The helper checks its line-coding result.** Reporting a successful `OPENED` after
a failed `SET_LINE_CODING` left CardSat believing a rate the radio never received,
which presents as a radio that answers nothing.

Still open, and listed in `SNAPSHOT.md`: the `ext_port.c` reset-poll backoff,
per-attempt enumeration retry state, acquire fences on the adapter registry,
explicit host-channel-exhaustion diagnostics, and consolidating the three
near-identical CAT/rotator binding paths — which is the maintenance argument the
teardown bug above proves.

## Late-cycle review fixes: RAM, PSRAM, and one Grove-wire defect

A final pre-release review pass (`docs/design/REVIEW_RAM_PSRAM_0_9_73.md` and
`docs/design/USB_CODE_REVIEW_0_9_73.md`) produced fixes that went in before the
cut rather than into the next cycle.

**Opening the USB-helper screen no longer steals the Grove wire.** The screen's
eager link bring-up claimed UART1 unconditionally — the same wire wired CI-V, a
Grove GPS, a Grove rotator or Grove rigctl may already hold — so browsing the
screen while any of those was engaged silently killed it until a re-engage, with
nothing pointing at the cause. The bring-up now happens only when the wire is
genuinely free; otherwise the screen opens in a wire-busy state and says why. And
leaving the screen with no helper transport configured releases the claim again.

**9,464 bytes of static RAM reclaimed** (163,408 → 153,944, 49% → 46%) by
converting four screen-scoped buffers to heap-on-demand, the same pattern the
APRS/DX/ADS-B feeds already use: AO-7 observations (2.7 KB), the CAT monitor ring
(2.6 KB), the rove-plan browser (2.1 KB) and the debris-group buffers (1.9 KB).
Each allocates on entry, frees on leaving its screen, and tolerates a nullptr on
every path that can reach it — draw, key, print and parse. The CAT monitor
conversion loses nothing: its ring was always reset on entry and its trace sink
released on exit, so no history ever survived a session anyway.

The fifth candidate — the activation lists (4.6 KB) — was deliberately left
static: those arrays span ten functions in the flow that carries the known-open
activation footprint bug, and converting them blind is the wiring-without-reading
trap. They convert when that bug is fixed.

**The helper now uses its PSRAM.** Its two link rings moved to PSRAM-first
allocation and grew 30× (USB→link 8 KB → 256 KB, link→USB 2 KB → 32 KB): a CI-V
radio in transceive mode can now flood for minutes during a Grove-link stall
without losing a byte, helper→host credit starvation stops being a reachable
state, and ~10 KB of internal heap goes back to the USB host stack — the side
that actually needs DMA-capable memory. On a PSRAM-less part the allocator falls
back to internal RAM at the old sizes.

**EspUsbHost patch 11** makes the library's per-device CDC RX ring heap-allocated
and size-configurable (`-DCARDSAT_CDC_RX_RING`; the helper sets 16 KB,
PSRAM-preferred; CardSat keeps the 512 B default, byte-for-byte unchanged
behaviour). Alongside patch 10 this is upstream-worthy — the fixed 512 is an
upstream limitation. See `third_party/EspUsbHost/PATCHES.md`.

**Patch 10's stall path was itself fixed before release** after a fresh review
caught a race: it cleared the busy flag synchronously after halt/flush/clear, but
a flushed transfer stays HCD-owned until its CANCELED callback — so the next
write could resubmit a transfer the stack still held. The callback now clears the
flag; the stall path only rate-limits repeat halts. That is the second defect
found in patch 10 before it ever ran, which is why its recovery path is first on
the bench list.

## First-bench fixes

The first hardware session confirmed tilt direction (clearing `gameTiltAxis()`
for all four tilt games) and found two defects, both addressed:

* **The Stick's status screen flashed.** `drawStatus()` cleared the physical
  panel and repainted it every 400 ms — a 2.5 Hz full-screen strobe. It now
  paints into an offscreen sprite (~63 KB, PSRAM-preferred) and lands in one
  `pushSprite`, same cadence, tear-free. Direct-draw fallback if the sprite
  cannot allocate.
* **No USB device ever enumerated.** Root cause not yet established, so the
  firmware now instruments the question instead of guessing: the Stick screen
  and a v1-compatible STAT extension report raw attach interrupts (never
  cleared), `seen` vs `usable` device counts, and host-stack state — read the
  same numbers from the Cardputer's USB-helper screen or `helper_probe.py
  --stats`. `attach 0` with a device plugged in points at power or wiring, most
  likely missing VBUS on the Stick's USB-C; the helper README gains a
  step-by-step checklist and a build-gated PM1-boost experiment
  (`-DCSUH_FORCE_EXT_OUTPUT`, Grove 5 V disconnected only).
* Also found and fixed on the CardSat side: the **USB-helper screen never
  refreshed** — it was in no redraw cadence at all, so it painted once on entry
  and went stale. It now repaints at 2 Hz and polls helper stats at 1 Hz while
  open.

## Second-bench fix: dual-rig over the helper never opened its port

The VBUS diagnosis held — a Y-OTG cable with 5 V injected gets the IC-705
enumerated. The next layer then failed: **CAT_DUAL with a helper leg never sent
the CSUH OPEN.** `UsbHelper::open()` was only called from the single-rig
CAT_HELPER branch, so for a dual configuration the link came up, the device key
was configured, and the port-open request was simply never made — `active()`
stayed false and the stream never attached, while every retry mechanism politely
waited on a request that did not exist. The dual construction path now issues the
OPEN with the leg's resolved radio baud (explicit leg setting, else the leg
profile default — 115200 for an IC-705's USB CI-V). Downlink-only dual (no
uplink radio) is a supported shape throughout: `DualRig::ready()` deliberately
ignores a "None" leg.

## Third-bench fix: the link flap

With the OPEN fix in, dual-over-helper **worked** — IC-705 frequency control
confirmed on the air — and then the link began cycling between "link up" and "no
response on Grove". The root cause was a one-line invariant violation in the
keepalive: the ping was gated on the last frame **received**, but the helper's
liveness clock only sees frames **sent to it**. During an IC-705 transceive
flood, data pouring in kept CardSat's gate satisfied and CardSat silent; five
seconds of that one-way silence and the helper unlocked its baud scanner, walked
off the correct rate, and turned its own transmissions into noise here — after
which the two ends chased each other in a sustained flap. It is stable exactly
while frequency commands flow, which is why it "worked once".

Fixed four ways, defence in depth:

* **The invariant**: pings are now gated on the last frame this end *sent*, so
  outbound gaps can never exceed 1.5 s regardless of inbound traffic.
* **Hysteresis**: CardSat's link-dead timer moved from 5 s to 6.5 s, so when a
  real gap occurs the helper gives up *first* and re-locks against a host still
  transmitting steadily — the stream never detaches for a transient.
* **Dwell-on-unlock**: after a silence-unlock the helper now retries its
  last-locked baud before scanning away from it; silence overwhelmingly means a
  quiet host, not a re-rated one.
* **4 KB link-UART driver buffers** on both boards (the ESP32 default is 256 B —
  ~11 ms of headroom at 230400, less than a single screen paint on either
  board). Wired CI-V inherits the same buffer, since it sees the same
  transceive floods.

The CardSat helper screen also gains a red `rst N e L/R` verdict line, shown only
when non-zero: helper reboot count, and CRC/framing errors in each direction —
separating stick-reboots from either direction of link corruption at a glance.

## Bench polish

* **Deorbit is winnable.** Launch speed cut ~27% (−95/±55 → −70/±40), the
  per-level speed ramp eased (12% → 8% per shell set), the edge-of-paddle
  horizontal kick softened with a lower cap (2.2×/150 → 1.8×/110 — the old
  near-horizontal edge volleys were most of the "too fast" impression), the
  paddle widened 34 → 42 px, and keyboard travel per press raised 12 → 16 px.
* **The USB-helper screen is reachable from the Rotator section.** Choosing the
  helper as the rotator wire left the operator in a section with no path to the
  screen where the helper link, device and baud are configured — the entry row
  lived only under Radio / CAT. The same row (state-labelled: unused / no link /
  linked / open) now appears in both sections.
* **The rotator's USB-helper transport explains itself.** It always lived on
  Settings → Rotator → "Rot wire" (cycle to "USB helper"), but that row showed a
  bare "n/a" for network and direct-Yaesu rotator types — which read as "not
  present". The n/a now says "serial types only", and the "Rotator USB" row
  reads "on helper" when the helper transport is selected.

## Fixes and internals

* The CAT-type slot table was duplicated in two places. With `CARDSAT_HAS_USBCAT`
  and `CARDSAT_HAS_USBHELPER` independent there are now four build combinations, so
  it is one table with a `static_assert` against `CAT_TYPE_N`.
* The rotator transport row cycled raw enum values and would have landed on "USB
  helper" in a build without it — a rotator that looks configured, refuses to
  engage and gives no reason. It now steps past transports the build cannot
  construct, as the leg-bus cycler already did.
* Two monolith-only defects, invisible in the `src/` build because each `.cpp` is
  its own translation unit: file-scope statics in the new transport collided with
  three names already used elsewhere in `CardSat.ino`, and inlining a header that
  defines free `static inline` functions above `rig.h` moved Arduino's
  auto-generated prototype block ahead of `class Rig`.

## New verification

| tool | what it proves |
| --- | --- |
| `tools/host_usbhelper/csuh_frames_test.sh` | CRC and COBS against published reference vectors; every frame type at every length; every single-bit corruption caught; stream resynchronisation after injected noise |
| `tools/host_usbhelper/csuh_link_test.sh` | the shipped `src/usbhelper.cpp` against a mock helper — including the credit invariant under an 8 KB flood, confirmed to have teeth by deliberately breaking the grant |
| `tools/host_qsl/qsl_layout_test.sh` | the QSL card at 32/42/48/64/80 columns, three QSO shapes, failing on any overflow |
| `tools/check_csuh_parity.py` | the two copies of `csuh_proto.h` are byte-identical |

The link test compiles the code that ships, against a minimal Arduino shim — not a
re-implementation of it.

## Documentation

* `docs/interfaces/CSUH_PROTOCOL.md` — the wire protocol and the reasoning behind it.
* `docs/design/QSL_CARD_SCOPE.md` — the printing audit and the card design.
* `companion/CardSatUsbHelper/README.md` — wiring, power, build, debugging.

## The link blink: fourteen benches to one stale timestamp

Through the bench cycle the Grove link kept "blinking" — up after each handshake,
down moments later, while data demonstrably flowed and Doppler worked in the good
windows. Fourteen instrumented bench rounds eliminated, with measurements: marginal
baud (flap survived 230400), the Grove cable (swaps changed nothing), the hub (none
was connected), VBUS (solid on a meter), helper loop stalls (a max-loop-gap
instrument read 20 ms), the handshake (its counters advanced), and the dead-timer
itself (an on-screen `age` readout never left 0.1 s while the drop counter climbed
to 110 — the contradiction that cracked it).

The root cause was one line: the liveness timer captured `now` **before** the RX
pump ran, and the pump stamps the last-valid clock with a **later** `millis()`
whenever a frame decodes. `now - lastValid` was then older-minus-newer — unsigned
underflow, ~4.29 billion, always past the timeout — so **the link flag died at the
exact moments frames arrived**, more often the more traffic there was. The fix is a
fresh capture after the pump plus a signed-delta guard; the 0.9.74 fault harness
gains a regression test for a frame arrival straddling the capture.

Two real defects were fixed along the way because the instruments exposed them:
helper events can no longer clear the link flag (an event that *arrived over the
link* is proof of life, not death), and the track screen's radio line now keeps its
identity during transport hiccups — color follows the operator's own engage switch,
the rig stays named, and transient un-readiness is a small "wait" note instead of a
blanket "n/a".

## Communication-reliability audit

An external audit of the CSUH link arrived mid-bench and was evaluated finding by
finding against the code. Implemented in this release: the helper transmits **no
DATA or events while its auto-baud is unlocked** (an unlocked UART is at an
arbitrary rate; everything it sends is garbage plus silently consumed credits);
`HelperStream::write` is **all-or-nothing** for anything ring-sized, so a radio gets
a whole CAT command or none; an **OPEN identical to the active port** answers
success without bouncing DTR/RTS on the radio (the TH-D75's session sensitivity made
that one personal); **HELLO enforces protocol parameters** so a mismatched flashed
pair says "flash BOTH boards" in red instead of half-working; a **2-second STAT
heartbeat** runs whatever screen is showing; and **pong tokens are validated**, with
the measured RTT shown on the link line. Deferred to 0.9.74, in the audit's own
order: the fault-injection harness first, then session SYNC with credit reset,
LINE/CLOSE acknowledgements, adaptive baud-down, and the v2 stop-and-wait design.

## New tool: Space-Track orbital history

**Tools → Satellite & orbital → Space-Track history** answers "how has this orbit
changed": it fetches the satellite's GP history from space-track.org (`gp_history`,
CSV, derived values served by the API), decimates the stream into 120 time bins on
the fly — a 25-year archive costs the same ~4 KB as a 30-day window, freed on
leaving the screen — and plots one metric with the **current** elements as a marker
and a delta readout: `now 6793.42km D-1.83 -10.2m/d`. Spans run 30 days to **max
(all)**, which first asks Space-Track for the object's earliest epoch so the full
archive plots edge to edge. Metrics: semi-major axis, period, apogee, perigee,
inclination, eccentricity, B*. Press `t` for a **scrollable data table** of every
populated bin; `p` prints a first→last summary; `u`/`w` store the account in the
standard config (a free space-track.org account; a dedicated password is wise —
config storage is plaintext like every stored key).

The bench shook out real-world server behavior the hard way, and the notes record
it for posterity: Space-Track **retired** its query-with-login single POST
("Single command deprecated"), failed logins return **HTTP 200** with
`{"Login":"Failed"}`, query replies arrive **chunked** (defeated with HTTP/1.0),
CSV epochs use a **space** separator where OMM uses `T`, and old records carry
**empty or quoted** value cells that naive `atof` turns into zeros. Every one of
those now has a defense and a named error. `tools/st_inspect.py` reproduces the
firmware's pipeline byte for byte on a PC for future diagnosis.

## Multi-grid logging (VUCC rovers)

A rover on a grid boundary logs **all its grids**: enter them comma-separated
("FN20,FN30", up to four) in the log's grid field. They store joined with `/` —
the log file is CSV, so a comma cannot live inside a field — and **awards credit
every listed grid** (the VUCC rule) at both the all-satellites totals and the
per-satellite drill-down, while state/DXCC come from the first (primary) grid.
ADIF export writes `GRIDSQUARE` (first grid — the field is defined as a single
grid) plus **`VUCC_GRIDS`** with the full comma list, the ADIF field made for
boundary rovers. The QSL print measures the path to the first grid. LoTW upload
needed no change: the contact record never carried the worked station's grid —
VUCC credit flows from *their* upload.

## KESSLER over LoRa: the turn deadlock, then loss tolerance

The two-player netplay had a structural deadlock: after a station hit, the round
transition was split across the wire — the SYNC receiver rebuilt the next round
instantly while the sender parked in a "round over" screen where net-ENTER was
deliberately ignored. Whenever the sender was the loser due to open the next
round, both radios waited for the peer forever. The transition is now **local and
symmetric** — both ends run identical simulations, so both advance turn, seed and
phase themselves; SYNC is score reconciliation only.

A robustness pass then made the protocol survive lost packets: the host's HELLO
beacon **never gives up** (1 s cadence for 15 s, then every 3 s until a guest acks
or the host backs out — the old 15-try cutoff left an unjoinable "waiting" host),
a guest can press **`j`** to broadcast an "invite me" request that makes any
waiting host re-beacon immediately, and **FIRE — the one frame whose loss
deadlocked a match — is now reliable**: per-shot sequence numbers, resend until
acknowledged, duplicates detected and re-acked (a lost ACK is what causes a
duplicate). Stale mid-game invites can no longer reset a match, and if both
players press host, the smaller seed keeps hosting — deterministically, on both
radios. The FIRE frame grew by one byte, so **both Cardputers must run 0.9.73**
for netplay.

## Helper screen slimdown

With the root cause fixed, the bench instrumentation retired from the CardSat
helper screen: what remains is the link state with RTT and helper firmware
version, a red health line (`rst e dr`) that appears only when something is
non-zero, the port, and the device list. The probe key is gone. Every counter
stays in code for the 0.9.74 health layer, and the Stick's own screen keeps its
full diagnostic set.

## Upgrading

Settings carry forward. The CAT-type enum values are unchanged (`CAT_HELPER` was
added *after* `CAT_DUAL`), so a saved configuration still means what it meant. Two
new keys (`helpbaud`, `helpkey`) default harmlessly when absent.

**Power note.** The M5StickS3 cannot source USB VBUS — confirmed from the K150
schematic: its USB-C VBUS is input-only through an OVP switch, and no firmware can
change that. The bench-proven field solution is **external VBUS injection**: a
C-plug→A-socket OTG adapter whose power inlet is fed from a battery or from the
Cardputer's Grove 5 V (the "Grove loom" in the helper README, which also covers a
plug-style mini power bank — pick one with a documented low-current mode, or it
will shut itself off under a radio PHY's tiny draw). The same finding solved a
separate mystery: the Cardputer's own USB CAT flakiness was low VBUS all along —
fed properly, it is reliable without a hub. The clean hardware exit remains the
**CoreS3-SE**, whose PMU sources VBUS in firmware; the helper README's "Hardware
path" section covers the port. The Stick's EXT_5V rail stays forced to *input* so
it can only ever receive; never configure it to output 5 V while anything else
feeds that pin.
