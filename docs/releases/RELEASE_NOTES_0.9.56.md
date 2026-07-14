# CardSat v0.9.56 — release notes

*July 2026. **A pocket workbench.** The Tools hub gains a Tiny BASIC interpreter, a graphing
calculator, and a location converter; four new printable reports join the roster, the visual
ones carrying ASCII renderings of their screens; and the tools themselves can now print. This
release also completes an external code review of the 0.9.55 printing work — real bug fixes,
A4 support, a global emergency stop, and memory instrumentation — and reconciles the
documentation with the code.*

---

# New tools

## Tiny BASIC interpreter (new Tools entry)

A small line-numbered **BASIC interpreter** with a built-in editor is now in the Tools hub, for
writing and running little programs on the device. Programs are capped at **4 KB**.

Supported: `LET`/bare assignment, `PRINT` (with `,` `;` and strings/expressions), `IF…THEN`
(line-number jump or inline statement), `GOTO`, `GOSUB`/`RETURN`, `FOR…TO…STEP…NEXT`, `REM`,
`END`; 26 numeric variables A–Z; and `ABS INT SQR SIN COS RND` (trig in degrees). Lines can be
typed in any order and run sorted by number. `Fn`+`R` runs (output to a scrollable console),
`Fn`+`S`/`Fn`+`L` save/load `/CardSat/basic/*.bas`, `Fn`+`N` starts a new program.

**Safety is bounded on every axis** so nothing a user types can hang the device: at most 200
program lines, GOSUB nesting to 16, FOR nesting to 8, a 6 KB output cap, and a per-run budget of
500,000 statements — an infinite loop halts with a `loop?` message. The run loop also yields
periodically to keep the watchdog fed.

Implementation note: the interpreter was prototyped and validated host-side before porting — the
execution model (loops, GOSUB, nested FOR, IF/THEN, sorted lines, PRINT punctuation) was checked
against a suite of programs, the runaway-protection was confirmed to catch infinite loops and
stack overflows without hanging, and the final in-firmware source was re-extracted and re-run to
confirm no transcription errors. It shares no state with other subsystems.

## Graphing calculator (new Tools entry)

A **Graphing calculator** in the Tools hub plots **y = f(x)** using the scientific calculator's
expression parser and full function set, with the variable **x** added. Type a function (ENTER),
and the curve is sampled one point per pixel column across a pan/zoomable window: arrow keys pan,
`+`/`-` zoom about the centre, `a` auto-fits the vertical range to the visible data, and `r`
resets the window. Trig is in degrees (matching the sci calc), so the default window is
x ∈ [-180, 180]. Poles and discontinuities (`tan(x)`, `1/x` at zero) break the curve instead of
drawing a spurious vertical line. Fully local, no network.

Implementation note: rather than write a second expression parser, this extends the existing one
with an `x` binding, so the two calculators share identical math and function support. The parser's
`x` evaluation was validated end-to-end (10/10 test expressions) and the plot's discontinuity/clamp
handling was checked against `sin`, `tan`, `1/x`, and `x^2` sample sets.

## Location-format converter (new Tools entry)

A new **Location converter** in the Tools hub shows one position simultaneously in every format a
satellite or field operator is likely to need, all computed on-device with no network. Seed it
from your station location (or type a grid / lat / lon), and it derives:

- **Maidenhead** grid, 6- and 8-character
- **Decimal degrees**, **DMS** (deg-min-sec), and **DDM** (deg-decimal-min)
- **Plus Code** (Open Location Code, 10-digit)
- **UTM** (zone + easting/northing, WGS84)
- **MGRS** and **USNG** (the same military grid, packed and spaced forms)

`;`/`.` pick the editable field, ENTER types, `,`/`/` scroll the derived list, and **`s`** adopts
the shown position as your station QTH.

The UTM/MGRS projection and the Plus Code encoder are the error-prone part of this kind of tool,
so all three were validated **byte-for-byte against reference implementations** (`utm`, `mgrs`,
and Google's `openlocationcode`) across six points spanning both hemispheres and several zones,
including the Norway/Svalbard UTM zone exceptions — and re-validated from the final source to catch
any transcription error. They are accurate to about a metre.

**what3words** was intentionally not included: it is a proprietary, network-only wordlist lookup
rather than an offline algorithm, so it doesn't fit an offline converter (and CardSat keeps all
Tools math local).

