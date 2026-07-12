# CardSat v0.9.54 — release notes

*July 12, 2026. Theme: large GP sources handled honestly, a bench console — and a Learn corner.*

## Favorites-first GP loading, and the cap made visible

The 150-satellite in-RAM catalog has always been a silent truncation point: pick a
CelesTrak group larger than 150 (several are; some are *far* larger) and CardSat kept
the first 150 in file order without telling you — a satellite you cared about could
simply never load, with no indication it existed.

v0.9.54 replaces that with a **two-pass priority load** (`loadGpFromFilePreferring`):

- **Pass 1** streams the file and loads only satellites on your **favorites** list —
  guaranteed, even if they sit past the 150th object.
- **Pass 2** streams again and fills the remaining slots in file order.
- The parser now counts objects **seen** separately from objects **loaded**
  (`seenCount()` / `wasTruncated()`), so truncation is *visible*: the status line says
  **"Loaded X of Y (cap X)"** and the boot log prints
  `[gp] parsed X of Y satellites (truncated; favorites kept)`.

The boot sequence now loads favorites *before* the cached-GP load, so the guarantee
holds offline too. Both passes reuse the same static buffers as before — streaming,
256-byte reads, flat RAM regardless of file size — so there is **no steady-state heap
cost**. The cost is a second parse on refresh (the file is already on SD; tens of KB
for AMSAT, negligible).

If you need a bird from deep inside a big group: favorite it, refresh, done.

## What happened to "rule-based subsets" (honesty section)

The design work proposed rules like "the 150 nearest right now" for mega-constellations.
That was evaluated for this release and **deferred, with a concrete reason**: a
nearest-now rule requires a full SGP4 propagation per candidate, and at
mega-constellation scale (thousands of objects) per-candidate propagation at every boot
is not viable on this software-double, no-PSRAM part. What ships instead is the honest
core: favorites always survive, the fill order is documented and predictable, and the
truncation is announced. The new `scanGpFile(accept, ctx)` callback is deliberately the
seam a future **on-demand** rule action (user-triggered, not at boot) would plug into.
`docs/design/GP_LARGE_DISTRIBUTIONS_SCOPE.md` carries a status update to match.

## Download preflight

GP downloads are now checked **before any byte is written**: if the server declares a
Content-Length that (plus a 32 KB margin) exceeds free space on the active filesystem,
the download is refused with **"file too big for storage (XKB > YKB free)"** — and the
previous good catalog is left untouched. This mainly protects **internal-flash units**,
whose LittleFS partition a multi-megabyte CelesTrak group would otherwise fill
mid-write. On SD the check effectively never fires.

## USB serial console (read-only)

Connect any serial monitor at **115200 baud** and type `help`:

```
ver    firmware version + build date
heap   free heap + largest contiguous block
sats   catalog count (loaded/seen) + truncation
fav    favorites count + active satellite
next   next pass for the active satellite
net    WiFi state, IP, RSSI
time   current UTC + clock status
gps    fix, sats, lat/lon, grid
bat    battery percent
fs     storage backend + free space
up     uptime
pass <sat>   next pass, any catalog sat
```

Design constraints, deliberately: **read-only** (no command changes device state, so it
is safe to leave connected on a shared bench) and **zero heap** (a fixed 64-byte line
buffer, no allocation). It rides the same USB cable used for flashing, and it exists
because this project's whole history is diagnostic-driven — `heap` and `sats` are the
two numbers we reach for first. `pass` accepts a name fragment or a NORAD number
("pass ao-91", "pass 43017") and answers for any bird in the catalog, not just the
active one — the case-insensitive match uses a local helper rather than the
non-portable strcasestr.

## CubeSatSim C2C reference (Tools hub, now 32)

A new offline reference card: how to command AMSAT's **CubeSat Simulator** over the air
— DTMF (`<mode #>` then `#`), APRS (`MODE=<letter>`), and the bare-carrier mode-step —
plus the mode table (1 APRS/a … 7 FUNcube/j; note CW is `m` and repeater is `e`) and the
key `CubeSatSim/config` options (`-q` squelch, `-F` frequency, `-A` command another sim,
`-o` beacon). The table was **verified against the CubeSatSim v2.x docs**
(cubesatsim.org Lite readme + the config script source, July 2026), and the screen says
so — with a "check the wiki for your firmware version" caveat, since CardSat has not
exercised these commands on the air. CardSat does not transmit them itself; this is a
crib for the operator.

