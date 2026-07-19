# App-object RAM staging analysis (0.9.60)

Three proposals to shrink the 92,128 B `App` object and reduce heap fragmentation.
Every size below is measured from the DWARF layout of the shipped ELF, not
estimated. The headline: the App object is 72% of static RAM, and the no-PSRAM
S3's real enemy is *contiguous*-heap fragmentation, so the prize is genuine — but
the specific fields named in the proposals are not all the biggest, and one
common assumption (moving cold data frees BSS *and* helps the heap) only half
holds. Recommendations are per-proposal.

## What actually dominates the App object

| Field | Bytes | Nature | Refs | Stageable? |
|-------|------:|--------|-----:|:----------:|
| `db` (SatDb / `_sats[150]`) | 23,296 | the catalog — always live | many | no (hot) |
| `logRecs` (PendingQso[]) | 8,640 | QSO log queue | 10, incl. logging path | risky |
| `passScratch` (PassPredict[]) | 5,120 | pass finder scratch | hot | no |
| `activeTx` (Transponder[]) | 4,864 | active-sat transponders | 68, incl. Doppler | no (hot) |
| `hamsatList` (Activation[]) | 2,860 | hams.at cache | activations screen | yes |
| `noteList` / `noteTime` | 2,560 | notes editor | notes screen | yes |
| `tsHits` + `tsNextPass` | 3,200 | target search | one screen | yes |
| `skyBars` | 1,920 | sky-glance bars | one screen | yes |
| `dgSat` (SatEntry[]) | 1,904 | debris-group screen | one screen | yes |
| `msgRing` (LoraMsg[]) | 1,728 | LoRa chat ring | messaging + netplay | risky |
| `roveList` / `userSked` / `sched` | 4,660 | rove plans / sked / schedule | few screens | yes |
| `catMonLines`+`catLines` (String[]) | 1,792 | CAT monitor + self-test | 2–3 sites | yes (see P2) |
| `ovhName` | 1,040 | overhead snapshot | overhead screen | yes |
| `mutual`+`satsatWin`+`emeMut` | 1,472 | mutual-window arrays | 2–3 screens | yes |
| `pdEl`+`pdAz` | 800 | pass-detail polar curve | pass detail | yes |

The scroll integers the proposal names first (help/gloss/guide/learn/sat-history)
are `int16` — about 2 bytes each, ~20 bytes total. They are not where the memory
is; the arrays above are.

## Proposal 1 — stage cold screen state into an on-demand context

**What's real.** A conservatively "cold" set — genuinely touched only from one
screen's draw/key paths — totals about **21–24 KB**: `tsHits`, `tsNextPass`,
`dgSat`, `skyBars`, `hamsatList`, `noteList`/`noteTime`, `ovhName`, the
mutual-window trio, `pdEl`/`pdAz`, `ctsRows`, `amsRpt`, `rosterList`,
`transitHits`, `planRow`, `whSeg`, `roveList`, `userSked`, `sched`. Moving these
into a `union`-style heap block allocated on screen entry and freed on exit would
cut the resident App object by that much and — more importantly — return that span
to the heap as one contiguous block whenever those screens are closed, which is
almost always.

**What's NOT safe.** `activeTx` (68 refs, read every Doppler update) and
`logRecs`/`msgRing` (touched by background logging and the LoRa RX interrupt-ish
path) must stay resident. They look cold by name but are hot by wiring; staging
them would mean a use-after-free the first time a frame arrives on a non-LoRa
screen.

**Costs.** This is the expensive proposal. Each staged field's ~30–150 access
sites must be rewritten to go through a pointer (`ctx->tsHits[i]`), a nullptr
guard added on every entry, and the dual-representation invariant means doing all
of it byte-identically in two files. Screens that can be reached *from each other*
(e.g. pass list → pass detail) can't share one context slot without care. The
gates don't check pointer-lifetime correctness, so this leans hard on the two-
device/же on-hardware bench. High risk, high one-time cost, but the single
biggest structural win available and directly on-target for fragmentation.

**Recommendation: worth doing, but incrementally and not now.** Pick the 3–4
highest-value *isolated* screens first — `dgSat` (1,904), `skyBars` (1,920),
`tsHits`+`tsNextPass` (3,200), `hamsatList` (2,860) ≈ **10 KB from four
self-contained screens** — behind one small `ScreenCtx` allocation, prove the
pattern and the lifetime discipline on hardware, then extend. Do it as its own
0.9.61 line item with a bench pass, not folded into an unrelated release. Don't
attempt `activeTx`/`logRecs`/`msgRing`.

## Proposal 2 — replace `String[]` with fixed buffers (CAT monitor, self-test, status)

**The subtlety.** `catMonLines` (1,024 B) and `catLines` (768 B) are arrays of
*pointers*; the BSS they occupy is small. Their real cost is that each element is
an Arduino `String` whose backing store is a separate heap allocation, churned as
lines scroll — and Arduino `String` never frees its buffer on reassignment (a
lesson already in the codebase), so these are a textbook mid-heap hole generator.

