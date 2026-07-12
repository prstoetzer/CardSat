# Handling GP distributions larger than MAX_SATS — design & scope

*Design note. No implementation yet. Consolidates the earlier "over-150 mechanisms" and
"picker feasibility" drafts and corrects both for the CelesTrak reality (see the scope section
— this is a **present** problem for CelesTrak sources, not future-proofing).*

## The problem, stated correctly

`MAX_SATS = 150` (config.h) caps the **in-RAM** `SatEntry _sats[MAX_SATS]` array, not the
download. The full distribution is streamed to `FILE_GP` (`gp.json`) on the SD card regardless;
the parser then copies only the first 150 objects in file order into RAM
(`loadGpFromFile`, satdb.cpp:636, loops `while (... && _n < MAX_SATS)`) and **silently ignores
the rest**. Satellite #151 is on the card but never loaded, and the user gets no indication a
cap was hit.

So this is not a download problem. It is a **selection** problem: which ≤150 of the available
satellites go into RAM, and how does the user find out when a choice had to be made.

### Scope: this is a present problem, because of CelesTrak

CardSat's GP source (`cfg.gpUrl`) can be **AMSAT** (the default), a **CelesTrak category**, or a
**custom URL**. The AMSAT amateur distribution is small — a recent boot parsed **91
satellites**, well under the cap — so on the default source this is only future-proofing.

**CelesTrak is different, and makes this a real, shipping bug today.** CardSat's CelesTrak
category picker offers groups that routinely exceed 150 by wide margins:

- **Thousands:** Starlink (~thousands), Active (many thousands), GNSS, OneWeb, Kuiper, Qianfan.
- **Hundreds:** Iridium NEXT, Globalstar, Orbcomm, GPS/GLONASS/Galileo/Beidou, Weather, Last 30
  Days, Space Stations, Analyst, and others.
- Even **Amateur Radio** (`GROUP=amateur`) and **SatNOGS** run larger than the AMSAT curated
  list.

The moment a user selects one of these, the 150-cap truncation fires **now**, silently: the
world map and pass predictions look complete but are actually the first 150 in CelesTrak's file
order. This reprioritizes the work — the honesty fix below is a present correctness issue, not a
nicety.

### Two regimes (this shapes every solution)

The oversized sources split into two fundamentally different cases that likely need different
handling:

- **Moderately oversized (hundreds):** amateur, SatNOGS, weather, stations, the GNSS
  sub-groups. Here per-satellite selection is meaningful and a picker/curation model works.
- **Mega-constellations (thousands):** Starlink, Active, OneWeb, Kuiper. Here per-satellite
  selection is both **memory-infeasible** (see the picker section) and **poor UX** — nobody
  wants to check-box 150 of 7,000 Starlinks. These want a *rule* ("first 150," "nearest N now,"
  "highest upcoming elevation"), not a list.

Any real solution must not assume the moderate case; the mega-constellation case is where naive
designs break.

## Enablers already in the code

- **The full file is already on SD** — nothing is lost at download, so any selection mechanism
  can stream candidates from `gp.json` without re-downloading.
- **Favorites are keyed by NORAD id** (`favs.txt`, one per line; `loadFavs`/`saveFavs`,
  app.cpp:2478/2491), resolved via `indexOfNorad()` (satdb.cpp:16). A favorite's identity is
  independent of array position, so a favorite can be *guaranteed loaded* regardless of file
  position.
- **A curated-subset-merged pattern already exists**: `mgp.json` (manually-entered sats) is a
  separate file merged via `loadManualGpFile()`. "Load a chosen subset, not the whole file" is
  already a thing here.
- **`gp.json` is a single JSON array parsed by a streaming state machine** (256 bytes at a time)
  — never fully buffered. Candidate scans reuse this.

Constraint, as always: **no steady-state per-satellite RAM growth** (no PSRAM), **no
fragmentation-inducing churn**. The array stays a fixed `MAX_SATS`; the only question is which
entries fill it, plus whatever *transient* memory a selection UI needs while open.

---

## Step 0 — Surface the cap (mandatory, do first, independent of everything else)

