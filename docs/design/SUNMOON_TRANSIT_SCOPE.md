# Scope: Sun / Moon Transit Predictions

**Status: design scope only — not implemented.** This scopes predicting when a
selected satellite **crosses the disc of the Sun or the Moon** as seen from the
operator's location — the celebrated astrophotography event (ISS silhouetted on the
Sun/Moon) and a striking demonstration of orbital geometry.

---

## 1. The need

A satellite transit of the Sun or Moon is one of the most dramatic things a ground
observer can capture, but the viewing path is only a few kilometers wide and the event
lasts under a second, so you need a precise local prediction. CardSat already computes
satellite look angles and Sun/Moon positions for the same instant and location — so it
is unusually well-placed to answer "when, from here, does this bird cross the Sun or
Moon?" Nothing on-device does this today.

---

## 2. What already exists (building blocks)

- **Satellite look angle:** `Predictor::look(t)` / `azelAt(t, az, el)` gives the
  satellite's topocentric az/el at any time for the active observer.
- **Sun / Moon look angle:** `skyObjAzEl(t, lat, lon, isMoon, az, el)` (used by
  `drawSunMoon`) gives the Sun's and Moon's az/el for the same observer/instant.
- **Angular sizes:** Sun ≈ 0.53°, Moon ≈ 0.52° (varies ~0.49–0.57° with distance; the
  Moon term can use the existing lunar ephemeris distance if desired, else a fixed
  0.52° is adequate for a "transit possible" alert).
- **Observer location** already available (GPS or configured QTH).

So the core test — angular separation between the satellite and the Sun/Moon center vs.
the body's angular radius — is computable entirely from primitives already present.

---

## 3. The method

For a chosen satellite and a forward window (e.g. next 1–2 days):
1. Step time finely (the satellite moves fast across the sky; ~1 s coarse, then refine).
2. At each step compute the **angular separation** between the satellite (`azS,elS`) and
   the body (`azB,elB`) using the spherical law of cosines on az/el.
3. A **transit** occurs when separation < body angular radius (~0.26°) **and** the body
   is above the horizon. A **near-miss/conjunction** (within, say, 1°) is worth reporting
   too, since the few-km path width means the observer may be just off the centreline.
4. **Refine** around the minimum to get the transit time to sub-second and the minimum
   separation (central vs. grazing).

Because the ground path is narrow, the prediction is **location-specific**: report it
for the operator's exact QTH/GPS, and note that moving a few km changes the result.

---

## 4. How others handle it

- **Transit-Finder (ISS-Transit-Finder), CalSky (defunct), Heavens-Above:** same
  geometry — satellite ephemeris vs. solar/lunar ephemeris, angular-separation test,
  refined to sub-second, reported with the **center-line distance** because the path is
  ~few km wide. They search a date range and a small area around the observer.
- **Key realism:** these tools report transits for a *point* and emphasize how quickly
  the geometry changes with observer position. CardSat should do the same: predict for
  *here*, label the minimum separation, and be honest that it's a point prediction.

---

## 5. Proposed behavior

### 5.1 New "Transits" view (off Sun/Moon or the satellite menu)
For the selected satellite, list upcoming Sun and Moon transits/close conjunctions in
the window:
- `ISS · Sun transit · 14:22:07 local · sep 0.04° (central) · Sun el 31°`
- `ISS · Moon conj · tomorrow 03:11:54 · sep 0.7° (grazing) · Moon el 24°`

Each entry: body (Sun/Moon), date/time to the second (local + UTC), minimum separation
(and central/grazing label), body elevation, and the satellite's direction of travel.

### 5.2 Optional
- A **countdown + alarm** to the next transit (reuse the AOS-alarm mechanism).
- "All favorites" mode: scan every favorite for transits in the window — a "any bird
  crossing the Sun/Moon soon?" board.
- Sun-safety note in the UI: **never observe a solar transit without proper solar
  filtering.**

---

## 6. Settings

- **Transit search window** (1 / 2 / 7 days).
- **Conjunction threshold** — transit-only (≤ disc radius) vs. include near-misses
  (≤ 1°), since the narrow path means near-misses are often worth chasing.

---

## 7. Cost / risk

- **Compute:** a fine time-step scan over 1–2 days per satellite. Heavier than a normal
  pass scan because of the fine step, so it must run as an **incremental background job**
  with a progress bar, exactly like the sat-to-sat finder (which was reworked to be
  watchdog-safe for this reason). Coarse-step then refine to keep it cheap.
- **Heap:** a short results list; negligible.
- **Accuracy:** depends on element freshness and the Sun/Moon ephemeris precision
  (`skyObjAzEl` is low-precision but far finer than the disc size for timing; for the
  *centreline* the dominant error is element age, not ephemeris). Label minimum
  separation honestly; recommend fresh elements before relying on a central transit.
- **No new hardware/audio/toolchain dependencies.**

---

## 8. Out of scope

- Mapping the ground centreline / "drive X km north to center it" (needs a map + path
  solve; the existing world map could host this later).
- Planetary transits (Mercury/Venus across the Sun) — astronomical, not satellite.
- Sub-second camera-trigger output.

---

## 9. Verification

- Host: take a published ISS solar-transit prediction (Transit-Finder) for a known
  site/date, feed the same TLE/site, and confirm the transit time matches to within a
  second or two and the separation is sub-disc.
- Confirm a horizon-blocked body yields no transit, and a grazing case is labeled
  grazing.
- On-device: run the scan for the ISS and confirm it completes incrementally without
  tripping the watchdog.
