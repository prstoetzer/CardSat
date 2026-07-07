# CardSat 0.9.51 — on-device test checklist

Everything below was **host-verified only** (balance / parity / dispatch / mirror-identity,
plus host-compiled numerical checks against python-sgp4 and astropy, and a host round-trip of
the LoRa object protocol). **The device is the authority**; this list is what to exercise on
real hardware before tagging 0.9.51. Ordered roughly by risk — the things most likely to bite
are first: the untested LoRa radio path, then the new numerical code, then the new
screens/endpoints, then regressions in the shared paths the new code touched.

Legend: **[HW]** needs hardware behavior that can't be host-checked; **[2×]** needs two units;
**[REG]** regression check (existing behavior the new code could have disturbed).

---

## 0. Build & boot (gate — do first)
- [ ] **Compiles clean** in the Arduino IDE / your build for the ESP32-S3 (M5Stack Cardputer
      ADV). This is the single most important check — it's a large unflashed change adding
      numerical code (fitter, J2000 transform), a LoRa sub-protocol, and new screens.
- [ ] **Boots** to the home screen; no crash / reboot loop.
- [ ] **About** shows `0.9.51`.
- [ ] Free-heap / max-block look sane after a few minutes and after a GP update — watch for
      fragmentation from the new buffers (LoRa reassembly 384 B, rove viewer bounded 4 KB).

---

## 1. LoRa GP-element sharing (Feature 3) — **UNTESTED radio path; highest risk** [HW][2×]
This has never gone over the air. Everything here is new behavior.
- [ ] Enable LoRa on **both** units (Settings → Network/data), **same freq / SF / BW / region**.
- [ ] **Send:** Satellites → select a bird → **`L`**. Status shows "Sending … (N frames)…" then
      "GP element set sent". No UI stall while it sends (it should stay responsive — frames go
      one per ~250 ms).
- [ ] **Receive:** the other unit pops the **"Import satellite?"** screen showing sender, name,
      NORAD, incl/ecc/MM, and "Adds a new satellite" **or** "Updates an existing satellite".
- [ ] Press **`y`** → status "Imported <name>"; the sat appears/updates in the Satellites list;
      open its **passes** and confirm they look sane (matches the sender's).
- [ ] Press **`n`** / `` ` `` on a second send → declines cleanly, nothing imported.
- [ ] **Share a fitted set:** run the GP fitter (see §2), on the result screen press **`L`** →
      the other unit offers to import "FIT-SAT" (or your saved name).
- [ ] **Corruption/robustness:** if you can, move the units far apart so a frame drops — the
      receiver should NOT import; it stays waiting or reports "CRC fail (resend)". Re-send from
      the sender completes it. (No ARQ — a dropped chunk = manual resend, by design.)
- [ ] **One-at-a-time:** starting a second `L` while one is sending is refused ("A transfer is
      already running").
- [ ] Measure rough **airtime** per transfer at your SF/BW (a 3-frame GP set); note it — high
      SF will be slow and eat duty cycle.
- [ ] **[REG]** Ordinary LoRa **text messages still work** (send `@position`, `#SAT`, `!sked`
      from the Messages screen; the roster still populates). The 0xC6 object hook sits *before*
      the text path in `loraPoll`, so confirm it didn't shadow normal text (0xC5) frames.
- [ ] **Screenshot** the "Import satellite?" confirm screen.

---

