# CardSat 0.9.49 — bug list

Status key: **OPEN** · **FIXED** (done, pending release) · **WONTFIX**.

## 1. SD card access breaks when the Cap LoRa is not connected — **FIXED**

**Field report:** on a unit running from a microSD card, if LoRa messaging is enabled in
settings but no Cap LoRa module is attached, SD access misbehaves.

**Root cause:** the SD card and the SX1262 LoRa share one SPI bus (SCK40/MISO39/MOSI14,
differing only in chip-select). `LoraRadio::begin()` reconfigures that shared bus for the
radio (an explicit `SPI.begin(LORA_PIN_*)` plus RadioLib's own bus setup inside
`g_radio->begin()`), then, on success, calls `Store::remount()` to restore the SD driver's
bus configuration. **The two failure paths did not remount.** When no module answers,
`g_radio->begin()` returns an error and the code returned `false` having already
reconfigured the bus -- leaving the SD driver unable to talk to the card. Because
`loraStart()` runs in `setup()` *after* the early cache reads but *before* later writes,
the symptom is that boot-time reads succeed while the next `cfg.save()`, log append, or
transponder-cache write fails -- i.e. "SD struggles" specifically when LoRa is enabled and
the module is absent.

**Fix:** both failure paths in `LoraRadio::begin()` now call `Store::remount()` (and hold
SD CS idle-HIGH) before returning `false`, exactly as the success path does -- so a
missing or non-responding radio leaves the SD bus fully restored. `Store::remount()` is a
no-op on internal-LittleFS units, so this only affects SD units (the ones that had the
bug). Applied byte-identically to `src/lora.cpp` and the `.ino`; `begin()` now has three
`Store::remount()` calls (two failure paths + success) and is mirror-identical.

**Bench check:** on an SD-equipped unit, enable LoRa in settings with **no Cap LoRa
attached**, reboot, then change a setting (forces `cfg.save()`) and confirm it persists
across a power cycle; also confirm a QSO log write and a fresh transponder fetch land on
the card. Before the fix these SD writes failed after `loraStart()`.

**Note / possible follow-up:** with an absent module, `g_radio->begin()` relies on
RadioLib's internal BUSY-pin timeout to return an error, which can add a short delay to
boot when LoRa is enabled but unplugged. Not addressed here (it self-recovers); if the
delay is objectionable we could probe the BUSY/RST lines for presence before calling
RadioLib. Flagged for consideration, not fixed in 0.9.49.

## 2. Key conflict on the satellite screen: simulation vs status — **FIXED**

**Field report:** entering simulation and status conflict on the satellite screen.

**Root cause:** in keySatList, 's' was bound twice -- first to the AMSAT activity status
window, then (later, unreachable) to the simulation screen. The first guard returns, so
's' always opened status and **simulation was unreachable**. Worse, three sources
disagreed: the code ran status, the footer advertised "s status", but the Help screen
listed "s simulation (time)". SCR_SIM had no other entry point, so simulation could not
be opened at all.

**Fix:** rebound simulation to 'y' (free in keySatList; its bound keys were
2 3 d e f i k n o s t v x { }). 's' is now unambiguously AMSAT status. The Help screen
line corrected to "y simulation (time)", so code, footer, and help now agree. The
satellite screen has more keys than fit one 40-char footer (e/k/2/3/d/i were already
help-only), so 'y sim' joins that help-documented set rather than crowding the footer.
Mirrored byte-identically; keySatList identical between src and .ino.

## 3. Full key-conflict audit (all 107 key handlers) — **no other real conflicts**

Prompted by the above, audited every keXxx handler for a character bound by two separate
if-guards. Eight candidates surfaced; seven are legitimate mode-gated reuse where only one
branch is reachable per press, and are intentionally left as-is:
  - keyTrack / keyManual 's' and 'x': mutually exclusive passband-tune vs CAL-mode branches.
  - keyAmsatStatus 'u' and keyLoraRoster 'p': one binding inside an empty-list early-return
    block, the other on the populated-list path -- never both reachable.
  - keyToolForm ',': pick-list cycle (tfChoice>=0) vs numeric-output scroll -- gated by field
    type; by design.
