# CardSat v0.9.60 — release notes

Version 0.9.60 is a workbench-and-play release: a networked multiplayer game, a live star field over the sky dome, recallable station presets, a sweep of UI-consistency fixes, a real fix for high-orbit pass lengths, voice memos that now work alongside USB CAT, and a round of RAM-fragmentation hardening for the no-PSRAM ESP32-S3. Highlights below, grouped into new features, fixes, and internals.

## New features

## Stars over the dome, and five QTHs in your pocket


Two adoptions from the SatObserver-MX comparison (`docs/design/COMPARE_SATOBSERVER_MX.md`).
The **Sky sources dome grows a star field**: 1,018 stars to magnitude 4.6,
constellation lines, and proper names for the sixteen brightest, recomputed live
from the clock through the same `raDecToAzEl` the radio sources use, brightness
mapped to pixel weight. `c` cycles the layers. The catalog compiles into ~9 KB
of flash rodata from d3-celestial's BSD-3 data (credited; regenerate with
`tools/make_star_tables.py`) — the same source SatObserver-MX draws, at roughly
a millionth of the RAM. Point an antenna at night by starlight.

**Named QTH presets**: `q` on the Location screen opens five recallable station
slots — ENTER recalls one straight into the live location (predictor updated
immediately) and turns GPS off, because recalling a named site is an explicit
choice the receiver shouldn't quietly win back; `s` stores the current position,
`e` names, `x` clears. Persisted in config.json.

## KESSLER goes multiplayer over LoRa


Two Cardputers can now play KESSLER against each other **over the existing LoRa
link**. One player presses `n` on the title screen to host — becomes P1, picks
the seed, gravity, and round count, and beacons an invite; another CardSat joins
as P2. The design is deterministic lockstep, which suits a half-duplex radio:
both stations hold the same RNG seed, so they build byte-identical terrain and
run identical physics, and only three tiny fixed-layout frames cross the air —
HELLO (seed/gravity/rounds), FIRE (angle/velocity/wind), SYNC (round result).
Every shot arcs the same on both screens with nothing streamed per frame; you
aim only on your turn, and the wire is silent between shots. It rides the same
infrastructure as LoRa messaging on a third frame type (0xC7) that other traffic
ignores, and adds ~2 KB of flash and zero resident RAM (the netplay state lives
in the game's existing heap struct). Range is your link's range.

### KESSLER — a two-player GORILLAS.BAS tribute


The games arcade (**About > Games**, `z` from About) gains a seventh title. **KESSLER** is the 1991 QBasic classic, altered to
a satellite theme: two lunar ground stations lob retired CubeSats at each other
across a skyline of habitat modules, under solar wind, with Earth hanging in
the black sky wearing the sun's famous shocked face when a shot flies through
it.

The tribute is literal where it counts. The trajectory is GORILLAS' own
PlotShot equation — `x = x₀ + v·cos(a)·t + ½(Wind/5)t²` — run unchanged in the
original 640×350 EGA virtual space and only scaled to 240×135 at the pixel;
the wind is drawn from the same `FnRan(10)−5 ± FnRan(10)` distribution; the
terrain follows MakeCityScape's EGA constants (BottomLine 335, slope walk with
V and inverted-V profiles); the stations stand on the second or third building
from each edge, as the gorillas did; time advances at GORILLAS' ~5 t-units per
second regardless of frame rate; and a velocity under 2 still lands the shot
on your own roof. Deliberate deviations, each argued in a source comment: a
5 px blast radius (the faithfully-scaled 2.7 px crater reads as a flicker),
per-player angle/velocity prefill (retyping three digits on a thumb keyboard
is homage nobody asked for), and gravity picked from Moon/Mars/Earth presets
(1.62 default — the floaty lunar arc suits a small screen) instead of typed.

Craters carve the modules column-by-column, and a crater under a station drops
it to the new surface. Direct hits score; first to N (1–9) takes the match and
the surviving station's dish does the victory dance. Two players, one
Cardputer, passed back and forth.

