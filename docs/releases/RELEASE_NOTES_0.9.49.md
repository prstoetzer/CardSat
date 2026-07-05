# CardSat v0.9.49 — release notes

A focused maintenance release. The headline is a **field-reported hardware fix**: on units
running from a microSD card, having LoRa messaging enabled in settings **without** a Cap LoRa
module attached quietly broke SD-card writes — so settings (the GPS source was the usual
tell), logs, and caches silently stopped persisting across reboots. That's fixed. This
release also resolves a satellite-screen key conflict, and refreshes the documentation with
a current, correctly-labeled screenshot set.

No settings, log, or on-air formats change; upgrading from 0.9.48 is drop-in.

## The fix: SD card + absent Cap LoRa

**Symptom.** On an SD-equipped unit with LoRa enabled but no Cap LoRa attached, the GPS
source (and other settings) would not persist — always resetting to defaults on reboot. The
underlying cause was broader: **every** SD write was failing, GPS was just the setting most
often noticed.

**Cause.** The microSD card and the SX1262 LoRa radio share one SPI bus. When LoRa was
enabled but no module answered, bringing the radio up still claimed and reconfigured that
shared bus, and on this hardware the SD card could not reliably be restored afterward — so
reads done early in boot succeeded, but the next `cfg.save()` / log / cache write failed
silently. A serial-console trace from the field pinned it down: the card was healthy right
up until the LoRa bring-up touched the bus.

**Fix.** CardSat now **probes for the Cap LoRa before touching the shared bus.** It briefly
resets the radio and checks its BUSY line; a present SX1262 answers, an absent one does not.
If no module is detected, LoRa initialization is skipped entirely and the working SD bus is
left completely untouched, so writes keep persisting. If a module is present, LoRa comes up
exactly as before. The bring-up also now leaves a clear line on the serial console either
way. Verified on hardware in both configurations (module absent → SD persists; module present
→ LoRa still works).

## Fixed: satellite-screen key conflict (simulation vs status)

On the Satellites screen, `s` was bound to **two** actions — AMSAT status and Simulation —
so status always won and **Simulation was unreachable**, while the on-screen help still
listed `s` for simulation. Simulation is now on **`y`**, `s` is unambiguously AMSAT status,
and the help text matches. A full audit of every key handler in the firmware confirmed this
was the only genuine conflict (other keys that appear twice are mode-gated — only one branch
is ever reachable per press).

## Documentation

- **Screenshots refreshed.** The repository screenshot set was recaptured at the current
  build and correctly labeled, the manual gained a **Tools gallery** and images for the many
  screens that previously had none (EME, Awards, Games, AMSAT status, Messages/LoRa RX, and
  more), and the README gallery was expanded to show the newer capabilities. A couple of
  early mis-placements (an illumination/visible-pass mix-up, and a voice-memo screen) were
  caught and corrected.
- **Key-reference corrections.** The manual's Satellites key list now shows `y` for
  simulation (matching the fix above).

## Fixes rolled up

- SD-write failure with LoRa enabled and no Cap LoRa attached (above) — this also resolves
  the "GPS setting won't persist" report, which was the same underlying issue.
- Satellite-screen `s`/`y` key conflict (above).

The full itemized list, including the diagnostic trail behind the SD fix, is in
**[BUGS_0.9.49.md](BUGS_0.9.49.md)**.

## Upgrading

Flash as usual — no settings, log, or on-air format changes. If you run from an SD card and
had LoRa enabled without the module attached, this release is the one that makes your
settings stick again.