## Tools menu order, and refinements to the new tools

- **Memory:** the Tiny BASIC interpreter's ~3.8 KB working state is now **heap-allocated only
  while a program runs** (and freed after) instead of sitting resident in `.bss` for the whole
  session; the graphing calculator's default expression is likewise seeded lazily on first open.
  This returns the deferrable RAM to the boot-time free pool. (The remaining footprint of the new
  features is compiled code, not runtime allocation.)
- **Graphing calculator:** the expression is now drawn on its own row below the header instead of
  overlapping the title.
- **Tiny BASIC editor:** the program name/size line no longer overlaps the header; `Fn`+`;`/`Fn`+`.`
  now move the cursor **up/down a line** (previously left/right); no example program is
  pre-populated (the editor opens empty with a short hint); and a **PRINT bug** was fixed where an
  empty `PRINT` following a `,`/`;`-terminated `PRINT` failed to break the line.
- **Tools menu order:** the standalone tools are regrouped **compute-first** (scientific calc,
  graphing calc, programmer calc, Tiny BASIC, location converter, link budget), then satellite
  compute (state vector → GP, CubeSatSim), then reference lookups (DXCC, CQ/ITU zones, CTCSS,
  operating refs, radio math, char lookup).
- **Documentation:** a complete **Tiny BASIC reference** was added to the manual (editor keys,
  program structure, every statement, PRINT semantics, expressions and functions, four worked
  examples, and the safety limits). All example programs were validated against the interpreter.

---

# Printing

## Four new printable reports

Four screens that previously had no print option can now print their data with `p`:

- **Orbital analysis** (`p` on the Orbital-analysis screen) — a **permanent record** of the
  satellite's orbital characteristics: Keplerian elements, epoch, period, SMA, apogee/perigee +
  footprint diameters, orbital speeds, J2 node/apsidal drift, sun-sync flag, repeat-track
  resonance, longest-possible pass, beta angle + full-sun/eclipse geometry, decay estimate, and
  launch identity. It **omits live values** (range, Doppler, az/el, sub-point, next pass) so the
  printout is a lasting snapshot of the orbit, not a momentary look.
- **Illumination** (`p` on the Illumination screen) — the sunlit fraction of each orbit over the
  next 60 days, plus an **ASCII raster** (one line per day, `#` sunlit / `.` eclipse across the
  orbit) mirroring the on-screen shading.
- **10-day passes** (`p` on the 10-day screen) — a dated pass table (AOS/LOS/peak elevation)
  plus an **ASCII day-timeline** (a 24 h UTC track per day, passes marked by elevation tier).
- **6-hour timeline** (`p` on the Sky-at-a-glance screen) — all favorites' passes in the next six
  hours as a sorted list plus an **ASCII timeline** (one row per favorite across the window).

Each is reachable three ways: the `p` key on its source screen, the About-screen Print submenu
(now twenty reports), and the serial console (`print orbit|illum|tenday|timeline`). All follow the
existing zero-RAM streaming path and are width-aware (compact on 58 mm paper, fuller on 80 mm).

## Printing from Tiny BASIC and the tools

The tools can now **print their results** to the configured print sinks:

- **Tiny BASIC** — `Fn`+`P` in the editor prints the **program listing**; `p` on the output
  console prints the **last run's output** (including any error line).
- **Scientific calculator** — `Fn`+`p` prints the current entry and result.
- **Programmer calculator** — `p` prints the value in all four bases (hex/dec/bin/oct).
- **Location converter** — `p` prints the position in every format (grid, DMS, DDM, Plus Code,
  UTM, MGRS, USNG).
- **Graphing calculator** — `p` prints the expression and the current window.
- **Character lookup** — `p` prints the selected character's encodings (bases + ASCII name + Morse).

**Key-assignment care:** on the two screens where you type free text — the scientific calculator
and the Tiny BASIC editor — printing is on **`Fn`+`p`** so a plain `p` still types normally (the
calculator needs it for `pi`/`exp`; the editor needs every letter). On tools that don't accept
typed text, plain **`p`** prints. This mirrors the existing note-editor convention (`Fn`+`p`).

