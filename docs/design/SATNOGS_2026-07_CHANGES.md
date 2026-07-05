# Planning: SatNOGS DB changes (breaking change July 9, 2026)

**Status:** planning / assessment. No code changed yet.
**Author context:** prepared for the 0.9.48 cycle after SatNOGS announced a
satellite-`status` value change (breaking) plus two in-progress features
(transmitter parameters, launches/reception-status).

---

## 1. TL;DR — is CardSat affected by the breaking change?

**No, not directly.** The breaking change is to the **satellite** `status` field on
the `/api/satellites/` endpoint. CardSat does **not** use that endpoint. A repo-wide
search finds exactly one SatNOGS endpoint in use:

```
SATNOGS_TX_URL = "https://db.satnogs.org/api/transmitters/?format=json&satellite__norad_cat_id="
```

CardSat consumes only `/api/transmitters/` — for per-satellite transponder/beacon
frequencies. It never requests `/api/satellites/`, never parses a satellite-level
`status`, and does not branch on the strings `future` / `alive` / `dead` /
`re-entered` anywhere. So the July 9 value change (`alive`->`in-orbit`,
`dead`->`launch failed`, etc.) cannot break any current code path.

**Action required for the breaking change: none for correctness.** One small
verification task and one piece of housekeeping are listed in section 3.

---

## 2. What CardSat actually reads from SatNOGS today

Ground truth from `src/satdb.cpp` (`txBuildFilter` + `txFillFromDoc`) and
`src/satdb.h` (`struct Transponder`):

- Endpoint: `/api/transmitters/?satellite__norad_cat_id=<norad>` per active
  satellite, cached to a per-sat file (`txCachePath`).
- Fields parsed into `Transponder`: `description`, `mode`, `uplink_low/high`,
  `downlink_low/high`, `invert`, `type` (the last used to infer `isLinear`).
- Fields **allow-listed in the streaming filter but not yet acted on**: the
  **transmitter-level** `status` and `alive`. These are transmitter properties,
  distinct from the satellite-level status that is changing. Today every record
  with a tunable frequency is kept regardless of these two values (the code
  comment says so explicitly: "Keep the full SatNOGS list (active and inactive)").

The important conceptual point: **SatNOGS's change moves "activity" from the
satellite onto the transmitter/transmission entries.** That is exactly the level
CardSat already reads. The change is therefore not just harmless to CardSat — it
aligns the data model with where CardSat already looks.

---

## 3. Breaking change: concrete tasks

### 3.1 Verify (must-do, low effort)
- Confirm no regression by fetching a live `/api/transmitters/` payload for a few
  satellites after July 9 and re-running the parser. Expected: unchanged, because
  transmitter records are not the ones whose value set changed.
- Grep gate (add to the release doc checklist): assert the codebase contains **zero**
  references to `/api/satellites`, and no literal `"alive"`/`"dead"` handling on the
  satellite object. (Both true today.)

### 3.2 Housekeeping (optional, low effort)
- The transmitter streaming filter still allow-lists `status` and `alive`
  (`txBuildFilter`). These are transmitter fields and are **not** affected by the
  breaking change, but since `txFillFromDoc` does not use them, they can either be
  (a) removed from the filter to save a few bytes, or (b) kept and actually used
  (see 4.1). Recommend keeping them and adopting 4.1.

### 3.3 Do NOT do
- Do not add `/api/satellites/` consumption "to be safe." CardSat gets identity and
  elements from AMSAT GP; adding the SatNOGS satellite endpoint would newly expose
  CardSat to exactly this class of breaking change for no current benefit.

---

## 4. Refinements the *non-breaking* changes enable

These are opportunities, not obligations. Each is scoped against CardSat's real
transponder feature.

### 4.1 Transmitter `status` / `alive` -> mark inactive transmitters (ready now)

**Opportunity.** SatNOGS's stated direction is that transmitter/transmission entries
carry activity. CardSat already keeps inactive transmitters in the list but shows
them identically to active ones. With the two fields already in the parse filter,
CardSat could:
- Tag each `Transponder` with an `active` flag (from transmitter `status` == active /
  `alive` == true), and either sort active-first or dim inactive rows in the
  transponder picker (`SCR_TXDB` / the up/down/mode/tone layout).
