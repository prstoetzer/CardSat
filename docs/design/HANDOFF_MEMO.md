# CardSat — development & release handoff memo

Written at the start of the **0.9.61** cycle for a fresh session with no prior
context. Read this first. It captures the compilation process, the invariants
that must never be violated, the verification gates, the release procedure, the
file layout, and the hard architectural constraints — everything a new session
would otherwise have to rediscover by trial and error.

---

## 0. What CardSat is

Amateur-satellite tracker + CAT/rotator controller firmware for the **M5Stack
Cardputer ADV** (ESP32-S3FN8, **no PSRAM**, 8 MB flash, 240×135 screen). It does
SGP4 tracking, multi-radio CAT Doppler (CI-V/Kenwood/Yaesu/rigctl/LAN/USB), 7
rotator protocols on 3 wire types, transponder planning, QSO logging with
LoTW/Cloudlog upload, LoRa messaging (and a networked game), a Tiny BASIC, ~54
tools, EME/weather, and seven games. ~36 screens, 110 settings rows.

Author/tester: **Paul Stoetzer (N8HM)**. The division of labour is fixed: the
assistant writes all code; Paul compiles, flashes, and bench-tests on real
hardware, then uploads photos (`.HEIC`) and build artifacts (`.elf`/`.map`/`.bin`)
for audits. **The assistant never touches the remote git.**

Working copy of the repo: `/home/claude/cardsat/CardSat-main/`.

---

## 1. THE compilation process (read this carefully)

Compilation is the single most error-prone mechanical step. Follow it exactly.

### 1.1 The command

```bash
cd /home/claude/fullbuild
cp /home/claude/cardsat/CardSat-main/CardSat.ino CardSat/CardSat.ino     # STEP 1: sync the .ino
rm -f /home/claude/blN.log                                              # fresh log
setsid nohup arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,CDCOnBoot=cdc \
  --library /root/Arduino/libraries/M5Cardputer \
  --output-dir /home/claude/fullbuild/out \
  CardSat > /home/claude/blN.log 2>&1 &
```

- **Sketch directory is `/home/claude/fullbuild/CardSat/`, NOT the repo.** The repo's
  `CardSat.ino` must be copied there first (STEP 1). The build compiles the *copy*.
  Forgetting STEP 1 means compiling stale code — a silent, maddening failure.
- **`build_opt.h` lives in the sketch dir** (`/home/claude/fullbuild/CardSat/build_opt.h`)
  and carries exactly two lines: `-DESP_USB_HOST_MAX_DEVICES=4` and
  `-mtext-section-literals`. Do not delete it.
- **NEVER pass `build.extra_flags`** (via `--build-property` or otherwise). It breaks
  the `#define Serial HWCDCSerial` interposition the ConsoleLog tee relies on and
  produces a broken or non-linking image. This has bitten before. The two flags that
  *are* needed live in `build_opt.h`; nothing else goes on the command line.
- `setsid nohup … &` backgrounds the compile so the turn isn't blocked. Compiles take
  **7–10 minutes**.

### 1.2 Polling for completion

```bash
for i in 1 2 3 4 5 6 7 8; do sleep 55; pgrep -x arduino-cli >/dev/null || break; done
pgrep -x arduino-cli >/dev/null && echo "[STILL]" || echo "[EXITED]"
```

- **Use `pgrep -x arduino-cli`, NOT `pgrep -f`.** `-f` matches the whole command line,
  which includes this very poll command's own arguments → it always matches itself and
  reports "still running" forever. `-x` matches the process name exactly. This is a
  real, repeated foot-gun.
- Poll in ~55 s sleeps; a compile needs roughly 8–10 iterations.

### 1.3 Reading the result

```bash
sed 's/\x1b\[[0-9;]*m//g' /home/claude/blN.log | grep -iE "error:" | head -8
sed 's/\x1b\[[0-9;]*m//g' /home/claude/blN.log | grep -E "Sketch uses|Global"
```

- Strip ANSI colour codes (`sed 's/\x1b\[[0-9;]*m//g'`) or the log is unreadable.
- Success shows `Sketch uses N bytes (P%)` and `Global variables use N bytes (P%)`.
- As of 0.9.60 release: **~2,804,154 B flash (89%), 155,304 B RAM (47%)**. Flash is the
  tighter budget; watch the 89% headroom.

### 1.4 Toolchain facts

