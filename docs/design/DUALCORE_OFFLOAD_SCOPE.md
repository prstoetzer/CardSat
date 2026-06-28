# Scope: Dual-Core Offload of Radio / Rotator / Doppler Work

**Status: design + feasibility assessment only — NOT implemented, and not currently
recommended.** This document evaluates moving the time-critical control work (CAT
radio I/O, rotator pointing, Doppler computation) off the Arduino `loopTask` onto the
ESP32-S3's second core. The conclusion: it is *feasible* but is **not a "move these
functions to a task" change** — done naively it races on a large body of unsynchronized
shared state and risks a tracking engine that currently works correctly. There is also
**no observed problem today** motivating it. This is written so that *if* responsiveness
issues surface during wider user testing (slow CI-V rigs are the likely trigger), the
analysis and a safe staged plan already exist.

> Written at CardSat **v0.9.37**. Line numbers are approximate and will drift; trust the
> code over this memo. The architecture it describes changes slowly.

---

## 1. Motivation (and why it's speculative)

CardSat is, today, a **single-threaded Arduino sketch**: `loop()` calls `app.loop()`,
which runs on the default `loopTask` (core 1). There is no `xTaskCreatePinnedToCore`,
no FreeRTOS queue, and no mutex anywhere in the codebase. Everything — keyboard,
drawing, CAT, rotator, Doppler, WiFi servicing — is cooperatively interleaved in one
pass of `App::loop()`.

The hypothetical benefit of a second core is **UI responsiveness while a slow CAT
transaction is in flight**. The concern is latency, not throughput. But to be explicit:

> **No user has reported a responsiveness problem.** This is pre-emptive analysis. The
> single-core design has been carefully tuned (write deadbands, deferred uplink, ~1 Hz
> rotator updates) and works on real hardware including the IC-821H bench rig. Do not
> spend the risk budget here until there is a measured problem.

The likely future trigger is a user on a **slow CI-V radio** (high `catDelayMs`)
running **One True Rule**, where one Doppler tick performs a read-back plus two writes
and can block the loop long enough to make the screen and keys feel sticky.

---

## 2. What is actually expensive (measured against the source)

A common misconception is that the *Doppler math* is the heavy part. It is not.

### 2.1 The Doppler math is trivially cheap
`Predictor::dopplerFreqs()` / `passbandFreqs()` are a handful of double multiplies and
one `llround` each (see `predict.cpp`). Sub-microsecond. Moving *this* to another core
buys nothing.

### 2.2 The real cost #1 — blocking serial I/O
This is the latency that would actually justify a second core. In `civ.cpp`, every CAT
command is `write → flush → drainEcho()`:

- `drainEcho()` busy-waits up to **25 ms** ("echo seen, radio not replying") and up to
  its `timeoutMs` otherwise, calling `delay(1)` in the loop.
- After each command there is `delay(cmdDelayMs)` — **`catDelayMs` defaults to 70 ms**
  (`settings.h`), and the pending IC-821H tuning calls for it to be *higher*.

One True Rule (app.cpp ~3482–3531) does, per tick: a downlink **read-back**, a downlink
**write**, then a deferred uplink **write**. That is potentially **three CAT
transactions**, so a single Doppler service pass can occupy the loop for **~100–200 ms**.
During that window, keyboard polling and `draw()` do not run. *That* is the stickiness a
second core would hide.

`rig.cpp` (Yaesu/Kenwood ASCII CAT) has the same shape: `delay(2)` per character-wait,
drain loops with 20–50 ms windows.

### 2.3 The real cost #2 — stateful SGP4 propagation
`Predictor::look()` calls `_sat.findsat()`, and `rangeRateAt()` calls `sgp4(...)`. These
run the full SGP4 model. On the S3 this is fast (well under a millisecond) but it is
**CPU-bound** and, critically, **stateful** — `findsat()` writes its results into the
`_sat` object's member fields (`satAz`, `satEl`, `satDist`, …). See §3.1.

The rotator path additionally does **bursty** propagation: the LOS forward-scan
(app.cpp ~3567) calls `azelAt()` **up to 120 times** in one loop iteration, and
`predictPasses()` (for pre-positioning) is heavier still. These run at ~1 Hz, but when
they run they are the longest pure-compute stretch in the loop.

**Summary:** the candidate work to offload is the **blocking CAT/rotator transactions**
(I/O-bound) and the **SGP4 propagation** (CPU-bound, non-reentrant). The Doppler
arithmetic is incidental.

