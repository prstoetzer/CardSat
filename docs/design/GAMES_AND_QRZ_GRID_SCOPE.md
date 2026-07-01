# CardSat 0.9.42 — feature scope: game controls, new mini-games, QRZ grid backfill

Scoping for the three 0.9.42 requests, grounded in the actual 0.9.41 source. The through-line
constraint from the requester: **games must have zero heap impact** (fixed `.bss` state,
primitives only, no `String`/`malloc` in the loop), and the QRZ grid backfill must be
**minimal heap** and reuse the existing QRZ XML path. Nothing here is built yet; this is the
plan to agree before implementation.

Hardware note (confirmed in source): the **Cardputer ADV has a BMI270 IMU** (`M5.Imu.begin()`
at boot, `imuReady` flag, `M5.Imu.getAccel()` already used by the tilt-tune feature). The
**original Cardputer has no IMU**. Every tilt/IMU feature below MUST gate on `imuReady` and
fall back to keyboard control so it still works on non-ADV hardware. Sound uses the existing
`M5Cardputer.Speaker.tone()` wrapper (`beep()`), which is already present.

---

## Feature 1 — Zap the Sats: ergonomic controls + tilt option + sound

### Current state (source)
- Game lives at `SCR_GAME`, entered by `z` on the About screen. All state is fixed-size in
  `.bss` (`gGunX`, `gShotX/Y[4]`, `gInvAlive[18]`, etc.) — no heap, exactly the model to keep.
- Controls today (`keyGame`): `isLeft` / `isRight` move the gun, **`SPACE` fires**. On the
  Cardputer keyboard, the arrow-cluster keys (`, . /`) and space are all in the bottom-right
  corner, close together — the requester's ergonomics complaint is accurate.

### Design

**1a. Split the physical controls to opposite sides of the keyboard.** Looking at the ADV
keyboard layout, the ergonomic pairing for two-handed play is a **left-hand cluster** and a
**right-hand cluster**. Proposal — accept multiple keys per action so the existing arrow keys
still work, but add comfortable alternatives:
- **Move left:** `A` (left hand) — plus existing `,`
- **Move right:** `D`? No — `D` is mid-board. Better: **`G`** and existing `/`. Actually the
  cleanest left/right split on this keyboard is **`A` = left, `L` = right** (mirror positions,
  one under each hand), with **fire = `SPACE`** (thumb) or a top-row key.
- **Recommendation:** left = `A`, right = `L`, fire = `SPACE` **and** `ENTER` (either thumb).
  Keep `, . /` as legacy aliases so nothing breaks. Final key choice is a 1-line map and easy
  to tweak on the bench; the point is left-hand vs right-hand separation.

  (Open question for the requester: confirm the exact keys. `A`/`L`/`SPACE` is my
  recommendation, but `Q`/`P` or `ctrl`/`ok` are alternatives if those feel better in hand.)

**1b. Optional tilt steering.** Reuse the *exact* accel pattern from `serviceTiltTune()`: read
`M5.Imu.getAccel(&ax,&ay,&az)`, 1-pole LPF on `ax`, dead-zone around level, roll = left/right.
For a game (not a fine-tune) the mapping is simpler: tilt past the dead-zone moves the gun at a
fixed px/frame in that direction (no rate integration needed). Gated on `imuReady` **and** a new
`cfg.gameTilt` toggle; when off or on non-ADV hardware, keyboard control is unaffected. Fire
stays on the keyboard (`SPACE`) since tilt only covers the left/right axis. **Heap cost: zero**
— it's a couple of stack floats per frame, same as the tune feature.

**1c. Toggleable sound effects.** Feasible with zero infrastructure: `beep(freq,ms)` already
wraps `M5Cardputer.Speaker.tone()`. Add short non-blocking tones for: fire (short high blip),
hit (mid pop), life lost (descending), wave cleared (rising), game over (low). Gated on a new
`cfg.gameSound` toggle. **Important:** `tone()` is fire-and-forget (the speaker task plays it),
so it doesn't block the ~25 fps loop — but keep effects to a few ms and one at a time so they
don't stack awkwardly. **Heap cost: zero.**

**New settings:** `cfg.gameTilt` (bool) and `cfg.gameSound` (bool), two rows in Settings. These
are the only persistent additions. Both default OFF (tilt because not everyone wants it / non-ADV;
sound because a beeping device in the field may be unwelcome — the requester can pick defaults).

---

## Feature 2 — 2–4 new satellite-themed mini-games behind a "Games" menu

### Menu structure
Today `z` on About jumps straight into Zap the Sats. Proposal: the **"Games" button on About
opens a small games menu** (a new `SCR_GAMES` list screen — same fixed pattern as other list
screens, no heap), listing the available games; selecting one enters it. Zap the Sats becomes the
first entry. This is a small, self-contained list screen with a static array of game names.

### Candidate games (all fixed-`.bss`, primitives-only, flash-only cost)