Bench rev B (device photos) reworked three things. The **aim UI is now a
two-field form** — both values always visible at a fixed centred position, the
active field bracketed and the form anchored to the aiming player's own side of the screen (P1 left, P2 right-aligned), digits and **backspace edit** (backspace had quit the
game), `,`/`/` switch fields, ENTER fires from the velocity side — replacing the
sequential ask that could strand a player in the velocity field without ever
seeing the angle ask. **Flight termination is guaranteed** (wider geometric
bounds plus a 40 t-unit ceiling; a wind-curved lob could previously loiter
outside the old bounds forever, with keys ignored). The title screen centred. Rev D, from device photos: **collision is evaluated
on sub-steps** (dt = 0.02 t-units, ~2 px per test) instead of once per frame —
at 15 fps a fast shot crossed ~13 px per frame, wider than the station box, so
photographed direct hits tunneled through untouched — and a **crater within
blast radius (+2 px) of a station now destroys it**, as GORILLAS' explosion
killed the adjacent gorilla; exactness is for the harness, not the players.
Turn handoff **repaints without a keypress** (a one-shot kick keeps the 15 fps
tier alive one frame past each in-draw phase change), the in-flight footer is
pinned to the recorded **shooter**, and the aim form shrank to a compact
per-side line that clears the Earth disc it used to overlap.
On the Sky dome, the **star-name labels became selectable objects** instead:
6-px labels collided with each other and the panel, so the sixteen named stars
now join the `;`/`.` list — cursor one, read its name/az/el in the side panel,
see a diamond on the dome. A later bench find: a projectile that flew off the TOP of the screen and arced
back down onto the enemy could pass through the hit box untouched. The sub-step
collision loop was sound, but the per-frame work cap *fast-forwarded* the sim
clock when a frame's time gap was large -- which happened exactly during a long
off-screen excursion -- silently skipping the descent sub-steps where the return
hit should register. The cap now bounds the sub-step COUNT and advances the
resume point only as far as actually simulated, so the next frame continues with
no step skipped, even across a long off-screen arc. (Verified: worst-case
vertical motion is ~7.5 px per sub-step, under the 9 px hit box, so a descent
always lands inside the window.) One launch-geometry fix also landed: the CubeSat spawns six
pixels above its station — *inside* the shooter's own nine-pixel hit box — so
the first flight frame scored a self-hit on every shot, at any angle and
velocity. A per-shot latch now keeps the shooter's own box immune until the
projectile has genuinely left the pad (arcing back down onto your own roof
remains a legitimate, classic loss), and a shot too slow to ever clear the pad
— the velocity-under-2 gag — detonates in place after ~1.6 seconds of pathetic
wobble instead of hovering forever.

Engineering notes: the entire game is one heap-allocated struct (~1 KB) that
exists only while the screen is open — the no-PSRAM discipline for
rarely-used features — and it animates on the existing 15 fps loop tier only
during flight and explosions, so the aim screen costs nothing. Costs at rest:
**8 bytes** of RAM (the pointer) and 5.5 KB of flash. And no, it is not
written in Tiny BASIC: GORILLAS needs `INPUT`, and the no-interactive-programs
rule stands — which is exactly why it lives in firmware.

## Voice memos during USB CAT (record + playback, RAM-gated)


Audio was refused outright while USB CAT was engaged, because the speaker's ~8 KB
I2S buffers don't fit the tight contiguous heap that leaves. That was too blunt:
recording uses the *mic* (~4 KB), and playback only needs the speaker when there's
actually room. `audioAcquire()` now checks the real largest free block at the
moment of the request and brings audio up if it clears a safe bar
(`AUDIO_MIN_BLOCK`), so **memo playback works alongside USB CAT whenever the heap
can spare the block** and declines with a clear message when it can't. Recording
gets a matching lightweight check (its mic buffer fits where playback's speaker
may not). Cosmetic beeps still simply stay silent under a tight heap. No blanket
refusal, no stranded allocations.

