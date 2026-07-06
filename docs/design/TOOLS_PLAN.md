# Planning: Tools hub — refinements and additions

**Status:** planning only — no code changed.
**Basis:** audit of the current Tools implementation (20 tools; form engine in
`toolFormInit`/`drawToolForm`; `keyTools`; the tables in app.cpp) plus the operating
patterns the rest of CardSat already establishes.

---

## 1. Where the Tools hub stands

Twenty tools: seven standalone screens (sci calc, programmer calc, char lookup, DXCC, CQ,
ITU, link budget) and thirteen numeric **form tools** on a shared engine (≤ `TF_MAXF` = 6
fields; numeric or pick-list fields; live recompute; scrollable results with `,`/`/`).
Since 0.9.50 the standalone/form split is guarded by `TOOLS_FIRST_FORM` + a
`static_assert`, so insertions are safe from mis-indexing — but the *discipline* remains:
a new form tool touches the enum, `TOOLS_NAMES`, the init switch, the compute switch, and
(if it has presets) `tfChoiceLabel`.

Friction observed in the audit:
- **The menu has no first-letter jump** — Home has one (a letter moves to the next item
  starting with it), and at 20 scrolling entries Tools wants it more than Home does.
- **`toolsSel` resets to 0 on every entry** (`About → t`), so a repeatedly-used tool is
  re-scrolled to every time.
- **Every tool re-opens with factory defaults.** Station-specific values — your coax
  type, feedline length, TX power, antenna gain — are re-typed on every visit. Nothing
  persists.
- **Units are imperial-fixed in the antenna tools** (lengths in ft; metric shown only as
  a secondary output line), while the weather already honors a units setting
  (`cfg.units`). Metric-station users edit around this constantly.
- One item from the 0.9.50 plan was only half-delivered: RF exposure got its louder
  disclaimer, but the **per-mode duty-cycle presets** (CW/SSB/FM/FT8 typical duty) were
  not implemented. Carried forward here rather than quietly dropped.

## 2. Refinements to what exists

