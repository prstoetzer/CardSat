# CardSat v0.9.15 — Release Notes

A radio- and rotator-control refinement release, with several ideas adapted from
the open-source **OscarWatch** tracker (Peter Goodhall, MM9SQL) after studying its
Doppler and rotator source. The Doppler-correction *math* was already on par; the
improvements here are in **when and how CardSat talks to the radio and rotator**.
This release also documents that **per-satellite calibrations can be hand-authored
on the SD card**, and makes those files comment-friendly.

> **Hardware status.** Pass prediction, plots, GPS, the AOS alarm, deep sleep, and
> the offline caches are confirmed on hardware. The radio and rotator refinements
> in this release are **host-verified only** (tokenizer-balanced, logic-checked)
> and have not yet been exercised against a physical rig or rotator. The constants
> below use OscarWatch's field-tuned values as a starting point and may want
> adjustment for your specific CI-V latency or rotator slew rate.

---

## Doppler / CAT control

Three refinements to the real-time Doppler service, gated so they do nothing on
slow, low-elevation passes and only engage where they help (fast overhead passes):

- **Mode-aware write deadband.** CardSat already skipped re-sending a leg that
  hadn't moved; the deadband is now **mode-aware** — loose for FM (300 Hz, whose
  passband absorbs Doppler) and tight for linear SSB/CW (50 Hz). This cuts
  needless CI-V traffic and audible stepping, and matters most on slow-CI-V rigs.
- **Adaptive threshold near TCA.** When the Doppler slew rate is high (the fast
  geometry around closest approach), the deadband tightens automatically — ramping
  down above 15 Hz/s, halving by 35 Hz/s, with a 25 Hz floor — so tracking keeps
  up where it counts and stays relaxed elsewhere.
- **TCA-tapered predictive lead.** The correction can be computed slightly ahead
  (up to 50 ms) to mask CAT latency, blended in proportionally to how fast Doppler
  is changing and **tapered to zero near closest approach**, where range rate is
  small and a forward lead would overshoot.

All three are compile-time constants (`DOPP_*` in `app.h`) — no new settings. The
cost is one extra range-rate evaluation per service tick (a second only when the
lead is active), negligible on the ESP32-S3.

---

## Rotator control

- **450° azimuth lookahead (shortest-path over north).** On a 450° rotator,
  CardSat already used the 361–450° overlap band to avoid unwinding ~360° when a
  pass crosses north — but reactively, after the bearing had already crossed. It
  now **looks 3 s ahead** (one extra propagation per ~1 Hz rotator tick) and
  **pre-commits to the overlap band before an imminent north wrap**, turning a
  long unwind into a short move. Two guards keep this safe: it is computed only on
  a 450° rotator, and **never when a pass is flipped** (where the +180° azimuth
  makes the overlap reasoning meaningless). The hint is single-shot, so it can
  never leak into a later park, pre-position, Sun/Moon, or manual command.

---

## Calibration & tones on the SD card

- **Documented: hand-author per-satellite calibrations.** Per-sat calibration has
  always been stored as a plain-text file; this release **documents the format**
  so you can bulk-edit it on a computer instead of nudging each bird in **CAL**
  mode. Edit **`/CardSat/calib.txt`** — one `norad downlink_Hz uplink_Hz` line per
  satellite — and the values apply the next time you open that bird (no reflash).
  CTCSS overrides work the same way in **`/CardSat/tones.txt`**
  (`norad tone_tenths`). See **Manual §10**.
- **Comment-friendly files.** Both files now ignore blank lines and lines starting
  with `#` or `;`, so you can annotate your calibrations. Saving on the device
  preserves your comment lines.

---

## Tunable from Settings (no reflash)

The Doppler and rotator refinements above shipped with sensible compile-time
defaults; the four values most likely to want field-tuning are now adjustable on
the device under **Settings**, persisted across reboots:

- **Dopp FM band** (default 300 Hz) and **Dopp linear band** (default 50 Hz) — the
  per-mode CAT write deadbands (Radio / CAT category).
- **Dopp lead** (default 50 ms; `0` = off) — the predictive-lead cap. Raise it for
  a slow-CI-V rig, or disable it entirely.