## Fixes and polish

### High-orbit pass length (the 60-minute ceiling)

High-orbit pass durations were capped at 60 minutes, and the fix had two parts
because the bug did. The visible symptom was a formatter: several pass displays
printed duration as a two-digit `%2ldm` field, so a four-hour Molniya or a
horizon-long geostationary window overflowed and read wrong. Fixing every
display surface (both pass lists, the Schedule screen, pass detail, sat detail,
the Length row, and the web dashboard) with a fixed-width helper that switches to
`HhMM`/`HHh` above an hour was necessary — but not sufficient, because the number
itself was still 60. The real cap lived in `buildSchedule`: an in-progress pass's
LOS was found by scanning elevation forward from now, hardcoded to 120 steps of
30 s = exactly 60 minutes. A LEO bird sets before that ceiling; a Molniya or a
GEO in view does not, so both reported a genuine, wrong 60-minute length. The
scan is now adaptive — for high orbits it walks a 24 h horizon with a
period-sized step and reports LOS at the horizon for a bird that never sets,
mirroring the finder's own synthetic pass. A Molniya now reads its true
multi-hour length and a GEO reads `24h`.

## Every selection list wraps around


Fourteen selection lists that clamped at their ends now wrap — press up at the
top and land on the last entry, down at the bottom and land on the first, the
way the games menu, Home, Settings, and most newer screens always have. The
converts: the **satellite list** (the named offender — reaching the far end of
150 birds no longer means holding a key), AMSAT status, the visible-pass list,
the DX Doppler table, Sun/Moon transits, sat-to-sat windows, CelesTrak search
results, DXCC lookup, the CQ and ITU zone lists, the transponder editor, both
target-search lists (the pick list wraps in the draw, where its filtered count
lives), and the sim step picker. Every converted list guards its count (an
empty list can't divide by zero) and every scroll-follow is absolute, so a
wrapped jump lands with the selection in view.

Free-scroll **document viewers** — help, glossary, the guides, reference
tables, report output — deliberately still stop at their ends: yanking a page
from under a reader mid-document is not navigation, it's a prank.

## The menu-order audit


Every menu reviewed for logical consistency (`docs/design/MENU_ORDER_AUDIT_0_9_60.md`);
reordered only where a band was genuinely broken. On **Home**, "Overhead now"
joins the sky column beside World Map and "Weather" opens the station column —
one swap, four renumbered cases, bands intact. The **Tools** menu's fifty-four
entries — accretion order, with orbit tools in three places — now display in
six labeled-in-spirit bands (calculators & code, satellite & orbit, antennas &
feedline, RF & measurement, electronics & power, references & lookups) via a
pure display permutation: canonical ids, form tables, print stems, and
persistence are untouched. **Settings** got five surgical in-category
regroupings (row ids verified unchanged as sets): rigctld joins Radio's
transport band, Rotor reads wire → network → motion-shaping → manual, and the
printer block leads with its transport. The Log category and the Games menu
were already right and were left alone.

## Under the hood

### Fixed-buffer line stores (fragmentation)


The rigctld, rotctld, and web-dashboard LAN handlers assembled incoming lines in
Arduino `String`s, appending one character per received byte — a heap
reallocation per byte on every CAT frequency-set, rotator command, and HTTP
request, thousands per operating session. All four line buffers are now fixed
`LineBuf` storage that allocates nothing on that hot path, handing each completed
line to the unchanged handlers as a C-string. Static RAM rises <1 KB; the churn
is gone. The CAT monitor's line ring and the self-test result list got the same
treatment (moving into fixed `char[][]` BSS). Together these remove the two
largest String-churn sources on the hot paths; static RAM rises a few KB in
exchange, which on this no-PSRAM part is the right trade — heap *fragmentation*,
not static size, is what threatens stability. First of the RAM-hardening items
from the staging analysis (`docs/design/RAM_STAGING_ANALYSIS_0_9_60.md`).
