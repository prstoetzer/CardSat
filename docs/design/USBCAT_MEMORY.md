# USB CAT Memory Analysis (0.9.58-wip, EspUsbHost v2.3.0)

Method: struct-walk of the v2.3.0 header with a recursive member-size script
(flat sums; padding adds a few percent), grep-verified constants, and the
arduino-esp32 3.2.1 sources for what the console teardown returns. Items marked
**estimate** cannot be measured off-device; the SD stage log now prints
`heap=` / `largest=` on every line, so the first bench engage measures them.

> **fix38 — the loop-task watchdog is gone.** Bench: `reset reason=6 (task-wdt), task=IDLE1`
> while uploading to LoTW with the radio engaged. `enableLoopWDT()` was armed at engage to catch
> a `begin()` that never returned — but loopTask feeds the TWDT only ONCE PER `loop()` PASS, so it
> was really asserting "no `loop()` pass may exceed the 5 s timeout." `Net::postFile` allows a
> 30 s no-progress budget inside a single pass (six times the timeout), and its `delay()`/`yield()`
> hand off to the scheduler without ever feeding the watchdog. IDLE1 starved and the board reset —
> while doing exactly what the operator asked. The span it was meant to protect is already watched
> precisely by `armFreezeWatchdog()`, a TWDT **user** subscription scoped to `begin()`'s risky code
> and disarmed on every exit (verified mechanically). A user subscription watches a SPAN OF CODE,
> not a task, so it still catches a `begin()` that hangs forever — with the RTC breadcrumb intact —
> without ever second-guessing a healthy long `loop()`. Net: the freeze protection is kept, the
> false positives are gone.
>
> **fix37 — the teardown question is CLOSED: the host stays resident.**
> Eight revisions tried to release the IDF host stack on disengage. It cannot be done from
> outside EspUsbHost v2.3.0. The bench diag that ended it:
> `clients=1 devices=1 polls=160 flags=0x00 freeAll=259` — the client was never deregistered
> and its device never closed, so `usb_host_device_free_all()` refused (it needs
> `num_clients == 0`) and `usb_host_uninstall()` refused after it. 160 event polls saw **no
> flags at all**: there was nothing to drain, and every event-flag theory was wrong.
> The cause is the library's own `end()` ordering: it kills the **client task** first, then lets
> the daemon run a cleanup (`releaseInterfaces` / `usb_host_device_close` /
> `usb_host_client_deregister`) whose every call is client-scoped and needs the client's event
> queue serviced — and that queue is already dead. `clientHandle_` is private and there is no
> public release hook, so we cannot drive it ourselves either.
> **So `end()` now detaches the CDC port and leaves the host running.** A re-engage rebinds it:
> fast, no allocation, no enumeration wait, and `usb_host_install()` runs exactly once per boot,
> which makes 259 structurally unreachable. Cost: ~11.8 KB held from the first engage until
> reboot, and the USB CDC console does not return. Both are bounded and visible; the
> destroy-and-recreate alternative produced, in order, a heap corruption, an IDLE0 panic, a
> use-after-free in the rig layer, and a permanently wedged stack.
> The retired notes below are kept for the mechanisms they document (the reap ordering, the
> byte/word units, the `INVALID_STATE` ambiguity) — all still true, none of them the cause.
>
> **Superseded in part (0.9.58-wip fix29):** three findings below have moved.
> (1) *Teardown.* The resident-host workaround ("~45 KB stays allocated until reboot",
> "console stays down until reboot") is retired. Root cause, from the v2.3.0 source:
> `taskLoop()` parks in `usb_host_lib_handle_events(portMAX_DELAY)` and never observes
> `running_ = false`, so the library's 200 ms wait force-killed it before its own cleanup
> could run — THAT is why `end: host stop` freed zero bytes. `UsbSerial::end()` now arms a
> 30 ms esp_timer poke that calls `usb_host_lib_unblock()` *after* `running_` flips; the
> daemon wakes, runs its complete exit path (`usb_host_uninstall()` included), self-deletes,
> the objects are then deleted, and the console is restored. Disengage returns the whole
> ~46 KB. If a task still refuses to exit, end() falls back to the old resident behavior
> and refuses re-engage — a bounded leak, never corruption.
> (2) *Lever 2 is no longer PlatformIO-only — **bench-confirmed**.* The Arduino IDE's
> sketch-folder `build_opt.h` is passed to every translation unit, libraries included
> (arduino-esp32 3.2.1 platform.txt); the repo ships `-DESP_USB_HOST_MAX_DEVICES=4`.
> Measured on a full rebuild: the ALLOC-stage heap delta fell from ~20 KB (8 slots) to
> **11,848 B** (4 slots) — ~8 KB saved, and no ODR mismatch. **Requires a full rebuild**
> after adding or editing `build_opt.h`: the IDE's object cache does not watch it, and a
> stale library object compiled at 8 slots against a sketch compiled at 4 is the classic
> ODR corruption this file warns about. PlatformIO does not read `build_opt.h`; use
> `build_flags` there.
> (3) The fix28 disengage panic was `xTaskGetHandle("EspUsbHostClient")`: 16 chars trips
> `configASSERT(strlen(name) < configMAX_TASK_NAME_LEN)`, and FreeRTOS stores task names
> truncated to 15 anyway. Headroom lookups now truncate to the stored form.
> Bench-pending: console re-attach after teardown, and END-stage heap deltas confirming
> the reclaim.

