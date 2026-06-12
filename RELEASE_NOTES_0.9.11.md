# CardSat v0.9.11 — Release Notes

All changes since **v0.9.10**. This release adds two new footprint-coverage
screens — **Workable US states** (WAS) and **Workable DXCC** (the full 340-entity
list) — alongside the existing Workable grids, and clears a round of display
overlaps across several plot screens.

> **Hardware status — unchanged from v0.9.8.** Pass prediction, the plots, GPS,
> the AOS alarm, deep sleep, and the offline caches are confirmed on hardware.
> Everything that talks to a **radio, rotator, or network** remains **host-tested
> only**. The new coverage screens are geometry/lookup over the existing SGP4
> sub-point and footprint, host-verified (containment math, parity, compile) but
> not yet exercised on the physical Cardputer display.

---

## Highlights

- **Workable US states** — the US states + DC under the footprint (the `w` key),
  for WAS chasing, in the same places and modes as Workable grids.
- **Workable DXCC** — the **full 340 DXCC entities** under the footprint (the `e`
  key), via a hybrid of country polygons + island/micro-entity reference points.
- **Display overlaps cleared** — Sun/Moon, polar, pass-polar, GPS sky, the
  simulation map, and the orbital-analysis Info page no longer bleed into the
  header bar or the status line.

---

## New features

### Workable US states (`w`)

A companion to the workable-grids screen for **WAS chasing**: the US states (and
DC) currently inside the satellite's footprint. Press **`w`** from **Passes** for
the union of states covered across the selected pass, or from **Track** /
**Manual** for the states under the footprint live (refreshed ~3 s, with radio
and rotator tracking uninterrupted). States are listed by two-letter USPS code,
six per row, alphabetically, with the same cyan workable-count line and `;`/`.` ·
`{`/`}` scrolling as the grids screen.

Membership uses a point-in-polygon test against **bundled simplified** state
boundaries (~0.1°/11 km, about 1.8 KB of data). At footprint scale this is
plenty; a footprint grazing a state line may briefly list both neighbours, which
is correct (both are workable). AK, HI and DC are included.

### Workable DXCC (`e`)

A third member of the footprint-coverage family (after grids and states), for
**DXCC chasing**. Press **`e`** from **Passes** for the union of DXCC entities
covered across a pass, or from **Track** / **Manual** for the entities under the
footprint live. Entities are listed by common prefix (e.g. `DL`, `JA`, `VK`,
`9V`), five per row, with the same count line and scrolling.

Coverage is the **full 340-entity DXCC list**, via a **hybrid model**:

- **161 major countries** as simplified boundary polygons, so the right country
  is picked from the footprint geometry.
- **179 island/micro-entities** — the DXCC long tail — as each entity's
  reference coordinate from **cty.dat**, counted as workable when that point
  falls within the footprint plus a small (~80 km) claim radius. This is what
  surfaces the rare ones a polygon never could (Baker/Howland `KH1`, Rotuma
  `3D2/r`, Glorioso `FT/g`, Swains `KH8/s`, and so on).

About 6.5 KB of data in flash, merged into a single 43-byte runtime bitset.
Borders are coarse and point entities are claimed as a unit rather than by exact
shape, so treat the screen as **chasing guidance** — which entities are roughly
reachable on the pass — and confirm the actual worked entity from your own log.

The DXCC entity list is derived from **cty.dat**, maintained by Jim Reisert,
AD1C (<https://www.country-files.com/>). The data is bundled in flash; the
device does not download it.

---

## Fixes

- **Display overlaps cleared.** Several plots bled into the top header bar or the
  bottom status line. The Sun/Moon sky dome, the pass-polar and polar plots, and
  the GPS sky plot were resized/recentred so their compass labels and
  below-horizon markers stay within the drawing area; the time-step simulation
  map was shortened so its data strips no longer overlap the footer.
- **Orbital-analysis Info page no longer overlaps the footer.** Satellites that
  show the optional "Decay rng" row pushed the Info page to 12 rows, dropping the
  last row ("Asc node") onto the status line. The page line height was tightened
  (10→9 px) so the full 12-row case now clears the footer.
- **10-day chart scrolls cleanly.** Stepping a day no longer flashes a
  "computing" frame, and the "N pass(es)/10 d" status line — which sat on top of
  the chart's last rows — has been removed.
- **Illumination steps by the full 60-day window.** The illumination screen
  advances a whole 60-day raster per keypress again (`,`/`/` = +/-60d) instead of
  one day.
- **IC-910 satellite-mode command corrected.** Engaging satellite mode now sends
  the IC-910's own CI-V command (`0x16 0x07`) instead of the generic
  IC-9100/IC-9700 sub-command (`0x16 0x5A`), which the IC-910 does not accept.
  Verified against the IC-910H command table and Hamlib's rig-specific
  `S_MEM_SATMODE910`. The satmode sub-command is now a per-rig field, so each
  Icom uses the correct byte (IC-910 `0x07`; IC-9100/IC-9700 `0x5A`). The
  IC-820H/IC-821H are also marked satellite-mode-capable — though it isn't needed
  for them, since they already operate full-duplex via independent Main/Sub
  control. Host-verified (frame construction) only; not yet exercised against a
  physical IC-910.

---

## Notes & caveats

- The coverage screens reuse the workable-grids engine: the same footprint walk,
  the same per-pass-union vs live-now modes, the same scrolling list UI. Only the
  per-point lookup differs (Maidenhead math → point-in-polygon for states →
  hybrid polygon+point for DXCC).
- Key map for the family: **`g`** grids, **`w`** US states, **`e`** DXCC. `e`
  ("entities") was chosen because `c`/`d`/`s` were already bound on Track/Manual.
- All three screens are reachable in the same three places: live off **Track**
  and **Manual** (with tracking uninterrupted), and as a per-pass union off
  **Passes**.
- DXCC accuracy is bounded by the simplified boundaries and single reference
  points; this is intended as operating guidance, not a logging authority.

---

## Internals

- New screens `SCR_STATES`, `SCR_DXCC`; `build*/draw*/key*/addFootprint*` trios
  mirroring the grids feature; `stateBits[7]` / `dxccBits[43]` runtime bitsets;
  `*Live` / `*Scroll` / `*BuiltMs` state; the live-redraw guard and screen
  dispatch extended for both.
- `STATEPOLY` / `STATE_CODE` (51 entities) and the DXCC hybrid data:
  `DXCCPOLY` / `DXCCPOLY_CODE` (161 country polygons), `DXCCPT` / `DXCCPT_CODE`
  (179 point entities from cty.dat), combined `DXCC_N = 340`. `statePolyTest`,
  `dxccPolyTest`, `dxccGcKm`, and `dxccCode` (variable-length prefix extraction
  across the polygon/point index boundary).
- Entry keys `w` / `e` added to `keyTrack`, `keyManual`, and `keyPasses`; footer
  and help text updated; cty.dat attribution added to README data sources.
- Orbit-page `LH` 10→9; Sun/Moon dome `cy/R` 70/50→68/42; pass-polar and polar
  `cy/R` 78/50→70/44; GPS sky `cy` 74→75; sim-map `MH` 96→92; `buildVis` no
  longer sets a count status or an intermediate "computing" draw; `keyIllum`
  steps by `ILLUM_DAYS` (60) instead of 1.
- `RadioProfile::satModeSub` per-rig field; `enableSatMode` uses it (CI-V and
  LAN paths). IC-820H/IC-821H `hasSatMode` set true.
- `FW_VERSION` → **0.9.11**.

---
