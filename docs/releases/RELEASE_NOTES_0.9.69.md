# CardSat v0.9.69 — release notes

A correctness release. v0.9.68 shipped native dual-radio support; this one fixes what was
wrong with it — including two defects that made the headline feature unreliable — and
completes the parts that were missing.

Most of this came from outside review rather than from testing that happened to pass: an
external functional audit of the v0.9.64→v0.9.68 range, and bench review of the dual-rig
screens. Both P0 findings in that audit were correct, and both were regressions introduced
by the 0.9.68 dual-radio work. The full finding-by-finding evaluation, including the ones
that changed nothing, is in `docs/design/AUDIT_RESPONSE_0_9_69.md`.

# Fixed — dual radio

### The Dual CAT type was discarded on every reboot

`Settings::load()` validated the CAT transport with a range check against the last
enumerator *as it stood when that line was written*. `CAT_DUAL` is newer than that bound,
so a saved native dual-radio configuration was silently reset to **wired CI-V** on every
boot: it worked until you power-cycled, then came back driving the Grove UART — which could
also seize that UART from a Grove GPS or rotator on the way past.

This was the **second** time that clamp discarded a new transport; the comment above it
documented the first. Range checks against enum growth cannot be kept correct, so it is now
an explicit whitelist, and a new gate (`tools/audit_settings_clamps.py`) fails the build if
any settings clamp compares against a non-final enumerator. The gate was validated by
reintroducing the shipped bug and confirming it catches it.

### Changing settings while dual-USB was engaged stranded the second port

`applyRadioFromCfg()` tore down only the first CAT port. The second one — its CDC, its
adapter, and its share of the USB host — was left allocated and invisible. Switching away
from dual-USB therefore left the USB-C PHY owned and the serial console permanently gone,
and changing an engaged uplink leg's model, baud or adapter silently kept the old session
because the port reported itself already open.

There is now **one** USB CAT teardown used by every path. A third defect implied by the
same root cause was found and fixed while there: the reconciler's outer condition also
tested only the first port, so after switching to a non-USB configuration the teardown
inside it could never run at all.

### Single-wire CI-V now works on a dual-rig leg

It never had. Single-pin CI-V is a delicate sequence — clear the UART signal inversion, add
a pull-up, set open-drain at the pad register so the output matrix stays attached, then
re-assert the RX input on the shared pad — and all of it lived inside the wired CI-V
backend. Dual-rig legs opened a plain two-wire UART instead, so a one-wire radio simply
never answered.

That matters for most of the catalog: the IC-705, IC-7100, IC-706MKIIG, IC-275/475 and all
nine IC-R receivers present CI-V on a single 3.5 mm jack. The sequence is now one shared
function used by both backends, a leg honors *Settings → CI-V wiring* exactly as the wired
path does, and the pad bookkeeping is global — which it has to be, now that two backends
can open the one UART.

### Either leg can be None

Setting a leg to **None** is now a normal configuration rather than a refusal: that half of
the link simply isn't CAT-controlled and its Doppler goes nowhere, while the other leg
tracks normally. That covers the common station where only one radio has a computer port —
an SSB downlink rig with a hand-tuned HT on the uplink, or a CAT-capable receiver alongside
a transmitter you key yourself.

The screen says which half is driven, and the rig names itself `IC-705 (DL only)`, so a
working one-legged setup is never mistaken for a half-broken two-legged one. An absent leg
also claims **no bus** — which took care throughout, because its bus setting is still
sitting there in the UI: the conflict guards, the Grove-vs-GPS arbitration and the USB
reconciler all had to stop counting it.

### Receive-only radios are refused as the uplink

v0.9.68 warned and continued, which produced a configuration that looked complete and
could never transmit — with no standing indication of why uplink Doppler did nothing. Now
refused at engage, with the radio named, and flagged in red on the screen.

# Fixed — elsewhere

- **The web page can no longer contradict the device about the battery.** `/api/status`
  and the About page were still reading the charger status that the Cardputer ADV does not
  expose usefully — the very value the v0.9.66 charge-screen rework exists to avoid. Every
  consumer now goes through the same accessors, which infer charge state from the voltage
  trend, and the accessor was made cheap enough to call from a draw path.
- **The Telnet client negotiates.** It spoke raw TCP only: a real Telnet daemon's option
  bytes were printed as garbage, and a server that waits for a reply before showing a login
  could hang. It now answers negotiation correctly — refusing every option, which is the
  spec-compliant way to stay in the line-oriented mode this terminal is built around — and
  swallows subnegotiation blocks.
- **USB enumeration waits for the second adapter.** The adapter scan returned as soon as
  the *first* device appeared, so with two adapters (dual-USB CAT, or CAT plus a USB
  rotator, especially through a hub where devices come up staggered) the list depended on
  enumeration order. It now waits for a bounded quiet period. Registry entries are also
  published with a release fence, so a reader can no longer see a count that includes a
  half-written entry.

# Under the hood

- **The `.ino` header was rewritten.** It still advertised "three CAT families" and a key
  list from an era with a fraction of today's screens. It now documents what a contributor
  actually needs: the dual-representation invariant, the pinned library versions and board
  settings, all six CAT transports and every rotator transport, the wiring cautions, the
  project conventions, and the gate suite to run before shipping.
- **A correction to our own claim.** The v0.9.68 notes said the build was clean with zero
  warnings. That was not meaningful: the build runs at the toolchain's default warning
  level, which suppresses them. Measured properly, there are 103 warnings, 70 in our own
  code. The three that could indicate defects were fixed (a member-initialization order
  bug in code added in 0.9.68, a misleading integer-in-boolean-context expression, and a
  dead array); the rest are recorded as a tracked baseline rather than quietly accepted.
  Warning counts in these notes will only ever come from a warnings-enabled build.
- **17 static gates** (one new) and **8 host harnesses**. A documentation contradiction was
  also resolved: the single-pin CI-V document carried both a "confirmed on hardware"
  banner and a stale "unverified" line. The confirmation is correct — the project owner has
  since re-confirmed single-pin CI-V working on an IC-821.

# Still not bench-verified

Unchanged from v0.9.68 and stated plainly: **native dual-radio has never driven a real
radio.** The CAT dialect encoders are byte-verified against the companion firmware's
bench-validated frames, and these fixes remove defects found by reading code, but that is
not the same as system verification. The single-wire-CI-V-on-a-leg path in particular is
new code reaching a proven electrical sequence — the sequence is confirmed on hardware, the
plumbing to it is not. `docs/THINGS_TO_VERIFY.md` lists the specific checks, including a
test that exercises both of this release's dual-rig changes at once using an IC-821.
