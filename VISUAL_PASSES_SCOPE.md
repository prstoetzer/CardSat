# Scope: Visual Pass Predictions ("can I see it?")

**Status: design scope only — not implemented.** This scopes flagging passes that
are **visually observable** — satellite sunlit, observer in darkness, bird bright
enough and high enough to see with the naked eye — and surfacing that on the pass
list, schedule, and a dedicated "visible tonight" view.

---

## 1. The need

CardSat predicts radio passes thoroughly, but a large fraction of its likely users
(and almost everyone "interested in space") also want to **watch** the ISS and other
bright objects cross the sky. Today nothing tells you which of the upcoming passes
are actually visible to the eye, so you can't use CardSat the way you'd use Heavens
Above or an app like ISS Detector. The math to answer "can I see it?" is almost
entirely already computed; what's missing is the visibility test and the UI to show
it.

A pass is visually observable when **all** of these hold during the pass:
1. the **satellite is sunlit** (not in Earth's shadow),
2. the **observer is in darkness** (Sun sufficiently below the horizon — civil/nautical
   twilight or darker),
3. the **satellite rises high enough** to clear terrain/haze (a peak-elevation gate),
4. the object is **bright enough** to see (a magnitude estimate, or simply a known-bright
   allow-list like the ISS).

---

## 2. What already exists (building blocks)

This feature is unusually cheap because the geometry is already in `LiveLook`
(`predict.h`):

- `LiveLook.sunlit` — satellite illuminated (Earth cylindrical-shadow test). Already
  computed every `look()`; also available standalone via `sunlitAt(t)`.
- `LiveLook.sunEl` — **Sun elevation from the observer** (degrees). This is the
  observer-darkness signal: `sunEl < -6` (civil dark) … `< -12` (nautical).
- `LiveLook.el`, `LiveLook.rangeKm`, `LiveLook.satAltKm` — for the elevation gate and
  the magnitude model.
- `eclipseDepthDeg(t)` exists if a softer penumbra test is ever wanted.
- The **pass scheduler** (`buildSchedule()`, `SCHED_MAX`, `SCR_SCHEDULE`) already walks
  all favorites and computes AOS/LOS/peak-el per pass — the natural place to compute a
  per-pass `visible` flag.
- `SCR_PASSES` / `SCR_PASSDETAIL` / `SCR_PASSPOLAR` already render passes; adding a
  marker is a draw change, not new prediction.

So the only genuinely new computation is (a) sampling sunlit + sunEl across a pass and
(b) an optional magnitude estimate.

---

## 3. How others handle it

- **Heavens-Above / ISS Detector / GoSatWatch:** identical logic — satellite sunlit,
  observer in twilight-or-darker, peak elevation above ~10°, and a magnitude estimate
  (standard magnitude scaled by range and solar phase angle). They typically require
  the brightest part of the pass to meet a magnitude threshold (≈ +4 naked-eye, often
  filtered to +2 or brighter).
- **Magnitude model (standard form):** `m = m_std + 5·log10(range/1000km) − 2.5·log10(phase_factor)`,
  where `m_std` is the object's standard magnitude at 1000 km and 50% phase, and the
  phase factor comes from the Sun–satellite–observer angle. For a naked-eye allow-list
  (ISS, bright rocket bodies) a fixed `m_std` per object is plenty; a full catalog
  magnitude would need a data source CardSat doesn't carry.
- **Twilight thresholds:** civil = Sun −6°, nautical −12°, astronomical −18°. Most
  visual-pass tools use "Sun below −6°" as the default observer-darkness gate.

---

## 4. Proposed behaviour

### 4.1 Per-pass visibility flag
When building the schedule (and in `SCR_PASSDETAIL`), sample the pass at a few points
(or reuse the existing AOS/TCA/LOS samples). Compute:
- `satSunlitAtTCA` (and whether the sunlit window overlaps the above-horizon window),
- `obsDarkAtTCA` (`sunEl < cfg.visSunElMax`, default −6°),
- `peakEl ≥ cfg.visMinEl` (default 10°).

A pass is **visible** if a sunlit + above-horizon + observer-dark interval exists, with
peak elevation over the gate. Store one bool (and optionally the brightest magnitude)
per scheduled pass.

### 4.2 Surfacing it
- **Pass list / schedule:** a small star/eye glyph (and/or a different row colour) on
  visible passes. Optional filter key to show **visible-only**.
- **Pass detail:** a line like `Visible: yes — sunlit, Sun −9°, max el 62°, ~ -2.4 mag`,
  or `Visible: no (daylight)` / `(satellite in shadow)` / `(too low)` so the *reason* is
  explicit.
- **New "Visible tonight" view (optional):** a cross-favorites list of just the
  observable passes in the next N hours, sorted by time, each with time/direction/peak
  el/brightness — the "what can I go outside and see" screen. This reuses the schedule
  builder with the visibility filter applied.

### 4.3 Magnitude (optional, phased)
- **Phase 1:** allow-list only. A small table of `m_std` for the brightest, well-known
  objects (ISS ≈ −1.8, CSS/Tiangong ≈ −1, bright rocket bodies). Objects not in the
  table show "visible (mag n/a)".
- **Phase 2:** compute the range/phase-scaled magnitude from `m_std`, `rangeKm`, and the
  Sun–sat–observer phase angle (derivable from `sunAz/sunEl` and the satellite look
  angles already in `LiveLook`).

---

## 5. Settings

Network/data or a new "Visual" group:
- **Visible passes** on/off (compute + show the flag at all).
- **Darkness gate** — Sun below −6° / −12° / −18° (default −6°).
- **Min peak elevation** — default 10°.
- **Brightness filter** — show all visible / only bright (≤ +2 mag) — only meaningful
  once magnitude exists.

Persisted in `settings.cpp` alongside the existing pass/alarm settings.

---

## 6. Cost / risk

- **Compute:** negligible — a handful of extra samples per pass on top of the schedule
  that already runs. No new propagation passes if folded into the existing scheduler
  loop; keep it incremental (the scheduler is already watchdog-safe and chunked).
- **Heap:** one bool (+ optional float) per scheduled pass — `SCHED_MAX` is small.
- **Accuracy:** the sunlit/shadow test is cylindrical (no penumbra); fine for naked-eye
  use. Magnitude is an estimate; label it as such.
- **No new hardware, no audio, no toolchain constraints.**

---

## 7. Out of scope

- Flares/glints (Iridium-style) — needs attitude/panel geometry CardSat doesn't have.
- A full magnitude catalog for every object (no data source on-device).
- Atmospheric-extinction modelling beyond the simple elevation gate.

---

## 8. Verification

- Host: feed known ISS passes (sunlit, observer dark, peak el) and confirm the flag and
  reason strings match Heavens-Above for the same TLE/site/time.
- Confirm daylight passes flag **not visible (daylight)** and deep-shadow passes flag
  **not visible (satellite in shadow)**.
- On-device: a clear evening ISS pass should light up the flag and the "visible tonight"
  list; verify against the eye.