### 2.1 Menu usability (small, high value)
- **First-letter jump**, exactly Home's mechanism (compare `HOME_ITEMS[i][0]`).
- **Remember the last tool** for the session (don't reset `toolsSel` in the `t` handler);
  optionally persist it with the tool-defaults file below.
- Optional: thin **category separators** in the list (Calculators / References / RF &
  antennas / Mission), as Home's band separators — worth it only if the list grows past
  ~24; not before.

### 2.2 Persist per-tool values (the biggest quality-of-life win)
A small `tools.json` (`Store::fs()`, existing calib.txt/tones.txt pattern): on leaving a
form tool, save its `tfVal[]`/`tfChoice[]`; on open, load them over the defaults.
~13 tools × 6 doubles — trivially small, streamed like everything else. The link budget
(12 inputs, its own screen) is the strongest case: a station's power/lines/antennas are
stable; only distance and frequency change per pass — and those two now sync live. A
reset-to-defaults key (`x` is already "clear" in several screens) completes it.
Care: the live-synced link-budget fields (distance/freq) should NOT be persisted over —
persist the station half, sync the pass half.

### 2.3 Units preference for antenna tools
Honor a length-units choice (reuse `cfg.units` or a dedicated setting) in dipole /
vertical / yagi / quad / coax-length: inputs and primary outputs in the chosen system,
the other system as the secondary line (today's metric line, mirrored). Pure
presentation; the math stays metric-internal.

**Hard constraint — the units preference NEVER applies to orbital or spacecraft
quantities.** Orbital distances (slant range, altitude, apogee/perigee, footprint) and
satellite physical properties (CubeSat body dimensions, cross-section area, mass) are
metric in this domain, always, worldwide -- aerospace and orbital mechanics do not use
imperial, and an operator would rightly find ft/lb altitudes and satellite sizes wrong.
So the units switch is scoped strictly to **antenna and feedline dimensions a ham cuts by
hand** (dipole legs, vertical/radial lengths, yagi/quad element lengths & spacing, coax
length, stub/phasing-line length). The link-budget Distance (km), the debris tool
(km / kg / m2), the cross-section tool (cm / m2), and any slant-range/altitude readout
stay metric regardless of the setting. The RF units tool (dBm/W/V) and unit converter are
explicit converters and are unaffected by the preference (they show all forms by design).

### 2.4 Individual tool touch-ups
- **RF exposure:** the carried-forward **duty presets** — a pick-list field (CW ~40%,
  SSB ~20%, FM/digital 100%, FT8 ~50%) that sets the Duty value; Custom leaves it alone.
  Same pattern as the link budget's Mode row.
- **Coax tool:** add a **velocity-factor column** to `COAX_TBL` (RG-58 .66, LMR-400 .85,
  etc.). This is groundwork for the phasing-line tool below and lets the coax tool also
  show electrical length.
- **Unit converter:** add kg↔lb and in↔cm rows (satellite mass and hardware dimensions
  come up constantly in the debris/cross-section workflow).
- **Sci calc:** a degrees/radians indicator is present; no change needed (audited fine).

## 3. New tool candidates

### Tier A — build these
1. **Phasing line / stub calculator** *(flagship — directly on-mission).* Circularly
   polarized satellite antennas (turnstiles, eggbeaters, crossed yagis) need 90°
   phasing harnesses; matching often uses λ/4 stubs. Inputs: frequency, coax type
   (reuses `COAX_TBL` + the new VF column), fraction (¼λ, ½λ, ⅜λ, custom degrees).
   Outputs: **electrical length in feet+inches and meters** for that cable, plus
   free-space wavelength for reference. Small form tool; the VF column is the only
   prerequisite. No other tool on the device covers this, and it is exactly what a
   CardSat user building a satellite antenna needs at the bench.
2. **Wavelength / frequency** — λ=c/f with ¼/½/⅝-wave lines in both unit systems, and
   the reverse (length→frequency). Trivial math, constantly used, and it declutters the
   dipole tool from being misused as a wavelength calculator.
3. **Operating references (Q-codes + phonetics + RST)** — one standalone reference
   screen, three tabs/keys: common Q-codes (~20), ITU phonetic alphabet, RST system.
   Static PROGMEM text like the CQ/ITU zone screens; zero RAM cost worth mentioning.
   New-ham value on a device AMSAT demos publicly is high.
4. **CTCSS tone reference** — the standard EIA tone list, with the FM birds' known
   uplink tones surfaced from the existing `knownCtcssHz` table ("ISS 67.0, SO-50 67.0
   (74.4 arm)…"). Tiny; pairs naturally with the FM-satellite workflow.

### Tier B — good, second wave
5. **Attenuator pad designer** — pi/T resistor values for a target dB at 50 Ω (and the
   reverse: nearest-standard-resistor check). Classic bench math, small form tool.
6. **dB chain quick-sum** — add/subtract a short chain of gains/losses with a running
   total (the link budget does the full system; this is the two-line "coax plus
   preamp minus splitter" case). Possibly fold into RF units instead of a new entry.
7. **Char lookup: full-table view** — a browsable A–Z/0–9 Morse + Baudot table (the
   lookup shows one character at a time today). A display mode inside the existing
   tool, not a new menu entry.

### Tier C — watch list (not now)
8. **TLE line decoder** (type a TLE line, see epoch/incl/RAAN decoded + checksum
   verify) — useful for debugging manual satellite entry, but keyboard-entering 69
   characters on the Cardputer is painful; the manual-sat flow already parses TLEs.
9. **Timezone/UTC converter** — the header already shows Z time; low value.
10. **Anything duplicating existing screens** — grid distance/bearing, band plan
    (Learn → `f`), EME degradation, Doppler (analysis page) all live elsewhere;
    Tools should link, not clone. (A "see also" line in the manual suffices.)

## 4. Engine and infrastructure notes

- **`TF_MAXF` = 6** fields is enough for every candidate above (phasing tool needs 4).
  No engine growth required.
- Menu length goes 20 → 24 with Tier A. Still fine on the 11-row scrolling list,
  especially with first-letter jump; categories (2.1) only if a later wave pushes past
  that.
- New reference tables (Q-codes, phonetics, CTCSS) go in PROGMEM/flash like the zone
  tables — 8 MB flash has ample headroom (ino currently ~1.7 MB of source; the compiled
  app partition is the real budget and these tables are a few KB).
- Every addition follows the now-guarded insertion discipline (enum + names + init +
  compute + `tfChoiceLabel` if preset), dual-applied; the `static_assert` catches
  count mismatches at build time.
- Each new tool costs a **manual section, a screenshot, and possibly a cheat-card
  line** — the cheat card is at its 2-page limit, so new tools get covered by the
  existing collective Tools line unless something earns key-level mention.
- Persistence (2.2) adds one new file write path — uses `Store::fs()` (never raw
  LittleFS), same as every data file since the 0.9.48 fix.

## 5. Suggested shape

**Wave 1 (with the next cycle):** menu first-letter jump + last-tool memory (§2.1),
per-tool persistence (§2.2), coax VF column (§2.4), **phasing-line calculator** (§3.1),
wavelength tool (§3.2).
**Wave 2:** units preference (§2.3), RF-exposure duty presets (§2.4), operating
references + CTCSS reference (§3.3–3.4).
**Wave 3:** attenuator pad, dB chain, char-table view (§3.5–3.7) as filler alongside
other work.

## 6. Open decisions (for Paul)

- **Persistence scope:** persist all form tools, or a whitelist (station-flavored tools
  only: coax, link budget, RF exposure, battery)? All is simpler; whitelist avoids
  surprising "why does the debris tool remember last week's satellite" moments.
- **Units setting:** reuse the existing weather `cfg.units` (one switch governs both) or
  a separate "antenna lengths" setting? Reuse is fewer settings; separate matches
  operators who think in °F but build in metric.
- **VF values for `COAX_TBL`:** I'll source standard published VFs per cable type at
  implementation time — worth a quick sanity pass from you, since a wrong VF quietly
  produces wrongly-cut phasing lines (this tool gets the same "verify before trusting a
  build" caveat the antenna tools carry).
- **Q-code/phonetic content:** happy to draft; you may want a specific AMSAT-flavored
  selection (e.g. include QRZ-the-question vs the website joke, satellite-relevant
  Q-codes like QSL/QRM emphasis).
