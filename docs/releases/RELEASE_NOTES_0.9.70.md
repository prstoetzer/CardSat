# CardSat v0.9.70 — release notes

**A USB release.** Everything below came out of one long bench investigation into why a
Kenwood TH-D75 would work once over USB CAT and then never again without power-cycling
both the radio and the Cardputer. The answer turned out to be four separate defects
stacked on top of each other, three of them in the USB host library and one in the
radio's own firmware behaviour. Along the way the TH-D75's CAT dialect was rebuilt from
measurement, and several unrelated bugs were found and fixed.

**If you build CardSat yourself, read
[`third_party/EspUsbHost/PATCHES.md`](../../third_party/EspUsbHost/PATCHES.md) before
you do.** The stock USB host library will compile and appear to work, then strand the
USB stack the first time a radio stops answering.

---

## USB is now reliable across engage/disengage cycles

You can switch radio control on and off as often as you like, change satellites, and
switch the radio off and on, without rebooting anything.

Four things had to be fixed to get there:

**The USB host stack was stranded by an undrained write.** The library halts and
flushes its audio and vendor output endpoints during shutdown, but not the CDC serial
output endpoint — and serial writes are submitted without being recorded anywhere the
shutdown code can see. A radio that stopped reading its port left a write enqueued
forever, which blocked the interface release, which blocked the device close, which
blocked the client deregistration, which meant the host was never uninstalled. Every
subsequent attempt to start USB returned "busy" until a reboot. This is fixed in the
vendored library and reported upstream.

**The port was never closed.** On a CDC device, asserting DTR is what tells the device
the host has the port open, and there is no other close notification. CardSat asserted
DTR when binding and never dropped it, so from the radio's side the host opened the
port and then vanished. A TH-D75 kept its CAT session open until the battery was
pulled. DTR and RTS are now de-asserted at every port detach — radio, second CAT port,
and rotator.

**The USB host now stays resident between engagements.** Disengaging detaches the port
but leaves the host installed and the device enumerated. This is not a workaround for a
broken teardown — the teardown was fixed and confirmed — it is because some devices
never re-initialise their firmware after a re-enumeration. A TH-D75 re-enumerates
perfectly and then accepts exactly two packets into its endpoint FIFO and stops reading:
its USB hardware is fine, its CAT application never comes back. Waiting five minutes
does not help. Keeping the host resident reproduces what a desktop does when it closes
and reopens a port, which the same radio survives indefinitely.

**Press `Fn`+`u` on the tracking screen to fully release USB**, which is also when the
serial console returns. It refuses while a radio or rotator is still on, and tells you
so.

This applies to every USB path — radio, second radio, and rotator. The rotator had been
carrying its own copy of the teardown code and had silently missed several fixes; it
now shares one implementation.

## Kenwood TH-D74/D75 CAT rebuilt from measurement

The handheld path never worked properly and has been rewritten against a real radio
rather than against documentation — both available references disagreed with the
hardware, and one contradicted itself.

- **Frequency is set with `FQ`, one frame, no round trip.** The previous code used an
  `FO` record read-modify-write. The radio *refuses* `FO` writes — on both bands, in
  both VFO and memory mode, even when handed back the exact record it had just emitted.
- **Three preconditions, all required and all now sent:** the band must be in VFO mode,
  it must be the control band, and the frequency must sit on the current step grid.
  Off-grid writes are rejected, not rounded — which is what the earlier "intermittent"
  behaviour actually was.
- **Mode digits corrected.** `RM_FM` now maps to NFM, because band B — the all-mode
  receiver CardSat drives — refuses plain FM outright, which would have failed on every
  FM satellite. AM was transposed with DV; a previous release "fixed" a transposition
  that did not exist and thereby created one.
- **Fine mode (20 Hz) is applied only where the radio accepts it** — SSB, CW and AM.
  FM and NFM do not support it and get the finest normal step (5 kHz) instead, which is
  well inside FM bandwidth.

Verified on hardware: 60 consecutive Doppler steps, all exact, at 33 ms per step.

Two scripts in `tools/` drive a TH-D75 from a Mac using exactly CardSat's command
sequence — `thd75_probe.py` for the command set and `thd75_verify.py` for a per-mode
sweep of what the radio actually accepts. They settled several questions in minutes
that on-device logs could not.

## Other fixes

- **Battery/charge.** CardSat read the battery ADC directly, which broke M5Unified's
  own reader and produced bursts of `adc1 is already in use` errors while returning
  nothing. It now uses a single reader with a short cache. The **charge indicator is
  removed**: the Cardputer ADV has no PMIC and no charger status line, so charge state
  is not knowable, and the old indicator reported "on battery" while plugged in. The
  charge screen shows terminal voltage instead — a real measurement that does rise on
  the charger. Battery percentage is unaffected.
- **BASIC immediate mode** could not type the letters `b` or `h` — they were being
  taken by the global screenshot and help hotkeys. Backspacing past the start of a line
  no longer drops you out of the prompt. `Fn`+`p` prints the session transcript, and
  `LPRINT` at the prompt now actually reaches the printer (it opened it and never
  flushed or closed it).
- **Two footers were wider than the screen** and had been silently clipped — one of
  them hiding the graph's CSV export key.

## For people building this themselves

`third_party/EspUsbHost/` contains the patched USB host library CardSat is built
against, with `PATCHES.md` explaining every patch, why it exists, and how to re-apply
after an upstream version bump. Three patches are drafted as upstream reports and
attachable `.patch` files. The M5StickS3 DualRig companion uses the same library and
needs the same treatment.

`PATCHES.md` also carries a warning worth reading before improving anything in the
teardown path: do not clear an endpoint halt before releasing an interface. Clearing
*resumes* queued transfers, which blocks the release — a mistake made during this work
that produced a bug indistinguishable from the one being fixed.

## Verification

22 automated source gates plus host test harnesses; two new gates this release catch
one-shot latches that are never reset, and text-entry screens that leak keystrokes to
global hotkeys. Both were written after real bugs of exactly those shapes.
