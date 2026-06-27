# CardSat v0.9.32 — Release Notes

**0.9.32** is a bug-fix point release over 0.9.31. It resolves a set of **LoRa
messaging** problems and, most importantly, a **shared-SPI-bus** issue in which
microSD access stopped working after a LoRa radio operation. All observer/operator
features added in 0.9.31 (visual pass predictions, decay/reentry flags, Sun/Moon
transit predictions, per-satellite notes) are unchanged.

## Upgrading

Two prebuilt binaries ship with the release. **`CardSat.bin`** installs through
**[Launcher](https://github.com/bmorcelli/Launcher)** and **preserves your saved data**
(settings, calibration, per-satellite notes, favorites, cached elements) — the
recommended path for an in-place update: drop the new file in Launcher's bin folder and
pick CardSat. **`CardSat_Merged.bin`** is a complete standalone image for **M5Burner**
or a **direct flash** at `0x0`; it carries an empty filesystem, so flashing it erases
**internally-stored** data. Note that CardSat prefers a **microSD card** for storage, so
**if you run with an SD card in, your configuration persists across any flash** — the
data-loss case only applies to internal (no-SD) storage with a full merged flash.
Confirm the new version on the **About** screen after flashing. Full details in the
README and the manual (§5 Installing the firmware).

## microSD restored after LoRa operations (shared-SPI-bus fix)

The microSD card and the LoRa SX1262 share one SPI bus. The SD card is mounted at a
high clock; RadioLib runs its transactions at a lower clock and mode and releases the
bus in that state. As a result, **after any LoRa radio operation — changing the
channel, spreading factor, bandwidth or TX power, sending, or receiving — the next
microSD access could fail.** This presented as settings appearing not to persist (a
later config write landed on an unusable bus and left the file unwritten) and as the
satellite list or logs failing to load after touching LoRa settings.

The fix re-establishes the SD card's bus configuration after each LoRa SPI operation,
so the next SD access works normally. This runs only when storage is on the microSD
card; the internal-flash fallback is unaffected.

## LoRa messaging fixes

- **Incoming messages now appear immediately.** Previously a received message would
  not show on the Messages screen until the next keypress; the screen now refreshes
  as soon as a message arrives.
- **Smoother scrolling.** Scrolling up through older messages no longer shrinks the
  visible window row by row (which looked like lines being deleted). The message list
  now keeps a full window and stops cleanly at the oldest message.
- **Compose field length cap.** The message entry field is now capped to the on-air
  message length, so long messages can no longer overflow the screen; what you see is
  what is sent.
- **Message-notify setting persists.** The LoRa message-notification preference
  (off / banner / banner+beep) is now saved and restored across reboots.

## Configuration robustness

- A configuration file that **exists but fails to parse** (for example from a
  transient read error) is now **left intact** rather than being overwritten with
  defaults. Defaults are only written to disk on a true first boot when no
  configuration file is present, so a single bad read no longer discards saved
  settings.
- `Settings::save()` now reports a failed (zero-byte) write instead of silently
  reporting success.

## Diagnostics

A compile-time `CARDSAT_CFG_DEBUG` switch (off by default) adds serial output around
configuration load/save and the SD remount, for troubleshooting storage behavior on
specific hardware.

## Known issues

- **Own messages may show a trailing character.** A message you send can be echoed
  back with an extra character appended. This is on the receive/display path and is
  still under investigation; it does not affect message delivery.

## Notes

LoRa messaging remains an **at-your-own-risk** hardware path; verify on-air behavior
against your own setup. See `docs/THINGS_TO_VERIFY.md`.
