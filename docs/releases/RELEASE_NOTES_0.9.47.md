# CardSat v0.9.47 — release notes

A release for the **operator in the field**. CardSat can now **report a bird's status to
amsat.org with two keypresses** while you work it — and it targets the **right mode entry**
(AO-7's `[U/v]` vs `[V/a]`) from the transponder you're actually on. A new **Tools hub**
brings a scientific calculator, a programmer's calculator, coax-loss and antenna-dimension
calculators and more, all computed on-device. An **offline-robustness pass** makes every
cached data source safe against mid-transfer drops, so weather, space weather and AMSAT
status stay usable with no network at all. Add the readiness checklist, a two-column home
menu, an EME 30-day planner, and the largest fix set of the 0.9.4x line — much of it found
and confirmed on real hardware during this cycle. Nothing changes the on-air message
format; everything is backward-compatible with 0.9.46 CardSats.

## Report satellite status to AMSAT

The AMSAT status system is now **read-write**. While tracking a bird, press **`i` twice**
("I heard it") and CardSat posts a public **Heard** report to amsat.org under your callsign
and grid — the same report the web form submits, without leaving the pass.

The clever part is **mode awareness**. The status system tracks some birds per mode —
`AO-7_[U/v]` and `AO-7_[V/a]` are separate entries — and CardSat picks the one matching the
**active transponder's uplink/downlink bands**. Work AO-7's 432-up/145-down passband and the
report lands on `[U/v]`; the 145-up/29-down passband reports `[V/a]`. FM, telemetry and
digi tags match the transponder's character the same way. When the mode can't be resolved
(a beacon selected on a multi-mode bird), a **picker** opens instead of guessing.

The picker is also the deliberate path: **`p` on the AMSAT status screen** offers the mode
and all four canonical statuses (Heard / Telemetry Only / Not Heard / Crew Active). The
server replaces a repeat report for the same satellite, hour and 15-minute period, so a
double-send is harmless. Reporting needs your callsign in Settings, a set clock, and WiFi.

Under the hood this is driven by a fetched **catalog name map**: every entry of the status
API's satellite catalog is matched to your element set by a five-step resolver
(parenthesised designator → whole name → delimited token → legacy de-zeroed base → a small
alias table for birds with two unrelated names, e.g. CAS-3H ↔ LILACSAT-2). Validated
against the live catalog and the real AMSAT daily bulletin: every present bird matched,
zero false positives. The map refreshes with each elements update and is cached for
offline boots.

And the status screen answers one more question: **`g` ("who heard it")** fetches the
selected bird's recent individual reports — callsigns, grids, status and age, plus a
distinct-grid count, a quick sense of the reporting geography.

## Tools

A **Tools hub** (About → **`t`** — the main menu is full, and About is where CardSat's
auxiliary hubs live) with ten offline bench tools:

- a **scientific calculator** with a traditional **scrolling tape**: type an infix
  expression (`2+3*sin(45)`), ENTER evaluates onto the tape (`[`/`]` scroll history).
  Degree trig, `pi`/`e`/`Ans`, sin cos tan asin acos atan sqrt ln log exp abs.
- a **programmer's calculator**: a 64-bit value shown at once in **hex / dec / bin /
  oct**, up/down arrows move the entry base, `w` sets the width (8/16/32/64), with
  bitwise AND/OR/XOR/NOT, shifts, negate and the four arithmetic ops — CI-V byte math
  on the device that talks CI-V.
- **coax loss / power** (eight cable types, matched loss + SWR-added loss + watts at
  the load), **dipole**, **vertical/ground-plane**, **yagi** and **quad** dimensions
  (both with a selectable element count, listing every element as starting dimensions),
  **RF unit** conversions (dBm/dBW/Vrms/Vpp), **SWR / return loss**, **free-space path
  loss**, and a **unit converter**.

The antenna and feedline math is validated against the standard references; the numbers
are honest starting points (real yagi lengths depend on boom and element diameter). In
all of Tools, **DEL only edits — it never exits** (backtick leaves).

## Usable offline, safely

Everything CardSat fetches was already cached and reloaded at boot; this release closes
the remaining hole. The AMSAT status and catalog downloads previously wrote **directly to
the live cache**, so a mid-transfer drop — exactly what happens on a hilltop — could
truncate the cache and poison the next offline boot. All fetches now stage through a
scratch file and atomically swap over the real cache **only on success**: a failed refresh
always keeps the last good data. Weather (including cloud cover), space weather, AMSAT
status marks, the catalog map and elements all render cache-first with no network, and
each screen says plainly when a refresh wasn't possible. Only *submitting* a report and
the who-heard-it detail inherently need a connection.

## More from the data you already fetch

- **Cloud cover on visible passes.** The weather fetch also pulls the hourly cloud
  forecast (48 h), and the **visible-pass list and transit finder** now show the cloud
  percentage at event time, color-coded — an optically-promising pass under overcast is
  no longer a surprise.
- **Launch siblings, by name.** The orbital **Phys** page lists every cataloged object
  that shared your satellite's launch (COSPAR prefix), wrapped and clipped safely — plus
  the **element-set number**.
- **Space-weather trends.** Flux, Kp and A now show **rise/fall deltas** against the
  previous sample, so you can see which way conditions are moving, not just where they are.

## Operating flow

- **Station readiness** (About → `r`): one green/red checklist — clock, location,
  elements age, radio, rotator, WiFi, battery, storage — before you head out.
- The **Home menu is a two-column grid**: all twenty destinations visible at once, band
  separators, and **first-letter jump**. The layout was pixel-tuned on the actual display.
- **Post-LOS handoff**: at LOS the Track screen announces the next favorite pass; `q`
  (60 s window) deep-sleeps until it.
- The AOS get-ready alert **warns on a low battery** (≤30 %).
- An **EME 30-day planner** (`p` on the EME screen): per-day Moon declination and path
  degradation with good-day stars and a Sun-proximity warning.
- A **voice memo during a pass drops a log stub** (UTC, satellite, mode, frequencies;
  callsign blank) so the QSO is half-logged before you land.
- **Settings reorganized into six categories** (Radio/CAT, Rotator, Passes & alerts,
  Display & sound, Station & logging, Network & data) with `{`/`}` paging — all 85
  settings preserved.

## Fixes

The 0.9.46 post-release audit and this cycle's **on-hardware field testing** produced the
line's largest fix set. Highlights: the web panel's orbital velocity used the wrong angle
units; the three rotator "master" modes weren't mutually exclusive; the Mutual DX-Doppler
path could anchor to a stale activation; the QRZ→grid lookup froze the UI during the
fetch; the band-plan reference had several data errors (mode letters B/J swapped, three
microwave allocations corrected, and the 29 MHz designator confirmed as **A**); the AMSAT
reports parser stopped at the first `]` — which every mode-tagged name contains — and so
showed nothing (validated against the live payload after the fix); fetch banners lingered
over their result screens; the AMSAT status footer overflowed the display; and the DEL
key exited the new Tools screens instead of backspacing (the Cardputer delivers DEL via
its own flag, now handled like the codebase's proven text editor). Documentation got a
consistency pass with all spelling normalized to American English across 34 files.

The complete itemized list, including the audit findings and the hardware-compile fixes,
is in **[BUGS_0.9.47.md](BUGS_0.9.47.md)**.

## Upgrading

Flash as usual — no settings, log or on-air format changes. The AMSAT catalog map and the
new caches populate on the first elements update. First genuine status report doubles as
the live test of the POST path (the server's duplicate protection makes it harmless).
