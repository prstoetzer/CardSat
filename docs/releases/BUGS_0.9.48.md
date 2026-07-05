# CardSat 0.9.48 — bug list

Status key: **OPEN** (found, not yet fixed) · **FIXED** (done, pending release) · **WONTFIX**.

## 1. Weather / space weather did not persist across reboots on SD-equipped units — **FIXED**

**Field report (0.9.47):** update weather and space weather before leaving, reboot in the
field, data gone — despite the 0.9.47 "offline pass" claiming both were cached.

**Root cause:** a filesystem split. `Store::begin()` mounts **one** filesystem — the
microSD card when present (the default; the header comment says so plainly), internal
LittleFS only as the no-card fallback. On an SD-equipped unit **LittleFS is never
mounted**. GP and AMSAT status marks persist because they go through `Store::fs()`; but
the weather cache **writer and reader** (fetchWeather / loadWeatherCache), the space
weather loader (loadSpaceWeather), and the AMSAT **catalog-map loader**
(applyAmsatCatalogFile) all used **raw `LittleFS.open`** — which silently fails on every
open when LittleFS isn't mounted. So on SD units nothing was ever cached and nothing ever
loaded, while the fetches still populated RAM, making everything *look* fine online. The
0.9.47 offline audit verified call-sites and cache formats but never asked *which
filesystem* — precisely the blind spot.

