# CardSat v0.9.64 — release notes

A USB-control release. The headline is a substantial overhaul of how USB CAT and USB rotators
are brought up and torn down: the EspUsbHost library was upgraded so a disengaged USB device
now actually gives its memory back, radio and rotator can share the USB host on two adapters,
and every "turn it off" path — on every screen — reliably releases the device instead of
holding it. Alongside that, the radio and rotator settings gained explicit **None** options,
and the Dual-Rig setup screen was redrawn to fit the display cleanly.

Everything here is built and gate-checked; the USB paths that need two adapters (dual radio +
rotator over USB) are supported in code but should be exercised on hardware as part of this
testing round.

Since the first cut of this release, CardSat also went through **four independent audits** —
three broad security/lifecycle passes and one dedicated to the Dual-Rig companion path — and
the confirmed findings were fixed. The highlights: file writes that matter (settings, notes,
transmitter and GP caches) are now **transactional**, so an interrupted or failed write can no
longer destroy the previous good copy; **downloads reject truncation** instead of caching a
half-file as if complete; the **GPS, voice-memo, and rigctl/rotator lifecycles** were tightened
to stop resource leaks and false "ready"/"saved" reports; and input validation was centralized
for coordinates, grids, and LoRa/Grove settings. On the Dual-Rig side, three release-blocking
Grove bugs were fixed (the UART ran RX and TX on the same pin, the 115200 baud couldn't be
represented, and the model catalogue parsed zero entries), along with the companion sketch's
Wi-Fi-credential, config-rebind, CI-V echo, and read-back-honesty issues. Full disposition is
in `docs/design/AUDIT_FINDINGS_TRACKING.md`.

The Dual-Rig companion path (both the TCP and Grove rigctl links) is implemented and both
firmwares compile, but it has **not** been exercised on two-radio hardware yet — treat it as
first bring-up, not a known-good path, until a two-radio regression (one CI-V + one non-CI-V,
over both transports) has been run.

# New

### "None" is now a first-class choice for both radio and rotator

The rotator settings used to carry a separate "Rotator: on/off" row next to the type selector,
which meant two settings had to agree to say "I have no rotator." That row is gone: the type
selector itself now includes **None** (cycle to it the same way you pick GS-232 or rotctl), and
whether the rotator is enabled is derived from the type. A configuration saved with the rotator
previously turned off migrates automatically to **None** on first boot, and a fresh install
defaults to None. Selecting any real type enables it.

The radio model list gained a matching **None** entry, so CardSat can run as a pure
tracker/rotator controller with no radio attached. With **Radio: None** selected, the CAT
features become no-ops and turning the radio on from Track reports "Radio: set a model in
Settings" rather than claiming it engaged. Existing radio configurations are untouched — None
is opt-in.

### Radio and rotator can both run over USB

With two USB-serial adapters plugged in, the radio and the rotator can each be assigned to
their own adapter and both run at once over the single USB host. The two ports bind to
distinct device addresses, and the order in which you engage them no longer matters — whichever
you turn on second waits for its own nominated adapter to enumerate before deciding it's
missing, so a slow-to-appear adapter is no longer mistaken for a missing one. This is reliable
when each adapter reports a unique **serial number** (most FTDI and CP210x do). Adapters
without a serial (many CH340 clones, and identical same-model pairs) can only be told apart by
their USB address, which can change across a reboot, hub change, or replug — after any of those,
re-check the **Radio USB** / **Rot USB** selections. With only one
adapter present, the two ports still can't share the same wire, and the settings picker now
says so plainly ("Only adapter is the radio's / rotator's") instead of silently refusing to
move off "Auto".

# Fixes

### Turning a USB device off now frees its memory

Previously, disengaging USB CAT left the host stack resident — the memory it held (~12 KB) only
came back on a reboot. The EspUsbHost library has been upgraded (2.4.1) so its shutdown handshake
completes cleanly, and disengaging USB CAT now releases the host and its buffers on the spot. The
USB rotator is symmetric: turning it off (the 'o' toggle on Track, Sun/Moon, EME, or the Grid
bearing screen, or backing out of any of those) now parks the antenna and releases the USB host,
rather than holding it idle. Both re-engage on demand the next time you turn them on.

