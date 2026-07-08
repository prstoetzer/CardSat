# CardSat v0.9.52 — release notes

This release is built around **two new ways to answer "where and when can I work a place?"**,
plus a substantial round of **performance and memory work** driven by on-device testing.

The two features are complementary. **Workable horizon** looks *outward*: over the next ten days,
which US states, DXCC entities, and grids will *ever* be reachable through any of your favorite
satellites? **Target search** looks *inward*: pick one place — a state, a DXCC entity, or a grid —
and get every upcoming pass, on any favorite, where that place falls inside the footprint, listed
in time order with the workable window for each pass.

Alongside those, the footprint-coverage engine that both features share was made dramatically
faster, and the speaker now powers up only when it's actually making sound — a change that frees
memory for network uploads (LoTW, Cloudlog) on this no-PSRAM board.

Upgrading from 0.9.51 is drop-in — no settings, log, or on-air format changes, and nothing
existing behaves differently.

> **Note on screenshots:** the images in the manual and README were captured on an earlier build
> and do **not** yet show the new 0.9.52 screens (Workable horizon and its states/DXCC drill-down,
> Target search and its results list). The text is current; the pictures will catch up in a later
> round.

---

## Workable horizon — what can I work in the next ten days?

From **Next Passes**, press **`w`**. CardSat sweeps every favorite over the next ten days and
builds the **union** of everything that will *ever* be workable in that window: every US state,
every DXCC entity, and (on demand) every 4-character grid whose area passes through a footprint
while the satellite is above your horizon.

A progress bar tracks the sweep as it runs — it works incrementally, one pass per loop, so the
interface stays responsive — and the running counts of states and DXCC grow live as coverage
accumulates. When it finishes you get a summary with the totals, and you can drill in:

- **`s`** — the full list of **workable states** for the ten-day window.
- **`d`** — the full list of **workable DXCC** entities.
- **`w`** — save the whole result to a text file under **`/CardSat/workable/`**.

**Grids are off by default.** The grid union needs a few kilobytes of working memory, and on this
board that memory is better left free for other things most of the time. States and DXCC — the
common case — need no extra memory at all. If you *do* want the grid list, press **`g`** on the
results screen and the sweep re-runs with grids included.

The result is a planning answer you couldn't get before at a glance: not "what's the next pass,"
but "over the whole coming week and a half, what is the complete set of places my station can
reach through the birds I care about."

## Target search — when is *this* place workable?

From **Next Passes**, press **`s`**. Pick a single target — a **US state**, a **DXCC entity**,
or a **grid** — and CardSat finds every pass, across all of your favorites, over the next ten days
where that target sits inside the satellite's footprint while the bird is up for you. Grids are
entered through the on-screen editor; states and DXCC are chosen from a filterable list (type a
letter to narrow it).

The results are listed **in time order across your whole fleet** — the soonest workable passes
first, mixed together regardless of which satellite each one is on — showing the satellite, the
date, the workable window (the span of the pass during which the target is actually in the
footprint), and the maximum elevation. Press **ENTER** on any row to see that specific pass drawn
as a **polar plot**, and **`w`** to save the results under **`/CardSat/search/`**.

This is the direct question a rover or an award-chaser asks: *I need Fiji (or Wyoming, or EM10) —
when's my next shot?* Target search answers it for the next ten days in one screen. If a place
simply isn't reachable from your location in that window — say a distant DXCC entity no LEO
footprint can cover together with your QTH — the search completes and tells you so, rather than
leaving you guessing.

---

## Under the hood — speed and memory

None of the following changes anything you see in the results; they're about making the two new
features usable on the hardware.

### A much faster footprint engine

The coverage sweeps test, for many sampled points along each pass, which states and DXCC entities
fall inside the footprint. The first working version was correct but slow — a full ten-day,
all-favorites horizon sweep took well over an hour. Three changes brought that down to a few
minutes, each verified to produce **byte-identical** results to the version before it:

- **Coarser sampling.** A footprint is roughly 2,500 km in radius while the sub-point moves only
  a few hundred kilometres a minute, so samples spaced a couple of minutes apart still overlap
  completely — there are no coverage gaps in a union. Sampling every three minutes instead of
  every thirty seconds cut the work several-fold with no loss.
- **A per-sample candidate pre-filter.** Instead of testing every entity against every mesh cell,
  each sample first builds a short list of only the entities whose bounding box overlaps the
  footprint at all, then tests just those. For a footprint over, say, the central US that turns a
  check against 160-odd DXCC polygons per cell into a check against a handful.
- **A single merged mesh walk.** The states pass and the DXCC pass used to walk the same
  footprint grid twice; they now share one traversal, checking both per cell.

### On-demand audio (memory for network uploads)

On this no-PSRAM board, the largest *contiguous* free block matters as much as the total free
memory — a TLS handshake for a LoTW or Cloudlog upload needs a big contiguous chunk. The M5
speaker driver holds several kilobytes of I2S/DMA buffers whenever it's active, and previously it
came up on the first sound of a session and stayed up, sitting in the middle of the heap.

The speaker is now **powered only when it's actually making sound** — around games, voice-memo
record and playback, alarm beeps, and the settings volume/sound previews — and released a few
seconds afterward. Between those moments the buffers are freed, which keeps the largest
contiguous block high for uploads. The power-up and release are scoped to whole sessions (an
entire game, a whole alarm sequence) so you won't hear a click between individual beeps.

### Housekeeping fixes found in testing

- **Workable-horizon drill-down showed empty.** The states/DXCC lists gate their display on a
  count that the horizon sweep wasn't publishing, so the on-screen lists came up empty even though
  the exported file was correct. The union counts are now published to those screens; the lists
  display properly, titled **Workable states** / **Workable DXCC** to distinguish them from the
  per-pass rove-planner lists.
- **Exports get their own folders.** Horizon results save under `/CardSat/workable/` and target
  searches under `/CardSat/search/`, instead of cluttering the card root.
- **Next Passes footer** now advertises the two new keys: `w` (horizon) and `s` (find).

---

## Keys added this release

All on the **Next Passes** screen:

- **`w`** — Workable horizon (10-day union of states / DXCC / optional grids across all favorites).
- **`s`** — Target search (every pass over 10 days where one chosen place is workable).

On the Workable-horizon results screen: **`s`** states list, **`d`** DXCC list, **`g`** re-run
with grids, **`w`** save. On the Target-search results screen: **ENTER** polar plot of the
selected pass, **`w`** save.

---

## Upgrade notes

Drop-in from 0.9.51. No changes to settings, the log format, the on-air/CAT behavior, wiring, or
any existing screen. The new features are additive and reachable only from the two new keys on
Next Passes; if you never press them, nothing about your existing workflow changes.
