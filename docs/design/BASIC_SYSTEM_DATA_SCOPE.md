# Exposing system data to Tiny BASIC — feasibility

*Assessment only. Nothing here is built. Written for 0.9.57+ planning.*

## The question

Can a BASIC program read the satellite/observer/time data the rest of CardSat already has, so
it can do satellite calculations — and what does it cost in RAM?

## Short answer

**Yes, cheaply — about 4 bytes of RAM.** The data already exists in `App`; BASIC just needs a
way to ask for it. The real constraint is **not memory, it's the watchdog**, and it forces one
specific design decision (a per-run snapshot). Details below.

---

## Approach

### The one that works: read-only system variables, resolved by a host callback

The VM's `atom()` already dispatches named tokens (`ABS`, `INT`, `SQR`, `SIN`, `COS`, `RND`).
System values slot in the same way — as bare names rather than functions, since they take no
argument:

```basic
10 REM Is the bird up, and where do I point?
20 IF SATEL < 0 THEN 60
30 PRINT "AZ ";INT(SATAZ);" EL ";INT(SATEL)
40 PRINT "RANGE ";INT(SATRNG);" KM"
50 END
60 PRINT "Below the horizon"
```

`BasicVM` gains **one function pointer** (~4 bytes); `App` answers by name. No new buffers, no
new heap, no per-variable storage. The name table and dispatch are ~600–900 bytes of **flash**,
not RAM.

### Rejected: snapshot a struct into the VM

~150 bytes of VM state, and it buys nothing the callback doesn't — the callback reads the same
data without copying it.

### Rejected: string variables / arrays

Per-string `String` object plus a heap buffer; 26 string vars is 400 bytes of object plus
potentially several KB of churn. **This is precisely the fragmentation that broke LoTW uploads
in 0.9.56** (see the 0.9.57 notes). Not worth it for a pocket BASIC.

---

## The actual constraint: the watchdog, not RAM

This is the finding that matters, and it nearly bites hard.

`basicRun()` runs the whole program **to completion inside a single key handler**. It never
yields to `loop()`. The rest of CardSat is built around the opposite assumption — the planner
and horizon sweeps are *jobbed one pass per `loop()`* precisely because, as the source says, a
full sweep in one tick "starved the watchdog."

The runaway budget is **500,000 statements**. That is fine for arithmetic (~1 s worst case).
It is catastrophic if a statement can trigger SGP4:

| per-statement cost | worst-case run | verdict |
|---|---|---|
| pure arithmetic (~2 µs) | ~1 s | fine |
| SGP4 `look()` (~300 µs) | ~150 s | **watchdog reset** |
| SGP4 `look()` (~1 ms) | ~500 s | **watchdog reset** |

And the program that does it looks completely innocent:

```basic
10 PRINT SATEL
20 GOTO 10
```

That is a guaranteed reset — the same shape as the runaway loop that already caused the
0.9.56 heap bug, but with a worse failure mode.

### The fix: one snapshot per run

Compute the `LiveLook` **once at run start** and serve every read from it. SGP4 is then called
exactly once per run no matter what the program does — the runaway above costs one call, ~1 ms.

The trade-off is that values are **frozen for the duration of a run**. That is a non-loss: a run
lasts milliseconds, so nothing would visibly move anyway. And it is honest to describe — the
snapshot is *"the sky at the moment you pressed `Fn`+`r`"*.

---

## What could be exposed

Everything below already exists in the firmware. Tiered by what it costs to read, because that
turns out to be the deciding factor.

### Tier 0 — free (already sitting in an `App` member)

No computation at all; the value is a member variable. Safe to read per-statement.

| group | names | source |
|---|---|---|
| Station | `MYLAT` `MYLON` `MYALT` | `Observer` |
| Clock | `UTCH` `UTCM` `UTCS` `UTCDAY` `UTCMON` `UTCYR` | `nowUtc()` |
| Elements | `SATINC` `SATECC` `SATRAAN` `SATMM` `SATNOR` | active `SatEntry` |
| Next pass | `AOSIN` `LOSIN` (minutes away), `PASSEL` (peak el), `PASSVIS` | pass table, already computed |
| Space wx | `SFI` (F10.7), `KP`, `AINDEX` | `spaceF107` / `spaceKp` / `spaceA` |
| Weather | `WXTEMP` `WXWIND` `WXDIR` `WXHUM` | Open-Meteo cache |
| Device | `BATT` (%), `GPAGE` (element age, days), `NFAV` | misc |

