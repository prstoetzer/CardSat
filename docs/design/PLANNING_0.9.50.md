# Planning: CardSat 0.9.50

**Status:** planning only — no code or documentation changed yet.
**Basis:** repo audits run against the released 0.9.49 tree (grep/parse audits, the key-conflict
audit script, doc-vs-code cross-checks) plus the two field serial logs from the 0.9.49 SD/LoRa
debugging, which contain evidence beyond the bug they were captured for.

---

## 1. Bug candidates (investigate first)

### 1.1 AMSAT catalog map loads 0 entries — **strongest lead, from field logs**
Both field serial logs show `[amsat] catalog map: 0 entries` at boot, while the status file
loads fine (`status file: 45 sats marked`) — so networking, SD reads and the status parse all
work, but the catalog (name→NORAD map that powers posting reports by API name) is empty.
Candidate causes, in likelihood order:
1. **Strict parse pattern.** `applyAmsatCatalogFile()` scans for the exact bytes `"name":"`.
   If the AMSAT status API returns pretty-printed JSON (`"name": "` with a space) or changed
   key order/format, the scan matches nothing and silently yields 0. Fix would be a
   space-tolerant scanner. *Host-checkable:* fetch `AMSAT_CATALOG_URL` and inspect the bytes.
2. **Cache never populated.** `fetchAmsatCatalog()` runs alongside GP updates; if it failed
   quietly before the logs began, boot applies an absent/empty file. The failure path does
   print, and no `[amsat] catalog fetch failed` appears in the logs — weak support.
3. Truncated/partial cache from an earlier failed write (pre-0.9.49 SD bug era) that now
   parses to nothing. A one-time `Update` with 0.9.49 would clear this.
**0.9.50 action:** fetch the live catalog host-side, diff against the parser's expectations,
fix the parser if needed, and add an unconditional `catalog map: 0 entries (check cache /
re-run Update)` style warning so this can't sit silent. Bench: run Update, confirm nonzero.

### 1.2 Heap headroom after downloads — watch, not yet a bug
A field log shows free-heap largest block ~14 KB after the download sequence on this
no-PSRAM part. Known fragmentation risk (previously caused download failures). No failure
observed now, but 0.9.48 added three embedded tables and more screens. **Action:** add the
largest-free-block figure to the About screen (it already shows free heap) so field reports
carry it; treat any new streaming buffer with the established in-RAM/streaming discipline.

### 1.3 Structural audit results (clean)
Raw `LittleFS.open` on data files: **0**. Key-conflict audit: only the 7 known legitimate
mode-gated reuses. Dispatch cases: 108 matching src↔ino. No action; keep both audits in the
release gate.

---

## 2. Documentation errors / drift (found by audit — to fix in 0.9.50)

1. **FEATURES.md line ~215:** "the Home menu is a two-column grid (**all twenty** …)" —
   the home-item count claim predates recent growth and must be re-verified against the
   actual HOME_ITEMS array before the wording ships again. (Ground-truth extraction of the
   array is non-trivial by grep — verify in-editor.)
2. **Historic version call-outs read as current.** MANUAL.md carries several "As of
   v0.9.4x…" notes (0.9.41 manual-reset, 0.9.43 batching ×3, 0.9.40 passband warning in the
   README's archive section). Not wrong, but several describe behavior that is now simply
   *the* behavior; a sweep should either drop the version framing or move them to a history
   note so the manual doesn't read as layered patch notes.
3. **Cheat card key coverage lag.** The satellite screen's help-only keys (e/k/2/3/d/i and
   now y-sim) are documented in Help and the manual key table, but the cheat card's
   Satellites row was trimmed for space at 0.9.48; verify it still names the keys an
   operator most needs (y-sim was fixed at 0.9.49 release; the row remains dense).
4. **Verified clean this audit (no action):** internal manual anchors (48 links, 0 broken);
   world-map `n` toggle documented; calculator `[`/`]` scroll documented; no stale
   `s`-simulation references remain; Tools count (20) consistent across README/manual/code.
