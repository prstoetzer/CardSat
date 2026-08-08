# CardSat 0.9.73 — release snapshot

**This is a release.** `FW_VERSION` is `0.9.73`. A USB release: the `CardSatDualRig`
companion is retired and replaced by **CardSatUsbHelper**, a second USB host on an
M5StickS3 reached over the Grove port — because the Cardputer's eight USB host
channels cannot hold two USB radios when one of them is an IC-705.

## State at packaging

* Build: EXIT=0, flash 3,082,362 (73.4% of the 4 MB app partition), static RAM
  153,952 (46%).
* All **static gates** pass, plus `check_body_parity` and the new
  `check_csuh_parity`.
* `CardSat.ino` in this zip is byte-identical to the source that produced
  `firmware/CardSat-app.bin` (MD5 `f9cd0c1a2d153313f51caf03cf3f7296`).
* Companion `CardSatUsbHelper-app.bin` MD5 `9a0878d98bdfafcc61cd142d0524b613`,
  built from the source in `companion/CardSatUsbHelper/`. 717,346 bytes (21%),
  static RAM 39,040 (11%).
* Vendoring verified in the map for **BOTH** firmwares: 1,269
  `libraries/UsbHostSrc` references and `libusb.a(` at zero in each. The helper
  linked Arduino's prebuilt stack until late in this cycle — see
  `docs/design/USB_REVIEW_EVAL_0_9_72.md` §0.
* Partition table is the repo's `partitions.csv` — 4 MB app0, 1.5 MB spiffs,
  coredump present.

## Building this package

Two steps beyond a normal Arduino build, and **both are silent if skipped**:

1. Copy `third_party/EspUsbHost/` over the installed EspUsbHost library.
2. Run `./tools/vendor_usb_host.sh` to install the ESP-IDF USB host component.

Then **delete the sketch build cache** — `build_opt.h` is not a dependency of any
object file, so a changed flag otherwise yields a byte-identical binary. Verify the
vendoring took by checking the map: `libraries/UsbHostSrc` in the hundreds,
`libusb.a(` at zero.

The companion is a **separate sketch**: open `companion/CardSatUsbHelper/`, not the
repo root. Its exact FQBN is in `companion/CardSatUsbHelper/firmware/README.md`.

## Read this before building

`third_party/EspUsbHost/` is a **patched** EspUsbHost 2.7.0 (MIT, upstream credit
intact). It is *not* used automatically — arduino-cli resolves libraries from
`~/Arduino/libraries`, so it must be copied there and the build redone in full. A
build against the stock library compiles cleanly and looks normal, then strands the
USB stack the first time a radio stops answering. See
`third_party/EspUsbHost/PATCHES.md`.

## Largest open items

* **First hardware session (bench) results.** Tilt direction is CORRECT — which
  clears `gameTiltAxis()` for all four tilt games at once. Two defects found:
  the Stick's status screen flashed (fixed: sprite-buffered paint) and **no USB
  device ever enumerated, powered hub or not** (open — see below).
* **USB enumeration on the helper: VBUS confirmed as the cause, worked around.**
  A Y-OTG cable with 5 V injected on the spare tail gets the IC-705 enumerated —
  the Stick sources no VBUS, exactly as the diagnostics predicted. The
  `CSUH_FORCE_EXT_OUTPUT` question is now ANSWERED from the K150 schematic
  (V0.6): EXT_5V_EN feeds Grove/Hat/IR only; USB-C VBUS is input-only (AW32901
  OVP → LGS4056 charger — no boost, no register) with fixed Rd on the CC pins.
  No firmware can ever source VBUS on this board. Injection is permanent; the
  helper README's "Field power" section defines the Grove-powered loom that
  replaces the Y-cable + battery with one Cardputer-fed harness. The clean
  hardware exit exists too: **CoreS3-SE** sources VBUS in firmware (AXP2101
  `USB_OTG_EN`; `setUsbOutput()` is implemented for that board family) and runs
  the helper firmware with a small mechanical port — see the README's
  "Hardware path" section.