---

## 3. The hard blocker: shared mutable state with zero synchronization

Because the code can't currently run concurrently, the control path and the UI path
freely share mutable state with no protection. A second core turns each of these into a
data race. This is the central reason the change is not low-risk.

### 3.1 The `Predictor` is not reentrant — and is shared three ways
There is **one** `pred` instance. It is used by:
- the **Doppler** path (`pred.look()`, `pred.rangeRateAt()`),
- the **rotator** path (`pred.look()`, `pred.azelAt()`, `pred.predictPasses()`),
- the **UI** (sky map, globe, pass lists — app.cpp ~2038–2118).

`findsat()` mutates `_sat`. If a worker core propagates for Doppler while the UI core
calls `pred.look()` to render the globe, they **clobber each other's results** in the
shared `_sat` object. The underlying Sgp4 library is stateful and not reentrant.

*Mitigation:* give the worker its **own second `Predictor` instance** (its own `_sat`),
fed the same TLE. Two independent propagators do not share state. This costs a second
satrec's worth of RAM (small) — far cheaper than locking every `pred` call site.

### 3.2 `pbOffset` — 17 writers, written from both candidate threads
The passband offset is written by the **knob-follow logic** (worker-side in a split) and
by **keyboard tuning** (UI-side). A torn read or lost update here **directly mistunes the
radio**. There is no atomic or barrier today.

### 3.3 Transponder state — ~70 touch points, reallocated under the reader
`activeTx[]`, `curTx`, `activeTxCount` are read every Doppler tick (`activeTx[curTx]`) and
are **wholesale rebuilt by `buildSatView()`** when the user changes satellite or edits a
transponder. A worker reading `activeTx[curTx]` while the UI calls `buildSatView()` is a
**use-after-realloc**.

### 3.4 `rig` / `rot` objects are deleted and recreated at runtime
`applyRadioFromCfg()` does `delete rig; rig = new …` and `applyRotatorFromCfg()` does the
same for `rot`, whenever the user changes a radio/rotator setting (app.cpp ~258, ~274). A
worker calling `rig->service()` or a CAT write while the UI core deletes `rig` is a
**guaranteed crash** (use-after-free on a vtable).

### 3.5 Calibration and mode
`calDl`/`calUl` (8 writers) and `tuneMode` are read mid-computation on one side and
written from Settings on the other. Lower-stakes than the above, but still unsynchronized.

---

## 4. Platform constraints specific to this build

- **No PSRAM.** A worker task needs its own stack (SGP4 + CI-V buffers ⇒ budget ~8 KB),
  plus the inter-core queues. That comes out of the **same tight DRAM** that already
  forced `MAX_SATS` and `LOG_VIEW_MAX` reductions. The memory cost is real and must be
  budgeted, not assumed free.
- **The M5Cardputer display/canvas is not thread-safe.** `draw()` must stay on the
  loop/UI core. (It already does — drawing only happens in `App::loop()`.) The split must
  keep **all rendering on the UI core**; the worker may never touch the canvas.
- **Core 0 already hosts WiFi/LWIP.** The Arduino `loopTask` is on core 1, so a worker is
  conventionally pinned to **core 0** — where WiFi, the **Icom-LAN net CAT**, and the
  **rigctld/rotctld** TCP servers also live. That argues for keeping anything
  socket-touching where it is and moving only **wired** CAT/rotator + propagation to the
  worker, to avoid fighting the WiFi stack.
- **Watchdog.** A worker that busy-waits on serial must `vTaskDelay()` (not bare `delay`)
  periodically so the task watchdog on core 0 is fed.

---

## 5. Two designs, only one of which is safe

### 5.1 NOT acceptable: wrap the Doppler block in a task + sprinkle mutexes
Wrapping the existing service block in a FreeRTOS task and guarding the shared state with
mutexes fails on its own terms: the biggest cost (§2.2) is the **CAT transaction**, and a
mutex held across that transaction simply **re-serializes** the very thing you tried to
parallelize — the UI core blocks on the lock instead of on the radio. And §3.2–3.5 would
need locks threaded through ~70+ call sites. This path is rejected.

### 5.2 Acceptable: message-passing with snapshots (no shared mutable state)
The only design that is safe without pervasive locking converts every shared-state hazard
into a **copy through a queue**:

- **Worker core (0)** owns the `rig` and `rot` objects **and a dedicated second
  `Predictor`**. It runs the blocking serial transactions and SGP4. It **never touches the
  canvas**.
