# AO-7 mode-switch estimator — scope

Status: **implemented** (0.9.65 cycle). This document also records the design.

> Note: an earlier draft assumed Mode A/B followed calendar day parity (odd/even days). That
> was only true under active command years ago; the implemented tool derives the switch
> cadence purely from report timestamps via a period+phase fit (see Job 2). Scopes a tool that estimates AO-7's
Mode A / Mode B switch time and tells the user which mode it should currently be in.

## Background — why AO-7 needs this

AO-7 (launched 1974, listed as **AO-07** in some GP distributions) has no working batteries;
it runs entirely on solar power and is only active in sunlight. It carries a **24-hour timer**
that, **when the satellite is in continuous sunlight**, automatically alternates between:

- **Mode A** — 145 MHz uplink → 29 MHz downlink (2 m up / 10 m down)
- **Mode B** — 432 MHz uplink → 145 MHz downlink (70 cm up / 2 m down)

When AO-7 passes through eclipse each orbit, the timer resets on each power-up, so the mode is
effectively tied to the illumination cycle rather than a free-running 24 h clock. Only during
**continuous-sunlight** seasons (when the orbit's beta angle is high enough that AO-7 never
enters Earth's shadow) does the timer run uninterrupted and flip modes on a fixed cadence
(period near but not exactly 24 h, so the switch drifts against UTC). So the tool has two jobs:

1. Determine whether AO-7 is currently in **continuous sunlight** (24 h timer active) or
   eclipsing each orbit (mode tied to power-up).
2. If in continuous sunlight, use the **AMSAT Status API** reports for the past week to
   **estimate the daily switch time** by observing when reports shift from one mode's
   transponder to the other's, and project the current/next mode from that.

## Grounding — what CardSat already has

Almost everything this tool needs already exists:

- **Continuous-sunlight detection:** `Predictor::sunlitAt(t)` (`predict.h:64`) and
  `betaAngleDeg()` (`predict.h:77`). Sample `sunlitAt` across one orbit; if the satellite is
  sunlit at *every* sample, it is in continuous sunlight (equivalently |beta| > betaStar,
  the same test the thermal tool and Outlook screen use). This directly answers job 1.
- **The AMSAT Status API is already wired:** `AMSAT_REPORTS_URL`
  (`www.amsat.org/status/api/v1/reports.php?name=…&hours=…&limit=…`, `config.h:377`) and the
  parser `App::fetchAmsatReports()` (`app.cpp:21044`), which already extracts per-report
  `callsign`, `grid_square`, `report` status (Heard / Not heard / Telemetry / Crew), and
  `reported_time` → unix via `isoZToUnix()`. This is the raw material for job 2.
- **Mode-tagged names:** the status API distinguishes transponders by a mode-tagged name
  (the parser comment notes names like `"AO-91_[FM]"`). AO-7 reports appear under its two
  transponders, so **the Mode A vs Mode B signal is already in the report stream** — a report
  against the 10 m-downlink transponder implies Mode A was active at that time; a report
  against the 2 m-downlink transponder implies Mode B.
- **The `AO-07 → AO-7` alias** already exists (`app.cpp:10802`), so the GP-distribution naming
  variance is handled.
- The `hours=` window is already a user setting (`cfg.amsatWindowH`); the tool would request a
  wider fixed window (~168 h / 7 days) for this analysis.

**So the tool is mostly analysis on top of existing fetch + parse + propagation**, not new
infrastructure.

## The method

### Job 1 — continuous sunlight?

- Load AO-7 from the catalogue (alias-resolve AO-07/AO-7), propagate one orbit, sample
  `sunlitAt(t)` at ~120 points. If all are sunlit → continuous sunlight, the 24 h timer is
  free-running, and the switch-time estimate is meaningful. If any sample is eclipsed → report
  "AO-7 is eclipsing each orbit; the 24 h timer resets on power-up, so mode follows the
  illumination cycle rather than a fixed daily switch" and stop (the daily-switch estimate does
  not apply).
- Report the current beta angle and the eclipse fraction so the user sees *why*, and — a nice
  extension — the next date the continuous-sunlight window opens/closes (scan beta forward over
  weeks until |beta| crosses betaStar).

### Job 2 — estimate the switch cadence (only in continuous sunlight)

**Important correction:** the mode is NOT tied to calendar day parity. The "Mode A on odd
days / B on even days" pattern only held when AO-7 was actively commanded years ago. The
onboard timer now free-runs with a fixed period near — but not exactly — 24 h, so the switch
instant **drifts against UTC** and must be derived purely from report timestamps. The
implemented method:

