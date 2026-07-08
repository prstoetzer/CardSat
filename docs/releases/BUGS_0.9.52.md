# 0.9.52 change tracker

## 1. Shared infrastructure -- **DONE (host-verified)**
`pointInFootprint(lat,lon, subLat,subLon,altKm)` -- the spherical-cap membership test factored
out of the addFootprint* fills, for single-KNOWN-point tests. Host-validated (at sub-point inside,
edge in/out, antipodal outside). Mirrored byte-identical to CardSat.ino.

## 2. Workable horizon (SCR_WORKHZN) -- **DONE (host-verified)**
Off Next Passes (`SCR_SCHEDULE`) -> `w`. 10-day "ever workable" union of DXCC / US states / grids
across all favorites. The union accumulator IS the shared stateBits/dxccBits/gridBits (zeroed once,
OR'd per sample, never cleared between passes) -- ~4.1 KB total, independent of pass count. Grid
block freed on done AND cancel (no-PSRAM heap discipline). Determinate progress bar (cheap up-front
AOS-only pass count as the denominator), live growing counts, distinct Done state, drill-down
s/d (states/DXCC lists via the union, back via new wListReturn member), grids count-only + `w`
export to /CardSat/workable_*.txt. Jobbed 1 pass/loop. Fast mode (skip grids) supported in whStart.
Host tests: pass paging enumerates each pass once (no double-count, no infinite loop); union OR
semantics; progress monotonic.

## 3. Target search (SCR_TGTSEARCH pick / SCR_TGTHITS run+results) -- **DONE (host-verified)**
Off Next Passes -> `s`. Pick ONE target (US state / DXCC / grid) and find every pass on any
favorite over 10 days where it's workable, WITH per-pass timing. Inverse of #2: keeps a small .bss
hit list (HitRow[40]) with the workable sub-window (inStart/inEnd) per pass, not a union. **Zero
heap allocation.** Membership = one pointInFootprint() per sample vs the target's representative
point: state & DXCC-polygon -> bbox centre (from the *_LOMIN/LOMAX/LAMIN/LAMAX tables already in
firmware); DXCC point-entity -> DXCCPT coord; grid -> gridToLatLon centre. **No DXCC lookup-table
bridge needed** -- the picker lists the geometry prefix list (DXCCPOLY_CODE/DXCCPT_CODE) directly,
so the pick index IS the geometry index. Result list (earliest first): sat / date / workable
window / max el; ENTER tracks the pass's satellite (trackReturn=SCR_TGTHITS); `w` export to
/CardSat/search_<TARGET>_*.txt. Grid entry via the shared editor (editTarget 760, uppercased,
returns to SCR_TGTSEARCH). Zero-hit and cancel paths handled. Host tests: membership + contiguous
sub-window extraction; full hit-recording flow with TS_HIT_MAX cap.

## Defaults chosen (per Paul's "go with your suggested defaults")
- Entry keys: `w` (horizon), `s` (search) -- both confirmed free on SCR_SCHEDULE.
- Horizon: 10 days (HORIZON_DAYS constant, shared).
- Scope: horizon = all favorites (single-sat plumbing present via whStart args, not yet on a key).
- Grids: count headline + export; lists on demand for states/DXCC.
- DXCC membership: bbox-centre (option: geometry prefix picker, no generated centroid table needed).
- Sub-window: nearest sample minute (no edge bisection this round).
- TS_HIT_MAX = 40 (static, earliest-N kept).

## Status
balance 0, parity green, all 14 new/changed function bodies mirror-identical src<->ino, dispatch
cases present in both files for all 3 screens, no debug leftover. **NEEDS: on-device compile +
flash + timing pass** (host-verified only; the SGP4/predictPasses timing over 10 days x all favs
is the thing to confirm feels responsive, and that the progress bar tracks honestly).

## Not done this round (deferred)
- Release notes / manual section / cheat card / README for 0.9.52 (features first; docs next round).
- Single-sat scope on a key for the horizon feature (engine supports it).
- Multi-target search; edge-accurate polygon membership; configurable horizon.

---

## Final status (release) — DONE

Both features shipped and confirmed on device (Alabama, Brazil/PY, the ZP overlap, and
Fiji/3D2 all correct; exports correct). Refinements landed after the initial tracker above:

- **Target search: chronological merge.** Reworked from per-favorite sequential scanning to a
  global merge that always processes the earliest next pass across all favorites, so results
  are time-ordered across the whole fleet and the 40-hit cap keeps the *soonest* passes rather
  than filling from the first satellite. Host-validated: distinct passes, time-ordered, cap
  keeps the globally-soonest, all favorites represented, and the no-hit case terminates.
- **nextpass re-init fix.** The refill fetches a small batch and skips the stale re-returned
  pass via a per-favorite last-AOS guard — fixes the "40 hits on one pass / never stops"
  Hopperpop `initpredpoint`+`nextpass` trap.
- **ENTER on a hit → polar plot** of that specific (future) pass via `buildPolarForPass`, not
  the Track screen; results list rewritten with a header row matching the Passes screen.
- **Finish repaint.** Loop forces one draw when the sweep transitions RUNNING→done, so the
  Done/results screen paints.
- **Performance (all byte-identical results):** 30s→180s sampling; per-sample bbox candidate
  pre-filter (`dxccCand`/`stateCand`); merged states+DXCC mesh walk (`addFootprintStatesDxcc`).
  Full 10-day all-favorites horizon sweep down from >1 h to a few minutes.
- **Empty drill-down fix.** `whFinish` now publishes the union counts to `stateN`/`dxccN` (the
  states/DXCC list screens gate on those), and the lists are titled "Workable states" /
  "Workable DXCC". Export was always correct; this fixes the on-screen view.
- **Exports to subdirectories:** `/CardSat/workable/` and `/CardSat/search/`.
- **Grids off by default** in the horizon sweep (avoids the ~4 KB gridBits alloc/free heap
  churn); `g` on the Done screen re-runs with grids on demand.
- **On-demand audio.** Speaker is powered only around games, voice memos, alarm beeps, and the
  settings volume/sound previews, and released after (game audio released on leaving all game
  screens). Frees the M5 speaker's ~8 KB I2S/DMA buffers between sounds to keep the largest
  contiguous block high for TLS uploads. `beep()` made a member; all Speaker.tone/begin sites
  routed through acquire/release; no path bypasses it.

**Docs:** release notes, manual (Workable horizon + Target search sections, Next Passes keys),
cheat card (two new entries + `w`/`s` on Next Passes; still 2 pages), README callout + feature
bullet, FEATURES.md, and regenerated `CardSat_Manual.pdf` (v0.9.52) and `CardSat_CheatCard_4x6.pdf`.

**Gate:** balance 0, parity green, all changed/new bodies mirror-identical src<->ino.
