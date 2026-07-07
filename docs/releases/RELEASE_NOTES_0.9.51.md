# CardSat v0.9.51 — release notes

This release is built around **planning a rove and working with brand-new satellites**. It adds
a full **rove pass-planner** (survey every favorite from any grid and time before you travel), a
**state-vector → GP-element fitter** for tracking objects that don't have a published TLE yet
(including pre-launch payloads), on-device **rove-plan browsing**, **file download over the web
interface**, and an experimental **share-elements-over-LoRa** feature. It also folds in a large
round of on-device bug fixes from bench testing.

Upgrading from 0.9.50 is drop-in — no settings, log, or on-air format changes, and nothing
existing behaves differently.

> **Note on screenshots:** the images in the manual and README were captured on an earlier
> build and do **not** yet show the new 0.9.51 screens (rove planner and its pass detail, the
> State-vector → GP tool, the saved-plan browser and viewer, the import-satellite confirm
> screen, and the web Files page). The text is current; the pictures will catch up in a later
> round.

---

## Rove pass-planner

Planning a portable operation means answering one question for every satellite you care about:
*when does it pass over the place I'll be, and how high?* CardSat can now answer that in one
pass. From **Next Passes**, press **`p`** to open the **Rove planner**.

Enter a **grid**, a **date**, a **time**, and a **± window** in hours, then **GO**. CardSat
surveys **all of your favorites** over that window *from the entered location* — which may be
nowhere near where you are now — and lists every pass, sorted by acquisition time, showing the
satellite, AOS, maximum elevation, and how many **US states** and **DXCC entities** are workable
during that pass. Press **ENTER** on a row for a detail screen with the polar arc as seen from
the planned site, the AOS/LOS/max-elevation geometry, and the full workable-**state** (`s`) and
**DXCC** (`d`) lists for that pass.

The survey runs incrementally in the background (one favorite per loop) so the interface stays
responsive while it computes, and it holds the heap flat — no large allocations, no network. It's
a planning tool: point it at your destination grid and departure weekend and it tells you the
whole schedule before you pack the car.

## State-vector → GP: track satellites that don't have a TLE yet

Everything CardSat tracks normally runs on **GP mean elements** (the modern form of the classic
two-line element set). But a freshly launched satellite doesn't have published elements for the
first hours or days — what you get instead, from the launch provider, is a **state vector**.

**What a state vector is.** It's the most direct possible description of an orbit: *where the
object is and how fast it's moving, at one instant.* Just two 3-D vectors at a single epoch — a
**position** `(rx, ry, rz)` in kilometres from the centre of the Earth, and a **velocity**
`(vx, vy, vz)` in kilometres per second. Six numbers and a timestamp. Where orbital elements
describe the *shape and orientation* of the orbital ellipse, a state vector is the raw kinematic
snapshot; both describe the same orbit, just in different coordinates. For a low-orbit satellite
the position components each run a few thousand kilometres (the orbital radius is around
6,700–7,400 km) and the velocity components each a few km/s (orbital speed is about 7.5 km/s);
signs depend on which way the object is heading. Launch providers, rideshare integrators, and
orbit-determination tools all distribute state vectors — often *before* deployment, with an epoch
days or weeks in the future — so you can plan the first passes before the object is catalogued.

**The tool.** Under **Tools → State vector → GP**, enter the **epoch** (UTC), the **position**
and **velocity** components, and the input **frame** — **TEME** (used directly, the frame SGP4
works in) or **J2000** (GCRF/ICRF, what most providers publish; CardSat rotates it into TEME
on-device). CardSat then computes a set of GP mean elements that reproduces your state vector,
and shows the fitted mean motion, eccentricity, inclination, RAAN, argument of perigee and mean
anomaly, along with derived apogee/perigee and a **fit residual**. Press **`s`** to save the
result as a manual satellite and track it like any other.

**Why it fits instead of converting.** A state vector converts *directly* only into
**osculating** elements — the instantaneous ellipse — but SGP4 expects **mean** elements, with
the short-period gravitational wobbles averaged out. Handing SGP4 osculating elements produces
multi-kilometre errors. So CardSat instead runs a small **differential-correction fit**: it
starts from the osculating elements as a first guess and repeatedly nudges the six mean elements,
re-running its own SGP4 each time, until the propagator reproduces your entered state to within
the achievable precision. This release uses a **Levenberg-Marquardt** solver that converges
reliably across orbit types — near-circular, polar/sun-synchronous, and eccentric — typically in
one or two iterations. Because a single instant carries no drag information, the drag term `B*`
is set to zero; the elements are excellent for the first days and slowly drift after that, so
re-acquire real published elements once the object is catalogued.

**One common pitfall — the frame.** The same orbit produces different position/velocity numbers
in TEME versus J2000, because the two frames' axes point in slightly different directions.
Choosing the wrong one makes the fit fail or come out subtly wrong. If your source says "J2000",
"GCRF", "ICRF", or "EME2000", pick **J2000**; if it says "TEME" or "true equator", pick **TEME**.

The manual's *Understanding state vectors* section (§21) explains the concept in full.

## Pre-launch (future) epochs