- Fetch a **long window** (up to 30 days — 720 h, the API's documented maximum) of reports for
  each of AO-7's two mode-tagged names **separately**, as server-side-filtered requests:
  `reports.php?name=AO-7_[V/a]` (Mode A, 145 up / 29 down) and `name=AO-7_[U/v]` (Mode B,
  435 up / 145 down) — confirmed against the live API; the catalog's `AO-7[A]`/`AO-7[B]`
  aliases are not what reports use. Fetching per-mode rather than one shared unfiltered request
  puts the API's full 500-record limit on AO-7 specifically, instead of diluting it across
  every satellite in the feed. Each **"Heard" report** becomes a `(unix time, mode)`
  observation, streamed object-by-object from the response into a small fixed scratch buffer
  (never the whole body in RAM). "Telemetry Only" reports are NOT counted as switch evidence —
  real data showed the telemetry beacon is audible somewhat independent of which transponder is
  actually switched in, and including it produced spurious rapid-flip artifacts that corrupted
  the fit (see the grid-search note below for the full failure this caused on live data).
- **Sort** the observations by time.
- **Exclude pre-illumination data:** before bracketing switches, drop every observation older
  than the satellite's most recent continuous-full-sun start. That start is found by walking
  backward from now, one day at a time, sampling ~24 points across one orbital period at each
  day via the same SGP4 propagator + `sunlitAt()` used for the Job 1 check, until a day is
  found that is not fully sunlit — illumination began the day after that. Reports from before
  that instant may still be from an eclipsing season, when the timer resets each orbit instead
  of free-running, and would inject a stale or irregular phase into the fit.
- **Bracket switches:** every adjacent pair (within the illuminated window) with **opposite
  modes brackets a switch**; take the midpoint as a switch-instant estimate and the pair's time
  span as that bracket's uncertainty. Reject brackets wider than a fixed 20 h ceiling (a longer
  silent gap could plausibly hide more than one real switch).
- **Grid-search the period rather than assume it.** The true switches form a linear sequence
  `s_k = t0 + k·P`. An earlier version indexed each bracket by `round((s_i − s_0)/P_guess)` with
  `P_guess ≈ 24 h` and iterated that rounding a few times — but real AO-7 data showed this can
  alias catastrophically: on live reports pulled 2026-07-24, the true current cadence turned out
  to be **~19.5 h**, nowhere near 24 h, and the 24h-anchored fit produced a wildly wrong
  next-switch prediction (17:39Z ± 340 min against an observed switch at ~04:00Z). The fit now
  **grid-searches P** over a domain-plausible 12–30 h range (5-minute steps — wide enough for AO-7's
  real drift/variation, narrow enough to exclude implausible sub-hour aliases a handful of sparse
  points can otherwise fit just as well), picking whichever candidate period minimizes an
  **inverse-span-weighted** residual (a 20-minute bracket counts far more than an 18-hour one).
  On the same real data this recovers 19.5 h at 47.6-minute RMS — about 7× tighter than the old
  approach's 340 minutes, and it lands within the operator-observed switch window.
- **Prefer recent data when it fits better:** fit twice — once over every illuminated-window
  boundary, once over just the most recent half — and prefer the recent-only fit **only** when
  its residual is substantially tighter (at least halved), which signals the recent behavior is
  more self-consistent than the full baseline. A marginal improvement isn't preferred, since
  fewer points can always fit at least as well just by having less to explain; a real,
  substantial improvement is. (The "is the baseline fit already near-perfect" guard is pinned to
  the grid search's own step size, not zero, since even a perfectly consistent dataset has a
  quantization-floor residual of roughly half a step.)
- **Project:** the next switch after now is `t0 + ⌈(now−t0)/P⌉·P`; the current mode comes from
  the most recent observation stepped by the parity of the number of switches between its
  interval and the current one.
- **Near-boundary honesty:** when `now` falls within the fit residual (or ~30 min) of a
  predicted switch, the mode is genuinely ambiguous — the tool flags "near a switch: mode
  uncertain now" rather than committing.
- Report the fitted **period**, the number of boundaries used (and the total found, when a
  recent-only fit was preferred), the reports within the illuminated window (A/B split, and how
  many older reports were excluded), the **fit residual** (±minutes) as the confidence measure,
  the current mode, and the time + UTC instant of the next switch.

