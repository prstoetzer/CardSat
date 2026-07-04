# CardSat v0.9.46 — release notes

A release for the **VHF / UHF / microwave and EME** operator, and for anyone who works the
bands alongside the birds. It adds a complete **EME (moonbounce)** screen with real lunar
Doppler, a **grid-square distance & bearing** calculator, a worldwide **band-plan reference**,
a new **orbital "Phys"** page (orbital velocity + launch date), and an **HF/6m propagation**
guide driven by the space-weather data CardSat already fetches. Plus a **DX-Doppler fix** for
multi-transponder birds and a small consistency pass on **grid/callsign text entry**. Nothing
changes the on-air message format — everything here is backward-compatible with 0.9.45
CardSats.

## EME / moonbounce

A dedicated **EME screen**, reached with **`e`** from the Sun/Moon screen (the Sun/Moon screen
itself is unchanged). It answers the question an EME operator actually asks — *where is my echo,
and is the Moon workable right now* — from a proper topocentric lunar model:

- **Self-echo Doppler per band** — the round-trip Doppler on your own signal off the Moon, for
  50 / 144 / 432 / 1296 / 10368 MHz. This is dominated by your station's own rotation (not the
  Moon's orbital motion), so it is computed **topocentrically** — the figure swings up to a few
  kHz at 1296 and tens of kHz at 10 GHz over a Moon pass, exactly what you tune for.
- **Topocentric range and range-rate** — the live Earth–Moon distance and how fast it is
  changing (the source of the Doppler above).
- **Path degradation** — the extra path loss versus perigee (about 2 dB at apogee), with a
  near-perigee / near-apogee note, so you know when the Moon is at its most (or least) workable.
- **Sky-noise flag** — a coarse cold-sky / warm / **hot** indicator from the Moon's galactic
  latitude, since pointing near the galactic plane raises the 144 MHz noise floor.
- **Mutual-Moon window** — press **`m`**, enter a DX station's grid, and get the upcoming
  windows when the Moon is above the horizon for **both** of you (the common EME window), scanned
  over the next two weeks.
- **Point the rotator at the Moon** — **`o`** aims a connected rotator at the Moon and tracks it.

The lunar ephemeris was checked specifically for Doppler use: the position model is the same
one the Sun/Moon screen uses, but the **range-rate** is computed as a true topocentric quantity
(observer position and velocity included), because a geocentric-only figure would be wrong by
thousands of Hz at microwave.

## Grid-square distance & bearing

A **grid calculator** on the main menu (just before QRZ Lookup): enter a Maidenhead grid and
get the **great-circle distance and beam heading** from your station, short path and long path,
in km and miles. Useful for terrestrial VHF/UHF/tropo/contest work, not just satellites.

- **Point the rotator** at the computed bearing with **`o`** (azimuth only, elevation 0 — a
  terrestrial beam heading).
- **Look up a callsign's grid** with **`q`**, which opens a small **QRZ → grid** screen; on
  ENTER it seeds the calculator with that station's grid. This is a separate screen — the
  existing QRZ Lookup screen is untouched.

## Band-plan reference

A scrollable **worldwide amateur band reference**, off the **Help** screen (press **`f`** —
"frequencies"). It runs **LF to light**: the HF bands with their ITU Region 1 / 2 / 3
differences, the VHF/UHF/microwave bands with **calling and EME frequencies**, the **satellite
subbands**, the IARU **band designators** (H/A/V/U/L/S/C/X/K), and common satellite modes
including QO-100. A quick on-device answer to "where does this band start" and "what's the
calling frequency."

## Orbital analysis: a new "Phys" page

A tenth page on the orbital-analysis screen (the pages you reach from Satellites), plus the same
values in the web UI's orbital analysis:

- **Orbital velocity** — the satellite's instantaneous speed (via vis-viva), correctly higher
  near perigee and lower near apogee, with the apogee/perigee velocity spread.
- **Launch date & age** — the launch year and launch number, and how long the satellite has been
  in orbit, derived from its COSPAR International Designator. A nice bit of context when you are
  working a decades-old bird like AO-7.

## HF / 6m propagation guide

A new **propagation screen** off Space Wx (press **`p`**). CardSat already fetches the **10.7 cm
solar flux** and the **Kp index** for the space-weather screen; this turns those two numbers
into the read an HF/6m/VHF operator wants:

- **HF band conditions** from the solar flux — which bands are likely open in daylight
  (10/15/20 m at a glance).
- **Geomagnetic effect** from Kp — quiet / unsettled / storm, and what that does to HF.
- **Auroral VHF** likelihood — a high Kp is the trigger for 6 m / 2 m auroral propagation, so the
  screen flags it and says which way to beam.
- **D-layer absorption** on the low bands.

These are climatological rules of thumb, not a real-time propagation model — the screen says so,
and notes that 6 m sporadic-E (the dominant summer opening) is seasonal and not predicted by
these indices.

## Fixes and refinements

- **DX Doppler kept the wrong transponder for multi-transponder birds.** When seeding the DX
  Doppler page from an activation on a satellite with more than one transponder (AO-7, for
  example), the listed frequency did not always land on the matching transponder, and cycling
  transponders discarded the frequency. Now the listed frequency is remembered and **re-applied
  as you cycle** — it stays locked onto whichever transponder it belongs to, and is never lost by
  stepping through the others.
- **Grid and callsign entry now capitalizes consistently.** All Maidenhead-grid and callsign
  text-entry fields uppercase as you type (with shift for the rare lowercase). Most already did;
  a handful — the new grid/EME/QRZ screens, the sked-draft grid and callsign, and the grid
  filter — did not, so typing felt inconsistent field to field. They are all consistent now (the
  stored value was always upper-case; this aligns what you see while typing).

## Documentation

The manual, README, and features list are updated for the EME screen, the grid calculator, the
band-plan reference, the orbital Phys page, and the propagation screen. The manual and cheat-card
PDFs are regenerated.

## Compatibility

Fully interoperable with 0.9.45 CardSats. The on-air `#SAT` / `!SAT` / `@lat,lon` formats are
unchanged; everything in this release is local operating information and new screens.