## The design invariant

**USB CAT costs ~0 bytes at rest.** Everything below exists only between engage
and disengage: the host object and CDC port are heap-allocated in
`UsbSerial::begin()` and freed in `end()`; the two library tasks and the IDF host
stack live and die with them. Compiled in but idle, the feature is ~140 B of
`.bss` and flash.

## At rest (compiled in, not engaged)

| Item | Size | Where |
|---|---|---|
| `s_err[64]` + `s_dev[48]` + pointers/flags | ~140 B | `.bss` |
| Stage breadcrumb (`s_rtcStage` + magic) | 5 B | **RTC slow RAM** — separate 8 KB domain, zero DRAM |
| Code | flash only | (Paul's build: 92% flash with USBCAT=1) |

## While engaged (heap, transient)

| Item | Size | Basis |
|---|---|---|
| `EspUsbHost` object — one contiguous alloc | **~18–19 KB** | measured from header: `devices_[8]` ≈ 13.9 KB (DeviceState ≈ 1,742 B/slot, dominated by the 512 B vendor RX buffer per slot) + `endpoints_[16]` ≈ 2.8 KB (class-level, **not** per-device) + callbacks/hub bookkeeping ~1–1.5 KB |
| `EspUsbHostCdcSerial` | ~0.6 KB | 512 B RX ring + ~40 B state |
| Task stacks: `EspUsbHost` + `EspUsbHostClient` | **16.4 KB** | `taskStackSize` = 8192, used for **both** tasks (verified at both `xTaskCreate` sites) + 2 TCBs ~0.7 KB |
| IDF `usb_host_install()` internals (host lib, HCD, device object, pipes) | **~5–8 KB, estimate** | not measurable off-device — read it from the log's engage-time `heap=` delta |
| Per-write OUT transfer (`sendSerial`) | ~200 B, lifetime = ms | `max(len, 64)` + IDF URB overhead; freed by completion callback |
| **Console giveback** (HWCDC rings + event loop freed by `Serial.end()`) | **−~0.7 KB** | 256 B RX + 256 B TX defaults + loop handle |

**Net engaged footprint: ~40–44 KB** with the library-default 8 slots.
With the optional PlatformIO `-DESP_USB_HOST_MAX_DEVICES=4` (global flag): the
object drops to ~12 KB → **~34–37 KB total**.

## MEASURED (bench log + link map, 2026-07-16)

The estimates below were confirmed. Numbers from `CardSat_ino.map` and the SD
stage log's `heap=` column, engage on an IC-821 at 9600:

| Step | Δheap | What | vs estimate |
|---|---|---|---|
| `allocating` → `registering` | **−20,040 B** | `new EspUsbHost` + `new CdcSerial` | walk said ~19.6 KB — **±2%** |
| `closing serial console` | −32 B net | rings freed, log line allocates | — |
| `starting` → `waiting` | **−20,596 B** | `usb_host_install()` + **2 × 8 KB task stacks** | est. 22–24 KB; stacks dominate, IDF internals only ~4 KB |
| `waiting` → `bind` | **−6,180 B** | CDC endpoint buffers + IN transfer | **new**: allocated at bind, not inside the object |
| **Total engage** | **−45,956 B = 44.9 KB** | | est. 40–44 KB — **held** |

One correction to the table below: the per-slot 512 B vendor RX buffers are *not*
all resident in the object at construction; the object is ~20 KB and the
per-device endpoint buffers arrive later, at bind (the 6.2 KB step).

## DECISION: the host stays resident (0.9.58-wip)

`EspUsbHost::end()` **frees zero bytes** — reproduced in four consecutive bench
logs, with no exceptions. Three fixes were attempted and all three failed:

| attempt | theory | outcome |
|---|---|---|
| wait after delete | idle-task reaping latency | +20 KB still arrived during `consoleUp()` |
| wait before delete (600 ms) | same, earlier | capped out; heap never came back |
| `usb_host_lib_unblock()` poker | lib task stuck on `portMAX_DELAY` | no measurable change |

The hoist theory died too: the compiled `end()` (0x4210decc, 122 B) *does* contain
loads inside its wait loops. And the client task uses a **5 ms** timeout, not
`portMAX_DELAY`, so it should exit promptly — yet `end()`'s waits still run full.
Four theories, four wrong. I do not know why `end()` fails, and further guessing
costs bench cycles.

**So we stop depending on it.** The host object and its two tasks now stay resident
for the life of the firmware once USB CAT is first used. Disengage detaches only
the CDC port — all the radio needs. Re-engage rebinds it. **Nothing is ever
deleted, so nothing can be used after free.**

| | destroy/recreate (old) | resident (new) |
|---|---|---|
| re-engage | reboot, SD+WiFi dead, 2nd reboot to recover | works, ~250 ms |
| heap after disengage | 54 KB but **corrupted** | ~17 KB, sound |
| console after disengage | restored (briefly, then chaos) | stays down until reboot |
| leak per cycle | grows | **zero** (verified: 22 cycles, 1 allocation) |

The cost is honest: ~45 KB stays allocated until reboot. That is the *same* memory
the engaged state already uses, so nothing that works while engaged breaks. The
console stays down because the resident host still owns the S3's single USB PHY —
handing back a dead `Serial` would be a lie (and `serviceSerialCli()` would poll
it; the −1 sentinel that froze this firmware for a session is guarded now, but
still). **A leak you can predict beats corruption you cannot.**

One path still tears down fully: a host that *never started* holds no PHY, so that
failure deletes the objects and restores the console. A host that started but found
no device stays resident, so plugging the adapter in and retrying rebinds in ms.

*If a future EspUsbHost fixes `end()`, revert this. The one-line test: does
`end: host stop` free ~20 KB? Today it frees 0.*

## Can we free enough for voice memos and pass alarms?

Engage costs **45,956 B** (measured). Where it goes, and what is actually
recoverable:

| Item | Cost | Recoverable? |
|---|---|---|
| Two host task stacks (`taskStackSize` = 8192 **each**) | 16,384 B | **Yes — runtime config field.** Works in the Arduino IDE. Size it from measurement. |
| `EspUsbHost` object: `devices_[8]` @ ~1,742 B/slot | ~13,900 B | **Partly — `-DESP_USB_HOST_MAX_DEVICES` only.** `=2` saves ~10.5 KB. **PlatformIO only** (see below). |
| IDF `usb_host_install()` internals | ~4,000 B | No |
| CDC endpoint buffers + IN transfer (at bind) | ~6,200 B | No |
| Object's non-slot members | ~6,100 B | No — `MAX_ENDPOINTS`, `MAX_INTERFACES`, `VENDOR_RX_BUFFER_SIZE` etc. are hard `static constexpr`, **not** `#ifndef`-overridable. Only `ESP_USB_HOST_MAX_DEVICES` has a guard. |

Targets: **speaker/I2S ≈ 8 KB** (pass alarms), **voice memo** = speaker + mic +
its record buffer (more). Today's post-engage state is ~17 KB free / **~7.2 KB
largest block** — alarms fail on the largest-block wall, not the total.

### Lever 1 — task stacks (Arduino IDE, no flags) — **do this first**

`EspUsbHostConfig::taskStackSize` is a runtime field, so it needs no build flag
and carries none of the ODR risk that the per-file `MAX_DEVICES` define caused at
the start of 0.9.58-wip. 8192 is the library author's default, **not a measured
requirement**. Both stacks are now measured at every disengage and written to
`/CardSat/usbcat.log`:

```
## stack of 8192 each: EspUsbHost used=NNNN free=NNNN | Client used=NNNN free=NNNN
## -> safe taskStackSize ~= used + 2048, rounded up; frees 2x the cut
```

If the tasks use ~3 KB (plausible: both are event loops), then 4096+2048 ≈ 6 KB
would still be generous and **frees ~4–8 KB — enough for alarms**. Set it from the
log; never guess. A too-small stack trades a clean OOM for a stack overflow.

### Lever 2 — `-DESP_USB_HOST_MAX_DEVICES=2` (PlatformIO only) — **~10.5 KB**

| slots | object | saves |
|---|---|---|
| 8 (default) | ~20,040 B | — |
| 4 | ~13,072 B | 6,968 B |
| 2 | ~9,588 B | **10,452 B** |

`=2` (not 1: a hub is itself a device) is the sweet spot. **This must be a GLOBAL
build flag** — the slot array is a member of `EspUsbHost`, so a per-file define
diverges `sizeof()` between translation units and corrupts memory. That is
precisely the ODR bug that opened this whole debugging session. The Arduino IDE
has no global-flag mechanism, so **this lever is unavailable to the .ino build.**

### Combined

Levers 1 + 2 together plausibly free **~15–18 KB**, taking engage from ~46 KB to
~28–31 KB and post-engage free heap from ~17 KB to ~32–35 KB. That comfortably
clears the speaker's 8 KB — *if* the largest **contiguous** block follows, which
is the open question: the win must land as one span, not scattered.

### Recommendation

1. Read the stack numbers from the next disengage log. Free, no risk, IDE-native.
2. Set `taskStackSize` accordingly and re-measure `largest=` after engage.
3. If alarms still will not fit and you want them badly enough, that is a reason
   to move the primary build to PlatformIO — where lever 2 is also available.

Until then the guards stay: audio and TLS refuse cleanly while engaged rather
than failing mysteriously.

## Engaged is a low-memory MODE, not just a cost (bench-confirmed)

USB CAT engaged leaves **~17 KB free with a ~7,156 B largest block**. That is not
enough for two things the firmware otherwise does freely, and both now refuse
rather than fail:

| Subsystem | Needs | While engaged |
|---|---|---|
| Speaker / I2S | ~8 KB | `audioAcquire()` returns early — beeps are silently skipped, audio returns on disengage |
| TLS (BearSSL handshake) | several KB **contiguous** | `Net::tlsHeapTooLow()` (largest block < 16 KB) refuses with `"low heap (USB CAT on?)"` |

Both guards sit at a single chokepoint rather than per-call-site, and both fail
*early* — a handshake that fails after allocating is the worst outcome on an
already-fragmented heap. This makes "engaged" an explicit mode with documented
limits, instead of a state where unrelated features mysteriously break.

## SUPERSEDED: the teardown "leak" was task-reaping latency

**Measured** (bench, engage→tick→disengage, `usbcat.log`):

| step | heap | Δ | largest |
|---|---|---|---|
| engage header | 63,204 | | 31,732 |
| `engaged` | 17,032 | −46,172 | 7,156 |
| `end: cdc detach` | 17,056 | +32 | 7,156 |
| `end: host stop` | 17,064 | **+8** | 7,156 |
| `end: delete objects` | 35,232 | **+18,168** | 17,396 |
| `end: console up` | 55,376 | **+20,144** | 17,396 |
| `end: done` | 54,464 | −912 | 17,396 |

Two facts jump out:

1. **`host->end()` frees essentially nothing (+8 B).** All the recovery happens
   later — 18 KB at `delete` (our objects) and then **another 20 KB during
   `consoleUp()`**, a call that should *cost* memory, not free it.
2. That +20 KB is the two 8 KB task stacks and their TCBs. `vTaskDelete()` only
   *marks* a task dead; FreeRTOS frees the stack and TCB **from the idle task of
   the core the task ran on** — IDLE0, because `begin()` pins the host tasks to
   core 0. `end()` runs on loopTask (core 1), so the memory returns only once
   loopTask yields and IDLE0 is scheduled. It was arriving *during the next call*
   purely by luck of timing.

So the ~8.7 KB still missing at `end: done`, and the 18 KB largest-block, were
**reaping latency, not a leak** — `end()` was returning before the heap settled.
Since the host object needs ~20 KB contiguous, an immediate re-engage could fail
for no real reason.

**Fix:** `end()` now yields (`delay(1)`) until free heap stops changing, capped at
40 ms, before restoring the console. `delay()` is exactly what IDLE0 needs to run.
40 ms is far more than reaping two tasks takes and is invisible against the ~4 s an
engage cycle spans.

**Confirm on the next run:** `end: done` free heap should now come back to within
~1 KB of the engage header (the residual is the SD `File` churn and the status
`String`), and `largest` should return to ~31 KB. If it does not, *then* there is a
real leak — and `end: host stop` is where to look, since the library's 200 ms wait
expiring would mean `vTaskDelete()` killed a task before its cleanup ran.

## Superseded: largest free block 18 KB after disengage

USB CAT engaged and drove an IC-821 over a USB↔CI-V adapter. On **disengage**,
the largest free block came back at ~18 KB against 31.7 KB before the first
engage. The host object needs ~20 KB **contiguous**, so a re-engage fails.

Two candidates, and `largest=` alone cannot separate them — **total free heap**
is the discriminator, which is why the log prints both:

- **Leak** — total free does *not* return to ~63 KB ⇒ something was never freed.
  The teardown is now instrumented (`end: cdc detach` / `host stop` /
  `delete objects` / `console up` / `done`), so the log names the step.
- **Fragmentation** — total free *does* return to ~63 KB, but the old 31.7 KB
  span now has something sitting in it. `consoleUp()` → `Serial.begin()`
  reallocates the HWCDC rx/tx rings (256 B each) *after* the big free, and the
  allocator may place them mid-span. Small, long-lived, badly placed: a large
  drop in `largest` with nothing leaked.

Library teardown reads **correct** in v2.3.0 and is not currently suspected: both
tasks exit on `running_ = false`, and the lib task's exit path calls
`releaseAllEndpoints(true)`, `releaseInterfaces()`, `usb_host_device_close()`,
`usb_host_client_deregister()`, `usb_host_device_free_all()` and
`usb_host_uninstall()` before self-deleting. The residual risk is the 200 ms wait
in `EspUsbHost::end()`: if it expires, `vTaskDelete()` kills a task *before* that
cleanup, which would orphan everything. If the log shows the heap failing to
return at **`end: host stop`**, that is the upstream bug — report it.

### Bench: one measurement settles it

Engage → disengage → engage again, then read `/CardSat/usbcat.log`:

1. Compare `heap=` at `end: done` against the engage header's `heap=`.
   **Equal ⇒ fragmentation. Lower ⇒ leak, and the step where it stops
   recovering names the culprit.**
2. The second engage's header shows whether the state persists or compounds.

Mitigations already in place: `end()` now releases the TWDT user subscription
unconditionally, and a failed re-engage reports **"USB: heap too fragmented
(NNK free, NNK max)"** rather than a generic OOM — different message, different
fix, and it is visible without a console.

## The real constraint is static, not the engage

From the link map (`dram0_0_seg` = 333.8 KB total):

| | KB |
|---|---|
| `.dram0.dummy` (reserved, mirrors IRAM — **not usable**) | 66.5 |
| `.dram0.data` | 21.6 |
| `.dram0.bss` | **141.5** |
| **Contiguous DRAM left for the heap at link time** | **104.2** |
| Observed free heap pre-engage | ~63 |
| ⇒ runtime allocations before engage (WiFi/lwIP/caches/SD) | ~41 |

And **87.5 KB of that `.bss` is one symbol: the global `App` object** (`_ZL3app`)
— 73% of all attributed static DRAM in the build. That single fact, not USB CAT,
is what makes the heap tight: engage needs 44.9 KB out of ~63 KB free, leaving
the **7,156 B largest-free-block** the log reports.

Nothing to fix today — the engage works and the margin is real — but it is the
number to watch. Anything that grows `App` shrinks the USB CAT margin one-for-one,
and the ~20 KB contiguous allocation for the host object is the first thing that
will fail. If it ever does, the cheapest lever is the **task stacks** (16.4 KB of
the 44.9 KB): `EspUsbHostConfig::taskStackSize` is settable, and 8 KB × 2 is the
library's default, not a measured requirement.

## Against the board

Total DRAM 320 KB; the current build statically uses ~165 KB (50.3%). Runtime
free heap after WiFi + caches is the operating number the project has long
treated as **~55 KB** — so an engage consumes roughly **three-quarters of typical
free heap**. Workable, with three sharp edges:

1. **The single ~19 KB contiguous alloc is the fragility point**, not the total.
   A fragmented heap fails it long before exhaustion. The log's `largest=` column
   is the diagnostic: engage failing with `largest << heap` is fragmentation;
   `heap` itself low is genuine pressure. Failure is already clean
   (`"Out of RAM for USB host"`, console restored, no partial state — the
   `end()`-not-gated-on-`s_active` fix).
2. **Do not engage mid-TLS.** A LoTW/TLS burst wants ~40 KB of its own; the two
   do not fit together. The existing `Net::onTlsBusy` hook is the natural place
   to defer an engage if this ever bites (design note only — not implemented).
3. **Stack, not heap:** `onStage` → trampoline → `setStatus` + `draw()` runs
   *inside* `begin()` on loopTask (8 KB stack). The canvas is a static sprite so
   `draw()`'s frame is modest; watch item, not a problem.

## Dual-USB future (ROTATOR_TRANSPORTS.md)

Slot count does not change with a second adapter — the 8 (or 4) slots are
pre-sized. Adds: one more `CdcSerial` (~0.6 KB), one more persistent IN transfer
(~0.2 KB), hub bookkeeping already inside the object. **Dual-USB memory delta is
< 1 KB**; the real Phase-3 gate is VBUS, not RAM.

## Bench instructions

Engage once and read `/CardSat/usbcat.log`: the header line has pre-engage
`heap`/`largest`; each stage line tracks the drop. `starting USB host` →
`waiting for device` brackets the IDF install + task creation;
`bind:` lines bracket the CDC costs. Disengage and re-engage once more — the
second attempt's header shows whether teardown returned everything (leak check).

---
*Numbers derive from EspUsbHost v2.3.0 and arduino-esp32 3.2.1 sources; re-derive
after any library upgrade.*