Ordered by fit + feasibility. Recommend implementing **2–3** of these; all are sized to the
240×135 canvas and reuse the existing draw/animate/`beep` machinery.

**G1. "Doppler Lock" (IMU or keys) — STRONGLY RECOMMENDED, most on-theme.**
Simulates tuning a linear transponder against Doppler drift. A target frequency marker drifts
(mimicking the real ±Doppler curve of a pass); the player holds their signal on it by tilting
(reusing the tilt-tune feel exactly — this is almost a game-ified version of the real feature) or
with left/right keys. Score = time kept "in the passband." Teaches the actual skill of satellite
tuning. IMU optional, keyboard fallback. Fixed state: a few floats. **Zero heap.**

**G2. "Pass Predictor" / "Catch the Pass" — RECOMMENDED, on-theme.**
A satellite arc sweeps across a sky dome (az/el grid like the existing sky views); the player must
"key up" (`SPACE`) at the moment the sat crosses above a horizon/elevation-mask line to log a QSO.
Mistimed = missed. Scales in difficulty (faster arcs, narrower windows). Reuses the polar-plot
drawing style already in the orbital views. Fixed state. **Zero heap.**

**G3. "Rotor Runner" / "Track the Bird" (IMU) — good IMU fit.**
A satellite moves around the screen; the player slews a rotor crosshair to keep it centered, by
tilting the device (pitch + roll → az/el), like aiming a real antenna. Score = cumulative time
on-target. Uses both IMU axes, so it's a genuine "IMU game." Keyboard fallback (arrow keys nudge).
Fixed state. **Zero heap.**

**G4. "Morse Meteors" / "CW Zap" — on-theme (CW is core to sat ops), moderate effort.**
Letters fall (like a typing game); the player clears each by keying its Morse (short/long via two
keys, or tapping a single key with timing). Ties into amateur CW operating. Slightly more logic
(a small static Morse table — already exists elsewhere if there's a CW feature; otherwise a
~40-entry `const` table in flash). Fixed state. **Zero heap** (the Morse table is `const`, in
flash).

**G5. "Grid Chase" — on-theme (grid squares), simple.**
A Maidenhead grid is shown; the player picks the correct grid from 3–4 options (or types it),
racing a timer — a geography/grid-square trainer. Very light. Could reuse the grid-distance math
already in the code. Fixed state. **Zero heap.**

**Recommendation:** **Doppler Lock (G1)** + **Catch the Pass (G2)**, optionally **Rotor Runner
(G3)** if an IMU-heavy game is wanted. G1 and G3 justify the "games can use the IMU" note; G1 and
G2 are the most distinctively *amateur-satellite* rather than generic arcade. Final pick is the
requester's call — this doc lists options as asked.

**Cost:** each game is a `drawX()` + `keyX()` + a handful of `.bss` state vars + a `SCR_X` enum,
mirrored into `CardSat.ino`. Flash only; the resident heap is untouched. The main loop's fast-redraw
branch (already there for Zap) generalizes to "if the active screen is a game, redraw at its cadence."

---

## Feature 3 — QRZ grid backfill for grid-less QSOs (Log screen)

### Why this is feasible cheaply
The expensive part already exists and is already heap-bounded: **`qrzLookup()` already parses the
grid** — `qrzGrid = qrzTag(body, "grid")` — from an 8–16 KB capped `httpsGet`. So backfill is not
new network machinery; it's a loop that, for each QSO missing a grid, calls the existing lookup and
writes the returned grid back into the log. `PendingQso` already has a fixed `char grid[10]` field
and CSV column for it.

### Design (heap-conscious)

**Trigger:** a new action on the Log screen (e.g. a key, "Fill grids via QRZ") — only runs on
explicit user request, never automatically. Requires QRZ credentials set (same gate as the QRZ
screen); if unset, show a message and stop.

**Mechanism (mirrors `markLogUploaded`'s safe rewrite):**
1. Open the log, stream it line by line to a `.tmp` file on `Store::fs()` (SD or LittleFS — no SD
   requirement, same as everything else).
2. For each QSO row: if it already has a grid, copy it through unchanged. If it's missing a grid
   **and** has a callsign, call `qrzLookup(call)`; on success with a non-empty `qrzGrid`, fill the
   grid field before writing the row. On failure or empty, write it through unchanged.
3. Rename `.tmp` over the log (atomic-ish, same pattern already used).

**Heap discipline (the critical part):**
- **One QSO in flight at a time.** Never load the whole log into RAM — it streams line-by-line,
  exactly like `markLogUploaded`. Peak added RAM is one `PendingQso` (fixed) + the QRZ response
  (already capped at 8–16 KB in a single reused `String`).
- **The QRZ lookup is the existing capped path** — no large body ever in RAM.
- **Rate/scope guard:** each lookup is a separate TLS handshake (the QRZ path is per-callsign), so
  this has the same handshake-knife-edge exposure as any QRZ query, ×N callsigns. To bound it:
  - Process a **capped number per run** (e.g. up to 20 grid-less QSOs), with a progress display
    ("Filling grids 3/12..."), so a huge log doesn't mean dozens of back-to-back handshakes in
    one blocking operation. The user re-runs to continue — same "batch by re-running" model.
  - Skip duplicate callsigns within a run (cache the last-resolved call→grid in a tiny fixed
    buffer, or just accept re-lookups — QRZ dedup is a nice-to-have, not required).
  - **De-dupe QRZ sessions:** the session key is already cached (`qrzSessionKey`), so N lookups
    reuse one login — only the per-call query handshakes repeat.

**Honest cost note:** unlike the games, this is **not zero-cost** — it's N sequential HTTPS
lookups, each with the same ~32 KB-contiguous handshake exposure documented in
`docs/design/STREAMING_TLS.md`. It won't exhaust the heap (the response is capped and streamed
per-line), but on a marginal link a cold handshake can still occasionally fail — the operation
should tolerate a single lookup failing (skip that QSO, continue) rather than aborting the whole
run. The per-run cap keeps it bounded and interruptible. This matches the requester's "if feasible
and not too costly on the heap" — it's feasible and heap-safe, with the caveat that it's inherently
a multi-request network operation.

