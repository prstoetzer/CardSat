# AMSAT / JE9PEL frequency database — fallback & cross-check source

Status: **scoped, not implemented** (0.9.65 cycle). Held pending confirmation of the
CSV schema and a license/attribution check. This document is the implementation plan.

## Goal

Use the AMSAT / JE9PEL amateur-satellite frequency database (the machine-readable
palewire mirror at `github.com/palewire/amateur-satellite-database`) as a **fallback and
cross-check** for transponder data — specifically to fill gaps where SatNOGS returns
nothing for a satellite — **without disturbing existing behavior**. SatNOGS stays the
primary source in every path; the JE9PEL data is consulted only where CardSat currently
shows zero transponders, and never overrides SatNOGS frequencies.

## Why this source

The palewire repo is a machine-readable mirror of Mineo Wakita JE9PEL's frequency list
(the same data behind AMSAT-NA's amateur-satellite index). It publishes
`data/amsat-active-frequencies.{csv,json}` (active birds only) and
`data/amsat-all-frequencies.{csv,json}` (all statuses). It is small, human-curated, and
active-status-aware — complementary to SatNOGS, which is comprehensive but crowd-sourced
and sometimes stale on status. The same file already feeds Ham2K's logger and
SDR-Console's markers.

JE9PEL status codes: Active `*`, Inactive `i`, Reentered `r`, Failure `f`, Deep-space `d`,
Non-amateur `n`, To-be-launched `t`, Unknown `u`.

## Design principle — the safety guarantee

**SatNOGS remains primary in every code path.** JE9PEL data enters only:
1. as a **fallback** when SatNOGS yields zero transponders for a bird, and
2. as an **advisory cross-check** that never changes tracking or overrides frequencies.

Because the less-structured JE9PEL data enters only where CardSat currently has *nothing*,
there is no path where it can corrupt or replace working SatNOGS data. Every new code path
is gated behind an explicit "SatNOGS returned empty" condition; remove the gate and
existing behavior is byte-for-byte unchanged.

## Grounding in current code

- SatNOGS fetch: `Net::fetchSatnogsTransmittersToFile(norad, path)` (`net.cpp:742`) —
  streams `SATNOGS_TX_URL + norad` to a temp via `httpsGetToFile`, then
  `Store::promoteFileTransactionally` (H13 no-clobber). Per-NORAD cache files.
- SatNOGS parse: `SatDb::loadTxCache` → `parseTransmittersStream` (ArduinoJson, filtered,
  streamed — never a big RAM String).
- GP streaming precedent for a line-oriented parser: `SatDb::streamGpFileEntries`
  (`satdb.cpp:890`).
- **Integration point:** `App::ensureTransponders(SatEntry&)` (`app.cpp:1153`), a clean
  cascade — (1) LittleFS cache, (2) SatNOGS network if cache empty, (3) append manual
  transponders, (3b) rank. JE9PEL slots in as **step 2b**, only when `activeTxCount == 0`
  after SatNOGS.
- `Transponder` struct (`satdb.h:19`) already carries a `bool active` field usable for the
  status cross-check.

## Item 1 — Bulk "active frequencies" seed (one fetch, streamed CSV)

- **URL const** (`config.h`), pinned to the *active* file (small; inactive rows are useless
  for tracking):
  `#define AMSAT_FREQ_URL "https://raw.githubusercontent.com/palewire/amateur-satellite-database/main/data/amsat-active-frequencies.csv"`
- **New `Net` method** modeled exactly on `fetchSatnogsTransmittersToFile`:
  `bool Net::fetchAmsatFreqToFile(const char* path)` — same `httpsGetToFile` → temp →
  `promoteFileTransactionally` pattern, ~100 KB cap. No new networking machinery.
- **New `SatDb` streaming CSV parser** modeled on `streamGpFileEntries`:
  `int SatDb::streamAmsatFreqEntries(const char* path, uint32_t norad, Transponder* out, int maxN)`
  — reads the cached CSV line-by-line (never a large RAM String), matches rows by NORAD id,
  fills `Transponder` records. CSV parse is lighter than the ArduinoJson path, so no heap
  regression on the no-PSRAM S3.
- **Cache path:** one shared file `SatDb::amsatFreqCachePath()` → `/CardSat/amsat_freq.csv`
  (the active set is small enough to keep whole, unlike per-NORAD SatNOGS caches).
- **Refresh trigger:** piggyback the existing GP/catalog refresh (an action alongside the
  GP refresh), or lazy-fetch on first fallback miss. No new background task.

## Item 2 — Per-satellite fallback when SatNOGS has nothing

One insertion in `ensureTransponders()`, immediately after the SatNOGS block, as step 2b:

```
// 2b) JE9PEL/AMSAT fallback: only when SatNOGS gave us nothing for this bird.
//     Never overrides SatNOGS data -- it fills a genuine gap.
if (activeTxCount == 0) {
    activeTxCount = SatDb::streamAmsatFreqEntries(
        SatDb::amsatFreqCachePath().c_str(), s.norad, activeTx, MAX_TX_PER_SAT);
    // (optionally trigger a one-time fetchAmsatFreqToFile if the cache is absent)
}
```

That is the entire behavioral change. The manual-append (step 3) and ranking (step 3b)
already run afterward and treat these records identically — no downstream changes.

## Item 3 — Status cross-check / annotation (advisory only)

- **Provenance flag:** add `enum TxSource { TX_SATNOGS, TX_JE9PEL, TX_MANUAL }` (or a
  `uint8_t src`) to `Transponder`, set at parse time. ~1 byte/record, purely additive. The
  Track/transponder UI can show a small "via AMSAT/JE9PEL" tag so the operator knows the
  data is fallback-sourced and less structured.
- **Discrepancy note (optional, cheap):** when both sources exist for a bird and JE9PEL
  marks it failed/reentered while SatNOGS marks it active, surface a one-line advisory
  ("AMSAT lists this as inactive"). Uses only the existing `active` field plus the new
  provenance flag — no new persistent state, no change to tracking.

## Handling the less-structured data defensively

JE9PEL rows are name/mode-oriented free text, not a clean schema. The CSV parser must be
lossy-tolerant:
- Map free-text mode strings through a heuristic (reuse the spirit of the SatNOGS mode
  handling); when a mode won't map, store the frequencies with a generic mode rather than
  dropping the row.
- The inverting-linear convention (JE9PEL writes downlink low/high reversed) maps to the
  existing `Transponder::invert` flag — documented on JE9PEL's page, so deterministic.
- **Any row that fails to parse is skipped, not fatal** — a malformed JE9PEL line can never
  abort the load or poison the SatNOGS path (which already completed).
- Frequencies → `freq_t` (64-bit Hz), exactly as SatNOGS does.

## What this does NOT touch (regression surface ≈ zero)

- No change to `fetchSatnogsTransmittersToFile`, `loadTxCache`, or
  `parseTransmittersStream`.
- No change to the cache→SatNOGS→manual cascade ordering; JE9PEL is a new leaf that only
  runs on an existing dead-end.
- No change to ranking, Doppler, or CAT — they consume `Transponder` records agnostic of
  source.

## Verification plan (before it ships)

- All nine gates + `src`/`.ino` body parity.
- A **host-side CSV-parser unit test** in `tools/host_orbit_audit/` (mirroring
  `wrap_test.sh`): feed real and deliberately-malformed rows from a captured
  `amsat-active-frequencies.csv`, assert correct `Transponder` extraction and that
  malformed rows are skipped, not fatal. Validates the "defensive parse" claim without
  hardware.
- **Attribution + license check FIRST (gating):** confirm the palewire repo's license and
  JE9PEL's redistribution terms, and add an attribution line alongside the existing
  CelesTrak / Open-Meteo (CC BY 4.0) / NOAA credits. CardSat attributes all its data
  sources; this is not optional.

## Effort estimate

- Item 1: ~1 `Net` method (~30 lines) + ~1 `SatDb` CSV parser (~80 lines) + 1 URL const +
  cache path.
- Item 2: ~6 lines in `ensureTransponders`.
- Item 3: 1 struct field + parse-time set + optional UI tag/advisory (~40 lines).
- Plus the host-side CSV unit test (~120 lines) and an attribution line.
- All mirrored `src`↔`.ino`, ~300 lines total, one compile.

## Open item before implementing

Pin the parser to the **real column layout** of `amsat-active-frequencies.csv` (header
names/order) rather than assuming it — capture a few header + sample rows at implement
time. Everything else is pinned to current CardSat code.
