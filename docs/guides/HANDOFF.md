# CardSat — Handoff Memo (v0.9.31)

**For: a new assistant session picking up CardSat development.**
**From: the previous session(s).**
**You will be given: this memo + a fresh upload of the repo (`CardSat-main.zip`).**

Read this memo fully before touching anything. It captures who the developer is, how the
project works, the non-negotiable rules, the current state of code *and* documentation,
the failure modes that have actually bitten this project, and exactly how to resume safely.

---

## 0. Orientation (read first)

**CardSat** is an open-source amateur-satellite tracker + multi-radio CAT Doppler
controller + rotator controller, written by **Paul Stoetzer (N8HM)**, AMSAT Executive VP,
for the **M5Stack Cardputer ADV** (ESP32-S3FN8, 8 MB flash, **no PSRAM**). Public repo:
**github.com/prstoetzer/CardSat**. It is Paul's own project — he has full decision-making
authority over every design choice.

The single most important operational fact: **Paul commits, pushes, and flashes the device
himself. You (the assistant) cannot flash Xtensa hardware.** All verification you do is
**host-side** (x86 `g++` logic simulations + brace/parity checks). **Paul's bench
oscilloscope and serial logs are the only authoritative test of on-device behavior.** His
only bench radio is an **IC-821H**. When a hardware fix is uncertain, ship Paul a
diagnostic to run, not a theory.

---

## 1. Non-negotiable invariants

Break any of these and you ship a bug Paul has to catch on hardware.

**1a. Dual representation.** Every code change must be **byte-identical** in BOTH the
modular `src/*.cpp` / `src/*.h` AND the single-file **`CardSat.ino`** (the Arduino IDE
compiles the `.ino`; PlatformIO compiles `src/`). The **only** exception is `#include`
lines — they live in `src/` headers, not in the `.ino`. There are **36 code files** total:
35 under `src/` + `CardSat.ino`.

**1b. Verify after EVERY change.** The two host-side checks (you will recreate these
scripts at session start — they are not in the repo):
- **`balance.py`** — a comment/string-aware brace/paren/bracket tokenizer. Must report
  `(0,0,0)` on **all 36 files**. A nonzero count means a `str_replace` ate a brace.
- **`parity.py`** — confirms `src/ ↔ .ino` function parity via token-count grep, plus the
  `rows[NN]` audit after any settings change.

**1c. Host checks have hard limits.** `g++` sims and balance/parity do **not** catch:
ESP32 hardware behavior, pin-mux issues, or **C++ undeclared-type / forward-reference
errors**. (A real example this project hit: a `GeoLoc`-vs-`Observer` type error compiled
clean in the host sim and only failed in the Arduino build.) **Host-test any algorithm in
`g++` before mirroring it**, and accept that the final word is Paul's device.

**1d. Anchor-collision risk in `str_replace`.** Identical code blocks across files need
**class-specific neighbor lines** to disambiguate, or a replace can silently consume an
adjacent function header. (This actually happened: a CTCSS function header was eaten and
only caught by the balance check going to −1.) Use the heredoc patch pattern: multiple
`repl()` calls, each with `assert count == 1`, then one `write()`.

**1e. Filesystem resets between sessions.** Your working tree does not persist. At the
start of every session, **ask Paul to re-upload `CardSat-main.zip`** and verify the tree
before any code work. Working dir is **`/home/claude/cardsat/CardSat-main/`**. **There is
no git** — use `mv`, not `git mv`; Paul handles all git himself.

---

## 2. Environment & build

- **Board:** M5Stack Cardputer ADV (StampS3A = ESP32-S3FN8, 8 MB flash, **no PSRAM**),
  240×135 IPS LCD, 56-key keyboard, microSD, speaker, Grove port, 2×7 header.
- **Toolchain:** Arduino IDE, esp32 core **3.2.1**, **Huge APP (3 MB app / 1 MB SPIFFS)**
  partition, **PSRAM disabled**, **USB-CDC on boot**. Libraries: RadioLib **7.7.1**,
  ArduinoJson **v7**, M5Cardputer / M5Unified / M5GFX, TinyGPSPlus, Hopperpop **Sgp4**.
- **`FW_VERSION = "0.9.31"`** in **both** `src/config.h` (~L101) and `CardSat.ino` (~L214).
  Version shows on the **About** screen only.
- **Two release binaries:** `CardSat.bin` (Launcher, app-only, preserves internal data) and
  `CardSat_Merged.bin` (M5Burner / esptool at `0x0`, empty filesystem).