## 2. State vector → GP fitter, incl. J2000 (Tools) — new numerical code [HW]
Host-validated against python-sgp4 (fit) and astropy (J2000 transform, ~1–2 m), but confirm
on-device (float paths, no-PSRAM heap during the solve).
- [ ] Tools → **State vector -> GP** (tool #11). Form shows epoch, rx/ry/rz, vx/vy/vz, a
      **frame** field, and **SOLVE**.
- [ ] Enter a **TEME** state for a known object and SOLVE → converges; results show the six
      mean elements + apo/peri + **residual** (should be small, well under 1 km) + **B\*=0**.
- [ ] Toggle the **frame to J2000** (field 7, `,`/`/`/ENTER), enter the same object's J2000/GCRF
      state, SOLVE → elements match the TEME case to a few significant figures (the on-device
      transform should agree with the host's ~1–2 m).
- [ ] **`s`** save → prompts for a name → appears in Satellites as a manual sat; its passes look
      right.
- [ ] **Heap:** watch free-heap/max-block before vs. after several solves — no leak, no crash on
      a no-PSRAM unit.
- [ ] Bad input (all zeros, or a nonsense state) fails gracefully (no convergence / no crash).
- [ ] **Screenshot** the form (with the frame row) and the results.

---

## 3. Future-epoch (pre-launch) handling — audit fix [HW][REG]
- [ ] Fit or hand-enter a satellite whose **epoch is in the future** (days ahead). On the
      Track / Passes / Schedule screens its age reads **`pre-lnch`** in cyan (not a "GPn.nd"
      age, not blank).
- [ ] Its **passes still compute** (nominal pre-launch orbit) — no hang, no garbage.
- [ ] **[REG] The key fix:** with a future-epoch sat in the catalog *and* at least one genuinely
      **stale** (>7-day-old) real sat, confirm the **GP auto-refresh still fires** when online.
      (Before the fix, one future-epoch sat suppressed fleet-wide refresh.)
- [ ] **[REG]** Normal (past-epoch) sats still show the usual **`GPn.nd`** age in the right
      colour (green/yellow/red) on Track/Big/Passes/Schedule.

---

## 4. Rove planner + workable grids + text export — from earlier in the cycle [HW]
- [ ] Next Passes → **`p`** opens the **Rove planner**. Form: grid / date / time / ± hrs / GO.
      Defaults to your current grid + clock.
- [ ] **GO** surveys all favorites; the list stays responsive while computing (jobbed). Rows show
      name / AOS / max-el / **St** / **Dx** counts, sorted by AOS.
- [ ] **ENTER** a row → detail with the polar arc from the entered site and **States / DXCC /
      Grids** counts. `s` / `d` / `g` open the full workable **state**, **DXCC**, and **grid**
      lists for that pass.
- [ ] **[REG]** After viewing pass grids, open a **live** Track → grids view and confirm it shows
      *that* sat's grids (the planner's grid count invalidates the live cache; it must rebuild).
- [ ] **`w`** on the results → "Saved /CardSat/RovePlans/rove_…txt". Pull the file (via §6) and
      confirm the format: header + per-pass blocks with the **states list**, **DXCC list**, and
      **grids count only** (no grid list).
- [ ] Works on both an **SD** unit and an **internal-flash (LittleFS)** unit (export guards on
      any filesystem, not SD-only).

---

## 5. On-device rove-plan browser + viewer (Feature 2) [HW]
- [ ] Save a plan (§4), then press **`l`** on the planner → **Saved rove plans** list, newest
      first, with date-stamp + size.
- [ ] **ENTER** → read-only viewer; **`;`/`.`** scroll, **`{`/`}`** page. Content matches the
      file.
- [ ] A **large** plan (many favorites / big window) shows the **"(truncated — download for full
      file)"** note rather than crashing or exhausting heap.
- [ ] **`d`** → two-step delete confirm; deleting removes it from the list. **`r`** rescans.
- [ ] Empty state (no saved plans) shows the hint, doesn't crash.
- [ ] **Screenshot** the list and the viewer.

---

## 6. Web file download (Feature 1, download-only) [HW]
- [ ] Enable **Web control**; open the page; the header has a **Files** link → `/files`.
- [ ] The browser lists the **/CardSat** tree with sizes + modified times; folders navigate,
      the **up-a-level** link works.
- [ ] **Download** a file (a rove plan, a screenshot) — it saves correctly and the contents are
      intact (compare bytes/size).
- [ ] **Path safety:** try to escape the tree — e.g. request `/api/file?path=/CardSat/../secret`
      or `/api/files?dir=/` — it must be **refused** (404 / empty), never serve outside
      `/CardSat`.
- [ ] **No upload:** confirm there's no upload control and a `POST` to the file endpoints does
      nothing destructive (the server still only reads request line + headers).
- [ ] Works on **SD** and **LittleFS** units.
- [ ] **[REG]** The rest of web control still works: status/passes/orbit poll, transponder
      select, cal nudges, Track/Manual command buttons, the sky plot — the new routes didn't
      break the existing API. rigctld/rotctld still coexist.
- [ ] Big directory (many screenshots) lists without a memory spike (listing is streamed).

---

## 7. Documentation / packaging (spot-check)
- [ ] MANUAL PDF opens; the new sections read correctly: rove planner + **saved plans**, the GP
      tool with **J2000** + **pre-launch**, **web Files**, and **LoRa `L` element sharing**.
- [ ] All manual/README image references resolve (host check passed: 75 + 15, 0 missing).
- [ ] Screenshots are still text-only for the new 0.9.51 screens (rove planner/detail, GP form/
      result, rove list/viewer, GP-import confirm, /files page) — capture them this round if you
      want them in the manual.

---

## Known limitations to keep in mind while testing
- **LoRa object transfer is UNTESTED on hardware** and has **no ARQ** — a dropped chunk fails the
  transfer; the sender re-broadcasts. GP elements only (notes / rove-plan transfer not built).
- **Web file transfer is download-only** — no upload path exists yet (needs a request-body reader,
  a separate future item). The `/files` page is **unauthenticated on the LAN**, same as the rest
  of web control — use trusted networks only.
- **Pre-launch passes** are the *nominal* pre-deployment orbit; re-acquire real elements once the
  object is cataloged.
- **Fitter B\* = 0** (a single state carries no drag info) — predictions drift over days.

---

## Regression sweep (quick pass — the shared paths the new code touched)
- [ ] **[REG]** LoRa text messaging + roster (the object hook in `loraPoll`).
- [ ] **[REG]** GP auto-refresh fires for genuinely stale sats (the future-epoch min-age fix).
- [ ] **[REG]** Track/Big/Passes/Schedule element-age display for normal sats.
- [ ] **[REG]** Web control status/passes/orbit/tx/cal/select/fav/cmd (the new routes + header
      link).
- [ ] **[REG]** Notes browser + editor (the rove viewer reuses the note text-wrapper).
- [ ] **[REG]** Rove planner survey + detail + the `w` export (grids-count cache invalidation).
- [ ] **[REG]** Normal past-epoch pass prediction unchanged.
