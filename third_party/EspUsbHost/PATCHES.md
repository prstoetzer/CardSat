# EspUsbHost — vendored copy and CardSat's local patches

**Vendored upstream version: 2.7.0** (github.com/tanakamasayuki/EspUsbHost, master).

**If you are building CardSat yourself, read this file.** The stock library from the
Arduino Library Manager will compile and will appear to work — and then USB CAT will
strand the USB stack the first time a radio stops answering, with no recovery short of
a reboot. The patched copy in this directory is what CardSat is built and tested
against.

## Building against the patched copy

Copy this directory over the installed library:

```sh
cp -r third_party/EspUsbHost ~/Arduino/libraries/EspUsbHost
```

or point your build at `third_party/EspUsbHost/src`. CardSat's release binaries are
built exactly this way.

The M5StickS3 **DualRig companion** uses the same library and has the same exposure —
build it against this copy too. See `companion/README.md`.

## Patch placement trap — read before re-applying anything

The CardSat patch block **must sit above** the library's
`#if CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR` guard, which is where `TAG` is
defined. That guard is **false** on CardSat's FQBN, so anything placed inside it
silently vanishes. This caused two build failures during development and also explains
why the library's own `ESP_LOG` output is unreachable in CardSat builds no matter what
`LOG_LOCAL_LEVEL` says.

## The patches

Numbered `.patch` files here are the ones worth sending upstream; the rest is local
policy.

### 1. CDC-ACM: bind the *first* control interface (`0001-…patch`)

The control-interface latch is unguarded while the data-interface latch is guarded, so
on a device exposing two CDC-ACM functions `SET_LINE_CODING` and
`SET_CONTROL_LINE_STATE` are addressed to a function whose endpoints are never used.
The device enumerates, reports `connected()`, and never communicates.

Report drafted in `UPSTREAM_ISSUE.md`. **Not** the TH-D75's problem — that radio has a
single CDC function — but real for true dual-CDC composites.

### 2. Drain the CDC serial OUT endpoint on shutdown (`0002-…patch`) — **the important one**

`drainClientTransfers()` halts and flushes audio-out and vendor-out by address, but not
serial-out. `sendSerial()` submits its transfer without recording it in `endpoints_`,
so nothing iterating that table can cancel it. A device that stops reading leaves the
transfer enqueued forever, and then:

`usb_host_interface_release()` refuses the owning interface (259) → the device is never
closed → `usb_host_client_deregister()` refuses (259) → `usb_host_uninstall()` is never
reached → **every later `usb_host_install()` returns 259 for the rest of the boot.**

Measured on a TH-D75: without this patch, the first cycle with a silent radio stranded
the stack; with it, five consecutive cycles released the host completely and returned
every byte of heap. Report in `UPSTREAM_ISSUE_2_SERIAL_OUT.md`.

### 3. `clearLastError()` (`0003-…patch`)

`lastError_` is cleared only in `begin()`, so any timeout anywhere in a session is
still reported afterwards. CardSat tests `lastError()` after `end()` to decide whether
teardown succeeded, and stale values made clean releases look like wedges — a
nonexistent bug we chased for some time. Also covered in `UPSTREAM_ISSUE_2_SERIAL_OUT.md`.

### 4. `ESPUSBHOST_CLAIM_AUDIO`, default `0` — local policy, not a bug

USB audio interfaces are not claimed. CardSat drives CAT and a rotator and has no use
for audio; not claiming keeps the descriptor walk off endpoints it will never service.
Build with `-DESPUSBHOST_CLAIM_AUDIO=1` to restore upstream behaviour. A preference,
not a defect; not proposed upstream.

### 5. `quiesce()` — local

Brings the bus to rest *before* `end()`: stops the bulk IN pump re-arming, halts and
flushes what is in flight (including serial OUT), waits for completions, and leaves the
pipes halted. Not strictly required once patch 2 is in place, but it makes teardown
deterministic rather than timing-dependent.