Only keySatList 's' was a true conflict (two reachable guards in the same state); fixed
above. Audit script kept for future release gates.

## 4. Screenshot refresh + manual/README image expansion — **docs**

Replaced the repo screenshots with a fresh 120-shot batch captured at v0.9.48. Renamed to
the repo's kebab-case convention in docs/img/: all 42 existing screenshots updated in place
(so every existing MANUAL.md/README.md reference now shows a current capture), plus ~76 new
screens added for features that had none -- the entire Tools hub (20 tools), EME pages,
Awards, Games, AMSAT status/who-heard, Activations, Messages/LoRa RX, Notes, GPS position,
Charge/Sleep, and more. Five near-duplicate shots dropped. MANUAL.md image refs went 42 ->
72 (a new Tools gallery of 14 images + one representative image added to each major
previously-image-less section); README gallery expanded 9 -> 15 (added a Tools row and a
new-features row). All referenced images verified present on disk (no broken links); manual
PDF rebuilt at v0.9.49 (124 pages).

## 5. Screenshot placement corrections — **docs fix**

Re-audited the screenshot placements from item 4 (OCR'd every title bar against its
assigned name) after a report that the voice-memo screen looked misplaced. The dedup step
in item 4 had force-fitted several early shots to wrong names. Corrected:
  - illumination.jpg: was the "ISS visible" pass-list (shot 0139); now the real
    illumination raster (shot 0002).
  - ten-day-overview.jpg: refreshed with the actual "ISS 10-day" chart (shot 0003).
  - visible-passes.jpg: NEW -- the "ISS visible" pass list (shot 0139), placed in the
    Visible-pass list section.
  - voice-memo-recording.jpg: NEW -- the voice-memo RECORDING screen (shot 0006), distinct
    from the existing voice-memos.jpg browser; placed in the Voice memo section. (This shot
    had been wrongly copied over satellites-favorites.jpg.)
  - satsat.jpg: NEW -- the "AO-07 + sat" both-up sat-to-sat window (shot 0005).
  - satellites-favorites.jpg: REMOVED (image + manual ref). This batch contains no
    favorites-only capture, and the original was overwritten during item 4; rather than
    show a wrong image, the reference is dropped. **Needs a fresh capture** of the
    favorites-filtered satellite list (Satellites -> v) to restore.
All 74 manual + 15 README image refs verified to resolve; titles re-checked via OCR.

## 6. SD writes still failed with Cap LoRa absent (0.9.49 remount was insufficient) — **FIXED**

**Field report (after the item-1 fix):** SD still not read/written properly with no Cap
LoRa attached; concretely, the GPS setting never persists -- it always resets to its
defaults (useGps + Cap LoRa1262 source) on reboot.

**Diagnosis:** the GPS reset is the tell. Both GPS toggles call cfg.save() at change time,
and the load/save JSON keys ("gps", "gpssrc") match and are correctly mirrored -- so
there is no GPS-specific bug. The setting resets because cfg.save() is writing to a broken
SD bus and silently failing; every setting is affected identically, GPS is just the one the
user toggled. That is the same class as item 1, meaning the item-1 remount did not actually
restore the bus.

**Root cause (deeper than item 1):** Store::remount() did SD.end(); SPI.begin(SD pins);
SD.begin(). But on the ESP32 Arduino core, once the SPI bus is initialized (which RadioLib
does inside g_radio->begin(), whether or not a module answers), a bare SPI.begin() is a
no-op -- it does NOT re-point the bus to the SD clock/mode. So remount left the bus in
RadioLib's configuration and SD.begin() could not recover the card. This is exactly why a
fresh boot works (bus is virgin) but the post-LoRa remount did not.

**Fix:** remount() now tears the bus fully down before rebuilding it on the SD pins:
SD.end(); **SPI.end();** SPI.begin(SD pins); SD.begin(). SPI.end() forces the following
SPI.begin() to genuinely re-initialize the bus, making remount behave like a cold boot.
Only runs on SD units (guarded by g_sd) and only when LoRa actually touched the bus (poll
remounts only on a real RX event, not every poll), so no hot-path cost. Also made the
remount failure line print unconditionally ("[fs] remount SD -> FAILED (SD writes will
not persist)") so this failure mode is visible on the serial console instead of silent.
Applied byte-identically to src/storage.cpp and the .ino.