**Benefit.** Converting the CAT monitor and self-test line stores to fixed
`char[N][W]` ring buffers removes those per-line heap allocations entirely. BSS
barely changes (a fixed `char[16][48]` is ~768 B, similar to the pointer array it
replaces), but the *fragmentation* source is gone — which is the thing that
actually threatens stability here. Short status lines are already fixed `char[]`
in most places; the remaining `String` status assignments are the other easy win.

**Cost.** Low and contained — a handful of call sites per store, no lifetime
subtlety, and it's the kind of change the gates and a quick bench fully cover.

**Recommendation: do this first.** **DONE in 0.9.60** — see note below. Best effort-to-benefit ratio of the three:
small, safe, and it attacks fragmentation directly rather than just static size.
Good standalone 0.9.60 or 0.9.61 item.

## Proposal 3 — pack Settings / drop unused printer, BASIC, secondary-Wi-Fi fields

**The numbers.** The whole `Settings` (`cfg`) object is **1,368 B** static. The
droppable pieces are a fraction: `wifiAp[]` (secondary Wi-Fi) is 560 B, and the
printer/BASIC toggles are a few bytes each. Packing the remaining `char[]`
callsign/URL buffers and small fields might reclaim a few hundred bytes.

**The catch.** `cfg` is serialized to `config.json` by name, so removing a field
is a compatibility question, not just a struct edit — old configs with the key
load fine (the `| default` idiom tolerates absence), but a *renamed or
compile-time-removed* field silently drops a user's saved value. The printer and
BASIC fields are also genuinely used features, not dead weight; "unused" isn't
accurate — gating them out at compile time would fork the build and complicate
the Settings UI the proposal hopes to simplify.

**Recommendation: skip, with one exception.** The reclaim (a few hundred bytes to
~1 KB best case) is small next to Proposals 1–2, the compatibility surface is
annoying, and the "unused" premise doesn't hold. The one worthwhile sub-item:
if secondary Wi-Fi (`wifiAp[]`, 560 B) is truly unused in practice, dropping it is
a clean ~0.5 KB with only a config-key retirement to manage. Not worth a build
fork for the rest.

## Bottom line (recommended order)

1. **Proposal 2 (String→fixed buffers)** — do it; small, safe, hits fragmentation.
   ~1.5 KB of churn-prone heap allocations eliminated.
2. **Proposal 1, staged incrementally** — a `ScreenCtx` for 4 isolated screens
   (~10 KB) as a 0.9.61 line item with a hardware bench; extend later. The largest
   structural win, but earn it in steps and never stage the hot arrays.
3. **Proposal 3** — skip, except possibly retiring `wifiAp[]` (~0.5 KB) if unused.

None of this is urgent: flash is 89%, IRAM has headroom, and the SatEntry reorder
already trimmed the hottest block. These are fragmentation-hardening moves for the
no-PSRAM part, best taken deliberately rather than bundled.


---

## Proposal 2 — implemented (0.9.60)

`catMonLines[64]` and `catLines[48]` converted from `String[]` to fixed
`char[64][40]` and `char[48][56]`. The self-test append (`strncpy`, bounded),
the monitor hex-dump builder (`snprintf` into the slot, width-guarded), and both
draw paths (`%.36s`/`%.38s`, `strncmp` for the tag colouring) were updated;
`catMonIsTx`/`catCount`/`catMonHead` ring machinery is unchanged.

Measured effect: static globals 151,064 -> 154,520 B (+3,456), because the fixed
line storage (~5.2 KB) now lives permanently in BSS where the pointer arrays
(~1.8 KB) used to. That trade is deliberate and is the whole point on a no-PSRAM
part: **every per-line heap allocation is gone** (Arduino String never freed its
buffer on reassignment, so scrolling CAT traffic churned the heap), so the
mid-heap-hole source is eliminated at the cost of a few KB of *predictable* static
RAM. Fragmentation, not static size, is what destabilises this device. DWARF
confirms both members are now raw `char` arrays with no String backing. Flash
essentially unchanged.


## Proposal 2, extended — LAN line buffers (0.9.60)

Follow-up to the CAT-store conversion: the three rigctld/rotctld/web-dashboard
line assemblers (`rigdBuf`, `rotdBuf`, `webdBuf`, plus `webdReqLine`) were the
higher-value String churn source, because they append **one character per
received byte** inside the network receive loop — a heap realloc per byte on
every LAN CAT/rotator/HTTP command, thousands per WSJT-X or GPredict session,
sustained for the entire operating session rather than only when a diagnostic
screen is open. All four are now a small `LineBuf` (fixed `char[208]` + length,
per-buffer cap) that appends with no allocation and hands the completed line to
the unchanged String-taking handlers as a C-string — one temporary per newline
instead of a realloc per byte.

Measured: static globals +784 B (four LineBufs replacing four small String
members). Flash unchanged. DWARF confirms all four members are now `LineBuf`
with no String backing. This is the last of the safe, contained String→buffer
conversions; the remaining String members (status, calc/BASIC buffers, per-op
status lines, editor buffers) are user-paced or one-shot and are deliberately
left as String — converting them would add static RAM and rigidity for no
fragmentation benefit.