**Alternative considered (and why not):** batching all grid lookups into one request is impossible
— QRZ XML is one-callsign-per-query. So per-QSO lookups are unavoidable; the mitigation is the
per-run cap + session reuse + per-lookup failure tolerance, not a single bulk call.

---

## Summary of additions

| Item | Persistent state | Heap impact | Flash |
|---|---|---|---|
| Zap controls (key remap) | none | zero | trivial |
| Zap tilt option | `cfg.gameTilt` | zero (stack floats) | small |
| Zap sound | `cfg.gameSound` | zero (`tone()` exists) | small |
| Games menu (`SCR_GAMES`) | static name list | zero | small |
| New games (2–3) | fixed `.bss` per game | zero | moderate |
| QRZ grid backfill | none (reuses QRZ path) | bounded (1 QSO + capped response, streamed) | small |

**Zero resident-heap impact for everything except the QRZ backfill**, which is bounded and
interruptible by design and reuses the existing capped QRZ path.

## Open questions for the requester before implementation
1. **Zap keys:** confirm left/right/fire mapping. Recommendation: `A` = left, `L` = right,
   `SPACE` = fire (legacy `, . /` kept as aliases). Alternatives: `Q`/`P`, or `ctrl`/`ok`.
2. **Which new games:** recommendation is Doppler Lock + Catch the Pass (+ Rotor Runner for an
   IMU-heavy one). Pick 2–4.
3. **Defaults:** should `gameTilt` / `gameSound` default on or off? (Recommend off for both.)
4. **QRZ backfill per-run cap:** 20 QSOs per run reasonable, or a different number?

---

## Implementation status (0.9.42) — CODE COMPLETE, UNTESTED ON DEVICE

All three features are implemented in `src/` and mirrored byte-identically into `CardSat.ino`
(balance 0, parity clean, 29-function drift audit identical). **Not yet flashed or bench-tested.**
Built with the recommended defaults:

- **Zap the Sats controls:** `A` = left, `L` = right, `SPACE` = fire, with `, . /` kept as
  legacy aliases. Optional tilt steering (`cfg.gameTilt`, ADV-only via `imuReady`, keyboard
  fallback). Sound effects on fire/hit/wave/life-lost/game-over (`cfg.gameSound`). Both toggles
  default OFF, added to Settings > Station/display (edit codes 77/79).
- **Games menu (`SCR_GAMES`)** on About's `z`, listing all six games. New games implemented:
  **Doppler Lock**, **Catch the Pass**, **Rotor Runner** (two-axis IMU), **Morse Meteors**
  (flash-resident Morse table), **Grid Chase**. All fixed-`.bss`, primitives-only, zero heap.
- **QRZ grid backfill:** `fillGridsQrz()` on the Log menu ("Fill grids (QRZ)"). Streams the log
  line-by-line to a `.gfill` temp file and renames it over the log (markLogUploaded pattern);
  one `PendingQso` in RAM at a time; reuses the capped `qrzLookup` + cached session key; caps at
  20 lookups/run with a same-call cache; tolerant of individual lookup failures. Re-run to
  continue a large log.

### Bench-test checklist
- Zap: confirm A/L/SPACE feel right two-handed; toggle Game tilt (ADV) and confirm roll steers;
  toggle Game sound and confirm effects (and silence when off).
- Each new game: enter from the menu, confirm play, scoring, level-up, and back-out; confirm the
  IMU games fall back to arrow keys when tilt is off / on a non-ADV board.
- Grid backfill: run against a log with some grid-less QSOs (needs QRZ creds); confirm grids fill,
  the 20-cap message appears on a large log, and a failed lookup skips rather than aborts. Watch
  the serial console for the same handshake behavior as other QRZ lookups.
- Watch heap: `largest_free_block` should be unchanged by games (all `.bss`); only the QRZ
  backfill does network I/O.
