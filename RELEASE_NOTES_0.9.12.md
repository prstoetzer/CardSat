# CardSat v0.9.12 — Release Notes

A performance and fixes release. Computing **workable DXCC** and **workable US
states** for a *future pass* is now roughly **12–15× faster** with identical
results; the **Update screen** names the GP source that will actually be
downloaded; and **CelesTrak** GP queries use the correct request format (with a
migration for sources saved by older builds).

> **Hardware status — unchanged.** Pass prediction, plots, GPS, the AOS alarm,
> deep sleep, and the offline caches are confirmed on hardware. Radio, rotator,
> and network paths remain host-tested only.

---

## Fixes

- **Update screen now names the actual GP source.** The Update screen always read
  "download GP (AMSAT)" even when a CelesTrak category or a custom URL was
  selected. It now shows the configured source — e.g. "download GP (CT:amateur)"
  or "(Custom)" — using the same label logic as the Settings screen.
- **CelesTrak GP downloads.** CelesTrak queries were built with a lowercase
  `FORMAT=json-pretty` token; the documented token is uppercase, and CelesTrak's
  edge can reject or redirect malformed/legacy requests (and will temporarily
  firewall an IP that produces repeated errors — which surfaces as a
  *connection refused*). New CelesTrak sources now use the documented
  `FORMAT=JSON` (compact, valid, smaller), and a URL saved by an older build is
  **migrated automatically** to the correct token on the next download. If a
  CelesTrak fetch still fails with a refused/blocked-style error, the on-screen
  message now suggests trying AMSAT or waiting, since the IP may be temporarily
  blocked. *(Note: the underlying HTTPS fetch path is unchanged and remains
  host-tested only; this corrects the request the device sends.)*

---

## Performance

- **Workable DXCC / states: per-pass build ~12–15× faster.** The footprint walk
  now (1) skips any entity already found, doing zero work for it instead of
  re-walking its polygon, and (2) rejects each mesh point against a per-entity
  **bounding box** with four integer comparisons before ever running the
  point-in-polygon ray-cast — so the ~99% of entities nowhere near a given mesh
  point cost almost nothing. Point entities also get a cheap latitude-band
  pre-reject before the exact great-circle distance test.
- The optimization is **exactly equivalent** to the previous algorithm — verified
  identical results across hundreds of randomized footprints — so the lists you
  get are unchanged; they just appear far sooner. On a future MEO pass the build
  drops from several seconds to a fraction of a second on the device.

---

## Internals

- Added per-polygon index tables (`*_START`, `*_LOMIN/LOMAX/LAMIN/LAMAX`) in
  flash for both `DXCCPOLY` and `STATEPOLY`, derived from the polygon data
  (~2.1 KB total, no extra RAM).
- `addFootprintDxcc` / `addFootprintStates` rewritten to iterate entities with
  bounding-box rejection and skip-if-found; new single-polygon helpers
  `dxccPipAt` / `statePipAt` (test by start offset, no array advance). The old
  walk-and-advance `dxccPolyTest` / `statePolyTest` are removed (no longer used).
- Point-entity loops gain a conservative latitude pre-reject; the exact haversine
  distance is retained for the final decision (no accuracy change).
- New `App::gpSourceLabel()` helper (used by both the Update and Settings
  screens); CelesTrak URL builder uses `FORMAT=JSON`; `doUpdateGp()` migrates a
  stale `FORMAT=json-pretty` CelesTrak URL and adds a block-aware error hint.
- `FW_VERSION` → **0.9.12**.

---