- **UI core (1)** owns the screen, keyboard, settings, and `buildSatView()`. It **never
  touches `rig`/`rot` directly**.
- **One FreeRTOS queue each direction**, carrying **immutable value-type snapshots**, not
  pointers into shared arrays:
  - **UI → worker (command):** `{ norad, Transponder copy, tuneMode, calDl, calUl,
    pbOffset_at_send, engaged flags }`. A *copy* of the transponder, so a later
    `buildSatView()` can't pull the array out from under the worker.
  - **Worker → UI (result, display-only):** `{ rxHz, txHz, az, el, knob-derived pbOffset,
    rig/rot status }`. The UI uses these only to render; it does not write back into the
    control path.
- **Object lifecycle goes through the queue too:** "rebuild rig/rot" becomes a *command*
  the worker executes on its own core, so the `delete`/`new` (§3.4) never races a
  `service()` call.
- **Knob-follow ownership:** One True Rule's read-back-and-adopt logic lives entirely on
  the worker (it's part of the CAT transaction). The worker reports the resulting
  `pbOffset` to the UI for display; manual key-tuning sends a *new* command snapshot. This
  removes the §3.2 race by giving `pbOffset` a single writer per side, reconciled through
  the queue.

This is more than a refactor — it is a **controlled re-architecture of the control path**.
It should be built **behind a compile-time flag** so the single-core path remains the
shippable default until the dual-core path is proven across **full passes on the IC-821H
bench** (TCA is where any residual race would surface, since that's when writes are most
frequent).

---

## 6. Recommended staging (if/when a problem is observed)

**Step 0 — measure first.** Put a `micros()` delta around the Doppler service block
(app.cpp ~3458–3540) and log the worst-case stall during a One True Rule tick on the
IC-821H. **That single number decides everything below.** If the stall is tens of ms, do
nothing. If it's 150–200 ms and the UI feels sticky, proceed.

**Step 1 — fix the blocking on ONE core (low risk, ~80% of the benefit).** The latency is
the `delay(cmdDelayMs)` and the drain busy-waits, not the lack of a second core. Convert
the CI-V path to a **non-blocking state machine**: queue the write, then poll for
echo/ACK across successive `App::loop()` iterations instead of `delay`-ing inside one.
The loop keeps spinning (keys, draw) while the radio is mid-exchange. This needs **no
concurrency, no second Predictor, no queues** — it stays within the single-threaded model
the codebase is built around, which is the conservative move given how much single-core
invariant work is already banked. This likely resolves the stickiness on its own.

**Step 2 — only if Step 1 is insufficient:** implement §5.2 (message-passing two-core
split with a second Predictor and snapshot queues), behind a compile flag, validated on
the bench before it becomes default.

Do **not** jump to Step 2. Step 1 is cheaper, lower-risk, and probably enough.

---

## 7. Open questions to resolve before any implementation

1. **The measured stall (Step 0).** Unknown from source alone. Decides whether this is
   worth doing at all.
2. **Does Step 1 alone clear it?** A non-blocking CI-V state machine is a meaningful but
   self-contained change; benchmark before committing to two cores.
3. **DRAM headroom for a worker stack + queues** on the no-PSRAM S3, alongside the recent
   memory reductions. Must be budgeted explicitly.
4. **Net-CAT / rigctld / rotctld interaction** if the worker lands on core 0 with the
   WiFi stack — keep socket work on core 1, wired work on the worker.
5. **Rotator backends.** The Yaesu-direct backend (`rot->service()` self-driven on Wire1,
   ADS1115 + PCF8574) has its own timing; confirm it tolerates being driven from a worker
   core, or keep it on the UI core and offload only CAT.

---

## 8. One-paragraph summary for a future reader

CardSat is single-threaded with no synchronization primitives. The expensive thing in the
loop is **blocking CAT serial I/O** (`catDelayMs` + drain waits, up to ~200 ms per One
True Rule tick), not the Doppler math. A second core could hide that latency, but the
control path shares a **non-reentrant Predictor**, `pbOffset`, the transponder arrays, and
the runtime-recreated `rig`/`rot` objects with the UI, none of it locked — so a naive task
split races and a lock-based split just re-serializes the CAT transaction. The safe design
is **message-passing with snapshots and a second Predictor instance**, behind a compile
flag. But there is **no observed problem yet**; the right first move, if one appears, is a
**non-blocking CI-V state machine on the existing single core** (Step 1), which is likely
sufficient and far lower risk than going dual-core.