* **Fourth bench: link stable (`rst 0`, local errors 0) but `usb tx 0`** — the
  radio talks (usb rx flowing) and ~1600 host frames decode clean, yet not one
  byte has ever gone TO the radio. The 39 remote errors are consistent with
  auto-baud lock-in noise. The ambiguity — rig layer never writing vs the
  helper's radio hop failing — cannot be resolved from those counters, so this
  build adds the instruments that split it in one keypress: **`t` on the
  helper screen sends a raw CI-V read-frequency down the transport** (bypassing
  engage, DualRig, and readiness entirely); the screen shows `tx N cr M` (frames
  sent and DATA_OUT credit held); the Stick shows `txOvr` and `hold` for the
  ring→radio hop. Fifth bench readout: attach 3 / seen 1 / usable 1, port OPEN
  (IC-705 named), CardSat "no link @115200" with tx climbing and rx silent, `t`
  refused (needs active()). Analysis: link WAS up this session (the port got
  opened through it), and is down one-directionally — helper→CardSat silent.
  attach 3 on one device = the radio re-enumerated three times = VBUS dropped
  twice (power-bank auto-off signature). The diagnostic cluster also OVERLAPPED
  the screen (fragments at x=150 collided with the link/port rows) — fixed:
  one always-drawn row at y=28 with tx/rx/cr, s/o, rst, e; the port row moved
  to y=40. The wire-vs-firmware verdict is now CardSat `rx` frozen + Stick
  `frm tx` climbing = G1 conductor dead; `rx` frozen + `e` climbing = garbled
  (baud/stall). Sixth bench readout (tx 878 / rx 914 / e 17 @460800, Doppler working
  intermittently, "link up" cycling to "no link" right after each sync):
  **460800 on this Grove wiring is marginal** — 17 decode errors where 230400
  ran 1600+ frames clean. The cycle: corrupted CardSat→helper frames starve the
  helper's 5 s timer → it unlocks and its TX wanders bauds while scanning →
  CardSat's errors climb → CardSat drops at 6.5 s → dwell re-locks 460800 →
  brief link-up → repeat. "Radio: n/a" was a symptom (each flap detaches the
  leg stream, flickering DualRig::ready()). **Operator action: set the helper
  link baud back to 230400** (the row cycles with ,/. on the helper screen —
  the likely vector for reaching 460800 in the first place). Two firmware
  defects fixed from the same readout: the EV_RESTART handler cleared s_linked
  (a lie — the event arrived over the link; liveness belongs to the timers and
  HELLO alone), and the track screen's bare "Radio: n/a" for a waiting dual rig
  now names both legs with "(leg wait)". Seventh bench: flap persists at 230400 with cumulative counters climbing —
  which eliminates the marginal-baud theory and every event path (rescan is
  keypress-only). The surviving mechanism: **the helper's loop stalls for
  multi-second bursts inside USB re-enumeration each time VBUS cycles** (the
  ext_port.c reset-poll busy-loop, deferred since the USB eval), starving BOTH
  link directions; CardSat times out at 6.5 s, the stall ends, HELLO relinks —
  "blinking" in step with the supply. Confirmation is one observation: the
  Stick's `attach` counter climbing in step with the blinks. If confirmed, the
  hardware fix is the supply (bank low-current mode / Grove loom) and the
  firmware hardening is the deferred reset-poll backoff PLUS moving link
  service to its own task (the helper's rings are already cross-task-safe by
  design). Track-screen radio line unified across CAT methods per operator
  request: color follows radioOut, name always shown, transient un-readiness is
  an appended "wait" — no more identity loss during outages. Eighth bench (VBUS solid, blink persists, reproduces with an idle Prolific,
  attach counts are per-plug-in signatures not cycling): the CardSat side is now
  READ-VERIFIED SOUND end to end — service() runs per loop from
  serviceHelperCat(); the ping is TX-gated and reachable; the helper answers
  PONG and every valid frame stamps s_lastValid; linked() is pure s_linked;
  onOpened errors touch only s_open; CAT_HELPER single defers begin() (fat
  comment, correct); dual defers helper legs; no second UART1 claimant in this
  config. Forced conclusion: **the HELPER's loop stalls in >6.5 s bursts**
  (rst 0 excludes reboots). Ninth bench: NO external hub was in the loop all morning (falsifying the hub
  trigger; the Prolific also exonerates the hub code path — it is a plain
  single-function device, while the 705's attach-3 is its own internal hub).
  Two suspects remain: an intermittent G1 conductor (helper→CardSat — exactly
  the direction that starves, and the one component never questioned), or a
  real helper loop stall. The helper now MEASURES the latter directly:
  `loop gap max N ms` on the Stick screen (worst gap between loop passes since
  boot, red above 1 s). Blink with gap in the tens of ms = the helper is
  exonerated, swap the Grove cable. Tenth bench: loop gap 20 ms (helper
  exonerated by measurement), cable swaps ineffective, CardSat rx climbing
  steadily while linked() stayed false — the timer cannot be clearing the
  flag, so **HELLO acceptance itself is failing**. The unlinked state now
  names its own failure stage on-screen: `hq N hr M` (HELLO requests sent /
  HELLOs received at dispatch) plus the last setErr() text in red
  (protocol-version mismatch is the classic — its toast was transient
  before). Reading: hq climbing + hr 0 = requests leave, HELLOs never
  decode; hr climbing + still unlinked = onHello rejecting, reason printed.
  Eleventh bench: hq AND hr both advance and the persistent red text is the
  dead-timer's own message — the handshake WORKS, the link goes up, and the
  timer then fires despite "steady" rx. The only consistent world: no
  non-HELLO frame validates between handshakes. The no-link row now carries
  per-class RX counters (`p/s/d/c/e` = pong/stat/data/credit/event, `tNN` =
  last type decoded): the class whose count freezes is the class the fault
  eats. Monolith checked for duplicated statics: one declaration each of
  s_linked/s_lastValid — the timer and pump share state.
  Post-root-cause cycle (operator confirmed WORKING on the bench): the CardSat
  helper screen is slimmed to essentials (link state @ baud + RTT + helper fw;
  a red health line with rst/e/dr only when non-zero; port; device list back
  to three rows) -- the bench instrumentation displays (hq/hr, per-class
  counters, age) and the `t` probe key are retired from the UI; every counter
  stays in code for the 0.9.74 health layer. The Stick's screen is unchanged
  by request. **NEW TOOL: Space-Track orbital history** (Tools > Satellite &
  orbital, canonical id 63, SCR_STHIST): gp_history CSV streamed and
  decimated into 120 bins (~4 KB heap-on-demand, freed by the transition
  hook), spans 30 d / 90 d / 180 d / 1 yr / 2 yr / 5 yr / 10 yr / max -- max
  pre-queries the object's earliest epoch so the full archive plots edge to
  edge; current elements derived from the loaded SatEntry (SMA from
  meanMotion, apo/peri from ecc) drawn as a right-edge marker with a delta +
  m/day readout; `u`/`w` edit credentials into /sthist.cfg via the generic
  editor (edit targets 530/531); `p` prints a first->last summary through the
  normal print sinks. **0.9.73 RELEASE GATE**: full suite run — 27/27 python gates (compile-gate
  shim updated to the patch-11 API: atomic, quiesce, getDevices, serialReady,
  isHub, UsbHelper stub for the rotator leg), csuh link 54/54, csuh frames
  239,674 checks, §9 verified for both firmwares. The release sweep caught
  and fixed three real defects: edit targets 530/531 CANCEL routed to the
  QSO log (the same class as their commit bug — editHome rule added above
  the 5xx catch-all), the Space-Track table footer was one column over
  budget, and the D1 one-device status ran 45 columns (both sites
  shortened). Release notes completed with the root-cause chapter, audit,
  Space-Track, multi-grid, Kessler netplay, and slimdown; the stale
  self-powered-hub power note replaced with the VBUS-injection findings and
  the CoreS3-SE pointer; README banner added; manual rebuilt.
  Space-Track seventh-bench: "all rows no data" after the per-column pass →
  prime suspect (audit, unconfirmed pending the inspector): gp_history CSV
  may QUOTE its fields, and atof on a quoted cell is 0.0 — the epoch column
  survived (its parser skips quotes) but every VALUE column zeroed, which
  the positivity guard now discards as no-data (and which, before the guard,
  zero-poisoned the photo's plot: vmin=0, delta=full value). Cell parsing now
  skips leading quotes and treats quote/CR-only cells as empty. Shipped
  alongside: tools-side st_inspect.py (in the release outputs), a host
  inspector that reproduces the firmware pipeline byte for byte — login
  semantics, URL shape, \n split with visible \r, per-cell rules, 120-bin
  decimation — and prints per-column ok/empty/zero/bad counts plus bins>0,
  the number the device draws from. **Multi-grid logging** (operator request): a rover on a grid boundary logs
  "FN20,FN30" (up to four grids). The entry normalizes ','/spaces to '/'
  because the log file is CSV -- an embedded comma would split the record
  (PendingQso.grid widened 10→30; CSV round-trip adapts via sizeof). Awards
  credit EVERY listed grid (the VUCC rule) via the shared awardCreditGrids()
  tokenizer at both crediting sites (all-sats + per-sat drilldown), while
  state/DXCC come from the FIRST grid only (a rover stands in one state).
  ADIF export emits GRIDSQUARE = first grid (the field is defined as a
  single grid) plus VUCC_GRIDS with the full comma-joined list -- the ADIF
  field made for boundary rovers -- at both export sites. QSL print measures
  the path to the first grid. LoTW upload needs no change: the tCONTACT
  never carried the worked station's grid (VUCC credit flows from THEIR
  upload). Space-Track sixth-bench (photo: AO-7 max, 20,835 rows — pipeline works end
  to end): the unreadable plot was ZERO-POISONED BINS — decades-old
  gp_history rows carry EMPTY derived-value cells, atof() made them 0.0,
  vmin hit 0 and the 1974-era "first bin" the delta compared against was
  all-zeros (D+7827 = the whole current value). StBin now keeps PER-COLUMN
  counts; a cell only counts for its column when present (and positive for
  sma/period). Also: scrollable DATA TABLE view ('t' toggles; up/down
  scrolls, ,/. cycles the metric, position indicator top-right) listing every
  populated bin as date + value via the extracted stFmtDate helper; the
  delta line tightened to fit 240 px ("now 7827.20km D-1.83 -10.2m/d").
  Space-Track fifth-bench fixes: (1) "no rows" on EVERY span was ONE bug —
  the epoch parser demanded a 'T' separator but Space-Track CSV renders
  epochs with a SPACE; every row's epoch parsed to zero and failed the
  window filter (the max pre-query failed the same way as "no history
  rows"). Parser now eats either separator (%*c) and skips a leading quote.
  (2) HTTP -11 on the 10-year span = HTTPCLIENT read timeout — a large
  gp_history query makes the server think longer than the default 5 s before
  the first byte; the query now allows 45 s, the login 15 s. (3) Left/right
  arrows now change values alongside ,/. (4) Selector rows nudged to y=18
  (clear of the header), plot band 58–108, min/max labels moved inside the
  box. Space-Track fourth-bench fix: the "1e86" message was a HEX CHUNK-SIZE line —
  auth was WORKING (0x1e86 = 7,814 bytes of CSV behind it), but the query
  reply came HTTP/1.1 chunked and getStreamPtr() hands over the raw stream
  with the chunk framing in it, so the first "line" failed the EPOCH check
  and the validator quoted it. stQueryGet now forces useHTTP10(true), which
  forbids chunking — the raw stream is pure body; the login keeps 1.1
  (getString de-chunks internally). Space-Track third-bench fix — **measured live before writing** (curl from
  the build sandbox against space-track.org, 2026-08): the query-with-login
  single POST is RETIRED server-side ("Single command deprecated. See API
  help for cookie use." — the second cut's 400s were this, not encoding).
  Also measured: the login sets exactly ONE cookie (chocolatechip), so
  HTTPClient's one-header-per-name capture is sufficient; FAILED credentials
  return HTTP 200 + {"Login":"Failed"} (the silent path behind the first
  cut's uniform "no rows" — now detected and named); an unauthenticated
  query returns 500 + HTML (mapped to "session rejected"). Transport now:
  POST login → validate body → capture cookie → GET query (percent-encoded
  operators — a raw space is illegal on a GET request line) → EPOCH header
  row as final validation. Kessler setup footer shortened to fit 240 px.
  Space-Track second-bench fixes: credentials moved into the standard config
  following the QRZ precedent exactly (cfg.stUser/cfg.stPass in settings.h,
  "stuser"/"stpass" in the JSON round-trip, cfg.save() on edit commit) — the
  ad-hoc /sthist.cfg file at FS root failed to write and duplicated a solved
  problem; stLoadCreds/stSaveCreds/stLogin deleted. HTTP 400 root cause: the
  query URL carried pre-encoded %3E/%20 and the form encoder doubled them
  (%253E) — Space-Track saw a literal "%3E" where an operator belonged. Query
  URLs now carry raw '>' and ' ' and are form-encoded exactly once.
  Space-Track first-bench fixes: (1) footer shortened to fit 240 px; (2) the
  u/w credential edits now RETURN from the commit switch like their peers --
  break fell through to the QSO-log exit; (3) the on-screen instruction line
  is gone (footer covers it; the line now shows only status/progress); (4)
  **auth reworked to Space-Track's cookie-less mode**: the query is POSTed
  together with the credentials to /ajaxauth/login and the response IS the
  result. The first cut captured Set-Cookie from a separate login, but
  Space-Track sets several cookies and HTTPClient keeps one header per name
  -- the session cookie was routinely dropped, every query returned an
  unauthenticated page, and every object reported "no rows". The CSV header
  row now doubles as the auth check: a reply not starting with EPOCH surfaces
  its first characters as the error instead of a generic "no rows".
  **KESSLER loss-tolerance pass** (operator-requested robustness audit): the
  host HELLO beacon no longer gives up (old: 15 tries / 60 s, leaving an
  unjoinable "waiting" host) -- 1 s cadence for 15 s then every 3 s forever;
  a guest can also press `j` in setup to broadcast the 0xFFFF "invite me"
  sentinel, which makes any waiting host re-beacon immediately. FIRE -- the
  one frame whose loss deadlocked a match -- is now reliable: per-shot
  sequence byte, resend every 1.5 s until KES_ACK (12 tries then a loud
  "peer not responding"), receive-side dedup by sequence with ACKs sent for
  duplicates too (a lost ACK is what causes a duplicate). Stale mid-game
  HELLOs only refresh the guest's ack, never reset the match; and if BOTH
  players press host, the smaller seed keeps hosting and the other converts
  to guest deterministically (both radios compare the same pair). SYNC stays
  fire-and-forget: it is score reconciliation only. Wire format note: FIRE
  grew from 6 to 7 body bytes (seq) -- both Cardputers must run this build.
  **KESSLER LoRa deadlock fixed (prior cycle)**: the round transition
  after a station hit was split across the wire (SYNC receiver rebuilt
  instantly and advanced the shared seed; the sender parked in phase 4 where
  net-ENTER was ignored) -- when the sender was the loser due to open the
  next round, both ends waited for the peer forever, and the sender's only
  escape ran the LOCAL random kessNewRound (terrain desync). Now the
  transition is local and symmetric on both ends (identical sims -> identical
  transitions; loser opens; seed advances exactly once) and SYNC is score
  reconciliation only. **FOURTEENTH BENCH — ROOT CAUSE FOUND AND FIXED.** The instruments closed it:
  `dr` reached 110 (dead-timer firings) while `age` never left 0.1 s — both
  reading the same variable. The timer's `now` was captured BEFORE pumpRx();
  pumpRx() stamps s_lastValid with a LATER millis() whenever a frame decodes
  during the pump; `now - s_lastValid` was then older-minus-newer: **unsigned
  underflow, ~4.29e9, always > LINK_DEAD_MS** — the link flag died at the
  exact moments frames ARRIVED. Random-looking, worse with more inbound
  traffic, immune to baud/cable/hub/VBUS/device, helper provably innocent
  (loop gap 20 ms) — every session's symptoms in one stale timestamp, present
  since the timers were written. Fixed: fresh capture after the pump plus a
  signed-delta guard so any future ordering drift degrades to a slightly-late
  timeout instead of this. The helper's relock compare is capture-adjacent
  (verified safe). Also: device list VIS 3→2 (the third row ran into the
  y=114 status band — likely the reported "overlapping notifications";
  re-check after this build since the drop-churn generated the recurring
  toasts). 0.9.74's F12 harness gets a regression test for exactly this:
  frame arrival straddling the liveness capture.
  Thirteenth bench: the accreted diagnostics (five generations at ad-hoc
  coordinates) had become unreadable — fragments in the header band, over each
  other, over the port row (y=40 vs list y=42, a fifth-bench regression), and
  off the right edge. The helper screen's top is now ONE fixed four-line
  table (state+RTT / error-or-classes+age+drops / totals / port), every line
  width-bounded via modulo caps for 240 px, list moved to y=58. Meanwhile the
  latest numbers (heartbeat working: s climbing ~1.5/s; p small as expected —
  the heartbeat suppresses the ping gate; possible r≈470 ms RTT sighting)
  hint at QUEUEING rather than stalling if confirmed; the new age field
  (sawtooth to 6.5 = genuine RX silence; near-zero at drops = something else
  clears the flag) plus dr (drop count) makes the next readout decisive.
  External communication-reliability audit received and evaluated (twelfth
  bench cycle): findings F1 (credit desync — verified true from both decrement
  sites), F3 (unlocked TX — verified), F4 (partial writes — verified), F5
  (OPEN bounce — verified, TH-D75/patch-9 relevant) all confirmed against
  code. Implemented THIS release: F3 (helper DATA/EVENT gated on lock, rings
  retain), F4 (all-or-nothing HelperStream::write with s_writeTimeouts), F5
  (idempotent OPEN — identical request answered without touching USB), F8
  (HELLO enforces max-payload + credit-init; "flash BOTH boards" on-screen),
  F9-lite (always-on 2 s STAT heartbeat — also gives the link a steady
  bidirectional rhythm, which the eleventh bench suggested matters), F11 (pong
  token validated, RTT measured and shown on the link line). Deferred to
  0.9.74 per the audit's own ordering: F12 fault-injection harness FIRST
  (including the 4.9/5.1/6.4/6.6 s timer-boundary cases aimed at the live
  blink), then F2 session SYNC with credit reset, F6/F7 LINE/CLOSE acks, F10
  adaptive baud-down, and the CSUH v2 stop-and-wait design. The audit does
  NOT explain the live blink (its F1 stall is the inverse symptom) — but its
  harness is the right instrument to catch it deterministically.
  Superseded candidate below kept for the
  record. Prior candidate: **the StarTech hub** —
  both test devices sat behind it, it carries the 0.9.72 "power dance" history,
  and hub port-status servicing runs through the deferred ext_port.c
  reset-poll path. One-move test: radio cable straight into the Y/OTG, no hub.
  Committed for next cycle REGARDLESS of that result: move the helper's link
  service onto its own FreeRTOS task (the rings are cross-task-safe by design)
  so no USB-side behavior can ever starve the Grove link, plus the reset-poll
  backoff. Also recorded: **CardSat's native USB unreliability was low VBUS**
  — Y-cable + battery feeding the Cardputer directly makes native USB reliable
  (operator finding, eighth bench). **Also open: ~10 KB boot-heap drop reported between the last
  two CardSat builds; ~4 KB is the deliberate link-UART buffer, the remainder
  is not yet accounted for.**
* **Third bench: dual-over-helper WORKED (IC-705 frequency control confirmed),
  then the link flapped** — "link up" / "no response on Grove" cycling. Root
  cause found in code: the keepalive ping was gated on the last frame
  RECEIVED, but the helper's liveness clock only sees frames SENT — so during
  an IC-705 transceive flood CardSat went silent, the helper hit its 5 s
  relock, walked off the correct baud, and the two ends chased each other.
  Fixed four ways: ping gated on last-TX (≤1.5 s gaps guaranteed); CardSat's
  dead timer raised to 6.5 s so the helper always recovers first; the helper
  dwells on its last-locked baud before scanning away; and both boards' link
  UARTs get 4 KB driver RX buffers (default 256 = ~11 ms at 230400, less than
  one screen paint — wired CI-V gets the same buffer). The helper screen now
  shows a red `rst N e L/R` verdict line when reboots or CRC errors are
  non-zero, separating stick-reboot / this-direction / that-direction faults
  at a glance. **Not yet re-benched.**
* **Dual-over-helper CAT: first defect found and fixed.** With the port
  enumerated, CAT still did not respond: `UsbHelper::open()` was only ever
  issued by the single-rig CAT_HELPER branch, so a CAT_DUAL helper leg never
  sent the CSUH OPEN at all — link up, key configured, port never opened,
  stream never attached. The dual construction path now opens the port with the
  LEG's resolved radio baud. **Not yet re-benched.**
* **(superseded) root-cause hunt notes.** The
  firmware now instruments the question: the Stick screen and the extended STAT
  report `attach` (raw interrupts, never cleared), `seen` vs `usable` counts and
  host-stack state. `attach 0` with a device plugged in means power/wiring —
  most likely **no VBUS on the Stick's USB-C**: a hub's upstream port waits for
  host VBUS, and most devices will not raise D+ without it, which matches
  "hub or not". `setUsbOutput()` is a no-op for the StickS3 in M5Unified; the
  only firmware-reachable 5 V control is the PM1 EXT boost, and whether it also
  feeds USB-C VBUS was a schematic question, since answered: it does not (see
  the fourth-bench entry above). `-DCSUH_FORCE_EXT_OUTPUT` compiles
  in a bench experiment to enable it — ONLY with the Grove 5 V wire
  disconnected (see the helper README's checklist).
* **CardSatUsbHelper's data path has still not run on hardware.** The link layer is verified
  host-side — including the shipped `src/usbhelper.cpp` driven against a mock helper
  through handshake, enumeration, every failure code, the credit invariant under an
  8 KB flood, reboot recovery and link death — but no one has run the firmware on a
  real M5StickS3 with a radio attached. `tools/helper_probe.py` exercises the Stick
  with CardSat out of the loop and is the right first bench step.
* **Deorbit has never been played.** Written and compiled but never run: paddle
  feel, launch angle, brick collision at speed and above all **tilt direction** are
  all unverified. `gameTiltAxis()` notes that the raw `ax` sign runs opposite the
  physical roll on this board and is negated to compensate — if the dish steers
  backwards, look there first. The keyboard fallback should be right regardless.
* **The QSL card has never been printed.** Its layout is width-checked at
  32/42/48/64/80 columns by `tools/host_qsl/qsl_layout_test.sh`, but that is a
  simulation of the `Printer` primitives, not paper.
* **`ext_port.c` re-polls port status with no backoff** — ~20 control transfers in
  12 ms at the most fragile moment of enumeration. In code we control, looks
  actively wrong, untested as a fix. Deferred from 0.9.73 deliberately to keep this
  release to the companion swap.
* **An FT232R that works on the root port can still fail behind a hub**
  (`CHECK_SHORT_DEV_DESC`, transfer status 1). Not re-tested against 0.9.72's
  late-enumeration watching.
* **Heap headroom is tight** with hub + two devices: largest free block ~11 KB
  (measured before this release's RAM pass reclaimed 9,464 B of static — the
  figure should improve on re-measurement, but has not been re-measured).
* **The 0.9.73 heap-on-demand conversions have not run on hardware.** AO-7
  observations, the rove browser, the debris-group screen and the CAT monitor now
  allocate on entry and free on exit; every touchpoint is nullptr-guarded and the
  build is clean, but the out-of-memory paths and the free-on-leave hooks have
  only been reasoned about, not exercised.
* **hamsatList/userSked (4.6 KB) deliberately NOT converted.** The activation
  arrays span ten functions in the flow that carries the known-open activation
  footprint bug ("sat not in your list" for near-term passes). Converting them
  blind is exactly the wiring-without-reading trap 0.9.65 taught; do it together
  with that bug fix, when the flow is open on the bench anyway.

Full history, reasoning and method lessons: `docs/design/AUDIT_FINDINGS_TRACKING.md`.
Protocol reference: `docs/interfaces/CSUH_PROTOCOL.md`.