- **Storage fact:** `Store::begin()` **prefers microSD** — with a FAT card present, all
  config/cal/notes/favorites/cache live under `/CardSat` on the card, and flashing (which
  only writes internal flash) **never loses them**. Internal LittleFS is the fallback.
- **Doc/PDF build tools (in repo):** `tools/build_manual.sh` (pandoc + xelatex; greps
  `FW_VERSION` from config.h) and `tools_make_cheatcard.py`. After building, copy the PDFs
  to `docs/` and keep the root copies.

---

## 3. Current state — code

**v0.9.31 is shipped and stable.** The four observer features added in 0.9.31 are complete
and host-verified (not yet all hardware-confirmed):

1. **Per-satellite operating notes** — `N` on Track; stored tab-delimited in
   `/CardSat/notes.txt`; shows a `*` in the Track header.
2. **Visual pass predictions** — the schedule flags visually observable passes (sunlit
   satellite + dark sky + high enough) with a yellow `*`; verdict on pass-detail. Settings
   under *Station/display* (visible passes / sky-dark gate / min el).
3. **Decay/reentry watch** — a colored down-arrow on the satellite list (yellow watch /
   orange decaying / red imminent), from perigee + King-Hele lifetime; a Perigee line on
   the orbit screen.
4. **Sun/Moon transits** — `t` on the Sun/Moon screen runs a 48-hour incremental scan
   (watchdog-safe) for the active satellite crossing the solar/lunar disc.

**Hardware-confirmed:** single-pin CI-V works end-to-end on the IC-821 (full bidirectional
exchange over one open-drain wire); plus all the prediction/display/GPS/alarm/deep-sleep
paths. **Still host-tested only** (verify on the air): separate-pin CI-V on that specific
rig, Yaesu/Kenwood encoders, the GS-232/Easycomm/SPID and direct-Yaesu rotator backends,
and the rigctld/rotctld inbound servers. **Confirmed on hardware:** single-pin CI-V on the
IC-821 (Doppler + knob tuning), LoRa messaging (vs the CardSat Pager), and the Icom LAN path
(controlled an IC-705 — the IC-9700 itself is still untested). The **rotctl/rigctl and
PstRotator** network clients emit verified-accurate commands but haven't driven a physical
rotor/rig. The authoritative list is
**docs/THINGS_TO_VERIFY.md**.

**The radio layer:** abstract `Rig` base class (rig.h) with `CivRig`/`YaesuRig`/
`KenwoodRig`/`IcomNetRig`/`RigctlRig` backends, picked by `makeRig()` (rig.cpp:30). Ten
radios: IC-820/821/910/970/9100/9700, FT-736R/847, TS-790/2000.

**The rotator layer:** eight backends behind one pointing interface, selected by `RotType`
— GS-232, Easycomm I/II/III, SPID (all over an SC16IS750 I²C→UART bridge), rotctl-net (TCP
client), PstRotator (UDP), and **Yaesu-direct** (ADS1115 ADC + PCF8574 outputs, no GS-232
box, bang-bang loop built in app.cpp with live calibration).

---

## 4. Current state — documentation (heavily reworked this session)

The docs were **reorganized and reviewed extensively** in the most recent sessions. A new
session must understand the current layout before editing docs.

**Repo root holds only:** `README.md`, `MANUAL.md`, `CardSat_CheatCard_4x6.pdf`,
`CardSat_Manual.pdf`. Everything else lives under **`docs/`**:

```
docs/
  FEATURES.md  WIRING.md  BUILD_AND_FLASH.md  RADIOS.md  THINGS_TO_VERIFY.md
  interfaces/   CIV_INTERFACE, CIV_SINGLE_PIN, ICOM_LAN_PROTOCOL,
                ROTOR_INTERFACE, RS232_INTERFACE, RADIO_SETTINGS
  design/       10 *_SCOPE.md + CLOUDLOG_UPLOAD_POSTMORTEM.md + 2 CardSatZero port docs
  guides/       ARDUINO_SETUP, PORTING, CODE_REFERENCE, HANDOFF_0.9.31 (the old one)
  releases/     RELEASE_NOTES_0.9.{10–20,22,31–38}.md
  img/          screenshots
```

What changed, and the rules that came out of it (honor these when editing docs):

- **README** streamlined from 830 → ~180 lines: intro/status, "what it does," screenshots,
  hardware, install/upgrade, quick-start, a **documentation index table**, data sources,
  credits. Detailed content was extracted into the `docs/` files above.