5. **Standing cosmetic:** `parity.py`'s `rows[NN]` anchor heuristic is stale (matches
   NoteVRow rows[128]); harmless but prints noise — tidy when convenient.

---

## 3. Fragile constructs to harden (small, cheap, prevents future bugs)

1. **The Tools form-id offset (`toolsSel - 7`)** has shifted seven times and is maintained
   by hand in two places (+7 in drawToolForm's header). Replace with a named constant
   `TOOLS_FIRST_FORM` derived from the standalone-tool count, asserted at boot against
   TOOLS_NAMES length. One-line class of future bug eliminated.
2. **Macro-collision watch:** 23 locals named `N` exist (fine — `N` is not an Arduino
   macro), but the convention of short identifiers keeps brushing the macro namespace
   (`F`, `pi` both bit us). Add a grep to the release gate for new single-capital
   identifiers against a known-macro list.
3. **DEL/keyEdit idiom:** all current text-entry screens follow it; any 0.9.50 screen with
   typing must too. Keep the audit script in the gate.

---

## 4. Carry-forward bench items (unchanged status, restated so they aren't lost)

- **IC-821H CI-V:** higher default `catDelayMs` (currently 70 ms), MAIN-read as
  reference/push-only default, PTT poll default OFF — bench confirmation still open.
- **UNTESTED-on-hardware markers in source:** IC-910 CI-V path (civ.cpp), band-assign in
  rig.h, one radio profile, direct-Yaesu rotator path (rotator.h), voice-memo mic/I2S↔SPI
  coexistence (voicememo.h — note the SD/LoRa saga makes bus-coexistence claims worth
  revisiting). None block 0.9.50; each needs a willing tester with the hardware.
- **LoRa messaging path:** now provably initializes with the module present (0.9.49 probe
  bench check) but an actual two-unit message exchange in the field is still untested.
- **First live AMSAT POST** remains the outstanding test of the reporting path — and is
  blocked by 1.1 if the catalog map is truly empty (you cannot post by name without it).
  Fixing 1.1 likely unblocks this.

---

## 5. New feature candidates for 0.9.50

Ranked by (value to the field operator) × (implementation risk).

### Tier A — recommended for the cycle
1. **SatNOGS transmitter active/inactive marking** (SATNOGS_2026-07 doc §4.1, "ready now").
   `status`/`alive` are already in the parse filter; add a bool to `Transponder`, sort
   active-first / dim inactive in the picker. Small, backward compatible, real UX win —
   the live transponder stops hiding among decommissioned entries.
2. **Link budget ← live slant range.** Pre-fill the Distance field from the tracked
   satellite's current (or max-el) slant range, with a key to re-sync. Turns the link
   budget from generic calculator into a pass-planning tool. Small: the range is already
   computed on the track screen.
3. **Catalog-map fix + visibility** (from 1.1) — arguably a bug fix, but it unblocks the
   reporting feature, which is mission-central.

### Tier B — good, slightly larger
4. **RF-exposure polish:** duty-cycle presets per mode (CW/SSB/FM/FT8) and a louder
   on-screen "planning estimate — not a station evaluation" line (deferred from 0.9.48).
5. **Debris tool: elliptical-orbit note** — an on-screen "use perigee for elliptical
   orbits" hint (one string; deferred caveat from 0.9.48).
6. **Char lookup: selectable ITA2 variant** (US-TTY vs CCITT) — deferred from 0.9.48.

### Tier C — watch / larger scope, not this cycle
7. **SatNOGS transmitter-parameters JSON** (tone/baud from data; retire per-NORAD tone
   hard-coding) — schema still experimental; watch (§4.2).
8. **Launch-window "heard?" view** from Reception Status — requires adopting the
   satellites endpoint; deliberately avoided (§4.3).
9. **Satellite-screen key-density refactor** (analysis views behind a submenu) — design
   change flagged at the y-sim fix; only if key pressure grows.

---

## 6. Suggested 0.9.50 shape

Open with **1.1 (catalog map)** since it has live evidence and blocks the AMSAT-reporting
test; land **§3.1 (TOOLS_FIRST_FORM constant)** while the Tools code is warm; take **5.1
and 5.2** as the cycle's features; sweep **§2's doc fixes** at the release gate as usual.
Everything else stays tracked here.

---

## 7. Transponder/mode selection when reporting AMSAT status — **diagnosed; = the §1.1 catalog bug**

**Request:** a way to choose the transponder/mode when reporting AMSAT status; AO-7 is
stuck on V/A (Mode A) with no way to report U/V (Mode B).

**Finding — this is not a missing feature; the multi-mode picker already exists and is
being starved by the §1.1 catalog-map bug.** Traced end to end against the live API:

- The report picker (`SCR_AMSRPICK`, `drawAmsRpick`/`keyAmsRpick`) already has an **"As"
  row that cycles every AMSAT name for the satellite** (`pickName[0..pickN-1]`, `,`/`/`
  to change, shown as "(1/2)" etc.). The UI for choosing the mode is fully built.
- `pickName[]` is filled by `db.amsNamesFor(satIdx, …)`, which returns every catalog-map
  entry whose `.sat == satIdx`. For a two-mode bird that should be two names.
- The live AMSAT catalog (`catalog.php`) **does** carry AO-7 as two entries:
  **`AO-7_[U/v]`** (Mode B) and **`AO-7_[V/a]`** (Mode A). So the data exists; both share
  base "AO-7" and map to the same local sat index.
- **Root cause:** the catalog map is empty (`catalog map: 0 entries` in both field logs)
  because `applyAmsatCatalogFile()` scans for the literal bytes `"name":"` while the API
  pretty-prints `"name": "` (space after the colon). Zero matches → empty map →
  `amsNamesFor` returns 0 → `amsPickNameFor` falls back to the single `amsatName[0]` from
  the status file → the picker offers exactly one mode. That one mode is whatever the
  status summary last carried for AO-7 (V/A), which is exactly the "stuck on V/A" symptom.

**Fix (0.9.50): make the catalog name-scan whitespace-tolerant.** Match `"name"` then skip
optional spaces, `:`, optional spaces, then the opening quote — instead of the fixed
7-byte pattern. Confirmed host-side that the only structural difference is the inserted
spaces. This single change:
- restores the full catalog map (all ~81 mode entries, not just AO-7),
- makes the existing "As" row show **AO-7 (U/v) and (V/a)** to pick between,
- and unblocks the mission-central AMSAT-reporting path generally (see §4 — the first live
  POST is blocked by this).

**Secondary polish (optional, same cycle):**
- The picker shows the raw API name (`AO-7_[V/a]`). Consider showing `display_name`
  (`AO-7 [V/a]`) for readability — the catalog carries both fields; would require capturing
  `display_name` too, a small parser addition. Nice-to-have, not required.
- After the fix, `amsPickNameFor`'s active-transponder disambiguation (`amsTagFits`) will
  start firing for AO-7 (it needs ≥2 names to matter); verify the `[U/v]`/`[V/a]` tags map
  onto CardSat's transponder mode strings so the one-key "I heard it" path picks the right
  mode when a transponder is active, and only falls to the manual picker when genuinely
  ambiguous.

**Regression/verification:** host-diff done (space-form confirmed in live payload).
Bench after fix: boot serial should read `catalog map: N entries` with N≈81; open the
AO-7 report picker and confirm the "As" row cycles U/v ↔ V/a; send one report in each mode
(or against a test as appropriate) — this is also the first real exercise of the reporting
path, so treat it as the §4 live-POST test.

**Priority:** promote §1.1 from "investigate" to **confirmed bug, top of 0.9.50** — it is
both the catalog-map defect and this transponder-reporting request, and it gates the
reporting feature that is central to CardSat's purpose.
