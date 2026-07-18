# CardSat v0.9.59 — release notes
*Released 18 July 2026*

The workbench release. Twenty new tools, a whole-catalog CelesTrak search with
self-updating favorites, a Tiny BASIC that can reach the whole system (and draw,
and print, and — gated — log) while keeping its no-interactive-programs rule,
calculators grown into instruments, pass prediction that is finally honest above
LEO, and a long tail of citizenship: courtesy throttles, source-independent
satellite-name resolution, distinguishable USB adapters, and three audits
(orbital math, RAM/heap, and the compiled image). Two printable 4×6 cards now
ship: the key reference and a new hardware/data/calculator/BASIC reference.

*USB CAT joins the default build, and the documentation gets the audit the code
already had: every count, key, path, and status claim checked against the source —
plus the source's own comments checked against the source.*

---

# New

## Twenty new tools: a full satellite & construction bench

The Tools hub grows from 35 to **55**. Twenty new entries turn it into a genuine
field-and-bench kit for satellite operating, station engineering, and CubeSat
construction — and, in keeping with the project's habit, the highest-stakes ones
reuse machinery that was already audited rather than reimplementing it.

**Five standalone satellite screens** (they read the active satellite and, where
noted, the loaded catalog):

- **Conjunction screener** — pick any second object from the loaded satellites and
  scan the next 6 hours for close approaches to the active bird: a 30 s coarse grid
  with every candidate minimum refined to 1 s, listing the five closest under 800 km
  by time, miss distance, and relative velocity (orange < 100 km, red < 25 km). It
  reuses the state-vector fitter's pairwise SGP4 forward model.
- **Orbital neighborhood** — a no-propagation view of which loaded objects share the
  active satellite's altitude shell (perigee–apogee band within 150 km), sorted
  closest-first, showing each band, inclination, and gap (or OVLP). ENTER hands the
  pick straight to the conjunction screener.
- **Transponder planner** — an 11-row satellite-frame table of downlink/uplink dial
  pairs across a linear transponder's passband (inversion-aware), for coordination
  and net planning; `p` prints it. These are on-satellite frequencies with no Doppler
  — the live tracker adds that on the air.
- **Link margin vs elevation** — plots link margin across a pass from the changing
  slant range (altitude, frequency, horizon margin in), with the extra dB at TCA
  called out; `x` seeds altitude from the active satellite.
- **Debris group screen** — fetches a CelesTrak fragmentation-cloud group
  (Cosmos-2251, Iridium-33, Fengyun-1C, or last-30-days) as **GP JSON** — the same
  OMM/GP format the rest of the program uses, and the only one that still represents
  new objects now that the legacy 5-digit TLE catalog space is exhausted — keeps only
  objects in the active bird's band, and screens each for closest approach over 3 hours.
  The download is transient: streamed into a temp file, parsed object-by-object with the
  shared allocation-free GP parser (flat RAM even for a large cloud), then deleted — the
  resident 150-satellite database is never touched.

Both screeners say so on screen, but to be unambiguous: **public TLE/GP elements are
only kilometre-class accurate. These screens are for situational awareness, not
collision avoidance.**