- **Rot az lookahead** (default 3 s; `0` = off) — the 450° azimuth lookahead
  horizon (Rotator category). Tune to your rotator's slew speed, or turn it off to
  use the previous reactive-only overlap behaviour.

---

## IC-820H CI-V band-select (bug fix)

CardSat previously shipped the **IC-820H** with the same MAIN/SUB band-select
sub-commands as the IC-821H — which turns out to be **wrong**. Confirmed from each
radio's own manual (CI-V command table, command `07`):

| Radio | Address | Main band access | Sub band access |
|-------|---------|------------------|-----------------|
| IC-821H | `4C` | `0x07 D0` | `0x07 D1` |
| IC-820H | `42` | `0x07 D1` | `0x07 D0` |

The IC-820H **reverses** the two sub-commands relative to the IC-821H. CardSat now
ships the correct mapping for each (selMain/selSub swapped between the two
profiles), so an IC-820H tunes the right VFO out of the box. Previously it would
have Dopplered the wrong band. The IC-820 profile is now marked **verified**. See
**Manual §16**.

---

## EQX table for OSCARLOCATOR use

A new **EQX table** screen (Satellites → `e`) lists **equatorial crossing times
and longitudes** for the selected satellite, for plotting passes on a classic
**OSCARLOCATOR** board.

- Each entry is an **ascending-node** crossing (ground track crossing the equator
  northbound), with the **UTC time** and **sub-satellite longitude in
  West-positive** notation (`123.4 W`) — the convention printed on Oscarlocator
  dials.
- Covers the next **3 days**, day-grouped and scrollable (`;`/`.`), `r` to
  recompute, `d` to **toggle ascending ↔ descending node** (the header reads
  **EQX** or **DEQX**), computed entirely **on-device** from the satellite's GP
  elements via SGP4 (no network). Successive crossings step ~28.7°/orbit westward
  for an AO-7-class orbit.
- Works for **any** satellite in the catalog, not just AO-7. Mirrors the output of
  the **AO-7_OSCARLOCATOR** generator (N8HM). See **Manual §8**.

---

## Edit / delete manual satellites & transponders

You can now remove hand-entered data from the device — previously, manually-added
GP satellites and transponders could only be added, never cleaned up.

- **Delete a manual satellite** — on **Satellites**, select a sat you added with
  `n` and press `x` twice (arm, then confirm). It's removed from
  `/CardSat/mgp.json` and from your favorites. Network-cached sats are protected
  (they can't be deleted this way, since Update would just re-fetch them).
- **Delete a manual transponder** — on the **Transponder database** (`t` from
  Satellites), the list is now selectable (`;`/`.`), the selected entry is marked
  `>`, and your own entries are tagged `*`. Press `x` twice to delete the selected
  manual entry; its line is removed from `/CardSat/mtx_<norad>.json`. SatNOGS
  entries are protected the same way.
- **Editing** is delete-then-re-add: remove the entry and recreate it with `n`.
  This keeps the UI small (no separate edit screen) while covering the need.

Both deletes are two-press confirmed and only ever touch hand-entered data.

---

## CelesTrak GP queries & 9-digit NORAD IDs

Verified CardSat's GP/element requests against CelesTrak's current
specification (*A New Way to Obtain GP Data*). Requests already conform: host
`celestrak.org` (the `.com` host 301-redirects and risks an IP firewall), path
`/NORAD/elements/gp.php`, uppercase query keys, group/special values matching
CelesTrak's own examples, and an explicit valid `FORMAT=JSON` (rather than relying
on the server default, which changed to CSV in 2026). Error handling also matches
their guidance — responses are checked for HTTP 200, redirects are followed, and
retries are bounded with backoff rather than hammering.

Also audited and confirmed **forward-compatibility with 9-digit catalog
numbers** (the Space-Fence / 18 SDS `799xxxxxx` analyst ranges that exceed the
classic 5-digit TLE limit). CardSat stores every NORAD id as a 32-bit value
(good past 4.2 billion) and ingests GP only via OMM/JSON `NORAD_CAT_ID`, so
9-digit ids parse, store, dedup, cache, and propagate correctly — confirmed by a
host test across the full range up to 999999999. The only 5-digit-limited spot is
the catalog field of the *synthesized TLE line* fed to the SGP4 initializer, which
is cosmetic (the propagator uses the orbital elements, not that field, and CardSat
never reads it back as identity); this is now documented in `satdb.cpp`.

