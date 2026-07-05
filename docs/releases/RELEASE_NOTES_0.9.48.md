# CardSat v0.9.48 — release notes

This release fixes a real field bug and turns CardSat into a serious **field bench and
mission-planning toolkit**. The headline fix: on SD-equipped units, cached weather and
space weather **now genuinely survive a reboot**, so you can update at home and still
have the data in the field with no network. On top of that, the Tools hub grows from a
handful of calculators into **twenty tools** — a full RF/antenna workbench, three
offline ham reference databases (DXCC, CQ, ITU), and a set of mission-planning
calculators aimed squarely at the CubeSat community (link budget, RF-exposure, orbital
lifetime, cross-section area). Everything is backward-compatible with 0.9.47; no
settings, log, or on-air formats change.

## The fix: caches survive reboots (SD units)

Weather and space weather are meant to be fetched before you leave and viewed offline in
the field. On units with a microSD card that wasn't happening — a reboot lost the data.

The cause was a filesystem split. `Store::begin()` mounts **one** filesystem: the microSD
card when present (the default), internal LittleFS only as the no-card fallback. So on an
SD-equipped unit LittleFS is never mounted — and the weather cache writer/reader, the
space-weather loader, and the AMSAT catalog-map loader were all using raw `LittleFS.open`,
which silently fails when LittleFS isn't mounted. Fetches still filled RAM, so everything
looked fine online, and the data vanished on the next boot. All four sites now use
`Store::fs()` (the active filesystem), a repo-wide audit confirms zero raw `LittleFS.open`
calls remain on data files, and a boot serial line — `[boot] caches: wx=.. spacewx=..
(fs=SD|LittleFS)` — now reports cache-load results so this class of bug can't hide again.

## A field bench: the Tools hub

Reached from About → `t`, the Tools hub now holds twenty tools in a scrolling menu.

**Calculators.** A scientific calculator with a traditional scrolling **tape** (infix
entry, degree trig, `Ans`); a **programmer's calculator** (64-bit hex/dec/bin/oct with
bitwise ops, base on the up/down arrows); and a **character lookup** showing any byte in
all four bases plus its ASCII/Morse/Baudot-ITA2/BCD meaning.

**RF & antenna workbench.** Coax loss/power (eight cable types, SWR-added loss), dipole,
vertical, **yagi** and **quad** dimensions (selectable element counts), RF unit
conversions (dBm/W/V), SWR/return-loss, free-space path loss, and a unit converter.

**Ham reference databases — all offline.** A **DXCC entity lookup** (402 current and
deleted entities: search by prefix, name, or code; shows zones, continent, and ARRL
footnotes), a **CQ (WAZ) zone** reference (all 40 with definitions), and an **ITU zone**
reference. They cross-link: from a DXCC entity, `z` jumps to its CQ-zone definition and
`i` to its ITU-zone definition. Each database is generated from the authoritative source
(ARRL, cqww.com, RSGB) by a committed script, so they regenerate when the sources update.

**Mission planning.** A full **link budget** calculator (the whole chain — EIRP, path
loss, noise floor, SNR, margin — with mode presets and an S-meter estimate); an
**RF-exposure (MPE)** calculator (FCC OET-65 limits and compliance distances); a
**battery-runtime** estimator; an **orbital-lifetime / debris** estimator (drag decay vs
the 25-year and new 5-year disposal guidelines); and a **cross-section (drag area)**
calculator with CubeSat presets and deployable panels. The last two are designed to work
together: the cross-section tool's tumbling-average area drops straight into the
orbital-lifetime tool, giving a CubeSat builder a design-to-disposal-compliance workflow
on the device. All the mission-planning math was validated against published references
before it shipped, and the safety/compliance tools (RF exposure, debris) are clearly
labeled as planning estimates that don't replace the authoritative tools (a full station
evaluation, NASA DAS).

## Learn: modulation and telemetry

The on-device Learn page (Help → `l`) gains two reference sections: a **modulation**
walkthrough (HF, VHF/UHF, satellite, and EME modes and why each suits its band) and a
**satellite telemetry** explainer (how it works and the history of modes from OSCAR-1's
temperature-keyed CW beacon through modern FEC-coded BPSK).

## Smaller things

- **World map night shading is toggleable** (`n`), remembered across reboots.
- The Tools menu scrolls, and the scientific calculator became a proper scrolling-tape
  interface (from the 0.9.47-late UX work now fully settled here).

## Fixes rolled up

- Weather/space-weather persistence on SD units (above).
- The `F()` macro collision (an antenna-form lambda named `F` clashed with Arduino's
  flash-string macro) and the DEL-key handling across every Tools screen (the Cardputer
  delivers DEL via a flag, not a character code, so the calculators and forms were
  exiting instead of backspacing) — both fixed and host-verified.

The full itemized list is in **[BUGS_0.9.48.md](BUGS_0.9.48.md)**.

## Upgrading

Flash as usual — no settings, log, or on-air format changes. On SD units, verify the fix
after upgrading: update weather and space weather online, power-cycle with WiFi off, and
confirm both screens still show the data (the boot serial line should read `wx=ok
spacewx=ok (fs=SD)`).