## Raster printing bug fixes

**Long reports now paginate across pages (was: silently truncated).** In 0.9.55 the raster
path (PWG/URF) collected report text into a fixed buffer and rendered exactly **one** US-Letter
page — any line past what fit on that sheet was silently dropped. Reports that can run long
(all-favorite passes, QSO log, notes, rove plans, workable horizon, target-search results,
big schedules) were affected. The renderer is now **page-aware**: it computes how many text
rows fit on a sheet and emits as many raster pages as the report needs (PWG and URF both carry
multiple pages in one job). The line buffer also grew (90 → 400 wrapped lines, ~10 pages), and
if a report somehow exceeds even that, a visible `-- report truncated --` marker is printed
instead of dropping lines silently. Validated on the host: a 120-line report produces three
byte-clean pages, each exactly 3300 scanlines, decoded past the reference tooling.

**Socket writes are now checked.** `sockWrite()` previously ignored `WiFiClient::write()`
return values, so a job could connect, fail halfway through transmission, and still report
success — most concerning for raster jobs, which are many writes over a longer interval. All
writes now go through `wrChecked()`, which loops until every byte is placed or records a
failure on a dropped link. A new `documentSent()` state feeds the print status, which now
shows **`(send failed)`** when a job connects but does not fully transmit.

**URF was missing from the IPP status check.** The "(IPP ok)" / "(IPP: sent, not confirmed)"
status logic tested `printFormat == 7` (PWG) but not `8` (URF), so URF jobs never showed the
IPP acceptance detail. Fixed.

## A4 paper support

Raster printing now offers **US Letter or A4** — **Settings → Network → Raster paper**. A4
emits the correct page geometry (2480 × 3508 px at 300 DPI, 595 × 842 pt,
`iso_a4_210x297mm`). There is no automatic media negotiation; CardSat sends the selected size
and relies on the printer to place it on the loaded stock (as most do). This addresses the
0.9.55 limitation of Letter-only raster, which unnecessarily restricted international use.

---

# Safety, diagnostics, and reliability

## UI safety + documentation (UI/docs review)

A third external review covered UX at scale, documentation maintainability, and large-catalog
architecture. Two bounded, high-value items are addressed here; the larger structural
recommendations (task-hub navigation, a screen registry, accessibility modes, and a two-tier
disk-backed catalog) are scoped for later releases.