- **MANUAL.md** (~3,400 lines, 25 sections) got several **full review passes against the
  code**. Confirmed-accurate and not to be regressed: the rotator section lists **all 8
  backends**; **§8 is "Screen map and navigation"** (flow) while **§21 is
  "Screen-by-screen reference"** (catalog) — two distinct sections, don't re-merge or
  re-title; **§3** covers all three CAT transports (wired CI-V incl. single-pin, Icom LAN,
  rigctl-net) and the CI-V wiring choice; **CW mode** is explained in §9 Sidebands; the
  **CAT serial monitor** and **CAT self-test** have §21 entries and a §16 subsection;
  **tilt tuning** lives in §9 (a tuning method), not §10 Calibration.
- **Terminology convention (verified against device labels):** the network **client** is
  `rigctl (net)` / `rotctl (net)` (named for the client tool); the **server** CardSat runs
  is `Rigctld server` / `Rotctld server` (named for the daemon). Default ports: rig 4532,
  rotator 4533. The **rigctl client reuses the `catPort` field** (default 50001 — *not*
  4532; the user sets it to their rigctld's port). MANUAL §18 has a "Network control roles:
  client vs server" subsection explaining all four roles.
- **Version-note hygiene:** "new in vX" callouts were normalized into standard prose;
  release notes headline only genuinely-new items; "host-verified only / untested" caveats
  were kept (they're still true) but stripped of stale version framing.
- **Two new porting docs** (the most recent work):
  - **docs/guides/PORTING.md** — strategy, portability tiers (A–D), per-capability porting,
    a subset dependency cheat-sheet, off-Arduino guidance, and **worked examples** for
    another ESP32 board (+OLED), a headless ESP32→MQTT, a non-ESP32 MCU (Teensy/RP2040),
    and Raspberry-Pi/Linux. Includes a hand-written `tleToEntry()` helper because the
    firmware has no TLE→`SatEntry` parser (it only does `gpToTle` the other way).
  - **docs/guides/CODE_REFERENCE.md** — a file-by-file annotated reference: every module's
    purpose, portability tier, public interface, key functions **with line anchors**, and
    five cross-cutting data flows. Line anchors are stamped to 0.9.31 and will drift; each
    is also anchored to a greppable function signature.

**Doc invariants:** after any doc edit, verify all internal anchors and all cross-doc
`.md`/`.pdf` links resolve (a small Python script does this — see §7). Interface docs carry
prominent untested/at-your-own-risk banners. Scope facts to keep stated explicitly: **Icom
LAN is intended for the IC-9700** (confirmed controlling an IC-705, IC-9700 itself
untested); the **Grove port is 5 V** (GPIOs not 5 V-tolerant — never wire CAT
direct).

---

## 5. Architecture quick-reference

- **Keys:** `;` up, `.` down, `,` left, `/` right, ENTER select, `` ` ``/DEL back. Palette
  `CL_*` enum (app.cpp ~L24). `Screen` enum (app.h) ends `…SCR_CATMON, SCR_TRANSIT`.
- **App singleton** with static-member trampolines; `main.cpp` is a thin shell →
  `App::setup()` / `App::loop()`.
- **Settings:** `struct Settings` (settings.h), JSON load/save via `Store`. Row-ID arrays
  at app.cpp L4415 (`SET_RADIO/SET_ROTOR/SET_STN/SET_NET`); the `rows[N]` label array is in
  `drawSettings()` (app.cpp ~L11220, `const int N = 69`, highest index used = 68). After any
  settings change, run the `rows[NN]` audit.
- **Rig/CAT:** civ/yaesu/kenwood `begin(baud,uartNum,rxPin,txPin)`; CI-V on `CIV_UART_NUM=1`,
  G1(RX)/G2(TX); `civPinMode` 0=TX/RX, 1=1-pin G2, 2=1-pin G1. `icomnet.cpp` = RS-BA1 UDP,
  for the IC-9700 (confirmed on an IC-705). `catType` = `CAT_WIRED`/`CAT_NET`/`CAT_RIGCTL`.
- **Predictor:** `predict.h` `Predictor` class — `setSite(loc.obs())` / `setSat(*s)` /
  `look(t)→LiveLook` (az/el/range/rangeRate/sunlit/sunAz/El/subpoint) / `azelAt` /
  `predictPasses`; static `dopplerFreqs(dlNom,ulNom,rangeRate,calDl,calUl, rxHz&, txHz&)`
  (reference out-params). Elements rendered to TLE via `SatDb::gpToTle()` (satdb.cpp:490).
- **config.h** centralizes all pins/URLs/limits — the first file to edit for any hardware
  change. `MAX_SATS=220`, `MAX_TX_PER_SAT=64`.
- **Data sources:** AMSAT GP/OMM JSON, SatNOGS transmitters, NOAA SWPC (space wx),
  Open-Meteo (weather, CC BY 4.0), QRZ XML (user creds), cty.dat (bundled, DXCC).

(For the full annotated map, read **docs/guides/CODE_REFERENCE.md** — it's current.)

---

## 6. Hard-won lessons (failure modes that actually bit this project)

- **When a hardware fix fails repeatedly, ship a diagnostic, not another theory.** You
  can't see the device; Paul can.
- **Undeclared-type / forward-ref errors pass host checks.** Copy established local
  patterns exactly; don't invent a type name (the `GeoLoc`/`Observer` incident).
- **Anchor collisions silently eat adjacent code.** Disambiguate `str_replace` anchors with
  class-specific neighbor lines; the balance check is your safety net.
- **Specs from source only.** Protocol/interface docs (CI-V, LoRa, etc.) must be written
  from actual source extraction, never from memory or a memo summary.
- **Grove port is 5 V.** Hardware docs must say so to protect non-5 V-tolerant GPIOs.
- **Release-notes/version framing discipline.** Headline only what's genuinely new in a
  version; note previously-shipped content as already-shipped.
- **Don't fabricate release notes** for versions whose real notes live only in Paul's repo.
- **Decay/dial thresholds are judgment-call starting points** — Paul's bench is the real
  tuning signal, not a host sim.

---

## 7. How to resume (checklist)

1. **Get the tree.** Ask Paul to upload `CardSat-main.zip`; unzip to
   `/home/claude/cardsat/`. Confirm `FW_VERSION` and that `CardSat.ino` + `src/` are both
   present (36 code files).
2. **Recreate the verification scripts** (`balance.py`, `parity.py`) — they don't travel
   with the repo. Run `balance.py` once to confirm a clean `(0,0,0)` baseline on all 36
   files before editing.
3. **For code work:** make the change in `src/` *and* `CardSat.ino` (byte-identical, minus
   `#include`s). Host-test any algorithm in `g++` first. Run balance + parity (+ `rows[NN]`
   if settings changed) after every function edit. Rebuild the bundle zip and
   `present_files` when done. Paul commits/pushes/flashes.
4. **For doc work:** edit the file in place; if you move/rename, update every inbound link.
   Then run a link-integrity pass — a short Python script that, for each `.md`, checks every
   `](#anchor)` against the file's heading slugs and every `](path.md|.pdf)` against the
   filesystem. Rebuild the manual PDF (`tools/build_manual.sh`) if `MANUAL.md` changed and
   copy it to both root and `docs/`.
5. **Stage outputs** in `/mnt/user-data/outputs/` and `present_files` the relevant files.

---

## 8. Open / deferred items (candidate next work)

These are noted, not urgent. Confirm priority with Paul.

- **IC-821H bench confirmation still pending** for a few CAT defaults: higher default
  `catDelayMs`, MAIN-read as reference / push-only default, default PTT poll OFF.
- **Voltage level-shifter pin mapping** for the Elecrow/y2kblog single-wire CI-V module:
  open-drain channel needed; jumper patterns ③/④ identified, but the exact pin mapping was
  deferred to Paul reading the schematic PNG directly (image unreadable by fetch tools).
- **Optional LoRa hardening:** set preamble/CRC/header explicitly in `lora.cpp` rather than
  relying on RadioLib defaults.
- **Source-comment doc paths:** a few `src/` comments still say "See ROTOR_INTERFACE.md"
  without the new `docs/interfaces/` path. Cosmetic; touching them means a code edit (dual
  representation + balance/parity), so it was deferred.
- **CODE_REFERENCE line anchors** will drift as code changes; a small `tools/` script to
  regenerate the function→line table on demand was offered but not built.
- **PORTING `tleToEntry` epoch math** is left as a documented TODO (needs the L1 epoch
  conversion); finish it if a porter needs turnkey TLE ingest.

---

## 9. The old handoff

`docs/guides/HANDOFF_0.9.31.md` is an earlier handoff from the same version, written before
the big documentation reorganization and the porting docs. **This memo supersedes it.** If
both are present, trust this one for the documentation state; they agree on the code
invariants. You may delete the old one (with `mv`/`rm`, no git) if Paul wants a single
handoff.

---

*Welcome to the project. The code is in good shape, the docs are current and
cross-checked, and v0.9.31 is shipped. Read THINGS_TO_VERIFY.md and CODE_REFERENCE.md for
the technical lay of the land, and remember: you reason and verify host-side, Paul flashes
and confirms on hardware.*