Whatever mechanism is chosen, **stop truncating silently.** When the parser hits `MAX_SATS`,
record "objects seen" vs "objects loaded" and expose it: the satellite list shows
"Showing 150 of N — some satellites not loaded," and About/status notes it. This converts a
silent data-loss bug (currently misleading for every CelesTrak mega-category user) into an
informed, visible state. It is a tiny change — a counter plus one status string — and given the
CelesTrak reality it should land in the **next release** regardless of the larger design.

---

## Option A — Favorites always loaded (priority parse)

Guarantee every NORAD in `favs.txt` is loaded even if past the 150th object; fill favorites
first, then top up with file order until full. Mechanism: a priority-aware load (two streaming
passes, or one pass with care) that accepts favorites first, then non-favorites to fill.

- **Solves the moderate case well** — for amateur/SatNOGS-sized sources, favoriting *is* the "I
  want this one" signal, and this guarantees it survives truncation. Least code, no new files or
  UI, zero RAM growth, rides on the existing favorites design.
- **Helps the mega-constellations less** — a Starlink user rarely wants specific named birds to
  favorite; they want "a useful subset." Favorites-by-NORAD doesn't express that. So A is
  necessary-and-sufficient for moderate sources, and only partial for the thousands case.
- **Cost:** two passes ≈ double parse time on refresh (file is tens of KB; measure on-device). A
  single-pass eviction variant is possible but more complex.

**Verdict:** strongest cheap baseline; do it after Step 0. Fully solves moderate sources; needs
Option C or D for the mega-constellations.

## Option B — Explicit "included" list (generalized favorites)

A second NORAD-per-line file (`include.txt`) of "load these even if not favorited," consulted
before file order. Same priority-parse machinery as A plus an extra set.

- **Pro:** separates "load/keep available" from "actively track" if favorites carry other UI
  meaning (schedule, alarms) the user doesn't want to overload.
- **Con:** largely redundant with favorites for most users; added concept and UI for a
  distinction many won't need.

**Verdict:** only if favoriting-to-load conflicts with what favorites mean elsewhere. Otherwise
A covers it. Hold unless a concrete conflict appears.

## Option C — Rule-based subset (the right answer for mega-constellations)

Instead of naming individuals, load by a *rule* evaluated while streaming the file:

- "First 150 in file order" (today's behavior, but *chosen*, not silent).
- "The 150 nearest to my location right now" (needs a quick propagation of each candidate — more
  compute, but bounded).
- "The 150 with the highest upcoming elevation / soonest passes."
- "Only objects with an active amateur transponder" (needs the transponder/status data CardSat
  already downloads — a reliability dependency).

- **Pro:** the only model that makes sense for Starlink/Active/GNSS, where check-boxing
  thousands is absurd. One setting, no per-satellite burden, scales to any size.
- **Con:** "nearest/highest" rules cost compute over the whole candidate set; "active
  transponder" depends on status-data reliability and coupling. Rules can surprise a user who
  wanted an excluded object.

**Verdict:** the natural mega-constellation answer, and a good complement to A for moderate
sources ("favorites always + fill by nearest-now"). Phase 2, because the useful rules need
compute or status-data work.

## Option D — Search / filter-to-narrow picker (the scalable interactive answer)

An on-device picker that **never holds all candidates** — the user types a name fragment or
NORAD prefix, CardSat stream-filters `gp.json` to the (small) set of matches, and the user picks
from those. Selection persists to a NORAD-per-line file (`favs.txt`-style) consumed by the
priority loader.

This is the interactive-curation answer that survives a 7,000-satellite category, because RAM
holds only the *filtered matches*, never the full list. It doubles as a good experience for the
moderate sources too.

**Why not a scroll-through-everything picker:** a plain scrollable list must hold, or index, all
candidates. Even a thrifty `{norad, fileOffset, sel}` index at ~9 bytes each is **~63 KB for
7,000 Starlinks** — it blows the RAM budget outright. A fully-streamed scroll (re-read the file
per screen) is memory-flat but unusably slow at thousands of entries. **Search-to-narrow is the
only interactive model that scales**, which is why the earlier "scrollable candidate list"
framing is superseded for large sources.

**Verdict:** the right interactive picker *if* one is built. Filter-to-narrow, not
scroll-everything. Medium effort; the filtered-set list itself is small and can reuse ordinary
list rendering once the match set is built.

## Option E — Raise MAX_SATS (bounded stopgap)

One-constant bump. Each `SatEntry` is roughly **~100 B** (26-byte name, designators, a double
epoch, eight float mean elements, a double mean motion — the satdb.h comment's "~32 bytes/entry"
is the *savings* from the float-elements optimization, not the entry size). So 150 ≈ ~15 KB
resident; 200 ≈ ~20 KB (+5 KB); 256 ≈ ~25.6 KB (+10.6 KB). The 4bpp sprite freed ~16 KB of
headroom, so a modest bump may fit — but it is roughly 3× costlier than an earlier draft of this
note assumed.

- **Pro:** trivial; buys margin.
- **Con:** postpones, doesn't future-proof (a fixed number is still fixed, and useless against
  Starlink's thousands); spends the contiguous-heap headroom the recent work recovered — needs
  an on-device check that uploads still fit after the bump.

**Verdict:** reasonable stopgap combined with A; never a solution alone, and irrelevant to the
mega-constellation case.

## Option F — On-demand paged loading (the unbounded rewrite)

Stop holding the working set in one array. Keep the full file on SD (already true), build a
NORAD→offset index once per download, and load individual OMMs on demand; the resident array
becomes a small cache of actively-used sats.

- **Pro:** genuinely unbounded — RAM bounded by working set, not catalog. Most future-proof.
- **Con:** the largest, riskiest change. Everything that sweeps `_sats` (schedule, list screens,
  search, workable-horizon) assumes an in-RAM array; a seek-per-access model changes performance
  and complicates those features. Index maintenance, seek latency on hot paths. High risk vs.
  benefit given A + C + D cover the real cases.

**Verdict:** document as the escape hatch; don't build unless the catalog and usage genuinely
demand arbitrary access across many hundreds routinely.

---

## Picker feasibility (if an interactive picker is built — Option D)

**Feasible, but it must be search/filter-to-narrow, not scroll-through-everything**, for the
memory reason above. The easy parts ride on existing patterns:

- **Change-detection** ("a satellite was added to or removed from the distribution"): on each
  download, stream the new `gp.json` for its NORAD set, compare to a stored snapshot file
  (`dist_seen.txt`, `favs.txt`-format). Differ → the source's membership changed. A 400-sat set
  is ~1.6 KB; comparison is a sort+walk. Cheap and flat.
- **Persistence & re-launch**: the remembered selection is a NORAD-per-line file, read/written
  like `favs.txt`. Because the snapshot is compared every download, the picker re-appears
  automatically when — and only when — membership changes. Requested behavior, for free.
- **The picker cannot reuse the existing list.** `SCR_SATLIST`/`buildSatView` (app.cpp:2517)
  indexes the **in-RAM `db`** via `view[MAX_SATS]` (int×150) — it can only show *loaded* sats,
  but the whole point is choosing among *unloaded* ones. So the picker needs its own
  SD-streaming list. With filter-to-narrow, it holds only the filtered matches, so ordinary list
  rendering works on that small set.

Decisions the picker forces (choices, not blockers):

- **150 ceiling inside the picker** — cap selection at 150, say "150/150 — uncheck one first."
  The honest place for the constraint to surface.
- **First-run default** — no selection yet: pre-seed first-150-in-file-order + existing
  favorites, so the user edits a sane starting set.
- **Favorites auto-included / un-uncheckable** — can't drop a satellite you're actively
  tracking; ties cleanly to Option A.
- **Re-launch trigger** — a pure set-difference nags on *any* change (e.g. CelesTrak dropping a
  decayed object nobody selected). Refine to fire only on *meaningful* changes (a new candidate
  appeared, or a selected one vanished) to avoid picker fatigue — important for CelesTrak, whose
  large groups churn constantly.
- **Manual sats (`mgp.json`)** — the snapshot should track the *distribution* file specifically,
  so hand-entered sats don't muddy "did the source change." Compose with `loadManualGpFile`.
- **Drop back under 150** — later distribution shrinks to ≤150: load everything, ignore/clear
  the selection.
- **Boot-time modal** — GP parse runs at boot (app.cpp:755–761) and manual refresh; a modal
  picker at boot must not wedge unattended/headless use. Guard with a timeout → fall back to
  remembered/default selection if no one responds.

**Effort:** medium — the largest UI addition since the rove planner, concentrated in the
streaming filtered-picker widget. **Risk:** moderate, in two measured spots — the picker's
transient working memory on a no-PSRAM part, and the boot-time modal interrupt. **Heap:**
steady-state cost is just the selection-set membership at load (negligible); the picker's
working set is transient (only the filtered matches, freed on close).

---

## Status update (v0.9.54)

Step 0 and Option A **shipped**: `loadGpFromFilePreferring()` does a two-pass
favorites-first load, `seenCount()`/`wasTruncated()` surface the cap in the status
line and boot log, and the download path preflights Content-Length against free
storage. Option C was evaluated for this release and **deferred with reason**: a
"nearest-now" rule needs a full SGP4 propagation per candidate, which at
mega-constellation scale (thousands of objects) is not viable at every boot on this
part. The `scanGpFile(accept, ctx)` callback is the seam a future *on-demand* rule
action would plug into.

## Recommendation (sequenced, corrected for CelesTrak)

1. **Step 0 — surface the cap, next release.** A present correctness bug for CelesTrak
   mega-category users, independent of everything else. Tiny.
2. **Option A — favorites-first priority load.** Fully solves moderate sources
   (amateur/SatNOGS); minimal code, no heap cost, rides on favorites-by-NORAD. Measure the
   double-parse cost on-device.
3. **Option C — rule-based subset for the mega-constellations.** "First-150 (chosen, not
   silent)" immediately; "nearest-now" / "active-transponder" as the useful rules once the
   compute/status-data work is done. This is the only sane model for Starlink/Active/GNSS.