**Global emergency stop.** Pressing **Fn + back** (either `` ` `` or DEL) from any operating
screen now disengages *all* external equipment control at once — radio-frequency output and
every rotator-pointing mode (satellite tracking, Sun/Moon, EME/Moon, grid-bearing). Previously
you had to return to the Track screen and toggle radio and rotator off individually; now the
rig and antenna can always be halted with one keystroke, wherever you are. It beeps and shows
"All control stopped" (or "No control was active" if nothing was engaged), and is deliberately
inactive only in the two text editors, where those keys are editing keystrokes.

**Banners auto-dismiss everywhere.** All transient status banners already carried a timeout,
but on a few static screens (which repaint only on a keypress) an expired banner could linger
until the next key. A global expiry sweep now clears any timed-out banner on every screen, so
every banner disappears on its own within its timeout (1.5–8 s depending on the message).

**Documentation reconciled with the UI review's Phase-1 findings.** The manual's opening
"every feature has been exercised on hardware" claim — which contradicted its own later list of
untested CAT/rotator paths — is corrected to name the core features that *are* hardware-
confirmed. The Help screen's `o` (Orbit animations) shortcut, previously missing from the prose
description, was added. A consolidated **Printing** row was added to the key-reference table
(the `p` / `P` / `Fn+p` shortcuts were documented only in the printing section before). The
README's screenshot note now states the captures' version and the current firmware version.
(The 150-vs-220 satellite-limit and raster-printing contradictions this review also flagged
were already fixed earlier in 0.9.56.)

The emergency stop is documented in the manual's Global keys line, the new Printing/key table,
and the printed cheat card.

## Memory diagnostics (baseline for a future RAM refactor)

The whole-codebase review identified a real opportunity to reclaim ~25–35 KB of always-resident
RAM by giving mutually-exclusive screen state a bounded lifetime. That refactor is deferred, but
its prerequisite — a way to *measure* memory — lands now, so the baseline exists before any cuts.
(TLS certificate validation and LAN control hardening from that review were reviewed and
intentionally not pursued: CardSat runs on trusted LANs for hobbyist use, with informed users on a
memory-constrained device, and the added cost isn't justified for that threat model. See
`docs/design/RAM_LIFECYCLE_SCOPE.md` for the refactor plan.)

New read-only diagnostics (no behavior change, off by default where applicable):

- **`mem`** (serial console) — a full memory baseline: live free heap, min-ever high-water mark,
  largest contiguous block, `sizeof` of the key structures (SatEntry, SatDb, PassPredict,
  Transponder, MemoEntry, App), and the static byte cost of the large resident arrays (the 150-sat
  catalog, the two 128-entry pass buffers, the memo list, the transponder buffer) — the exact
  figures a refactor needs to prioritize by real bytes.
- **`memtrace`** (serial console) — toggles a one-line heap log (free + largest block) on every
  screen change, so the runtime cost of heavy screens can be profiled. Off by default.
- **About screen** — the heap line now shows live / largest-block / **minimum-ever** free heap, so
  the high-water mark is visible on-device without a serial cable.

---

# Under the hood

## Codebase-review fixes (orbital-data downloads + docs)

A second external review covered the rest of the codebase. Its top data-integrity findings are
addressed here; the security items (TLS validation, LAN hardening) and the RAM-lifecycle
refactor are scoped for later releases (see `docs/design/REVIEW_0.9.55_ASSESSMENT.md`).

**GP catalog downloads are now transactional.** Previously the destination file was opened for
writing — truncating the previous good catalog — before the new download was validated, so a
failed or interrupted update could leave you with no usable catalog. Now the download goes to a
`<path>.tmp` sibling, is verified for completion and non-emptiness, and only then atomically
renamed into place. **A failed download never destroys the existing catalog** — the old file
stays put and only the temp is removed. This matters most in the field, where re-fetching needs
signal.

**The fixed 400 KB download cap is gone.** It conflicted with the large CelesTrak groups the
docs advertise. The ceiling is now the actual free space on the filesystem (minus a safety
margin), and any server-declared Content-Length is preflighted against free space with an
explicit "download exceeds storage" error when it won't fit.

**Download errors are now typed, not string-matched.** A real bug: the retry loop bailed on
`lastErr == "low flash"`, but the code emitted `"file too big for storage"` — so the intended
immediate-abort for a full filesystem never fired, and a storage-full failure would be retried
pointlessly. Errors now use a typed `DownloadError` enum (`ConnectFailed`, `HttpError`,
`StorageFull`, `WriteFailed`, `ShortRead`, …) and the retry policy branches on the code, with
display text translated only at the UI boundary.

**Documentation reconciled with source.** `MAX_SATS` is now stated as **150** everywhere
(README and CODE_REFERENCE previously said 220; the code is 150). The CODE_REFERENCE version
basis was refreshed to 0.9.56 and its TLS description now names the actual wrapper
(`ESP_SSLClient`/BearSSL, still `setInsecure()` — the insecurity is unchanged and tracked as a
pre-1.0 security item). The single-pin CI-V settings comment, which still said "unverified,"
now records that it is **confirmed on an IC-821H** (bidirectional CI-V, Doppler, knob tuning
over one open-drain GPIO).

**Frequency ceiling documented.** Transponder frequencies are 32-bit unsigned Hz, capping at
~**4.294 GHz** — enough for every band CardSat tracks (through 23 cm and the 3.4 GHz
allocation) but not higher microwave transponders. This is now stated in `satdb.h` and the code
reference; a future 32-bit kHz format could lift it with no RAM cost.

## Memory footprint: consolidated, and documented honestly

The capability probe's separate 4 KB response buffer was removed — it now **reuses the raster
scanline workspace** (`s_rscan`), since probing and raster rendering never happen at the same
time. That drops the printing subsystem's static buffers from ~9.2 KB to ~5.1 KB.

More importantly, the documentation is corrected. The 0.9.55 implementation doc claimed
"Idle: 0" memory for printing; that was **wrong** — the raster/probe workspace is fixed static
(BSS) RAM, resident whenever the firmware runs. The `PRINTING_IMPLEMENTATION.md` memory
section now states the real figure (~5 KB static) and is precise about the property that
actually holds: **no path's memory scales with report or page size** — a 1-page and a 10-page
report cost the same working set, because the page is streamed one scanline at a time.

## Documentation reconciliation

The 0.9.55 printing docs had accumulated contradictions from several editing generations; a
reviewer catalogued them. All are fixed:

- The count said "eight formats" but nine are implemented (ESC/POS, plain text, PCL,
  PostScript, ESC/P2, Star, ZPL, PWG raster, URF raster) — corrected to **nine** throughout.
- `print.h`'s header comment still described "four page languages" and stated that raster-only
  printers were **not supported** — directly contradicting the same header's PWG/URF format
  definitions. Rewritten to describe all nine and to state that raster-only AirPrint printers
  **are** supported.
- The 0.9.55 release notes and manual carried a stale "raster-only printers are not supported"
  limitation block (written before the raster work landed in that same release) sitting next
  to the sections describing confirmed raster printing. Corrected.
- Target-search printing was described in one place as "intentionally not shipped" while being
  fully wired (screen `p` key, `print target` serial command, About print menu). It **is**
  shipped; the note is corrected.

## Documented (not yet fixed) limitations

Recorded plainly in `PRINTING_IMPLEMENTATION.md` §10:

- **IPP resource path** is fixed at `/ipp/print` (printing and probe). Printers requiring a
  different resource are not currently reachable.
- **IPP acceptance** is judged from the HTTP status line, not the parsed IPP operation-status
  or job-state. "(IPP ok)" means HTTP-accepted, not rendered — the UI never claims otherwise.
- **Capability probe** substring-searches the response for MIME tokens rather than parsing IPP
  attributes.
- **Media** is Letter/A4 with no live negotiation.

None affect the primary field receipt-printer path.

---

# Verification status

CardSat distinguishes **host-validated** (simulated on x86, checked against reference
implementations, gates green, `src/` ↔ `.ino` mirror-identical) from **bench-confirmed**
(flashed and exercised on real hardware). This release is the former except where noted.

**Host-validated in 0.9.56:**

- **The Tiny BASIC interpreter** — the execution model (loops, GOSUB, nested FOR, IF/THEN,
  line sorting, PRINT punctuation) against a suite of programs, and the runaway protection
  (infinite `GOTO`, infinite `GOSUB`, missing lines, `RETURN`/`NEXT` without their partners,
  million-iteration loops) confirmed to halt cleanly rather than hang. Re-extracted from the
  final firmware source and re-run to catch transcription errors.
- **The coordinate projections** — UTM, MGRS/USNG, and Plus Code validated **byte-for-byte**
  against the reference `utm`, `mgrs`, and `openlocationcode` implementations across six points
  in both hemispheres, including the Norway/Svalbard UTM zone exceptions. The 8-character
  Maidenhead extension checked against the reference `maidenhead` package.
- **The graphing calculator** — the `x` binding through the real parser (10/10 expressions) and
  the discontinuity handling against `sin`/`tan`/`1/x`/`x²`.
- **Multi-page raster** (3 pages, byte-clean, 3300 scanlines each), A4 header geometry, and the
  checked-write and pagination logic.
- **Report content and widths** — every ASCII rendering width-checked against both 58 mm
  (32-col) and 80 mm (44-col) paper.

**Bench-confirmed:** compilation (`arduino-cli`), which caught five scoping and
definition-ordering errors the host gates are structurally blind to.

**What stands between this release and a 1.0** — blockers, deferred decisions, and the
hardware-verification gap — is tracked in **[ROADMAP_TO_1.0.md](../ROADMAP_TO_1.0.md)**.

**Needs bench confirmation** — see **[THINGS_TO_VERIFY.md](../THINGS_TO_VERIFY.md)** for the
full list. In short: everything involving the **display, the keyboard, and real printer output**.
The Tiny BASIC editor's typing and cursor movement, the graphing calculator's rendering, the
location converter's layout, the ASCII renderings' character-cell aspect ratio on a real printer,
the `p` / `Fn`+`p` print keys, the multi-page and A4 paths, and the `(send failed)` status
against a genuinely interrupted job.