- `arduino-cli` **1.5.1** at `/usr/local/bin`.
- Core `esp32:esp32@3.2.1` (installed; a newer 3.3.10 may be *available* but the pinned
  build uses 3.2.1). **Do not bump the core** without bench-testing voice memo on the ADV.
  The pin is not arbitrary: arduino-esp32 3.2.1 bundles **ESP-IDF 5.4**, and the ADV's
  voice-memo capture (M5Unified `M5.Mic.record()` -> ES8311 codec over the *legacy*
  `i2s_driver_install` I2S path) breaks on **IDF 5.5** — the mic returns silent/constant
  samples because MCLK stops reaching the codec after the v5.4->v5.5 I2S driver refactor.
  arduino-esp32 3.3.x moved to IDF 5.5 (3.3.10 = IDF 5.5.4), so it reintroduces the bug.
  Root cause is upstream, tracked at **espressif/esp-idf#18621** (open, "In Progress",
  no fix merged as of Jul 2026). A fix can land in either place, and *neither has as of
  the latest builds checked*: (a) IDF fixes the legacy-driver MCLK routing in a future
  5.5.x that a later arduino-esp32 3.3.x then inherits, or (b) **M5Unified** migrates its
  `Mic_Class` off the legacy API to the new `i2s_channel_*` API (which already works on
  5.5). M5Unified was checked through **0.2.18** (Jul 2026, the latest) — no release from
  0.2.9 onward touches the ES8311 mic path (0.2.13 fixed the *StickS3* mic, a different
  board). Re-check both #18621 and the M5Unified changelog before ever raising the pin;
  the only real confirmation is flashing a candidate build and verifying the ADV mic
  captures non-constant samples.
- Library `/root/Arduino/libraries/M5Cardputer` (plus M5GFX, M5Unified).
- Xtensa binutils for ELF/DWARF audits:
  `/root/.arduino15/packages/esp32/tools/esp-x32/2411/bin` (add to `PATH`).
  `pyelftools` is installed (`--break-system-packages`) for DWARF member-size dumps.

---

## 2. The non-negotiable invariants

### 2.1 Dual representation (the big one)

Every source change must be applied **byte-identically** to BOTH:
- the modular sources `src/*.{h,cpp}`, AND
- the monolithic `CardSat.ino` (a flattened single-file copy of the whole program).

They are two representations of the same program. The `.ino` is what actually
compiles. If a change lands in `src/` but not `.ino` (or vice versa), the build
silently uses the stale one. **This has caused real, hard-to-find bugs.**

The mechanism used throughout: **Python assert-replace scripts** that edit both
files and assert the exact match count, e.g.

```python
def pair(old, new, files=("src/app.cpp","CardSat.ino"), n=1):
    for path in files:
        s = open(path).read(); c = s.count(old)
        assert c == n, f"{path}: {c} of {old[:52]!r}"
        open(path, "w").write(s.replace(old, new))
```

The assert is the safety net — if the anchor text isn't present exactly `n` times in
either file, the script aborts before writing anything, so the two files never drift.
When a type is defined in an inlined header, it must also be present in the `.ino`
(the parity gate checks this).

### 2.2 The verification gates

After EVERY source touch, run all of these (all in `tools/`), and they must all pass:

```bash
for g in check_balance check_parity check_screen_text check_settings_rows \
         check_defines check_ino_dupes; do
  printf "%-20s " "$g"; python3 tools/$g.py >/dev/null 2>&1 && echo PASS || echo FAIL
done
python3 tools/audit_screen_geometry.py   # must exit 0
```

- **check_balance** — brace/paren balance across the sources.
- **check_parity** — the dual-representation invariant: `src/` vs `.ino`, and that
  inlined-header type declarations are present in `.ino`.
- **check_screen_text** — on-screen string/footention constraints (footer ≤39 chars, etc.).
- **check_settings_rows** — every settings-menu id has a backing row; row bound sufficient.
- **check_defines** — `#define`/constant consistency.
- **check_ino_dupes** — guards against a duplicate-inlining bug (added after one bit us).
- **audit_screen_geometry** — screen-coordinate/overlap sanity.

If any gate fails, fix before compiling. The gates are fast; run them freely.

### 2.3 Other standing rules

- **`celestrak.org`**, never `.com`.
- **American spelling** in current docs (pre-0.9.58 release notes used British; leave them).
- **Tiny BASIC has no `INPUT`/`INKEY$`** — no interactive programs, by founding design.
  BASIC system data is read-only and snapshot-based.
- Arduino `String` **never frees its buffer on reassignment** — it's a mid-heap-hole
  source. Prefer fixed `char[]` on hot/repeating paths (see §4).
