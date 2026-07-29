# Documentation audit — 0.9.68 cycle open

Full audit of repo documentation and on-device documentation against the code as of
0.9.68-wip (post-0.9.67 release), covering consistency with current code, factual
correctness, grammar, logical ordering, and American English. Ground truth for every
count and key claim below was extracted from the source, not from prior docs.

## Ground truth extracted from code

| Fact | Value | Source |
|---|---|---|
| Home menu items | 20, two-column grid, no scrolling; `,`/`/` hop columns; letter jump | `HOME_ITEMS[]` + `keyHome` |
| Home item 13 | **Nearby & DX** (QRZ Lookup moved into it) | `HOME_ITEMS[]`, `keyNearby` |
| Nearby & DX hub items | APRS heard near me · DX cluster spots · ADS-B aircraft radar · QRZ callsign lookup | `NEARBY_ITEMS[]` |
| APRS feed | **live APRS-IS listen** (rotate.aprs2.net:14580), receive-only passcode −1, filter on login, requires callsign; socket closed on leave | `aprsStart()` |
| DX cluster keys | `f` fetch · `n` next band **with spots** · `p` print | `keyDxc` |
| ADS-B | operator-set aggregator URL (Settings → Network), `{"ac":[...]}`; `t` scatter target grid | `fetchAdsb`, `keyAdsb` |
| Tools | **63** tools, 6 categories | `TOOLS_NAMES[]`, `TOOLS_CAT_NAMES[]` |
| Games | **7** (incl. KESSLER 2-player) | `GAMES_N` |
| About → `p` print menu | **30** entries | `PA_ITEMS[]` |
| PrintReport enum total | 50 (contextual-only reports print from their own screens) | `enum PrintReport` |
| Orbit analysis pages | **11** | `"/11"` page indicator |
| Tiny BASIC caps | 4096-byte program, 500,000 statements/run, 2,000 SATSEL/run | `BASIC_PROG_MAX`, `BASIC_STMT_BUDGET`, `satselLeft` |
| Feed caps | APRS 250 · DXC 200 · ADS-B 250 · Telnet hosts 10 | `app.h` |

## Findings and dispositions

### Fixed — on-device help (`H[]` in `drawHelp`, dual-edited)

1. **HOME block said "(menu scrolls)"** — Home has been a no-scroll two-column grid;
   replaced with grid + `,`/`/` column-hop + letter-jump lines.
2. **ORBIT ANALYSIS said "flip pages (9)"** and omitted the last two pages — now
   "(11)" with `orbit-pos/phys/explore` in the page list.
3. **No Nearby & DX coverage at all** — added a NEARBY & DX section (hub + APRS
   HEARD + DX CLUSTER SPOTS + ADS-B RADAR key summaries). The *user guide* (`U[]`)
   already covered the feature; the Help keys list did not.
4. **QRZ LOOKUP header said "(menu)"** — now "(Nearby & DX)".
5. **About row and TOOLS block said "60 tools"** — now 63.
6. **PRINTING said "menu of ALL reports (40)"** — the About menu has 30 entries and
   20 further reports are contextual-only; reworded to "menu of 30 reports; feed +
   tool reports print from their own screens".
7. **WEATHER block lacked the UTC note** — added " times shown in UTC" (parity with
   the user guide and MANUAL §13, added for 0.9.67).

### Fixed — source comments (dual-edited)

8. **`FILE_TELNET` comment documented the removed `user` field** — now documents
   `label|host|port|prnCols|outMode` with a legacy-row note.
9. **APRS section banner said "APRS.fi stations"** — the implementation is a live
   APRS-IS listen; banner corrected. (This stale comment had already propagated a
   wrong belief into session notes — exactly the failure mode comments cause.)

### Fixed — MANUAL.md

10. **§8 Home list**: wrong item order vs code and listed QRZ Lookup — rewritten to
    the exact `HOME_ITEMS[]` order with Nearby & DX; noted the two-column layout and
    the `,`/`/` column hop.
11. **§22 Home entry**: "scrolling list" → two-column grid; QRZ → Nearby & DX in the
    list; removed stale `t`-opens-Tools and `q`-deep-sleep key claims (letters do
    first-letter jump on Home; Tools is reached from About, LOS-sleep lives on Track).
