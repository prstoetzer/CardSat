# USB code review — CardSat + CardSatUsbHelper (0.9.73, pre-release)

*A fresh systematic read of all four layers — the vendored EspUsbHost patches, the
vendored IDF host stack usage, `src/usbserial.cpp`/`src/usbhelper.cpp`, and the
companion sketch — with particular suspicion of the code written THIS cycle, since
it is the newest and least reviewed. One finding was serious enough to fix before
release; the rest are ranked below.*

---

## F1. Patch 10's stall path raced the HCD — **FIXED before release**

The bounded-OUT-transfer patch added this cycle had a defect in its own recovery
path. On an overdue write it did:

```
halt → flush → clear → serialOutBusy = false → return false
```

But a flushed transfer is **still owned by the HCD until its CANCELED callback
runs on the client task** — the library says so itself, at the vendor-OUT flush:
*"the transfer remains owned by the HCD until the flushed URB callback."* Clearing
`busy` synchronously meant the very next `sendSerial()` could pass the busy gate,
`memcpy` into the data buffer of a transfer the stack still held, and **resubmit an
HCD-owned transfer**. The window only opens against a device that has already
stalled for 1.5 s, which is why it read as plausible — the recovery path is exactly
where scrutiny drops.

**Fix (in this release):** the stall path no longer touches `busy`; the CANCELED
callback clears it when the cancellation is actually delivered. The stall path now
only refreshes `serialOutSubmitMs`, which rate-limits repeat halts to once per
stall window instead of once per call. If the callback never comes (a truly wedged
pipe), writes keep returning `false`, the halt retries every 1.5 s, and disconnect
or `resetDeviceState()` clears the slot — nothing is stranded.

This is the second defect found in patch 10 before it ever ran (the first was the
helper's pop-before-check drain loop). Both argue for the same conclusion: the
stall/recovery paths of this patch need to be **first on the bench list**, driven
deliberately — pull the radio's USB cable mid-Doppler, let a TH-D75 sit
powered-off-but-connected, and watch the halt/recover cycle in the SD log.

## F2. One lost CAT device tears down both CAT ports — deliberate, but worth knowing

`serviceDisconnects()` maps *either* `catLost` or `cat2Lost` onto one
`usbCatTeardown()`, which drops both CAT bindings; the reconciler then rebuilds
both. For a dual-USB CAT_DUAL station, unplugging ONE radio therefore bounces the
other's port too — a brief detach/re-attach of a healthy radio, visible as a
one-to-two-second CAT hiccup on the surviving leg.

I chose this over per-port teardown because `usbCatTeardown()` is the one
well-tested path and a half-teardown would be a new state with new edges. Fine for
0.9.73; if dual-USB stations become common, per-port teardown is the improvement,
and it belongs in the binding-path consolidation (E1 below) rather than as another
special case beside it.

## F3. `HelperStream::write()` can block the Doppler loop for up to 60 ms

When the helper's TX ring is full and credit is exhausted, `write()` pumps the link
with `delay(1)` for up to `WRITE_WAIT_MS` (60 ms) rather than truncate a CAT
command. That is the documented, intended trade — a torn CI-V frame presents as a
radio fault — but it is worth stating the worst case plainly: a dead Grove link
during an active pass costs one 60 ms hitch per CAT write until the link-dead
timer (5 s) fires and `active()` goes false. At the Doppler cadence that is a few
hitches, not a stall, and after 5 s the transport detaches cleanly. Acceptable;
no change. If it ever matters, the refinement is to give up immediately once
`linked()` is false rather than waiting out the deadline.

## F4. `active()` under `ESP_USB_HOST_ANY_ADDRESS` — correct today, fragile by nature

`connected()` resolves to `serialReady(address_)`, and with `address_ == 0xff`
that means "**any** serial device is present", not "**my** device is present". The
CAT/rotator bind paths all call `setAddress()` with a concrete address at bind
time, so today the predicate is always evaluated against a real address — but
nothing *enforces* that, and a future code path that binds without pinning the
address would get an `active()` that stays true so long as *anything* serial is
plugged in. Not a bug now; noted so the invariant ("bound implies pinned") is
written down somewhere. The right home for making it structural is E1.

## F5. Helper control-plane frames bypass flow control — correct, with one bound worth knowing

`ENUM`, `EVENT`, `OPENED`, `STAT` are written straight to the UART; only `DATA`
frames are credit-bound. That is the right design (control must get through when
data is jammed), and the UART's own blocking write is the implicit bound. The one
case with a burst is enumeration — one frame per device, back-to-back. At 230400
with the registry capped at 6 devices this is under 2 ms of UART time; fine. It
would only need thought if the registry cap ever grew by an order of magnitude.

## F6. Things checked and found sound

* **Helper disconnect callback** does no control transfers from the host task
  (`onUsbDisconnected` marks and queues; `loop()` tidies) — the mistake F1 is
  about, avoided here.
* **`gTxHold` lifecycle**: cleared on port close, on `handleOpen()` (a v1 OPEN
  replaces the port), and on the `!gPort.open` branch of the drain loop, so a held
  chunk can never leak across ports.
* **Credit arithmetic on both ends** grants from receive-ring space in whole-frame
  units; the 8 KB-flood test pins it, and the deliberate-break control confirmed
  the test can fail.
* **`serialOutBusy` synchronisation** post-F1: exactly one writer per direction
  (caller sets true before submit; callback sets false), `volatile` on a
  cache-coherent part — adequate, and simpler than an atomic would suggest.
* **Registry snapshots on the helper** (`portMUX` + copy-out) versus CardSat's
  release-fence-only registry: the helper's is the better pattern, already noted
  as E3.
* **Epoch/reconnect**: `RESCAN` drops local state without waiting for the helper's
  reply, so a wedged helper cannot wedge the UI; the fresh epoch rebuilds
  everything including a wanted-but-unopened port.

## E. Improvements deferred (unchanged from the evaluation, plus one)

* **E1. Consolidate the three CAT/rotator binding paths.** Now carrying three
  proofs: the teardown site missed in 0.9.72's fixes, F2's both-ports teardown,
  and F4's unenforced invariant. All three are the same argument.
* **E2. `ext_port.c` reset-poll backoff** — after re-baselining the FT232R on
  unmodified 0.9.72.
* **E3. Acquire fences on CardSat's adapter registry** (helper's pattern is the
  template).
* **E4. Per-attempt enumeration retry state** in the vendored stack.
* **E5. Channel-exhaustion diagnostics**, including the operator-facing
  "hub + TH-D75 + IC-705 cannot fit on one controller — use the helper" message.
* **E6. Script the release verification** (map greps, partition decode, §9
  hashes, `check_csuh_parity`) — every one of which was run by hand again for
  this re-cut.

## Bench list, in order

1. **F1's recovery path, driven deliberately** — cable-pull mid-Doppler, powered
   device that stops reading, watch halt/recover in the SD log. The stall
   threshold (1500 ms) is reasoned, not measured; it is a `#define`.
2. `helper_probe.py --enum` with only an IC-705 — decides whether auto-select
   works (one device) or the audio function enumerates separately (two).
3. Unplug/replug each of: CAT radio, CAT-B radio, USB rotator — the new
   disconnect propagation, including that the rotator does *not* self-re-engage.
4. Grove link-loss during a pass — F3's worst case, then the 5 s detach.