**Fifteen live-recalc form tools:** Doppler budget (max shift and Hz/s at TCA for any
orbit), cascade NF & G/T (Friis, antenna→LNA→coax→radio, with the "what the preamp
buys" line), a sun-noise G/T helper that reuses the live 10.7 cm flux and Sun
ephemeris to turn a Y-factor into measured G/T, a helix-antenna designer, L/Pi/T
matching networks, pointing loss (seeded from the rotator deadband), an IMD-products
finder that flags products landing in a passband, microstrip/stripline impedance,
toroid winding over the common Amidon cores, a delta-v mini-set (Hohmann, plane
change, deorbit), flat-plate thermal equilibrium, a polarization/Faraday estimate,
trace & wire ampacity, and a PLL/frequency-plan helper.

### The matching-network formulas were proved before shipping

The L/Pi/T tool is the kind of thing that's easy to get subtly wrong, so the Pi and
T designs were checked by an impedance/ABCD round-trip on the host — build the network
from the computed component values, confirm the input impedance collapses to the
source resistance across a range of transformation ratios and Qs. The first Pi
formulation was wrong (it left tens of ohms of reactance at the input); the corrected
virtual-resistance derivation lands the input on the source impedance to floating-point
zero. The L and T forms check out exactly too. This is the same "keep the proof"
discipline as the print-raster and orbital-math work.

The orbital and Doppler math these tools lean on was independently audited the same
cycle against skyfield — see the section below and
`docs/design/ORBITAL_MATH_AUDIT_0.9.59.md`.

## Search the whole catalog; favorites that update themselves

The satellite list gains `/`: type a name fragment or a NORAD number and CardSat
searches the **entire public catalog** on CelesTrak — whatever your primary GP
source is. Results stream in (name, NORAD, altitude band, inclination; green =
already loaded); ENTER adds the pick as a favorite.

The interesting part is what happens afterward. If your primary source doesn't
carry the object, it's persisted as a **CelesTrak extra** and both GP update
paths **re-fetch its elements from CelesTrak on every update** — so a NOAA bird
added on top of an AMSAT catalog stays exactly as fresh as the amateur sats
around it. Hand-entered manual satellites keep their old behavior on purpose:
state-vector fits and pre-launch objects usually aren't in the public catalog,
so auto-fetching them would only burn queries.

Because a firmware shipped to many users can turn one polite feature into a
distributed hammering, **CelesTrak's courtesy limits are enforced in code**: at
least 10 s between interactive searches; an identical query inside 2 h reuses
the cached result file instead of touching the network; the extras refresh runs
at most once per 2 h with its timestamp **persisted across reboots** (so power
cycling can't bypass it), spaces object fetches 2 s apart, and caps at 25
objects per update. The UI says when a limit is active. All fetches use
`celestrak.org` directly — the `.com` redirect has a history of firewalled IPs.

Mechanically the feature is small: results are twenty 40-byte rows; the full
entry is re-streamed from the cached result file on add; extras live in an
NDJSON file written by the same record writer as manual sats and merged by a
skip-if-present loader (a primary-catalog entry always outranks our cached
line), which evicts a non-favorite when the 150-slot catalog is full — explicit
picks outrank file-order fills, the same philosophy as the favorites-preferring
loader. Deleting with `x` now covers these extras as well.

## BASIC reaches the system; the calculators become instruments

Tiny BASIC keeps its one founding rule — **no interactive programs** (no `INPUT`;
every run is bounded, start to finish, in one keypress) — and grows in every other
direction.

**Language:** `TAN` `ATN` `LOG` `EXP` `SGN` `MIN` `MAX`, a `MOD` operator,
`AND`/`OR`/`NOT` in conditions, one `DIM @(n)` numeric array (≤256, freed with the
run), classic `DATA`/`READ`/`RESTORE`, and `ON expr GOTO`.

**System bridge:** `SATSEL i` re-snapshots the `SAT*` names for *any* catalog
satellite — each call is one SGP4 run through a new `Predictor::lookFor()` that was
**host-verified against `look()` to ≤0.0003° az/el, ≤10 m range, identical range
rate, and zero sunlit mismatches** before shipping, and is budgeted at 2,000 calls
per run. `TXSEL` snapshots any of the active bird's transponders (`TXDL/TXUL/TXBW/
TXINV/TXLIN`), the pass table goes eight deep (`PASSN`, `PASSAOS/LOS/MAX(k)`), and
`LSTHR`, the GPS group, `HEAPFREE`, `UPTIME`, `NSAT`, `NTX` join the bare names.

**Output:** programs can draw on the screen (`CLS PSET LINE CIRCLE TEXT SHOW`, ten
colors; a `SHOW`ed frame stays up after the run until a key), print through the
configured report sinks (`LPRINT`, opened lazily and closed at run end), and — only
behind a Settings toggle that **defaults OFF** to preserve the 0.9.57 stance —
append log lines under `/CardSat/basic/` (`FOPEN`/`FPRINT`/`FCLOSE`, plus `FILES`).

The scientific calculator adds ~25 functions in three flavors (general math with
true two-argument support, RF companions to the new tools like `nf2t`/`fspl`/`dop`,
and orbital one-liners from the audited formulas) plus the `f` femto suffix. The
graphing calculator becomes a small instrument: second function `Y2` with
intersection finding, a trace cursor with numeric dy/dx, bisection zero-finding,
Simpson integration between dropped marks, a table view, and a **CSV plot mode**
that streams `/CardSat/plot.csv` min/max-decimated into the plot columns — a
100,000-row log in flat RAM, the natural partner to BASIC's gated logging.

## The new tools print — and the 0.9.59 code got a RAM audit

`p` now prints on **every form tool** — all thirty-four for one refactor: the
form screen's output lambda tees each computed line to the report sinks when a
print is running, so the paper is byte-for-byte the same compute path the
screen shows, with zero per-tool code and zero buffering. The four new
standalone screens print too: the conjunction screener (approaches, element
ages, and the awareness-not-collision-avoidance caveat verbatim), the orbital
neighborhood table, the debris-group run with a UTC stamp (a screening paper
trail), and the link-margin curve as a 5°-step table plus a small ASCII plot in
the 0.9.56 screen-rendering tradition. Everything stays contextual — the Print
menu holds at 29.

The whole 0.9.59 codebase then went through a RAM-and-heap audit
(`docs/design/RAM_AUDIT_0_9_59.md`): per-line JSON-document churn was
eliminated from every CelesTrak-extras file walk (the no-PSRAM fragmentation
pattern, worst during TLS fetches), `removeCtExtra` was converted from a
whole-file String to a streamed temp-file rewrite, the extras file gained a
hard cap of 25 matching the refresh's courtesy cap, and every heap lifecycle
added this cycle (BASIC VM, LPRINT sinks, FOPEN handle, transient parsers) was
verified paired-and-closed on all paths, including error paths. The audit doc
inventories every byte of permanent RAM the cycle added, with a verdict on
each. A follow-up pass over the *compiled* image (ELF/map + DWARF layout)
found the `App` object to be 72 % of static RAM and its `SatEntry`*150 catalog the
largest block; reordering `SatEntry`'s fields largest-alignment-first removed 8
bytes of per-entry padding (144 -> 136 B) and reclaimed ~1.3 KB of RAM with a
byte-identical harness result. (Shrinking the epoch to 32 bits was rejected: it
carries a sub-second fraction the TLE epoch field and `tsince` both depend on.)

## Higher orbits, courtesy, and names that always resolve

**Pass prediction grows past LEO.** The tracking library's pass search hops one
revolution at a time and Brent-brackets a rise and a set — superb for LEO (the
harness holds it at 7/7 against Skyfield) and demonstrably wrong above it: fed a
Molniya it finds nothing, and a geosynchronous bird in view has no rise to
bracket at all. Deep-space SDP4 was in the propagator all along (the harness
shows a Molniya's live look spot-on), so for any period over ~225 minutes
CardSat now scans elevation itself and bisects the horizon crossings to ~1 s,
with the peak sampled inside the pass; continuous visibility reports as one
honest horizon-long pass. New HIORBIT harness sections verify it against
Skyfield: Molniya crossings within 0.04° of the true horizon and every one of
Skyfield's 194 up-samples covered; the parked-GEO case returns exactly one
86,400-second pass. (Two library quirks earned comments on the way: `Sgp4::init`
short-circuits on a byte-identical line 1, and `nextpass` keeps its LEO job.)

**The primary catalog fetch honors CelesTrak's guidance** — the same source URL
is not re-downloaded within 2 hours; the timestamp persists across reboots, a
skipped fetch reloads the cache with a status, and changing the source fetches
immediately. **USB device strings now lead with `#N`**, the device address, so
two byte-identical Prolific adapters can be told apart on a row that truncates
tails — and `#N` is exactly the id explicit binding stores. **Satellite names
resolve source-independently everywhere:** one bridge (parenthetical designator,
whole name, token) serves AMSAT status, hams.at — favorite activations now tint
green — and LoTW export, which auto-resolves CelesTrak-named QSOs instead of
prompting. The screening tools' caveat lines now correctly say **public GP
elements** — CardSat hasn't spoken TLE to a data source since the debris fetch
moved to GP JSON. On-device help gained the new keys, and the grapher (which now
claims bare `b` for its table view) joined the letter-using screens: `Fn+b`
screenshots there, per the convention.

## USB CAT is on by default

`CARDSAT_HAS_USBCAT` is now **1** in `config.h` / `CardSat.ino`. The reasoning is the
same as LoRa's: ship every feature. It was opt-in while unproven; 0.9.58 bench-proved
it on an IC-821 + FTDI adapter over many engage/disengage/Doppler cycles, and the
0.9.58.1 compiler audit fixed the last known defect in its teardown path. There is no
longer a reason for the flagship transport to be a build-your-own option.

What changes for builders:

- **EspUsbHost (TANAKA Masayuki) is now a required library** — **v2.3.1 or later,
  no patch needed** (see the next section: the `peripheral_map` fix landed
  upstream). Only stale ≤2.3.0 copies still need the one-line edit kept in
  `docs/BUILD_AND_FLASH.md`.
- **Arduino IDE:** install EspUsbHost (v2.3.1+); nothing else. The flag is already `1`
  and the repo already ships `build_opt.h` (`-mtext-section-literals` for the
  monolithic `.ino`'s literal-pool reach, `-DESP_USB_HOST_MAX_DEVICES=4` applied
  globally — the IDE passes `build_opt.h` to every translation unit, libraries
  included).
- **PlatformIO:** `platformio.ini` now ships with
  `tanakamasayuki/EspUsbHost @ ^2.3.1` under `lib_deps` and
  `-DESP_USB_HOST_MAX_DEVICES=4` active. No `-DCARDSAT_HAS_USBCAT` flag is needed —
  the default comes from `config.h`.
- **Building without it** stays a one-line choice: flag to `0` (IDE) or
  `-DCARDSAT_HAS_USBCAT=0` + comment the lib_dep (PIO). No EspUsbHost needed then,
  the *USB serial* CAT type disappears, everything else is byte-for-byte identical.

Nothing changes for operators of the prebuilt binaries — they already carried USB
CAT; now source builds match them.

## EspUsbHost verified through v2.3.2 — the patch is upstream

The one-line library patch this project has carried since 0.9.58 is **no longer
needed**: upstream **v2.3.1** wrapped the exact `hostConfig.peripheral_map`
assignment in `#if defined(CONFIG_IDF_TARGET_ESP32P4)` — a *target* guard, which is
even more precise than the `ESP_IDF_VERSION` guard our docs had suggested, since
it's the S3 target (single USB peripheral) that makes the field meaningless, not
just the IDF snapshot. On an ESP32-S3 the line now compiles out entirely: the same
effect as our patch, from the author's hand.

This was not taken on faith. The v2.3.0 → v2.3.2 diff was audited against every
symbol `usbserial.cpp` touches, and then proven the direct way:

- **A pristine v2.3.2 checkout compiles CardSat 0.9.59 clean** on arduino-esp32
  3.2.1 — both a minimal harness exercising the exact API surface (`EspUsbHost`
  begin/end/lastError/onDeviceConnected; `EspUsbHostCdcSerial`
  begin/end/connected/setAddress/setConfig/setDtr/setRts; the config structs and
  device-info fields) and the **full 2.1 MB monolithic `CardSat.ino`** with the
  repo's own `build_opt.h`: 2,682,486 bytes, 85% of the 3 MB app partition, zero
  patches.
- **The API surface is untouched between 2.3.0 and 2.3.2.** Classes, config
  structs, `EspUsbHostDeviceInfo` fields, the parity/stop-bit **enum values**
  (CardSat casts stored settings into them), the `"EspUsbHost"` /
  `"EspUsbHostClient"` task names (CardSat probes their stack headroom by name),
  and the `ESP_USB_HOST_MAX_DEVICES` `#ifndef` mechanism are all byte-identical.
- **`end()` is unchanged**, so the resident-host design still stands: the library
  still cannot release its client, and CardSat's keep-it-resident strategy (and
  the console-stays-gone consequence) applies to 2.3.2 exactly as to 2.3.0. The
  `usbserial.h` comment now says so explicitly.
- **`DeviceState` grew two bools** (`recoveryPending`/`resubmitPending`) — an
  object-layout change between versions, which is precisely why
  `ESP_USB_HOST_MAX_DEVICES` must stay a *global* define (`build_opt.h` /
  `platformio.ini`) rather than per-file. The discipline adopted after the
  0.9.58-wip freeze is what makes a library update layout-safe.
- **The two behavioral changes on serial paths both favor CardSat.** Transfer-error
  recovery is now *deferred to the client task*, so an adapter yanked mid-pass is
  released instead of being handed a new URB — the hot-unplug race our field use
  case cares about. And serial-endpoint binding is now pinned to the *recorded*
  CDC-data / vendor interface number (with CDC-NCM disambiguated) instead of
  first-match — a direct improvement for composite devices like an IC-9100/9700
  plugged in over its own USB port, the case `usbserial.h` has always flagged as
  unverified. Everything else in the diff is MSC and HID-keyboard work CardSat
  never touches, and git master is source-identical to the v2.3.2 tag.

`platformio.ini` now pins `@ ^2.3.1` (the PlatformIO registry serves ≥ 2.3.1), the
Library Manager currently serves v2.3.2, and a previously-patched local copy
updates cleanly — the update simply replaces the patch with the upstream guard.

## Fix: phantom "USB rotator: starting" under non-USB rotators

Selecting a network (rotctl/PstRotator) or direct-Yaesu rotator while the hidden
`rotTransport` wire setting still held its previous **USB** value produced a
spurious "USB rotator: starting..." status and needlessly reached for the USB
host — even though those rotator types don't use the transport setting at all
(they carry their own socket, or are I2C-direct). A `rotUsesUsb()` predicate now
gates every USB-engage branch, firing only when the transport actually applies to
the chosen rotator type. A genuine serial rotator on a USB adapter (e.g. GS-232
over USB) is unaffected and still reports normally.

## The documentation audit

A full pass over the manual, README, cheat card, wiring/build/verify/roadmap docs and
the 0.9.58 notes, with **every checkable claim verified against the 0.9.58.1
source**. The fixes worth naming, because each is the kind that misleads an operator:

- **The Illumination screen's `,`/`/` keys jump a full 60-day window** — the manual
  said "one day at a time" in three places (§8, §22, §23). The cheat card was the one
  document that had it right; the source (`keyIllum`, `ILLUM_DAYS`) decided.
- **The IC-910 *does* have a CAT satellite-mode command** (`0x1A 0x07`). Manual §3
  listed it among the rigs without one, contradicting §7, §16, and
  `radio_profiles.h` — all of which had it right.
- **Orbital analysis has eleven pages.** The docs variously said nine (§22, README)
  and ten (§8, §23, cheat card); the **Explore** what-if page was entirely
  undocumented in §8 and missing from every page list. Now counted and described
  everywhere.
- **"Settings → Log → Console to file" was the wrong path in six places** across
  five files — the row lives under **Station / logging**; "Log" is the Home-menu QSO
  logbook, so the printed path sent people to the wrong screen.
- **The About → Print submenu has 29 reports**, not the 28 the manual and cheat card
  claimed (0.9.58 added *Performance / heap* and the docs never caught up).
- **The 0.9.58 release notes' first bullet described the abandoned design** — "the
  console comes back when the radio is disengaged" — when the shipped behavior,
  stated correctly two sections later, is that it does not. Rewritten to the truth
  with a pointer to the resident-host story.
- **THINGS_TO_VERIFY never recorded the release's own headline confirmation.** USB
  CAT's bench proof is now in the confirmed list, and "the one CAT backend that is
  hardware-confirmed" (single-pin CI-V) reads "the CAT paths that are" — there are
  two. The 0.9.55 printing entry was also seven reports stale and predated the
  AirPrint raster confirmation.
- **WIRING.md was the most outdated file in the tree**: "one of two interchangeable
  backends" (there are eight), single-pin CI-V "unverified" (confirmed on an IC-821
  since 0.9.5x), no USB CAT alternative, no `Rot wire`, no Grove/USB rotator
  transports, and an Icom-LAN paragraph that offered the IC-7610/785x without the
  satellite-architecture caveat. Rewritten to the 0.9.58 truth, with the transport
  conflict rules pointed at `interfaces/ROTATOR_TRANSPORTS.md`.