- `uxTaskGetStackHighWaterMark` returns **bytes** on IDF Xtensa, not words.
- Footer text budget: **≤39 chars** at the 6-px glyph width; centering uses
  `x = (240 - len*6)/2`.
- Icom LAN support is **IC-9700 only** (not IC-705/7610/785x).
- Documentation claims must be **grounded in actual source** — verify every technical
  statement against the code before writing it. Fictional API methods have been caught
  this way; don't add to them.

---

## 3. Delivering & packaging

### 3.1 Per-iteration delivery

After a change compiles clean and gates pass:

```bash
cd /home/claude && rm -f /mnt/user-data/outputs/CardSat-0_9_61-wip.zip
cd cardsat && zip -rq /mnt/user-data/outputs/CardSat-0_9_61-wip.zip CardSat-main \
  -x "*/host_orbit_audit/work/*" -x "*/host_orbit_audit/de421.bsp"
```

then call `present_files` on the zip. The two `-x` excludes keep the harness work
directory and its 16 MB ephemeris out of the package.

### 3.2 Derived artifacts (all auto-read `FW_VERSION` from `src/config.h`)

- **Manual PDF:** `bash tools/build_manual.sh` → `CardSat_Manual.pdf` (~152 pp). Stamps
  the version on the cover automatically. `cp` it to `docs/CardSat_Manual.pdf`.
- **Cheat card:** `python3 tools_make_cheatcard.py` → `CardSat_CheatCard_4x6.pdf`.
  **MUST stay 2 pages** (the script prints the page count; if it grew, trim content).
  `cp` to `docs/`.