- This directly improves the operator experience: the live FM voice or SSB
  transponder floats to the top instead of being buried among decommissioned or
  never-commissioned entries.

**Cost.** One bool in `struct Transponder`, one assignment in `txFillFromDoc`, a sort
or a dim-render in the picker. Small. Backward compatible (absent field -> assume
active). **Recommended for a near-term cycle.**

### 4.2 Transmitter "parameters" JSON -> richer mode/tone/baud handling (watch)

**What SatNOGS is adding.** An experimental free-form `json` field on each transmitter
that will (eventually, with a schema) structurally describe the transmission — akin to
gr-satellites `.yaml`. Intended to drive dynamic flowgraphs on the Network/Client/Radio
side.

**Where it could help CardSat.** CardSat already hand-maintains structured facts that
SatNOGS's flat fields don't carry:
- **CTCSS/PL tone** — `Transponder::toneHz` is currently built in **by NORAD id** in
  firmware because "SatNOGS carries no structured tone field" (comment in `satdb.h`).
  If the parameters schema standardizes an access tone, CardSat could read it directly
  and retire per-satellite hard-coding.
- **Baud / submode** — for the telemetry-mode work and the new Char-lookup/telemetry
  education content, a structured baud/framing descriptor would let CardSat display
  "9k6 GMSK" vs "1k2 AFSK" from data instead of inference.
- **Inversion / passband nuances** already come through; a schema may add net offset or
  channel-plan detail useful for passband tracking.

**Recommendation.** **Watch, do not build yet.** The field is explicitly experimental
and "can get any valid json structure" with the schema still in progress. Building
against an unschema'd free-form field would be premature and fragile. Track the schema;
when it stabilizes, the tone-from-data win alone likely justifies adoption. Add a
placeholder in the parse filter only once the schema lands.

### 4.3 Reception Status + Evidence URLs -> launch-window "has it been heard?" (watch)

**What SatNOGS is adding.** New **satellite**-entry fields: `Reception Status`
(`Unknown` / `Unverified` / `Partially Verified` / `Verified`) and a list of
`Reception Evidence URLs`, plus a Launches organizing feature.

**Where it could help CardSat.** This is genuinely aligned with two things CardSat
already does:
- The **Phys page already lists launch siblings by name** (COSPAR-derived). A launch
  view that knows which freshly-deployed objects have been *received* would let CardSat
  answer the post-deployment question every operator asks during a launch window:
  "which of these new objects has anyone actually heard, and on what?"
- CardSat's **AMSAT reporting** feature is about first-hearing new birds. A
  reception-status hint ("Unverified — be the one to verify it") is a natural nudge
  toward exactly the reporting CardSat encourages.

**The catch.** These are **satellite**-endpoint fields — the very endpoint CardSat
avoids (and whose `status` just broke). Adopting them means taking on `/api/satellites/`
with its breaking-change exposure. **Recommendation: watch, and revisit only if/when
a launch-window feature is prioritized.** If adopted, isolate the satellites-endpoint
parsing behind a tolerant reader (unknown status values degrade to "Unknown", never
crash) so a future value change is a no-op — the lesson of this very change.

---

## 5. Summary table

| SatNOGS change | Type | CardSat impact | Action |
|---|---|---|---|
| Satellite `status` values change | **Breaking** | **None** (endpoint not used) | Verify post-7/9; add grep gate |
| Activity moves to transmitters | Model shift | Positive (already reads tx level) | Adopt 4.1 (mark inactive tx) |
| Transmitter `parameters` JSON | Experimental | Future upside (tone/baud from data) | Watch schema; retire tone hard-coding later |
| Reception Status + Evidence | In progress | Future upside (launch-window "heard?") | Watch; revisit with a launch feature |
| Launches feature | In progress | Future upside | Watch |

## 6. Recommended immediate actions (this cycle)
1. **Verify** the transmitters parser against a live post-7/9 payload (no change expected).
2. **Add a release-checklist gate**: zero `/api/satellites` references, no satellite-level
   `alive`/`dead` handling.
3. **Optionally adopt 4.1** now (mark/sort inactive transmitters) — small, backward
   compatible, real UX win, and it puts the already-filtered `status`/`alive` fields to use.
4. **Defer** 4.2 and 4.3 pending schema/feature maturity; record them here so they are not
   forgotten.