### Switching the radio or rotator away from USB releases the adapter

Changing the radio to CI-V or LAN (or to **None**) while USB CAT was engaged used to leave the
USB session running in the background: it kept holding the host and, worse, made the rotator
refuse the adapter with "That adapter is the radio's" long after the radio had stopped using it.
Disengaging is now reconciled whenever a USB session is active, not only while the radio is set
to USB, so leaving USB actually tears the session down and frees the device ID for the other
port. The same is true in reverse for the rotator. A stale "USB" transport left on a **None**
device no longer reserves its adapter against the other port.

### The CAT self-test and serial monitor start USB on their own

Both diagnostic tools need a live CAT link. For a USB radio that wasn't already engaged they
would simply report "radio not ready." They now bring USB CAT up themselves when opened — the
self-test asks you to re-run once the transport is up (it engages on the next loop), and the
monitor begins polling as soon as the link is live. Leaving either tool turns USB CAT back off
again, but only when the tool was what engaged it; if the radio was already running from Track,
it keeps running. The monitor's heartbeat poll was also corrected so a USB monitor with no
active satellite pass isn't left silent.

### Manual rotator control engages the rotator on entry

Opening the manual rotator screen (Settings → rotator manual control) now builds and engages a
deferred USB rotator on entry, so jogging and read-back work immediately instead of doing
nothing until the rotator had been engaged elsewhere first. Leaving the screen releases the USB
host if nothing else is using the rotator.

### Stop-all and charge mode release the rotator

The "stop all control" hotkey and entering charge/sleep mode now park and release a USB rotator
(and drop USB CAT), so neither leaves a device held while idle.

### Dual-Rig setup screen redrawn

The Dual-Rig companion setup screen had overlapping text: the enumerated-device list ran off
the bottom of the display and collided with a hard-positioned status line and the footer. The
screen was redesigned to fit within the display — the two legs are laid out as compact,
full-width selection rows with the leg tag inlined, the device reference shows a count and only
as many rows as fit (with a "…more" hint when there are more), and the status line only appears
when there's a message to show, clearing the footer. The key hints now live solely in the
footer.

# Under the hood

### EspUsbHost 2.4.1 and the resident-host retirement

The previous release worked around a library limitation by keeping the USB host object and its
tasks resident for the life of the session and only detaching the CDC port on disengage — the
stack was never actually released. The 2.4.1 upgrade performs the full drain → deregister →
free-all → uninstall handshake correctly, so the ~70 lines of hand-rolled teardown and its
drain-state bookkeeping were removed in favor of the library's own `end()`. On the bench,
disengaging USB CAT now returns the host memory (about 11.8 KB) each cycle, across many cycles,
with no leak.

### Reclaimed Bluetooth controller memory + a heap diagnostic

CardSat uses no Bluetooth, but the precompiled core left the Bluetooth controller memory
allocated. Overriding the core's `btInUse()` hook to release it reclaims about 3.8 KB. The `mem`
serial command also gained a 32-bit-capability line to its per-capability heap summary, which
confirmed there is no separate word-only region to exploit — the ~31.7 KB largest-contiguous
ceiling is a fixed property of the ESP32-S3 DRAM map.

### Serial console after USB CAT

Because the HWCDC serial console and the USB host share the S3's single USB PHY, engaging USB
CAT drops the console. Now that 2.4.1 releases the host on disengage, the console is
reinitialized at that point. This path is new and should be confirmed on hardware — after
turning USB CAT off, a serial terminal should be able to reconnect.

---

*This release focuses on the USB-control lifecycle. The dual-adapter (radio + rotator both over
USB) and serial-console-return paths are implemented and gate-checked but benefit most from
hardware testing; single-adapter USB CAT and USB rotator use, and all non-USB transports, are
unchanged in behavior apart from the fixes above.*
