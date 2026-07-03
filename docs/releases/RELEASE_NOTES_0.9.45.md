# CardSat v0.9.45 — release notes

A release focused on **operating during a pass**: a reworked web control panel with fast
frequency and calibration controls, live **AMSAT activity** with "heard N ago" recency and a
dedicated status screen, a configurable **AOS lead-time alert**, and two fixes. Nothing
changes the on-air message format — everything here is backward-compatible with 0.9.44
CardSats.

## Web UI: built for working a pass

The browser control panel (Settings → **Web control**, then point a phone or laptop at the
device's address) is reorganized so every control you need mid-pass is reachable at a glance,
without hunting or a keyboard:

- **Fast calibration pad.** RX-cal and TX-cal each get big one-tap **−/+** buttons with a
  tappable step (10 / 100 / 1000 Hz) and a live readout — correct a drifting linear-transponder
  signal instantly, no typing. A type-an-exact-value path is still there behind an **Exact…**
  toggle, and **Zero cal** is one tap.
- **Tuning cluster with a visible step.** The passband nudge shows its current step
  (100 Hz / 1 kHz / 5 kHz) and lets you cycle it — the same step the device's `s` key cycles —
  with **−/+/Center/TX→** beside it.
- **In-pass emphasis.** When a pass is up, the header flips to **"IN PASS — LOS in M:SS"** and
  the live card lifts with a green frame, so the working state is unmistakable.
- **The polar plot always shows the path.** In a pass it draws the current pass arc; between
  passes it draws the **next** pass arc — each with a **direction-of-travel arrowhead** and an
  AOS marker, and the live position dot overlaid while the bird is up. (Previously the arc
  blanked during a pass.)
- **The tracked satellite is always in the picker.** If the current bird isn't in the
  favorites/catalog slice the page lists, an option for it is injected so the dropdown always
  reflects what's actually being tracked.
- **AMSAT activity line** in the live card: "Heard 3h ago · 5 rpt", colored by status.

## AMSAT status: recency, a tighter window, and a dedicated screen

CardSat pulls the **AMSAT live status** feed to tag which birds have recently been reported.
That data is now surfaced properly:

- **"Heard N ago" recency.** Each report's timestamp is parsed, so the status now says *when*
  the most recent report came in — **"45m", "3h", "2d"** — for **all three** report states
  (Heard, Telemetry, Not heard). A "Not heard 1h ago" is itself useful: someone tried recently
  and missed. (Recency needs the device clock set; the feed buckets to ~30 min, so it's coarse
  beyond the first hour.)
- **Configurable window.** The "recently heard" window is now a setting —
  **3 / 6 / 12 / 24 / 48 / 72 hours, default 24** (was a fixed 72). A day is a much sharper
  "is it workable right now" signal; widen it for rarely-reported birds. Settings → change
  **AMSAT status window**, then re-fetch to apply.
- **A dedicated AMSAT status screen.** A scrollable list of every bird with a report in the
  window, **sorted most-active-and-most-recent first**, showing the status word (colored),
  recency, and how many stations reported. **ENTER** adopts a bird as the active satellite and
  jumps to Track; **`u`** re-fetches in place. Reachable two ways: **`s`** from the Satellites
  list, or the **AMSAT status** item on the main menu (right after Activations).

## AOS lead-time alert + next pass on the home screen

- **Lead-time alert.** A new **AOS lead alert** setting (**off / 2 / 5 / 10 / 15 min**) adds a
  distinct "get ready" chirp that many minutes *before* AOS — time enough to get to the radio
  and point an antenna — separate from the existing 60/30/10/0-second countdown.
- **Next favorite pass on Home.** The home screen now carries a persistent line for the soonest
  upcoming favorite pass: the satellite and a live AOS countdown (or **IN PASS**), so "what's
  next and when" is answered without opening Passes.

## Fixes

- **DX Doppler used the wrong transponders after viewing an activation.** After looking at an
  activation (which loads that bird's transponders), computing a mutual pass on a *different*
  satellite from the Passes screen kept the activation bird's transponders, so DX Doppler read
  the wrong up/down frequencies. Computing a mutual pass now reloads the pass satellite's own
  transponders.
- **`#SAT` mis-parsed satellite names containing spaces.** A reference like
  `#ISS (ZARYA)/25544` truncated at the first space to "ISS". The parser now treats a trailing
  `/<digits>` as the NORAD number and everything before it as the name, so spaced names — and
  internal slashes — survive. `!SAT` sked parsing gets the same treatment.

## Settings persistence audit

Every one of CardSat's settings was audited to confirm it survives a reboot. Three that did
**not** persist are fixed: the two new ones (**AOS lead alert**, **AMSAT status window**) and,
found in the audit, **Auto position reply** (added in 0.9.44 — it had never been saved). All
three now round-trip, with a stored out-of-range value snapping back to a valid choice. No
config migration is needed: on the first boot after flashing, the three take their defaults
(lead off, window 24 h, auto-reply off) and persist from the next save.

## Documentation

The manual, README, and features list are updated for the reworked web UI, the AMSAT status
screen, the recency display, and the new settings. The manual and cheat-card PDFs are
regenerated.

## Compatibility

Fully interoperable with 0.9.44 CardSats. The `#SAT`/`!SAT` and `@lat,lon` on-air formats are
unchanged — the `#SAT` fix only affects how a message is *parsed on receipt*, so a fixed unit
now reads spaced names correctly while everything else works across versions exactly as before.