---

## IC-910 CI-V satellite-mode & tone (bug fix), CAT cross-checked against manuals

Before release, the Icom CI-V profile constants were reconciled against Hamlib and
then against the official Icom manuals (IC-9700 CI-V Reference Guide, IC-910
instruction manual). This surfaced **two real IC-910 bugs**:

- **Satellite mode.** CardSat sent `0x16 0x07` to engage sat mode on every Icom.
  That's correct for the IC-9100/9700 (`0x16 0x5A`) once you account for the
  sub-command, but the **IC-910 puts satellite mode under a different command
  entirely**: `0x1A 0x07` (verified from the IC-910 CONTROL COMMAND table). The old
  code's `0x16 0x07` doesn't exist on the IC-910, so sat mode silently never
  engaged. Fixed: satmode now carries a per-rig **command byte** (`satModeCmd`), so
  the IC-910 sends `0x1A 0x07` while the IC-9100/9700 send `0x16 0x5A`.
- **CTCSS tone encoder.** On the IC-910, `0x16 0x42` is the *auto-notch filter*;
  the subaudible-tone encoder is `0x16 0x43`. CardSat had been sending `0x42`,
  which would toggle the notch instead of the tone. Fixed with a per-rig
  `toneEncSub` (IC-910 = `0x43`, IC-9100/9700 = `0x42`).

Confirmed-correct constants (no change needed):

- **IC-9100/9700 satellite mode** `0x16 0x5A` — from the IC-9700 CI-V Reference
  Guide ("5A … Send/read the satellite mode") and a live IC-9100 trace
  `fe fe 7c e0 16 5a fd`.
- **MAIN/SUB band select** `0x07 D0/D1` — IC-9700 confirmed from its CI-V guide
  (and the `0x07 D2` get/set variant); the IC-820H/821H reversal confirmed from
  their manuals earlier this release.
- **Repeater-tone frequency** `0x1B 0x00` and **read-freq** `0x03` — from the
  IC-9700 guide; Yaesu/Kenwood encoders cited against Hamlib `ft847.c`/`ts2000.c`.

Both the direct CI-V path and the network/rigctld path got the fixes. The IC-910's
tone-*frequency* (`0x1B`) command isn't documented in its CI-V table, so it's left
in with a caveat (if the rig NAKs it, the encoder on/off still applies the tone set
on the radio). The Kenwood TS-790/TS-2000 family remains the least bench-verified.

---

## Kenwood TS-790 serial framing (bug fix)

Reviewing the TS-790 manual (and the contemporary TS-850 External Control manual,
which shares the IF-232C interface) surfaced a framing bug: the **IF-232C
generation of Kenwood rigs requires two stop bits at 4800 baud**, not one. CardSat
had been opening the Kenwood port as 8N1 at every rate, so a TS-790 at 4800 baud
could mis-frame or ignore commands. Fixed: the Kenwood port now uses **8N2 at 4800
baud and 8N1 above it** (so the TS-2000 at 57600 is unaffected), selected
automatically by baud.

The rest of the TS-790 profile was confirmed: the `FA;` frequency read-back was
verified against a live Hamlib TS-790 trace, and CTCSS is left disabled
(`hasTone=false`) because the rig's tone squelch needs the optional TSU-5 decoder
unit. The TS-790's CAT runs through the optional IF-232C adapter; its operating
manual documents the interface but not the command set, so this family stays the
least bench-verified and should be confirmed against a serial trace on real
hardware.

---

## Notes

- The Doppler linear deadband (50 Hz) means up to ~50 Hz of uncorrected SSB drift
  between writes — intended, and matches OscarWatch's linear default.
- The rotator lookahead horizon (3 s) and the Doppler lead cap (50 ms) are the
  two values most likely to want field-tuning for your hardware.
- Credit: the Doppler-threshold/lead and azimuth-lookahead ideas were adapted from
  **OscarWatch** (AGPL-3.0, github.com/magicbug/OscarWatch-Tracker). CardSat's
  implementations are independent C++ for the Cardputer.