**Bench check:** SD unit, LoRa enabled in settings, NO Cap LoRa attached. Reboot, change
the GPS source (or any setting), power-cycle, and confirm it persists. Serial should NOT
show the remount-FAILED line. Then repeat with the Cap LoRa attached to confirm no
regression to the LoRa path.

## 7. SD persistence with Cap LoRa absent -- root cause found via serial log — **FIXED (probe)**

**Serial log evidence (from the field, SD unit, no Cap LoRa attached):**
  [fs] using microSD card for storage (/CardSat)   <- SD mounts, works
  [gps] source=Cap LoRa1262 ...
  [boot] caches: wx=ok spacewx=ok (fs=SD)           <- SD READS work fine
  [fs] remount SD -> FAILED (SD writes will not persist)
The remount-FAILED line prints from loraStart()->lora.begin()'s absent-module path. So the
SD bus is healthy right up until lora.begin() touches it, and neither the item-1 remount nor
the item-6 SPI.end() could recover it afterward: once RadioLib has run against an absent
SX1262, SD.begin() will not re-establish the card on this hardware.

**Real fix -- stop trying to recover; avoid the disturbance.** Since the SD bus is provably
fine until LoRa touches it, lora.begin() now PROBES for the module before doing any
SPI.begin(): it pulls BUSY (GPIO6) to a pulldown, pulses RST, and polls BUSY in a tight loop.
A present SX1262 drives BUSY HIGH through its ~1 ms power-on calibration (datasheet behavior);
an absent module reads LOW throughout. If absent, lora.begin() returns immediately WITHOUT
calling SPI.begin() or RadioLib at all -- the working SD bus is never disturbed, so cfg/log/
cache writes keep persisting (this is what fixes the GPS-setting-won't-persist symptom, which
was never GPS-specific -- it was every SD write failing). The tight poll from the instant of
reset-release cannot miss the calibration pulse, so a real module is not false-negatived.
Prints [lora] no Cap LoRa detected ... when it skips. The item-6 SPI.end() in remount is
retained as a backstop for the module-PRESENT path (normal LoRa RX bus handoff).

**Bench check:** (1) SD unit, LoRa enabled, NO Cap LoRa: reboot -> serial shows the '[lora] no
Cap LoRa detected' line and NO 'remount FAILED'; change GPS source, power-cycle, confirm it
persists. (2) WITH Cap LoRa attached: confirm LoRa still comes up (messages send/receive) --
this verifies the probe does not false-negative a present module.

---

## Bench confirmation (0.9.49)

**BENCH-CONFIRMED on hardware (SD-equipped unit):**
- Cap LoRa ABSENT + LoRa enabled: reboot shows the presence-probe skip, no remount failure;
  GPS source (and settings generally) now persist across power-cycles. SD reads and writes work.
- Cap LoRa ATTACHED: LoRa still comes up normally -- the presence probe does not
  false-negative a present module.
This closes items 1, 6 and 7 (the SD-persistence-with-LoRa-absent thread) and the GPS
setting persistence report. The presence-probe approach (item 7) is the shipping fix; the
earlier remount / SPI.end changes (items 1, 6) remain as backstops for the module-present path.

## Release review (packaging gate)

RELEASE_NOTES_0.9.49.md written (SD/LoRa fix as headline, key-conflict fix, docs refresh).
README New-in-v0.9.49 blurb added. Cheat card corrected (satellite-screen s->y sim; the
Help-hub s=sat-history left intact) and rebuilt at 2 pages. Manual rebuilt at v0.9.49 with
the restored satellites-favorites.jpg (correct favorites-filter capture supplied by Paul)
and two stale s-simulation key references corrected (Satellites section + key table).
Ground-truth: LoRa presence-probe present and ordered before SPI.begin in both files;
keySatList s=status/y=sim; help + cheat card + manual all agree; 75 manual + 15 README
image refs resolve. Suite: balance 0/46, parity green, 108 dispatch cases match, FW_VERSION
0.9.49 both files. **Paul has compiled and flashed both 0.9.48 and 0.9.49 — full-tree
build/flash gate CLEARED.** Release zip: CardSat-0.9.49.zip.