- **The cheat card's Home list was missing *AMSAT status*** (19 of the firmware's
  static-asserted 20 items), the Satellites block was missing the `s` key, and the
  rotctld line still said it "drives GS-232" — it drives whichever rotator is
  configured. The card also gains the post-LOS `q` deep-sleep key. Regenerated: still
  two pages at the same font sizes, now stamped v0.9.59.
- **Manual §23's Next Passes row was missing four keys** (`t` timeline, `p` rove
  planner, `w` workable horizon, `s` target search) and both Log-menu listings were
  short — the menu has nine items, including *Fill grids (QRZ)*.
- **README's license pointer** went to manual §23 (the key reference); the license is
  §26. The screenshots note also claimed the current firmware was v0.9.57.
- **Manual §3 now has the section 0.9.58 never got**: *CAT over a USB↔serial
  adapter* — supported chips, the console trade-off, build availability — and §17's
  protocol list no longer interleaves the `Rot wire` transports mid-list (the
  Easycomm/SPID bullets also stopped claiming the I²C bridge is their only wire).
  The Settings table gains the 0.9.58 rows (*Rot wire*, *Radio USB / Rot USB*,
  *Scan USB adapters*, *Console to file*) and the CAT type row's fourth value.
  Troubleshooting gains a USB CAT entry built from the firmware's own refusal
  strings, and the rotator "n/c" entry is `Rot wire`-aware.