4. **Option D — filter-to-narrow picker** if interactive per-satellite curation is wanted for
   the moderate sources; explicitly *not* a scroll-through-everything list, and explicitly not
   attempted for the thousands-case (rules handle those).
5. **Option E (small MAX_SATS bump)** only as a measured stopgap alongside A, with an on-device
   upload-contiguity check. **Option F (paging)** stays documented but unbuilt.

**Bottom line:** the previous "91 satellites, this is future-proofing" framing held only for the
AMSAT default and is **wrong for CelesTrak**, where mega-categories truncate silently today.
Step 0 (honesty) is the immediate fix; A handles moderate sources cheaply; C (rules) is the real
answer for the mega-constellations, where interactive picking is neither memory-feasible nor
sensible. An interactive picker, if built, must be filter-to-narrow.

---

## Grounding facts (checked in code)

- `MAX_SATS = 150` (config.h:181); `SatEntry _sats[MAX_SATS]` (satdb.h:170); truncation at
  `while (... && _n < MAX_SATS)` (satdb.cpp, `loadGpFromFile` at 636).
- Source is `cfg.gpUrl`: `"AMSAT"`, a CelesTrak `GROUP=`/`SPECIAL=` category, or custom
  (app.cpp:718–726; category table around app.cpp:22873+).
- Full `gp.json` written to SD (satdb.cpp:299); single JSON array, streamed 256 B at a time.
- Favorites: `favs.txt` NORAD-per-line (config.h:218); `loadFavs`/`saveFavs` (app.cpp:2478/2491);
  `indexOfNorad` (satdb.cpp:16).
- Manual sats: `mgp.json` (config.h:219); `loadManualGpFile` (satdb.h:122).
- Existing list indexes in-RAM `db` via `view[MAX_SATS]` (app.h:161; `buildSatView`
  app.cpp:2517) — cannot show unloaded candidates.
- Recent AMSAT boot: 91 satellites parsed (under cap). CelesTrak mega-categories: thousands.