12. **§13**: new **"Nearby & DX — live terrestrial feeds"** section (hub + all three
    feeds, with the APRS-IS/callsign/receive-only detail, HamQTH on-demand fetch, and
    the ADS-B source-URL + scatter-target behavior), placed before the QRZ section.
13. **§13 QRZ section**: "on the main menu" → "in the Nearby & DX hub"; follow-on
    wording aligned. **Grid dist/bearing** "just before QRZ Lookup" → "just before
    Nearby & DX".
14. **§22**: new entries for **Nearby & DX (hub)**, **APRS heard near me**,
    **DX cluster spots**, **ADS-B aircraft radar**; QRZ entry's *Reached from*
    corrected to Home → Nearby & DX.
15. **§23**: new rows for the hub, APRS heard, and ADS-B radar beside the existing
    DX-cluster row; QRZ row header "(main menu)" → "(Nearby & DX)"; Printing row now
    says the About submenu lists **30** reports.
16. **"sixty tools"** → "sixty-three" (§16 menu-layout paragraph).

### Fixed — other repo docs

17. **README "What it does"** had no live-feeds or Telnet bullet — added one.
18. **docs/FEATURES.md** (the canonical feature list) had **zero** coverage of the
    0.9.66 Nearby & DX hub and the 0.9.67 Telnet client — both added.
19. **docs/guides/CALCULATORS_TOOLS_GAMES_BASIC.md**: "60-tool" / "sixty tools" → 63.
20. **docs/design/HANDOFF_MEMO.md §2.2**: gate list was six gates + one audit; now
    lists the full 14-gate suite with one-line purposes, plus `check_compiles` and
    the five host harnesses. Fixed the "string/footention" typo.
21. **Cheat-card generator**: HOME tile "menu scrolls" → "2-column grid" and QRZ →
    Nearby & DX; ABOUT tile counts 55 → **63** tools, 29 → **30** reports, six →
    **seven** mini-games; new compact **NEARBY & DX** tile. Pagination re-validated:
    still 2 pages at 5×7 (back face auto-steps to 4.0 pt). Tree PDFs remain the
    release-stamped v0.9.67 copies; PDFs regenerate at release packaging.

### Fixed — American English (bulk pass, 488 replacements)

22. One deterministic word-boundary pass (identifier-safe, lowercase + Capitalized
    forms) applied to all `src/*.{h,cpp}` **and** `CardSat.ino` (204 in the monolith)
    plus every live `.md` and the card generators: colour/centre/behaviour/favourite/
    catalogue/metre/grey/cancelled/labelled/modelled/initialise/analyse/-ise family/
    manoeuvre/aluminium/dialling/honour/neighbour/whilst, etc. User-visible strings
    corrected include "AO-7 not in catalogue.", "grey=weak" (GPS sky plot ×2),
    "A few metres" (Tech help), the web UI "favourite" tooltip, and the
    "Upload cancelled" statuses. `docs/releases/*`, `BUGS_*`, and the versioned
    `HANDOFF_0.9.31.md` were excluded as historical records.

### Verified correct (no change)

- On-device TELNET terminal block: complete and accurate, including Opt+1/2/3 output
  modes and the Fn-combos-go-remote note.
- User guide `U[]`: Nearby & DX section and weather-UTC line present (added 0.9.67).
- Ref-card claims spot-checked: 25 CelesTrak extras (`CTX_MAX`), BASIC 4096 B /
  500,000 statements / 2,000 SATSEL — all match code.
- §23 DX-cluster row (added 0.9.67) matches `keyDxc` exactly.
- firmware/README.md describes the shipped 0.9.67 release — intentionally untouched.
- Home item count "twenty" — still correct (composition, not count, had drifted).

### Historical documents — deliberately untouched

`docs/releases/*`, `docs/releases/BUGS_*`, `docs/guides/HANDOFF_0.9.31.md`: these
describe past states (including 4×6-era cards and old counts) and stay as written.

## Process notes

- FW_VERSION set to **0.9.68-wip** at cycle open, so bench builds self-identify
  (bumped to **0.9.68** at release)
  against the 0.9.67 release binaries.
- All nine surgical on-device/comment fixes went through `tools/dual_edit.py`; the
  bulk American-English pass was applied byte-identically to `src/` and the monolith,
  then verified by `check_parity` and `check_body_parity` (all 14 gates green).
- The 4-count difference between src-side (208) and monolith-side (204) bulk
  replacements is the include-region comments the monolith replaces with its own
  prologue — confirmed benign by the parity gates.