- **ROADMAP** is re-anchored to 0.9.58.1: the §1.1 verification table gains rows for
  CAT-over-USB (confirmed), the Grove/USB rotator wires (host-tested only), and
  Easycomm/SPID; the Icom-LAN row records the IC-705 transport proof; and the LoRa
  row no longer says "UNTESTED" — messaging has been hardware-confirmed since 0.9.39
  (the RX monitor and sat-RX paths remain the untested part). §2.4's "CAT over USB
  would be a transport" proposal now reads as what it became: shipped, past tense.

**American English and dollars throughout.** ~45 British spellings normalized across
the manual, cheat card, and current docs (kilometres, centre, colour-coded,
labelled, defence, …), and the six pound-sterling prices are now dollars ($5
adapters, $25 printers). Historical release notes and bug logs before 0.9.58 were
left as written, code identifiers untouched.

## The source-comment audit

The same discipline, pointed at the comments — several described a smaller CardSat
than the one that exists:

- **`rotator.h`'s header said "the only backend so far is GS-232"** two paragraphs
  above the 0.9.58 transport block that lists three wires. It now names all seven
  backends and defers to the transport block.
- **`rig.h`'s backend table omitted `IcomNetRig` and `RigctlRig`** — the latter
  defined *in the same file*. Both listed now, plus a line noting the wire-level
  backends' `Stream*` makes UART-vs-USB a runtime choice.
