# CardSat v0.9.57 — release notes

*July 2026. Field-use fixes to the 0.9.56 Tools work — a memory bug that could break LoTW
uploads, keyboard conflicts on the new typing screens, and a missing orbit-report field — plus
three additions: BASIC programs can read the system's own data, nine more reports print
(including EME, which had none), and the on-device Help is corrected against the actual keys.*

---

# Fixes

## Tiny BASIC no longer starves the heap after a run

**Reported from the field:** running a program with an infinite loop, then attempting a LoTW
upload, failed and crashed. The `mem` trace told the story:

```
[mem] screen  23->128  free 55112  largest 31732
[mem] screen 128->129  free 48816  largest 25588   <- ran a program
[mem] screen 129->128  free 48816  largest 25588
[mem] screen 128->96   free 48872  largest 25588
[mem] screen  96->14   free 48872  largest 25588
[mem] screen  14->0    free 48872  largest 25588   <- back at Home, still gone
```

Both `free` and `largest` dropped by ~6 KB when the program ran and **never came back** — flat
all the way out to Home. That 6 KB is `basicOut`, the console's copy of the run's output, which
is capped at 6 KB and which a runaway program fills to the brim. It was never released. On this
no-PSRAM board a permanently-held 6 KB is enough to starve the single large **contiguous** block
a TLS handshake needs, which is why the upload — not BASIC — was what visibly broke.

Two bugs, both fixed:

- **Nothing ever released the buffer when you left BASIC.** It is now freed from the
  screen-transition hook in `loop()`, which catches *every* way out of the tool — including
  `Fn`+`h` to Help, which bypassed the editor's own exit path entirely. The program text is
  deliberately kept, so returning to BASIC in the same session still finds your program.
- **Arduino's `String` never releases its buffer on assignment — not by any route.** This is
  the part that matters, and it is worth stating precisely because the obvious fixes all fail.
  Checked against the real `cores/esp32/WString.cpp`:
  - `s = ""` → `copy()` → `reserve()`, which **early-returns** when the capacity is already
    big enough. Frees nothing.
  - `s = someEmptyString` → the same `copy()` path. Frees nothing.
  - `s = String()` (rvalue) → `move()`, which explicitly **keeps the destination's buffer**
    when `capacity() >= rhs.len()` — the header even documents this as the intended use case
    after a `reserve()`. Frees nothing.
  - `reserve()` cannot shrink; it only ever grows.

  Measured against the real implementation, each of those frees **0 bytes** of a 6 KB buffer.
  The only thing that calls `invalidate()` → `free()` is **destroying the object**, so the
  release path now explicitly destructs and placement-news the strings. That frees the whole
  buffer, is idempotent, and leaves the object usable. Verified: 0 bytes held after a runaway
  run, stable across repeated runs.

## Keyboard: `h` and `b` no longer stolen on screens that type

`h` (Help) and `b` (screenshot) are global hotkeys, suppressed on screens where letters are
input. The Tiny BASIC editor, the scientific calculator and the LoTW passphrase prompt were all
added without being added to that exclusion list, so **`h` opened Help instead of typing** —
`PRINT "HI"` was impossible to enter.

- Those three screens now type `h` and `b` normally.
- On **every** screen that claims bare letters — the three above, the two text editors, and the
  Tools / DXCC / character-lookup type-to-search lists — the globals are reachable as **`Fn`+`h`**
  and **`Fn`+`b`**. This also closes a gap that predates 0.9.56: the Tools list and the two
  lookup screens previously had **no way to take a screenshot at all**. Every screen can now be
  screenshotted. (`Fn`+`shift`+`b` still types a capital B.)

## Keyboard: emergency stop no longer fires while editing

`Fn`+Back is the global emergency stop (disengages radio and rotator from any screen). It was
excluded in the two text editors, where Back is backspace — but not in the **Tiny BASIC editor**
or the **scientific calculator**, which also use Back as backspace. In those, `Fn`+DEL silently
disengaged the rig and parked the rotator instead of deleting a character — potentially
mid-pass. Both are now excluded, driven off one shared predicate so the list cannot drift from
the editors again.

## Orbital-analysis report now includes LTAN

The Nodal page shows **LTAN** (local solar time of the ascending node), but the printed orbit
report omitted it — the only member of the nodal family (node drift, apsidal drift,
sun-synchronous flag, repeat track) that was missing.

It is now printed, with a wrinkle worth stating: unlike its neighbours, LTAN is **not** a pure
property of the orbit. It is `(RAAN − RA_sun)/15 + 12`, so it moves at `(node drift − 0.9856)/15`
hours per day. On a sun-synchronous orbit that is ~0 by design — a genuinely fixed property — but
on other orbits it sweeps fast (the ISS runs about **−24 min/day**), and a bare figure on paper
would be badly wrong within a week. So the report prints the **drift rate alongside it and the
UTC timestamp it was computed at**, keeping the "permanent record" honest rather than dropping
a value that silently rots.

---

# New

## BASIC programs can read the system