## AMSAT Fox anatomy (Learn)

Help gains `a`: an **animated anatomy of an AMSAT Fox CubeSat** — a rotating 1U
wireframe (procedural, ~15 fps on the Orbit-zoo tick) with one labeled callout at a
time, its leader line tracking the spinning body. Seven callouts, every one verified
against the **AMSAT Fox Documentation** compendium (amsat.org, June 2018): deployable
70cm-RX/2m-TX whips (*the only deployables*), fixed solar panels on all six sides, the
eclipse battery, the IHU flight computer, the FM U/v transponder, permanent-magnet
passive stabilization, and the university experiment bay (MEMS-gyro attitude
experiment). Zero heap (per-frame computation, fixed tables, no allocation — the
Orbit-zoo discipline), ~4 KB of flash, no bitmaps.

Two companion **text screens** round out the Learn set. `i` inside the anatomy opens
a short **Fox & CubeSats** primer — what a CubeSat is, told through AMSAT's own
series, every claim grounded in the Fox documentation (1U standard, FM-repeater
mission with simple ground gear, 200 bps telemetry under the voice, the MEMS-gyro
student experiment). Help `c` opens a **CubeSat Simulator** intro, verified against
cubesatsim.com (fully open source, solar + battery, UHF telemetry in real formats,
Pi Zero 2 + Pi Pico on three custom boards, kits and bare PCBs at the AMSAT Store),
cross-linking the Tools C2C reference for commanding one. Both are static scrollers —
zero heap, zero animation cost.

## Hygiene shipped alongside

- **Inline-comment accuracy pass**: nine stale mbedTLS-era / 8bpp-era comments corrected
  to describe current behavior (BearSSL transport, 4bpp sprite), with migration history
  deliberately preserved as history. The LoTW after-POST instrumentation comment now
  records the 0.9.53 on-device verdict (no ratchet) and stays as a regression sentinel.
- **The verification gate now travels with the repo**: `tools/check_balance.py`
  (comment/string-aware) and `tools/check_parity.py` are committed, so any environment
  can run the src↔ino gate.
- Documentation audits since 0.9.53: manual factual fixes (150-cap wording, the About
  screen's heap readout explained, a TLE-sunset note in §14, a planning-tools
  orientation), README callout consolidation, and new design/guide docs (GP large
  distributions, SSH client scope, codebase overview, development method).

## Global-hotkey audit (`h` / `b`)

An audit of the bare-letter global hotkeys found three screens where they stole
legitimate keystrokes: the **DXCC lookup**'s type-to-search could never receive an
`h` or a `b` (the prefix **HB9** was untypeable), the **character lookup** couldn't
look up those two characters, and the **Tools** first-letter jump couldn't reach the
**Battery** tool. Both hotkeys now share a single exclusion predicate covering the
two editors plus those three screens, so they can't drift apart again. Two suspects
were cleared: Home's menu jump has no H/B-named items (the globals keep working
there), and the CAT monitor's raw hex entry was already safe — it goes through the
shared editor. The manual's global-keys table also gains the previously undocumented
`h` row.

The audit extended to **every on-screen text block**, with a new committed gate
(`tools/check_screen_text.py`) that width-checks each static text array against its
renderer's pixel budget — modeling the band-plan table as the deliberately-clipping
columnar renderer it is, after the first naive run false-flagged it. Content findings,
fixed: the Help hub's key reference was missing sections for both 0.9.52 planners
(Workable horizon, Target search), the rove-plans list, and the CAT monitor — all
added with footer-verified keys only — and its GLOBAL block mixed true global keys
with Help-topic keys, now split into two labeled blocks. The glossary, guide, tech,
history, and all 0.9.54 text arrays passed width and typo checks clean.

## Verification status

Host-side gate: 46 files balanced, full src↔ino parity, mirror-identity confirmed on
every new function. **Not yet on hardware** — see `TEST_CHECKLIST_0.9.54.md`:
truncation status with an oversized group, the preflight refusal on internal flash, the
serial console, the CubeSatSim screen, and a favorites-past-150 survival test. The
upload instrumentation from 0.9.53 is retained this release as a regression sentinel.
