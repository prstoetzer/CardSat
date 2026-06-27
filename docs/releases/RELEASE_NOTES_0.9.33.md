# CardSat v0.9.33 — Release Notes

**0.9.33** adds **optically-visible pass** prediction (a `*` flag across the pass
screens plus a new **visible-pass list**), lets **radio/rotator tracking keep
running in the background** after you leave the Track screen, and brings a batch of
**mobile web control** improvements (Doppler readout, tap-to-copy frequencies, a
visible-pass timeline with AOS notifications, and adaptive refresh). It also refines
the **decay/reentry flags** so the decay flag is a meaningful "this satellite is
actually coming down soon" signal, and trims one redundant line from the orbital
analysis Info page. A handful of bug and visual-layout fixes round it out.

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

## Decay flags are now stricter and meaningful

- **Flags only inside one year of decay.** The satellite-list decay flag (the
  small down-arrow) previously fired on loose criteria — a satellite up to two
  years from reentry, or simply sitting at a moderately low perigee, would show a
  flag even with years of life left. The flag now appears **only when the estimated
  time to decay is under one year**. Within that window it still tiers by urgency:
  watch (90 days to a year), soon (under 90 days), and imminent (under 30 days),
  with a very low perigee escalating the urgency. A low perigee on its own no longer
  raises a flag when the lifetime estimate is still long.
- **Space stations no longer get a decay flag.** The ISS and the Chinese Space
  Station (Tiangong) are regularly reboosted, so a decay estimate is meaningless for
  them. They are now excluded from the decay flag entirely and will never show the
  down-arrow.

The orbital analysis screen still shows the full decay estimate and solar-activity
range for any satellite, so the underlying numbers remain available even when no
flag is raised.

## Orbital analysis screen

- **Removed a redundant line.** The Info page had grown one line too tall for the
  screen after a recent addition. The standalone **Perigee** row has been removed —
  the perigee altitude is already shown on the **Apo/Peri** line — bringing the page
  back within the display height.

## Progress screens auto-advance on completion

- **Sun/Moon transit scan.** The transit calculator could appear to freeze near the
  end of its progress bar and would not move to the results until a key was pressed.
  It now advances to the results screen automatically the moment the scan finishes.
- **Sat-to-sat search.** Likewise, the sat-to-sat mutual-visibility search reached
  100% but waited for a keypress to show its results (and pressing Enter re-ran the
  search). It now shows the results automatically as soon as the computation
  completes.

## Optically-visible passes

- **Visible flag on the per-satellite passes screen.** The all-favorites "Next
  Passes" screen already marks optically-visible passes with a `*`. The individual
  satellite's passes screen now shows the same `*` flag for passes where the
  satellite is sunlit against a dark sky.
- **New visible-passes list.** From a satellite's passes screen, press **`V`** to
  open a new screen listing every **optically-visible** pass for that satellite over
  the next 10 days (filtered to sunlit-satellite, dark-sky, above the visibility
  elevation gate). Each entry shows the time, duration, peak elevation and LOS;
  press Enter on one to open its pass-detail plot. This complements the existing
  10-day chart (`v`), which is unchanged.

## Refinements and fixes

- **Visible-pass list reaches the full 10 days.** The new visible-passes list now
  scans the window day by day, so satellites with many passes are no longer cut off
  before the end of the 10-day span.
- **Smoother passes screen.** The optical-visibility flag on the passes screen is
  now computed once when the pass list is built instead of on every screen refresh,
  removing a per-second recomputation.
- **Better navigation from the visible-pass list.** Opening a pass's detail plot
  from the visible-pass list now returns you to that list (rather than jumping back
  to the passes screen), and the list correctly prompts to set your location when
  one isn't set.
- **On-screen key hints no longer run off the edge.** Several screens (the satellite
  list, Track, Sun/Moon, and others) had footer hint lines longer than the display
  width, so the last few keys were clipped. These have been shortened to fit, with a
  `h=help` pointer where the full key list lives on the Help screen. The new `V`
  visible-pass-list key is now documented in Help as well.

## Mobile web control

The web control page gains several improvements:

- **Background-tracking badge.** A green **RAD** / **ROT** / **R+R** indicator in the
  header mirrors the device, so you can see from your phone when the radio/rotator
  are tracking (and the rig may be transmitting).
- **Adaptive refresh.** The page now polls faster during a pass (so the Doppler
  frequencies stay current) and slows down between passes to save battery and
  traffic, with automatic backoff and a "reconnecting…" indicator if the link drops.
- **Doppler readout.** A small **±N.NN kHz** figure beside the downlink shows how
  much Doppler shift is being applied at that moment.
- **Tap-to-copy frequencies.** Tap the RX or TX frequency to copy it to the
  clipboard.
- **Visible passes and a timeline.** The upcoming-passes table marks **optically-
  visible** passes with a ★, and a compact timeline bar shows the passes laid out in
  time. **Tap a pass** to arm a browser notification for its AOS.
- **Steadier controls.** Rapid taps on the control buttons are debounced so a burst
  can't queue stale commands.

## Fixes

- **Background tracking stays on its satellite.** When radio/rotator tracking is left
  running after leaving the Track screen, selecting a different satellite now stops
  tracking and parks the rotator instead of silently retargeting the rig onto the new
  bird. Re-engage on the new satellite's Track screen.
- **Doppler readout on the higher bands.** The web page's Doppler-shift figure is now
  computed in 64-bit, so it stays correct for 13 cm (2.4 GHz) and other downlinks
  above ~2.1 GHz rather than overflowing.
- **Web pass selection.** Switching satellites in the web page now clears any selected
  pass and any armed AOS notification, so a stale selection can't carry over to a
  different bird.
- **Header clock no longer overlaps the unread-message envelope.** When unread LoRa
  messages and the clock were both shown, the clock was drawn over the envelope icon;
  the clock now sits to its left.
- **Settings "Web control" row fits.** The row no longer runs off the right edge with a
  full-length IP address (the redundant `http://` prefix was dropped).
- **Long message rows no longer clip.** In the Messages list, the message text now
  shrinks to fit the space left after a long sender callsign instead of overflowing
  the screen edge.

## Notes

No changes to the propagation, Doppler, CAT, rotator, or LoRa subsystems in this
release.