**Fix:** all four call sites now use `Store::fs()` (satdb already included storage.h — its
status loader used Store::fs(), which is why status marks worked while the catalog map
didn't). A repo-wide audit confirms **zero raw `LittleFS.open` calls remain on data
files**. A `[boot] caches: wx=.. spacewx=.. (fs=SD|LittleFS)` serial line now reports
cache-load results at startup so this failure class is bench-visible.

**Impact of the fourth site:** the AMSAT catalog name map also never rebuilt from cache
offline on SD units (unreported — reporting needs WiFi anyway, but the status screen's
map-first matching lost precision offline).

**Bench check:** update weather + space weather online, power-cycle with WiFi off, confirm
both screens show the cached data and the boot serial line reads `wx=ok spacewx=ok (fs=SD)`.

## 2. Char lookup tool added to Tools — **new feature**

Third entry in the Tools hub (SCR_CHARLK, after the two calculators; the form-id offset
moved to toolsSel-3 in both files). Enter a byte in **hex / dec / bin / oct** (,// cycles
the entry base, digits type in it, ;/. browse ±1 with uint8 wrap) and the screen shows
everything the value represents at once: the ASCII character (control codes by name from
a 33-entry table + DEL), the **Morse** pattern (letters reuse the game's MORSE_TBL --
which precedes the block in both files, verified -- digits from a new 10-entry table),
the **Baudot/ITA2 (US-TTY)** letters/figures meaning for 5-bit values, and the **BCD**
reading (CI-V frequency bytes). Typing a non-hex letter looks that character up directly;
x zeroes; DEL drops the low digit (edit-only per the 0.9.47 convention); backtick exits.
Tables verified before firmware (ITA2 by the classic RYRY alternating-bit check plus
known figures; ASCII names spot-checked); the entry model host-tested across all four
bases, direct char lookup, masking and browse wrap (9/9).

## 3. DXCC entity lookup tool added — **new feature**

Fourth Tools entry (SCR_DXLK search + SCR_DXLKD detail; form-id offset moved to
toolsSel-4). Type a prefix, partial name, or entity code; matches list live (prefix
matches by comma/space-separated token startswith, name by case-insensitive substring,
code by numeric value so 001 == 1). ENTER opens a detail card: entity code, primary
prefix, continent, ITU and CQ zones (verbatim, so multi-zone like "06-08" and
note-referenced "(E)" render exactly), name, deleted/third-party flags, and up to two
ARRL footnotes. Deleted entities are dimmed in the list.

Data is an embedded table (src/dxcc_lookup.h, also inlined into the .ino) generated from
the live ARRL DXCC list by a committed generator (tools/gen_dxcc_lookup.py) -- re-run it
when ARRL updates (about yearly). Generator handles the file's quirks: multi-continent
entities (Maldives AS,AF; Turkiye EU,AS -- initially dropped by a single-continent regex,
caught against the 340 header count), separate footnote namespaces for the Current vs
Deleted sections (both number from 2), note 1 ("Unofficial prefix.") having no leading
paren, prefix cleanup of stray note-ref commas, and -- caught by a host compile --
leading-zero entity codes (049/082) that C++ reads as invalid octal, now emitted as
plain ints. Result: 340 current + 62 deleted = 402 entities, 107 footnotes. Search
host-tested against the real table (USA by code and prefix, multi-continent by name,
deleted entities flagged, numeric-code padding) -- all correct.

## 4. CQ (WAZ) zone reference added — **new feature**

Fifth Tools entry (SCR_CQZ list + SCR_CQZD detail; form-id offset moved to toolsSel-5).
All 40 CQ zones listed with their names; ENTER opens a detail card showing the full
prefix/region text, word-wrapped to the screen and scrollable (;/.) when long. Data is
an embedded table (src/cqzones.h, inlined into the .ino) generated from the CQ WAZ list
by a committed generator (tools/gen_cqzones.py) -- re-run when CQ updates. Nicely
cross-linked: from a DXCC entity's detail card, 'z' parses the leading number of the CQ
field (e.g. "03-05" -> 3, "(H)" -> no jump) and opens that zone's definition, returning
to the DXCC card on back. Word-wrap loop host-tested for termination and correct
space-breaking against the longest real zone entries; CQ-field parsing tested across
multi-zone, single, and zone-note-letter forms.

## 5. ITU zone reference added — **new feature**

Sixth Tools entry (SCR_ITUZ list + SCR_ITUZD detail; form-id offset moved to toolsSel-6).
ITU zones have no descriptive names -- just number + prefix/region text -- and the
numbering is not contiguous: the RSGB source lists 1-75, 78, and 90 (77 zones total).
Same list/detail shape as the CQ-zone tool with word-wrapped scrollable detail. Data is
an embedded table (src/ituzones.h, inlined into the .ino) generated by a committed
script (tools/gen_ituzones.py) from the RSGB ITU zones PDF -- the generator reads the PDF
via pypdf and handles the one entry (zone 78) that the source prints without a period
after the number. Cross-linked from DXCC: on an entity detail card 'z' opens the CQ zone
and 'i' opens the ITU zone (both parse the leading number of the respective field, so
"06-08" -> 6 and "(E)" -> no jump; openItuZone also no-ops for any number not in the
list). Zone-field parsing and the 77-zone extraction host-verified.

## 6. Help/Learn: modulation and satellite-telemetry sections added — **new content**

Two new sections appended to the on-device Learn page (`l` from Help): a MODULATION
reference (how modulation carries signals, then HF modes CW/SSB/AM/RTTY/PSK31/FT8, VHF/UHF
FM/SSB/CW and digital voice D-STAR/C4FM/DMR, satellite linear-transponder vs FM vs digital
modes, and EME with CW/JT65/Q65 and why slow coded modes win); and a SATELLITE TELEMETRY
explainer (what telemetry is, the sensor->ADC->frame->beacon->ground-decode chain, the
history of modes from OSCAR-1's temperature-keyed CW beacon through Morse/RTTY, 1980s-90s
AFSK/FSK packet AX.25 at 1200/9600 bps, to modern FEC-coded BPSK/FSK decoded by FoxTelem/
SatNOGS, and why reporting decoded telemetry helps). 103 lines added; all within the
~35-char render width (checked); the existing scroll clamp handles the longer page. Help
hub 'l' label updated to mention the new content. Text-only, no logic change.

## 7. Link budget calculator added — **new feature**

Seventh Tools entry (SCR_LINKB, dedicated screen; form-id offset moved to toolsSel-7).
Twelve inputs exceed the shared form engine's 6-field limit, so this is a standalone
handler: a 5-row scrolling input window (freq, distance, TX power/line/antenna, extra
losses, RX antenna/line/NF, bandwidth, required SNR) above four pinned live-output lines.
Row 0 is a mode preset (,//) setting bandwidth + required-SNR pairs -- CW 500 Hz/6 dB,
SSB 2400/10, FM 15k/12, AFSK-1k2 15k/13, GMSK-9k6 20k/13, FT8 2500/-20, JT65-Q65
2500/-24, Custom (leaves both untouched); both stay editable. Chain: EIRP = Ptx(dBm) -
line + gain; Prx = EIRP - FSPL(32.44 + 20log d + 20log f) - extra + RXgain - RXline;
N = -174 + 10log BW + NF; SNR; margin vs required, color-coded (>=6 green "solid", 0-6
orange "thin", <0 red "no copy"); plus an IARU S-meter estimate (S9 = -93 dBm >= 30 MHz,
-73 below, 6 dB/S-unit, clamped at S0). Math verified against the textbook FSPL figure
(145.2 dB @ 435 MHz/1000 km) and two worked budgets (LEO FM uplink, sat SSB downlink);
the exact screen computation ported and re-verified including the HF S-meter reference
switch and margin-color thresholds. keyLinkB follows the DEL-edit-only/backtick-exit
convention.

## 8. World map: night shading toggleable — **new option**

'n' on the World Map toggles the day/night terminator shading on/off, following the
screen's existing 'c' (recenter) precedent: a new persisted setting (cfg.mapNightShade,
JSON key "mapnight", default ON) saved with cfg.save() on each toggle -- the JSON
key-value config makes the new field backward compatible (older configs just default
it). The shading block in drawWorldMap is gated on the setting; footer updated to hint
'n night' in both center-mode variants (37 chars, fits). Struct field + load + save +
draw gate + key + footer applied to settings.h/settings.cpp/app.cpp and all mirrored
into the .ino (drawWorldMap/keyWorldMap byte-identical).

## 9. RF exposure (MPE) + battery runtime calculators added — **new features**

Two live-recalc forms added to the Tools form engine (TOOL_RFEXP, TOOL_BATT). RF exposure:
freq/power/duty/gain -> FCC OET-65 MPE limits (controlled and uncontrolled by band) and
far-field compliance distances d = sqrt(P_mW*G/(4*pi*S)), plus average power and a
ground-reflection (x2 distance) worst case. Validated vs known ARRL-calculator output
(100 W / 146 MHz / 2.15 dBi -> 2.55 m uncontrolled, 1.14 m controlled). Labeled as a
far-field planning estimate, not a substitute for a full station evaluation. Battery
runtime: capacity/RX/TX/duty/usable -> duty-weighted average current + runtime (decimal +
h:m); verified (20 Ah, 0.5/8 A, 30% duty, 80% -> 2.75 A, 5h49m). Both join yagi/quad in
the ,// output-scroll footer hint; host-tested; applied byte-identically to src and .ino.

## 10. Orbit lifetime (debris assessment) calculator added — **new feature**

Tenth Tools form (TOOL_DEBRIS). Inputs altitude/mass/area/Cd; outputs ballistic
coefficient and estimated orbital lifetime with pass/fail vs the 25-year and 5-year
post-mission disposal guidelines -- aimed at the AMSAT CubeSat-builder community.
Physics: near-circular drag decay da/dt = -B*rho*sqrt(mu*a), B = Cd*A/m, integrated in
2 km slices from the start altitude down to 150 km through Vallado's exponential-
atmosphere model (14-band nominal density + scale-height table). Validated against known
lifetimes BEFORE firmware (3U CubeSat ~0.5 yr @400 km, ~16 yr @600 km near the classic
25-yr line, ~260 yr @800 km; heavy ISS-altitude object ~5 yr), and the actual compiled
C++ orbitLifetimeYr() was host-run and matched the Python reference to 0.01 yr across all
cases. Step-converged (2 km stable vs 1 km). Clearly labeled in-tool and in docs as an
order-of-magnitude nominal-solar-activity estimate, NOT a compliance determination --
points to NASA DAS as authoritative. Joins the ,// output-scroll set; mirrored
byte-identically to the .ino.

## 11. Cross-section (drag area) calculator added — **new feature**

Eleventh Tools form (TOOL_XAREA). A pick-list preset field (0.5U/1U/2U/3U/6U/12U/16U/
Custom) that, when cycled, fills editable body X/Y/Z fields -- implemented via a new
preset-apply hook in keyToolForm (when toolId==TOOL_XAREA and the pick changes, non-zero
preset dims populate tfVal[1..3]; Custom leaves them alone). Plus a deployable panel-area
field. Outputs: end-on (min face), broadside (max face + panel), max projected at any
orientation sqrt(ab^2+bc^2+ca^2) (Cauchy-Schwarz), and the tumbling average (ab+bc+ca)/2
(Cauchy's theorem; a two-sided flat panel contributes area/2). The tumbling average is
surfaced as "-> debris Area" since it is exactly the Area input the Orbit-lifetime
(debris) tool wants. Geometry validated (cube -> 1.5s^2 tumble, sqrt3*s^2 max; 3U 0.035
m2 tumble; 6U+panels) before firmware; compute host-tested. First form to combine a
pick-list field with a live coupling to numeric fields -- tfChoiceLabel extended for the
preset names; all four touched functions (toolFormInit/drawToolForm/keyToolForm/
tfChoiceLabel) mirrored byte-identically.

# Release review (packaging gate)

Full ground-truth documentation review before packaging. RELEASE_NOTES_0.9.48.md written
(persistence fix as headline, then the Tools-as-field-bench story, Learn additions, night
shade, and rolled-up fixes). README "New in v0.9.48" blurb added above 0.9.47's. Cheat
card Tools line updated for the 20-tool hub -- initially spilled to 3 pages, trimmed to a
~110-char summary to hold 2 pages (auto-fit confirmed). Manual rebuilt at v0.9.48; cheat
card + manual copied to root and docs/. FEATURES current (each tool added as built).

Ground-truth cross-checks at the gate: TOOLS_NAMES = 20 entries (matches notes); embedded
tables DXCC 402 / CQ 40 / ITU 77 (match); 8 coax cables; DXCC z/i cross-link keys present;
all four mission tools documented in the manual. Suite: balance 0/46, all parity checks
green, 108 Screen dispatch cases matching between src and .ino, FW_VERSION 0.9.48 in both,
zero raw LittleFS.open on data files. Release zip: CardSat-0.9.48.zip (dot naming).

NOTE: the tree is host-verified only; a full compile-and-flash of the complete 0.9.48 tree
is still pending and remains the gating pre-tag step (see TEST checklist: persistence
bench check on SD, DEL/key behavior across all new Tools screens, the three lookup
databases and their jumps, and on-device render/scroll of the new calculators).