- **Reference card:** `python3 tools_make_refcard.py` → `CardSat_RefCard_4x6.pdf`.
  Also **2 pages**, blue theme (#0B3E7A). `cp` to `docs/`.
- Watch for **missing-glyph warnings** from the manual build (e.g. ⅜ ⅝ aren't in the
  PDF font) — replace such characters with ASCII (`3/8`, `5/8`) in `MANUAL.md` and rebuild.

### 3.3 Host orbital audit harness (when SGP4/Doppler math changes)

```bash
cd tools/host_orbit_audit
cp /home/claude/orbtest/de421.bsp .
bash build.sh /home/claude/orbtest/sgp4src
# ... then clean up:
rm -rf work de421.bsp
```

Verifies SGP4 + Doppler against Skyfield (crossings ≤0.04°). Its `build.sh` Arduino.h
stub needs `class File;` declared for satdb.h signatures the harness never calls. Skip
for changes that don't touch orbital math.

---

## 4. Architectural constraints & the RAM story

**RAM is the dominant constraint** — no PSRAM means heap *fragmentation*, not static
size, is what destabilises the device. Key facts and current state:

- Static globals ≈ **155 KB (47%)** of the **327,680 B** DRAM region. The `App` object
  alone is ~**96 KB** (dominated by `_sats[150]` ≈ 20 KB after the SatEntry reorder).
- The largest transient is a **TLS handshake (~31.7 KB, needs ~28 KB contiguous)**;
  hence a `TLS_MIN_BLOCK` gate that fails fast rather than crashing mid-handshake.
- **USB CAT runs the heap tight** (~17 KB free, ~7 KB largest block when first measured),
  which is why audio and TLS are gated (not blanket-refused as of 0.9.60) under USB CAT.
- Worst-case analysis: `docs/design/WORST_CASE_RAM_0_9_60.md` — everyday worst case
  ~73% / ~87 KB free; pessimistic all-at-once ~85% / ~47 KB free; nothing overcommits.
- Fragmentation-hardening done in 0.9.60: CAT monitor/self-test line stores and the
  three LAN line buffers (rigctld/rotctld/web) converted from `String` to fixed
  `char[]`/`LineBuf` — removing per-byte heap churn on the hot paths.
- **Next RAM lever (deferred to 0.9.61):** staged `ScreenCtx` for isolated cold screens
  (~10 KB), per `docs/design/RAM_STAGING_ANALYSIS_0_9_60.md`. Incremental; NEVER stage
  the hot arrays (`activeTx` 68 refs, `logRecs`, `msgRing`).

Other design notes worth knowing:
- CAT transport is `Stream*`-abstracted; every wired backend (`CivRig`/`YaesuRig`/
  `KenwoodRig`) talks through a `Stream*` and `begin()` binds either the G1/G2 UART or a
  USB adapter via `setExternalStream`. `setExternalStream` MUST stay virtual and clear
  both copies of the pointer (a 0.9.58 crash came from a stale cached `_stream`).
- Doppler has two legs: `setMainFreq/Mode` (uplink) and `setSubFreq/Mode` (downlink),
  today both on ONE rig (SAT dual-VFO radios only). Dual-radio (two FT-817/818) is
  scoped in `docs/design/DUAL_RADIO_SCOPE.md` — **design only, do not build without
  revisiting it**; the FT-817/818 isn't in `radio_profiles.h` yet.
- USB device strings lead with `#N` (device address) so identical adapters are tellable
  apart; the same binding model underpins USB rotator support.
- KESSLER game state is one heap-allocated struct (`App::Kessler* kess`); its LoRa
  netplay uses magic byte `0xC7` (0xC5 = text chat, 0xC6 = object transfer), deterministic
  lockstep, spec in `docs/interfaces/LORA_KESSLER_NETPLAY.md`.

---

## 5. Working-style notes (how Paul operates)

- **Corrections are terse and authoritative.** A one-sentence directive is a complete
  spec. When told to fix X, audit for **all** cascading occurrences of the X-class, not
  just the named site — several bugs this cycle were "fix the formatter" that turned out
  to need the data source fixed too (the 60-minute pass length is the canonical example:
  it took three reports because the real cap was in `buildSchedule`, not the formatter).
- **Bench photos drive game/UI fixes.** `.HEIC` uploads show real on-device behaviour;
  read them for the actual geometry, then re-derive the math rather than guessing.
- **Be honest about what can't be verified here.** Netplay needs two devices; USB CAT
  heap-feel needs hardware. Say so, and put the item in `docs/THINGS_TO_VERIFY.md`.
- **For big monolithic-file searches:** `grep -n` with several term variants piped to
  `head` to find an anchor, then `view` a targeted line range. Don't dump the whole file.

---

## 6. Where things live

- Sources: `src/*.{h,cpp}` + the monolithic `CardSat.ino` (dual representation).
- Version: `src/config.h` → `FW_VERSION` (currently **0.9.61**).
- Gates: `tools/check_*.py`, `tools/audit_screen_geometry.py`.
- Generators: `tools/build_manual.sh`, `tools_make_cheatcard.py`, `tools_make_refcard.py`,
  `tools/make_star_tables.py` (star tables from d3-celestial BSD-3 data).
- Docs: `MANUAL.md` (~3,400+ lines), `README.md`, `docs/releases/RELEASE_NOTES_*.md`,
  `docs/{interfaces,design,guides,releases,img}/`, interface specs at repo root
  (`ROTOR_INTERFACE.md`, `RS232_INTERFACE.md`, `CIV_INTERFACE.md`,
  `LORA_MESSAGING_PROTOCOL.md`) and under `docs/interfaces/`.
- Verification backlog: `docs/THINGS_TO_VERIFY.md`.
- Build tree (not the repo): `/home/claude/fullbuild/CardSat/` + `out/`.
- Xtensa tools: `/root/.arduino15/packages/esp32/tools/esp-x32/2411/bin`.

---

## 7. Release checklist (when a cycle ships)

1. Confirm `FW_VERSION` in `src/config.h` (and `.ino` — parity) is the release version.
2. Finalize `docs/releases/RELEASE_NOTES_<ver>.md`: drop "(in progress)", write an intro,
   group into **New features / Fixes and polish / Under the hood**, merge any redundant
   sections.
3. Prepend a "New in v<ver>" blockquote to `README.md`; bump the "current firmware is"
   line.
4. Ensure `MANUAL.md` covers the cycle's features (sections are added as work lands).
5. Regenerate the three PDFs (§3.2); confirm both cards are still **2 pages** and the
   manual cover stamps the version; fix any missing-glyph warnings.
6. `cp` all three PDFs to `docs/`.
7. Final compile — must be green; run all gates.
8. Build flashable binaries into `docs/releases/bin/`: the merged single-image
   (`esptool write_flash 0x0 …-merged.bin`), the component images (bootloader→0x0,
   partitions→0x8000, app→0x10000), and a `FLASHING.txt`.
9. Package as `CardSat-<ver>.zip` (NO `-wip`), same `-x` excludes; `present_files`.
10. Verify version consistency across config.h / notes title / README, and card page
    counts, before presenting.

---

*End of memo. When in doubt: apply changes to both files, run the gates, compile with
the exact command above (STEP 1 first, no extra_flags, poll with `pgrep -x`), and keep
documentation grounded in the actual source.*