- **"the planned USB rotator" appeared three times** (`usbserial.h`,
  `usbserial.cpp`, `BUILD_AND_FLASH`'s slot-count note) for a feature that shipped
  in 0.9.58.
- **`config.h`'s flag block claimed the Arduino IDE "has no field for" build flags**
  — contradicted by the repo's own `build_opt.h`, whose mechanism `usbserial.cpp`
  documents in detail (verified against arduino-esp32 3.2.1's `platform.txt`). The
  block also still called USB CAT "UNPROVEN on hardware". Rewritten for the new
  default, with the ODR warning kept.
- **`platformio.ini`'s USB comments** pointed at a lib_dep "above" that sits below,
  carried a leftover "2 slots, not 1" note from a draft where the flag was 2, and
  referenced the *design* transports doc instead of the shipped *interfaces* one.
- **`net.h`'s title undersold itself** ("HTTPS downloads") — it has carried the LoTW
  and Cloudlog uploads since 0.9.34/0.9.36. **`app.h`'s printing section** was still
  titled "Receipt printing (TCP:9100 ESC/POS)" over a nine-language, two-transport
  print system. And two comments still counted "five mini-games"; `GAMES_N` is 6.

All 18 `docs/…` paths referenced from source comments were checked — every one
resolves. The per-radio tables, protocol notes, and module headers
(`civ`/`kenwood`/`yaesu`/`icomnet`/`logstore`/`consolelog`/`lorarx`/…) checked clean.

## The screen audit

Every screen was audited against the 240×135 panel: does each one draw inside its
band (below the 16 px header, above the y=127 footer), and does anything overdraw
anything else? The audit is a new permanent tool —
**`tools/audit_screen_geometry.py`** — a static analyzer that walks all 136
`draw*()` bodies, tracks `setTextSize()`, evaluates literal coordinate arithmetic
including the two list-loop idioms, and checks every evaluable draw op (803 of
them; 187 runtime-coordinate ops are counted as skipped, not silently passed)
against the sprite and band invariants. It exits nonzero only on definite clips,
so it can join the gate run.

What the audit established, and what it found:

- **Dispatch is complete**: all 131 screens have both a draw case and a key case,
  and **every key handler has an exit path** — no unreachable or trap screens.
- **Text-size discipline holds**: `header()` leaves size 1, and every draw
  function establishes its size before printing (the one helper that doesn't,
  `drawCharLkTable`, is only ever called after its parent has).
- **The runtime-coordinate screens are bounded by construction** — reviewed by
  hand: the plot screens normalize or fit-to-box before mapping (the Doppler
  graph min/max-normalizes, Orbit-zoo/GP-fit scale apogee to the frame, the
  graphing calculator auto-scales), and the projection screens (polar, sky map,
  OSCARLOCATOR, globe, compass) are center±radius bounded.
- **86 same-position draw pairs** were flagged and triaged: all are mutually
  exclusive `if/else` alternatives (error message vs. data at the same spot,
  format variants) — no true overdraw among them.

**Three real bugs found, all fixed:**

- **The scientific calculator's second hint page ran 16 px off the right edge** —
  `"const: c kB Re mu g0  suffix p n u m k M G"` is 42 characters, and only 39 fit
  from x=4. The clipped tail was the **M and G suffixes** — and the line was
  *also* missing **T**, which the parser accepts (`case 'T': 1e12`). The line is
  now `const c kB Re mu g0 sfx p n u m k M G T` — 39 characters, complete.
- **Morse Meteors' bottom band was 4 px short.** The typed-key line sat at y=124,
  so the y=127 footer overwrote its bottom five pixel rows — and a letter one
  frame from the ground drew its code hint at y≈127, inside the footer band. The
  play area is now compressed (ground line y=108, letters die at 104, Key/target
  row at y=112) and a low letter's code hint flips above it instead of below, so
  nothing reaches the footer. Letters also now spawn at y=28 instead of 22, which
  had them clipping through the bottom half of the Score row.

Two informational items were left as designed: the y=120 last-content-row idiom
(Rove planner's scroll hint, Overhead's count line) lets only the descender pixel
row touch the footer's cap row — the audit tool now classifies that separately
(`DESC-TOUCH`) so it never drowns out a real overlap.

## The orbital & Doppler math audit

The highest-stakes math in the firmware got the full treatment: **the real
`predict.cpp` and the real `gpToTle`, compiled on the host and raced against
skyfield** (IAU frames, JPL DE421) on a fresh ISS element set. Zero defects.
Passes within ±1 s, look angles within 0.002°, range rate within 0.23 Hz at
70 cm near epoch, 31/31 eclipse transitions agreeing, beta angle to 0.002° —
and the One True Rule plus both hold modes closed against a simulated inverting
transponder to under half a hertz over a whole pass. The one apparent
discrepancy (0.34° of beta) turned out to be the *reference* mixing frames;
the firmware was right. Full report:
`docs/design/ORBITAL_MATH_AUDIT_0.9.59.md`. The harness is permanent —
`tools/host_orbit_audit/` — the same keep-the-proof pattern as the print
raster's `ppm2pwg` validation.

# Housekeeping

- `FW_VERSION` → **0.9.59**; the cheat card and manual PDF pick it up on rebuild.
- The superseded `docs/design/ROTATOR_TRANSPORTS.md` now carries a banner pointing
  at the as-built `docs/interfaces/ROTATOR_TRANSPORTS.md`.

# Verification status

- **Docs and comments:** every corrected claim was verified against the 0.9.58.1
  source at the cited lines; where two documents disagreed, the source decided.
  Balance / parity / screen-text gates pass after every source touch.
- **The default flip is a build-config change, not a code change** — the compiled
  paths are exactly 0.9.58.1's, and the full monolith has been compile-verified
  against pristine EspUsbHost v2.3.2 on arduino-esp32 3.2.1. Worth one sanity
  build+flash on the bench all the same — the first build after pulling exercises
  the EspUsbHost dependency on a machine that may not have it installed yet
  (v2.3.1+ from Library Manager; no patch).
- **Still open from 0.9.58.1:** everything in `docs/THINGS_TO_VERIFY.md` — notably
  two-adapters-at-once, the Grove/USB rotator wires against a physical controller,
  the IC-9700 over LAN and over USB, and the raw TCP:9100 receipt path.
