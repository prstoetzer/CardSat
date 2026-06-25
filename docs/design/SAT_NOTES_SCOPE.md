# Scope: Per-Satellite Operating Notes

**Status: design scope only — not implemented.** This scopes a small, persistent,
**editable note attached to each satellite** — for operating reminders (active modes,
schedule, quirks, your own observations) that travel with the bird and show up when
you tune it.

---

## 1. The need

Working amateur satellites carries a lot of per-bird lore: which transponder is
currently active, scheduled on/off times, "downlink is weak, use the high passes,"
"PL tone 67.0," "SO-50 needs the 74.4 arming tone," "beacon on the SUB only." Today an
operator keeps this on paper or in their head. A short note **stored per satellite** and
shown on the Track / info screens makes CardSat a self-contained operating reference and
field notebook.

This mirrors a storage pattern CardSat **already implements** for per-satellite
calibration, so the mechanism is proven.

---

## 2. What already exists (building blocks)

- **Per-satellite persistent storage by NORAD id** already exists for calibration:
  `loadCalForSat(norad)` / `saveCalForSat(norad)` persist `calDl/calUl` keyed on the
  satellite's NORAD number (app.cpp ~1234–1272), in LittleFS. A per-satellite **note**
  follows the identical model — key on `norad`, store/restore a short string.
- **Identity:** `SatEntry.norad` is the stable key (names change, NORAD doesn't).
- **Text entry:** the device already has a full on-screen text editor (`SCR_EDIT`,
  `editTarget` routing, the QSO-log and message composers) — note editing reuses it,
  no new input UI.
- **Display surfaces:** `SCR_TRACK`, `SCR_BIG`, `SCR_ORBIT`, the satellite list — all
  already render per-sat context where a note line/indicator fits.

---

## 3. Design

### 3.1 Storage
- A note is a short fixed-capacity string (e.g. **≤ 120 chars**) per NORAD id.
- Store in LittleFS, either as a small JSON map `{ "norad": "note" }` (one file,
  simplest) or one file per sat mirroring the cal approach. JSON map is preferred:
  fewer files, easy to enumerate, and the favorites set is small.
- **Capacity discipline (no-PSRAM):** cap note length and the number of stored notes
  (e.g. only favorites, or a hard max like 64 notes) so the map can't grow unbounded on
  the heap. Load lazily; keep only the active satellite's note in RAM, not the whole map
  during operation.

### 3.2 Editing
- A key on the Track / info screen (e.g. **`N`** for note — distinct from `n` which
  already jumps to beacon) opens the text editor pre-filled with the current note.
- Commit saves it under the active satellite's NORAD; empty string deletes the note.
- Reuses `SCR_EDIT` with a new `editTarget` value and an `editHome` route back to the
  originating screen.

### 3.3 Display
- **Track / Big:** if a note exists, show a compact indicator (a `*` or 📝 tag) and the
  note text on a line (scrolled/truncated to fit), or revealed on demand to avoid
  clutter on the small screen.
- **Satellite list:** a small glyph marking sats that have a note.
- **Info/orbit screen:** show the full note.

### 3.4 Optional niceties (later)
- **Seed notes from transponder/status data** CardSat already fetches (e.g. auto-note
  "FM transponder active" from SatNOGS/AMSAT status) as a starting point the operator can
  edit.
- **Export/import** notes with the rest of the config backup, so they survive a reflash.

---

## 4. How others handle it

- Desktop loggers and `gpredict`/SatPC32 keep per-satellite metadata in config files;
  operators commonly maintain a side document of bird-specific operating tips. There's no
  standard "note" field in TLE/GP data, so this is purely a local convenience — which is
  exactly why storing it on the device (the thing you take to the field) is valuable.
- The closest analog already in CardSat is **per-satellite calibration**, which proves
  the keyed-by-NORAD persistence pattern works well here.

---

## 5. Settings

Minimal — likely none needed. Possibly:
- **Notes** on/off (show/hide the note line on Track),
- **Max notes** cap (constant, not user-facing).

---

## 6. Cost / risk

- **Compute:** none meaningful — file read on satellite change (same cadence as
  `loadCalForSat`), file write on edit commit.
- **Heap:** one short string in RAM for the active satellite; the on-disk map is read
  lazily. The only risk is letting the map grow unbounded — mitigated by the length and
  count caps above.
- **Filesystem:** tiny; a JSON map of ≤64 short notes is a few KB. Mind LittleFS space
  shared with `gp.json` and cached elements — the cap keeps it negligible.
- **No new hardware/audio/toolchain dependencies.** Lowest-risk of the four scoped
  features.

---

## 7. Out of scope

- Rich text / multi-field structured metadata (keep it one free-text note).
- Cloud sync of notes (local only; export with config backup is the most it should do).
- Auto-generated notes beyond optionally seeding from data CardSat already fetches.

---

## 8. Verification

- Host: round-trip the note store (save under a NORAD, reload, edit, delete) and confirm
  persistence and the length/count caps hold.
- Confirm switching satellites loads the correct note and that an empty note removes the
  entry.
- On-device: add a note to a favorite, reboot, confirm it persists and shows on Track;
  confirm the satellite-list glyph appears.
