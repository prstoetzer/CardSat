# CardSat 0.9.50 — on-device test checklist

Everything below was **host-verified only** (balance / parity / dispatch / host-compiled
math). The device is the authority; this list is what to exercise on real hardware before
tagging 0.9.50. Ordered roughly by risk — the things most likely to bite are first.

## 0. Build & boot (gate — do first)
- [ ] **Compiles clean** in Arduino IDE on the ESP32-S3 (the two compile errors Paul hit —
      `oxSeedFromSat` scope + `drawOrbitZoo` TWO_PI macro collision — are fixed; confirm no
      new ones). This is the single most important check: it's a large unflashed change.
- [ ] **Boots** to the home screen; no crash/reboot loop.
- [ ] **About screen** shows `0.9.50` and the new "max blk" (largest heap block) figure.
- [ ] Free-heap / max-block look sane after a few minutes and after a GP update (watch for
      fragmentation regressions from all the new screens/tables).

## 1. Transponder list ordering + inactive marking  (§2 — highest-value behavior change)
- [ ] Open a bird with many transmitters (**ISS**) → **Satellites → `t`**. Confirm:
  - [ ] **Two-way transponders appear first**, one-way beacons/telemetry below.
  - [ ] **Non-amateur transmitters (Soyuz VHF, S-band TT&C) sort to the very bottom.**
  - [ ] **Inactive transmitters are dimmed grey and tagged `(off)`.**
- [ ] Open **AO-7** → confirm both linear transponders rank above beacons.
- [ ] Manual transponders (if any) are not dimmed and still appear.
- [ ] **Screenshot** the transponder list showing `(off)` entries.

## 2. AMSAT catalog / reporting  (§1 — the original bug)
- [ ] Boot serial log reads `[amsat] catalog map: N entries` with **N ≈ 80** (not 0).
- [ ] On **AO-7**, the AMSAT report picker offers **both modes** — `AO-7 [U/v]` and
      `AO-7 [V/a]` — with readable names (this was the headline bug: only one before).
- [ ] One-key "heard it" (`i`×2 on Track) with a transponder selected auto-picks the
      **matching** mode (U/V transponder → `[U/v]`, V/A → `[V/a]`).
- [ ] **A real AMSAT status POST** goes through (first live post since the catalog fix) —
      verify it appears under your call/grid on amsat.org. *(This is a live network write;
      do it deliberately.)*

## 3. Link budget live slant-range  (§3)
- [ ] Track a bird that's **above the horizon**, open **Tools → Link budget**. Distance
      field should be **pre-filled** and marked `(live)`; frequency = selected downlink.
- [ ] Press **`p`** during a pass → distance re-syncs to the new slant range.
- [ ] Hand-edit the Distance field → the `(live)` tag drops.
- [ ] With **no** sat tracked, it opens on sane defaults (no `(live)`), no crash.
- [ ] **Screenshot** with the `(live)` distance showing.

## 4. Orbit explorer  (§8 — new, and the source of a compile bug — exercise it)
- [ ] **Satellites → select a bird → Orbit → `,`/`/` to page 11 "Explore".** Confirm it
      **seeds** apogee/perigee/inclination from that satellite.
- [ ] Edit a value (`;`/`.` pick a row, type, ENTER) → derived outputs (period, revs/day,
      velocity, footprint, max pass, nodal drift, sun-sync) recompute live and look right.
- [ ] Sanity: set it GEO-like (apogee≈perigee≈35786) → period ~1436 min, ~1 rev/day.
- [ ] `x` reseeds from the sat. Leaving Orbit and re-entering with a **different** sat
      reseeds to the new one.
- [ ] Confirm it **never changes the real satellite** (other Orbit pages unchanged).
- [ ] **Screenshot** the Explore page.

## 5. Orbit animations  (§8 — new animated screen; the heap/fps risk lives here)
- [ ] **About → Help (`h`? or the help hub) → `o`** (orbit animations). Confirm it opens.
- [ ] Animation is **smooth** (~15 fps target) — satellite dot moves, faster at perigee.
- [ ] `,`/`/` cycles the six types (LEO, polar/SSO, MEO, GEO, Molniya/HEO, GreenCube);
      each redraws with the right shape + caption.
- [ ] **Leave and re-enter several times, then check About heap** — the fixed trail buffer
      should mean **no heap growth**; watch the max-block figure especially. *(This is the
      one screen where the "no per-frame allocation" claim needs real-hardware confirmation.)*
- [ ] Back key returns to the help hub cleanly.
- [ ] **Screenshot** a couple of orbit types.

## 6. Scientific calculator extensions  (§8 + §9)
- [ ] Ham functions: `wl(146)` ≈ 2.053, `dbm(100)` = 50, `db(2)` ≈ 3.01, `w(30)` = 1,
      `undb(3)` ≈ 2, `fq(2)` ≈ 149.9.