### 10. Bounded, reusable serial OUT transfer — **0.9.73**

`sendSerial()` allocated a `usb_transfer_t` *and its buffer* on every call and freed
them only in the completion callback. Nothing counted what was outstanding, so a
device that NAKs, stalls, is unplugged mid-transfer, or simply reads its port slowly
accumulated heap without limit. On a bus carrying a hub and two devices the largest
free block is around 11 KB, so this is not a theoretical concern.

Now: **one reusable transfer per device**, held in the `DeviceState` slot exactly as
`networkOutTransfer` already was, with a busy flag and a submit timestamp.

* A write while one is outstanding returns `false`. That is the right answer for CAT
  (a stale frequency update is worth less than the next one) and the necessary one
  for the USB helper, whose drain loop would otherwise submit as fast as its ring
  filled — the credit scheme bounds the *link*, not the transfers.
* An outstanding write past `CARDSAT_SERIAL_OUT_STALL_MS` (1500) is treated as a
  stalled endpoint, not a slow one: halt, flush, clear, and report. Without this a
  single stalled write wedged the port permanently while every later write returned
  `false` with no explanation.
* The completion callback marks the slot's transfer free rather than releasing it;
  anything else reaching that callback is a legacy one-off and is freed as before.
* `resetDeviceState()` frees it, alongside `networkOutTransfer`.

One refinement found by re-review before release: the stall path originally cleared
`serialOutBusy` synchronously after halt/flush/clear. That is a race — the flushed
transfer remains HCD-owned until its CANCELED callback runs on the client task, so
the next `sendSerial()` could write into and resubmit a transfer the stack still
held. The flag is now cleared only by the callback; the stall path just refreshes
the timestamp so repeat halts are rate-limited while the cancellation is delivered.
If the callback never comes (a truly wedged pipe), writes keep returning `false`
and the halt retries once per stall window — and disconnect or `resetDeviceState()`
clears the slot, so nothing is stranded.

Reported by the 0.9.72 USB review; see `docs/design/USB_REVIEW_EVAL_0_9_72.md`.
Worth sending upstream — the fire-and-forget allocation is upstream behaviour, not
CardSat's.

### 11. Heap-allocated, size-configurable CDC RX ring — **0.9.73**

`EspUsbHostCdcSerial`'s RX ring was a fixed in-class 512 B array. It is now
heap-allocated at construction, sized by `-DCARDSAT_CDC_RX_RING` (default 512, so
CardSat itself is byte-for-byte unchanged in behaviour), **PSRAM-preferred** where
the cap exists. The CardSatUsbHelper sets 16 KB via its `build_opt.h`, putting the
burst absorber between USB IN transfers and the drain loop in memory the DMA layer
cannot use anyway. Fallback chain: SPIRAM → internal → internal at 512; the read
and push paths guard on the pointer, so a failed allocation degrades rather than
crashes. Every touch is task-context under `rxMux_` — no ISR ever sees the buffer,
which is what makes PSRAM legal here. Upstream-worthy alongside patch 10: the
fixed 512 is an upstream limitation.

## A warning for anyone tempted to "improve" the teardown

**Do not call `usb_host_endpoint_clear()` before releasing an interface.** We did,
reasoning that release wants un-halted pipes. It is exactly backwards, and IDF says so:

> *"If the endpoint has any queued up transfers, clearing a halt will resume their
> execution."*

Clearing re-activates the pipe and can resubmit the transfer just cancelled, so the
interface release then refuses — a bug that took several rounds to unpick because it
looked identical to the one being fixed. **Halted and flushed is the releasable
state.** Upstream is correct here.

## Re-applying after an upstream bump

All patches are anchored on distinctive text and re-apply cleanly by hand. After
bumping, **verify each is present in the built binary rather than assuming** — the
placement trap above fails silently. CardSat's release procedure greps the compiled ELF
for the patched strings.