**The pass table is the sleeper here.** `aos`/`los`/`maxEl`/`visible` are already predicted and
sitting in RAM. "How many minutes until AOS, and how high does it get" is exactly the arithmetic
a field program wants, and it costs nothing.

**Space weather is the other one.** `SFI` and `KP` in a BASIC program is a genuinely nice fit —
propagation rules of thumb are simple arithmetic, and the app already caches the inputs.

### Tier 1 — cheap trig (~microseconds)

Closed-form series, same shape as `pred.look()` but far cheaper. `skyObjAzEl()` gives both.

`SUNAZ` `SUNEL` `MOONAZ` `MOONEL`

### Tier 2 — SGP4 (~0.3–1 ms per call) — snapshot required

From `LiveLook`, one `pred.look()` call (`src/predict.h`):

`SATAZ` `SATEL` `SATRNG` `SATRR` `SATLAT` `SATLON` `SATALT` `SATSUN`

**Derived, wants an argument:** `SATDOP` (Doppler at a given frequency) fits better as a
function than a bare name.

## What should not be exposed

- **Anything that transmits or moves hardware.** No CAT, no rotator, no LoRa. A BASIC bug should
  never key a rig or swing an antenna. Hard line, not a memory question.
- **Anything network.** A runaway would hammer an endpoint, and TLS needs the big contiguous
  block BASIC programs are least able to guarantee.
- **Anything that writes.** Reading is idempotent; writing invites corrupting the log or config
  from a typo'd loop.
- **The QSO log / awards totals.** Tempting — "how many grids have I worked" is a natural
  question — but `qsoCount()` opens the file and allocates a `String` **per line**. In a BASIC
  loop that is both the watchdog problem *and* precisely the heap churn that broke LoTW uploads
  in 0.9.56. Disqualifies itself.
- **`gridN` / `stateN` / `dxccN`.** These look like worked totals but are **footprint** counts
  (what is workable *now*), and they are only populated after the user has visited that screen.
  Exposing them would be actively misleading.
- **Per-satellite catalog iteration.** `FOR I = 1 TO 220 ... SATEL(I)` reintroduces the SGP4
  cost problem in a form the per-run snapshot cannot fix.

---

## Recommended scope, if built

1. **Read-only bare names**, resolved via a host callback (~4 bytes RAM).
2. **One `LiveLook` snapshot per run**, taken in `basicRun()` before the VM starts.
3. **Active satellite only** — the one the user already selected. No catalog iteration.
4. **Sentinels must never leak into BASIC.** This is the sharpest edge in the whole feature and
   the survey made it worse, not better: the app signals "no data" with **`-1`** (`spaceKp`,
   `spaceA`, `wxWindDirNow`, `wxHumidNow`) and **`-999`** (`wxTempNow`, `wxWindNow`). Those are
   *numbers*. BASIC has no NULL and no exceptions, so they would flow straight into arithmetic:

   ```basic
   20 IF WXTEMP < 0 THEN PRINT "FREEZING"
   ```

   With no weather cached, `WXTEMP` is −999 and this prints FREEZING — confidently, and wrong.
   The pointing case is worse: no fix means `SATAZ` reads 0, `PRINT "Turn to "; SATAZ` says
   "Turn to 0", and due North is a perfectly plausible bearing. The user aims at the wrong sky
   and has no idea.

   So: **halt the run with a real error** (`no weather data`, `no active satellite`) the way the
   existing bound checks do, and additionally provide explicit `SATOK` / `TIMEOK` / `WXOK` flags
   for programs that want to branch instead of stop. Erroring is the default because a silent
   plausible number is the worst outcome available.
5. Documented in the manual's Tiny BASIC reference, with the frozen-snapshot semantics stated
   plainly.

**Total RAM cost: ~4 bytes.** Flash: ~1 KB. The 0.9.56 memory work is untouched.

## Effort

Small and self-contained. The parser hook is one `else if` chain in `atom()`; the callback is a
name→value switch in `App`. The genuine work is the **validation**: the host harness pattern
already used for the interpreter can cover the name table, and each exposed value must be checked
against the screen that already displays it, so BASIC and the UI can never disagree.

The one thing I would not skip: a bench test of the runaway case
(`10 PRINT SATEL` / `20 GOTO 10`) confirming the snapshot holds and the watchdog stays quiet.
That is the failure this design exists to prevent.