- [ ] Constants: `c`, `kB`, `Re`, `mu`, `g0` return sane values; `cos(60)`=0.5 still works
      (confirm `c` didn't shadow `cos`).
- [ ] `'` toggles the ham/general **hint page**.
- [ ] **Metric-prefix input**: `100k`→100000, `2.2n`→2.2e-9, `146M`→1.46e8, `5m`→0.005.
      Confirm **case matters**: `M` vs `m`. `1e3` still = 1000 (no double-scaling).
- [ ] **`\` toggles engineering notation**: `4700` shows `4.7 k`, `0.5` shows `500 m`,
      the `ENG` indicator appears.
- [ ] **Screenshot** the calculator (ENG mode + a ham function).

## 7. New Tools reference screens  (§6, §9)
- [ ] **Operating references** (Tools) — Q-codes / phonetics / RST, `,`/`/` switch tabs.
- [ ] **CTCSS tone reference** (Tools) — 38-tone grid scrolls, sat tones noted.
- [ ] **Radio math reference** (Tools) — cheat sheet scrolls (dB table, AC factors,
      constants, formulas).
- [ ] **Screenshots** of each.

## 8. New Tools form calculators  (§6, §9) — spot-check the math on-device
- [ ] **Phasing line / stub**: 1/4-wave, LMR-400, 146 MHz ≈ **0.436 m** / 1 ft 5 in.
- [ ] **Wavelength / frequency**: 146 MHz → 2.053 m; 1/4 = 0.513 m.
- [ ] **Attenuator pad**: 6 dB @ 50 Ω → pi 150.5 / 37.4 Ω, T 16.6 / 66.9 Ω.
- [ ] **dB chain sum**: +20 −3 −1.5 → 15.5 dB.
- [ ] **Complex / polar**: 50 + j25 → mag 55.9, angle 26.57°.
- [ ] **Reactance & resonance**: 7 MHz, 10 µH, 100 pF → Xl 439.8, Xc 227.4, f0 5.033 MHz.
- [ ] **RC/RL time constant**: 1 kΩ, 1 µF → τ = 1 ms, cutoff 159.2 Hz.
- [ ] **Screenshots** of the new form tools (esp. phasing line).

## 9. Tools menu refinements  (§6)
- [ ] **First-letter jump**: press a letter → highlight jumps to next matching tool.
- [ ] **Last-tool memory**: open a tool, back out, re-open Tools → it remembers position.
- [ ] **Per-tool persistence**: set values in a form tool (e.g. coax length), leave, come
      back → values remembered. `x` resets to defaults. *(Writes `/CardSat/tooldef.txt`.)*
- [ ] **Antenna-length units** (Settings → Display) toggles ft/in ↔ metric in the antenna
      tools (dipole/vertical/yagi/quad/phasing). **Confirm orbital/debris/cross-section
      tools stay metric regardless** — this was an explicit requirement.

## 10. RF-exposure duty presets + Tier-B polish  (§4, §6)
- [ ] RF-exposure tool: **Mode duty** pick-list (FM 100% / SSB 20% / CW 40% / FT8 50%);
      the MPE distances change with the selection; "not a station eval" note shows.
- [ ] Debris tool: field labeled **Alt(perigee)** + "elliptical? use perigee alt" note.
- [ ] Char lookup: Baudot row annotates CCITT-vs-US-TTY differences (codes 5/9/13/17/20/26);
      **shift+T** opens the full ASCII/Morse/Baudot table (`;`/`.` scroll).

## 11. On-screen docs  (§7)
- [ ] Help hub lists **`o` orbit animations**.
- [ ] Help has a **TOOLS** section (entry point, jump, persistence, link-budget `p`, etc.).
- [ ] Learn has **COAX & VELOCITY FACTOR**; tech-help POLARIZATION explains CP.
- [ ] History has a **THE 2020s** section. **VET THE FACTS** (you're the authority):
      SO-50 launched 2002; IO-117/GreenCube ~5800 km MEO "first amateur MEO digipeater";
      "reach not seen since Phase 3"; AO-91 status framing.

## 12. Regression sanity (didn't break the core)
- [ ] Normal **Doppler tracking** on a live pass still works (CAT + tuning).
- [ ] **Rotator** control unaffected (if you bench it).
- [ ] Settings load/save round-trips (the new `antUnits` row shouldn't corrupt others —
      the settings array was resized 85→86; confirm all Display-category rows still work).
- [ ] LoTW / Cloudlog upload unaffected.

---

## Items awaiting your judgement (not pass/fail — decisions)
- [ ] **Coax velocity-factor values** in the phasing tool — I used standard published VFs,
      but a wrong VF quietly cuts a wrong phasing line. Sanity-check them.
- [ ] **Q-code selection** in Operating references — want an AMSAT-specific flavor?
- [ ] **History "THE 2020s" facts** — vet as above.

## Screenshots to capture (stale until you do)
Transponder list `(off)` · link budget `(live)` · Orbit Explore page · 2 orbit-animation
frames · calculator (ENG + ham fn) · operating refs · CTCSS ref · radio-math ref · phasing
line · the other new form tools.