Because launch providers hand out state vectors *before* deployment, CardSat now fully supports
elements whose **epoch is in the future**. The fit, the saved manual satellite, and every
pass/Doppler prediction work with a future epoch (SGP4 propagates correctly on either side of the
epoch). While the epoch is still ahead of the clock, the satellite's age reads **`pre-lnch`** in
cyan instead of a "GP _n_ d" age, and a pre-launch satellite never triggers the stale-element
auto-refresh. Passes computed before the epoch are the nominal pre-launch orbit; re-run them once
the true post-deployment elements are published. (This release also fixed a bug where a single
future-epoch satellite could suppress the whole fleet's auto-refresh.)

## Workable grids, and rove-plan text export

The rove planner counts workable **grid squares** per pass alongside states and DXCC, and the
detail screen can show the workable-grid **count** for a pass. A completed survey can be
**exported to a text file** with **`w`** — a timestamped `rove_YYYYMMDD_HHMM.txt` under
`/CardSat/RovePlans/` containing the plan header and, per pass, the workable **states** list, the
**DXCC** list, and the workable-grid **count**. Take it with you, print it, or pull it off the
device over the web.

## Saved rove plans, on-device

You no longer need a computer to read a saved plan. Press **`l`** on the rove planner to open the
**Saved rove plans** browser — every exported plan, newest first, with its date-stamp and size.
**ENTER** opens a read-only, scrolling viewer (`;`/`.` scroll, `{`/`}` page); **`d`** deletes a
plan with a confirm; **`r`** rescans. The viewer holds a bounded slice of the file so a very large
plan can't exhaust memory — if a plan exceeds the cap it shows a "download for full file" note,
and the web Files page (below) gives you the whole thing.

## File download over the web interface

The mobile **Web control** page gains a **Files** link. It opens a simple browser of the
`/CardSat` tree — rove plans, screenshots, logs, notes — with sizes and modified times; click a
folder to descend, click a file to download it. This lets you pull files off the device without
removing the SD card or rebooting into a launcher. It is **download-only** (there is no upload
path, so the page cannot modify the device's filesystem), access is confined to `/CardSat`, and
— like the rest of web control — it is **unauthenticated on the local network**, so use it only
on networks you trust.

## Share satellite elements over LoRa (experimental)

If you have the LoRa radio, you can now push a satellite's **GP elements** to another CardSat
over the air — handy in the field for sharing a freshly-fitted pre-launch orbit with a rove
group without WiFi. Press **`L`** on the **Satellites** screen (or on the State-vector → GP
result screen) to broadcast the selected satellite's elements. The set is split into a few small
checksummed chunks and sent one frame at a time, with progress shown. On the receiving unit,
CardSat reassembles the object, verifies the checksum, and shows an **"Import satellite?"** prompt
with the sender, name, NORAD, and key elements — press **`y`** to add or update it in your GP
data, or **`n`** to decline. **Nothing is imported without your confirmation**, and a corrupted
transfer is rejected rather than accepted.

> **Experimental — LoRa.** This rides on CardSat's LoRa path, which is not yet fully
> hardware-validated. Treat element sharing as experimental, and always sanity-check an imported
> orbit against a known pass before relying on it. Requires LoRa enabled on both units on the same
> frequency/spreading-factor. This first stage handles **GP elements only**; sharing notes and
> rove plans is planned for a later release.

## Bug fixes (from on-device testing)

A round of fixes from bench testing on real hardware:

- **State-vector fit convergence.** The fitter is now reliable across orbit types. Several
  distinct issues were resolved: the forward model wasn't re-reading perturbed elements (the SGP4
  library caches on the TLE's first line, which the fit never changed) so its Jacobian came out
  empty; the fit epoch wasn't being carried into the propagator; and the old Gauss-Newton solver
  could stall on near-circular orbits or diverge on others. It now uses a Levenberg-Marquardt
  solver with a properly-varying forward model and converges in one or two iterations.
- **Web Files browser** now opens the `/CardSat` directory correctly (a path-sanitiser bug had
  been rejecting every request).
- **Heap** recovers fully after viewing a rove plan and downloading a file — the viewer buffer
  and a grid-scratch buffer are now released promptly, so a download after browsing a plan no
  longer runs the largest free block down.
- **Rove planner UI:** the grid field now upper-cases as you type; the results table columns are
  aligned; the "computing…" state clears to the results the moment the survey finishes; the
  Next-Passes footer advertises the `p` planner; and the results footer no longer runs off the
  screen edge.
- **LoRa send status** clears when a transfer finishes instead of sticking on-screen.

---

## Upgrade notes

- Drop-in from 0.9.50: no settings, log, favorites, or on-air format changes.
- New files are written under `/CardSat/RovePlans/` only when you export a rove plan.
- The Home menu is unchanged (still a locked 10×2 grid); every new feature hangs off an existing
  screen — the rove planner off Next Passes, the fitter and its reference off Tools, the saved-plan
  browser off the planner, the Files page off Web control, and element sharing off the Satellites
  and fit-result screens.

## Known limitations

- **LoRa element sharing is experimental** (untested radio path) and currently GP-elements only,
  with no automatic retry — a dropped chunk fails the transfer and the sender re-broadcasts.
- **Web file transfer is download-only** — no upload — and unauthenticated on the LAN; use trusted
  networks.
- **A fitted satellite's `B*` is zero** (a single state vector carries no drag information), so its
  predictions drift over days; re-acquire published elements once the object is catalogued.
- On-device fit residuals settle at tens of metres rather than sub-metre — that's the precision
  floor of the TLE number format the propagator round-trips through, not an error.