Tiny BASIC programs can now read the data the rest of CardSat already has — satellite geometry,
next-pass timings, sun/moon position, space weather, terrestrial weather, your position, UTC, and
device state — as read-only bare names:

```basic
10 IF SATEL < 0 THEN 60
20 PRINT "AZ ";INT(SATAZ);" EL ";INT(SATEL)
30 PRINT "AOS in ";INT(AOSIN);" min"
50 END
60 PRINT "Below the horizon"
```

**Memory cost: zero permanent bytes.** The snapshot lives inside the interpreter's own state,
which is heap-allocated per run and freed when the program stops.

**Two design decisions worth stating.** First, the values are a **snapshot taken once when you
press `Fn`+`r`**, not live reads. A BASIC program runs to completion inside one key handler and
never yields, with a 500,000-statement budget — if a bare name could trigger SGP4, `10 PRINT
SATEL / 20 GOTO 10` would call it half a million times and trip the watchdog. Snapshotting costs
exactly one call per run regardless. Values are frozen for the run, which is no loss: a run lasts
milliseconds.

Second, **missing data halts the run rather than returning a number**. The firmware signals "no
data" with −1 and −999, and BASIC has no NULL — so `IF WXTEMP < 0 THEN PRINT "FREEZING"` would
print FREEZING when no weather was ever fetched, and an unset `SATAZ` reading 0 would say "point
due North", a perfectly plausible bearing. Instead you get `no weather data` / `no active
satellite`. For programs that would rather branch than stop, `SATOK` / `TIMEOK` / `POSOK` /
`WXOK` / `SPWXOK` / `PASSOK` test availability without erroring.

## Nine more printable reports

Printing had real gaps — **EME had none at all**, despite computing more paper-worthy content
than several things that already printed.

- **EME / moonbounce** (`Fn`+`p` — a bare `p` is the 30-day plan) — Moon az/el, range and rate,
  path degradation, sky noise, and self-echo Doppler for all five bands.
- **EME 30-day plan** (`p`) — declination and degradation per day.
- **EME mutual Moon** (`Fn`+`p` in the sub-view) — the common-window list against a DX grid.
- **QRZ lookup result** (`p`).
- **Station readiness** (`p`) — the pre-operating checklist.
- **Awards** (`p`) — totals plus the per-satellite tally.
- **Workable US states** and **Workable DXCC** (`p`) — the **entity lists**. The counts already
  reached paper inside other reports; *which* entities are workable never did, and that is the
  part a rover actually needs.
- **Visible passes** (`p`) — 10 days of optically-visible passes with rise direction.

All nine also appear in the About → Print menu, now 28 reports. Every line is width-checked
against 58 mm (32-col) paper.

## On-device Help: corrected and filled in

An audit against the actual key handlers found the Help screen was **telling users the wrong
key**: the Satellites topic said "ENT toggle favorite" when ENTER opens Passes and `f` is the
favorite toggle. That is fixed, and the topic now lists all sixteen of that screen's keys
instead of six.

Also added or corrected: a **PRINTING topic** (there was none, despite 28 printable reports), an
**EME topic**, `p`/`a`/`c` on About (printing was invisible there), five undocumented keys on
Next Passes, `e` on Sun/Moon, `p` on Space Wx and QRZ, and `f`/`N`/`i`/`q` on Track. The stale
"Tools hub (26 tools)" now reads 35.

---

# Verification status

- **Host-validated**: the LTAN block was extracted verbatim from the final firmware source and
  run against six cases (ISS-like, sun-synchronous, polar, AO-7-like, a midnight wraparound, and
  clock-not-set), then cross-checked **to the minute** against an independent NOAA-formulation
  solar-position implementation. Its printed width is ≤31 columns, so it does not wrap on 58 mm
  receipt paper. The keyboard predicate was checked exhaustively across every screen class ×
  {`h`, `H`, `b`, `B`} × {bare, `Fn`} for four invariants: every screen can screenshot, letter-
  claiming screens keep their letters, capitals stay typeable, and Help cannot re-enter itself.
  The `String` release semantics were confirmed against the **real**
  `cores/esp32/WString.cpp`, compiled and measured on the host: every assignment form
  (`= ""`, `= emptyString`, `= String()`) frees 0 bytes of a 6 KB buffer, and only
  destruct + placement-new releases it. An earlier fix in this cycle was written against a
  hand-written *model* of `String` that got this wrong, which is why it did not work.
- **Bench-confirmed**: compilation (`arduino-cli`), and — the one that matters — **the heap fix
  against the reported failure on real hardware**. A runaway program no longer strands its output
  buffer; the largest contiguous block returns to its pre-run value on leaving BASIC, and the
  LoTW upload that this bug was breaking now succeeds.
- **Needs bench confirmation**: the `Fn`+`h` / `Fn`+`b` bindings across the letter-consuming
  screens, an actual printed orbit report showing the LTAN line, and the nine new reports on
  paper (widths are checked against 32-col stock on the host, but the printer is the authority).
  See **[THINGS_TO_VERIFY.md](../THINGS_TO_VERIFY.md)** for the full list.