### Honesty about the signal

The estimate is only as good as the crowd-sourced reports: sparse days, gaps, or a mode with
few listeners weaken it. The tool must:

- Show the **number of reports** underpinning the estimate, the illuminated-window start date,
  and how many older reports were excluded, and widen/withhold the confidence interval when
  data is thin.
- Never present a two-boundary fit as confident — two points always fit a line perfectly, which
  is interpolation, not evidence of periodicity; at least three boundaries are required.
- Label the result an **estimate from listener reports**, not a definitive schedule.

## Outputs

- **On-screen:** continuous-sunlight yes/no (+ beta, eclipse fraction); if yes: current mode,
  time-to-next-switch, today's estimated switch time (UTC), the daily drift, and a small
  fit summary (period, boundary count, report counts, residual) so the user sees the data
  density and confidence. If no: the eclipse explanation.
- **Printable:** a `PR_AO7MODE` report through the universal form-tool print path (respecting
  the 0.9.65 narrow-paper helpers): the sunlight verdict, the fitted period + phase, the
  boundary/report counts, the fit residual, and the current/next mode.

## Compute + network budget (no-PSRAM discipline)

- One reports.php fetch (168 h window), streamed to the existing `FILE_DL_TMP` and parsed by
  the existing report parser extended to store (time, mode) observations. Window is 30 days,
  cap 200 observations (fixed arrays, ~3 KB .bss); the sort + linear fit are trivial.
- The sunlight sampling and the linear fit are trivial arithmetic; run the fetch as the
  existing cooperative network step, the analysis inline.
- Fixed ~3 KB `.bss` for the observation arrays (time + mode) plus the fit scalars.

## What must not regress

- Purely additive: a new tool screen + `PR_AO7MODE`, reachable from the Tools menu (Satellite
  & orbital category). No change to the existing AMSAT status screens, the report parser's
  current callers, or prediction.
- The existing `fetchAmsatReports()` behaviour for its current caller is untouched; the tool
  either reuses it (if the mode tag can be captured without changing the struct) or adds a
  parallel `fetchAmsatReportsByMode()` so the on-screen status list is unaffected.

## Verification plan

- All nine gates + `src`/`.ino` body parity.
- A **host-side estimator unit test** (mirroring the orbit/wrap/thermal harnesses): feed a
  synthetic week of mode-tagged reports with a known switch time + drift and assert the fit
  recovers them, and that thin/one-report days are flagged low-confidence rather than reported
  as precise. Validates the estimation math without network or hardware.
- Sanity-check against a live week of real AO-7 reports once on hardware (the one step that
  needs the network), comparing the estimate to the community-known behaviour.

## Effort estimate

- Continuous-sunlight test + beta/eclipse readout (reuse `sunlitAt`/`betaAngleDeg`): ~40 lines.
- Report fetch with mode tag + per-(day,mode) bucketing (extend/parallel the existing parser):
  ~90 lines.
- Timestamp sort + boundary bracketing + period/phase least-squares fit + mode projection
  with near-boundary flag: ~120 lines.
- Screen + `PR_AO7MODE` print path: ~120 lines.
- Host-side estimator unit test: ~150 lines.
- All mirrored `src`↔`.ino`; one compile. Only the live-report sanity check needs the network.

## Open items before implementing

1. ~~Confirm the exact mode-tagged name(s)~~ **CONFIRMED** against the AMSAT status API's own
   published name list: the API uses **`AO-7[A]`** (Mode A, V/a: 145 up / 29 down) and
   **`AO-7[B]`** (Mode B, U/v: 435 up / 145 down) — the simple `[A]`/`[B]` bracket suffix, the
   same pattern as `PO-101[FM]`, `UO-11[B]`, `FO-118[V/u]`. (The `V/a` / `U/v` notation seen on
   the status *page* is the human-facing mode label, not the API query name.) The tool queries
   `reports.php?name=AO-7[A]` and `name=AO-7[B]`, percent-encoding the brackets.
2. Decide the reports window (7 days proposed) and report cap (raise from 24) for enough
   two-mode coverage without overrunning the parse budget.
3. Confirm the continuous-sunlight betaStar threshold uses AO-7's actual (high, ~1450 km,
   e≈0.0012) orbit — its altitude makes betaStar smaller, so it reaches continuous sunlight at
   a lower beta than a LEO cubesat; the analytic test already handles this via the real
   altitude, just confirm on a known continuous-sunlight date.
