# CardSat v0.9.36 — release notes

*Work in progress.*

## Logbook of the World

- **New: upload to Cloudlog / Wavelog.** CardSat can now upload satellite QSOs to a
  self-hosted **Cloudlog** (or compatible **Wavelog**) online logbook over WiFi, via
  **Log → Upload to Cloudlog**. Set your instance URL, a read-write API key, and your
  numeric station profile ID under *Settings → Station / display*. Since a Cloudlog
  instance can forward QSOs on to LoTW itself, this is generally an alternative to the
  on-device LoTW upload rather than an addition — CardSat tracks the two independently so
  nothing is double-counted. Supports re-sending already-uploaded QSOs (`a` to toggle).
  See the manual (§8 → Cloudlog / Wavelog upload) for setup.
- **The Log menu is now a scrolling list.** With the Cloudlog option added it no longer
  fits on one screen; core logging functions (New QSO, View/edit, Export, LoTW, Cloudlog)
  are listed first, with Voice Memos and Notes grouped at the bottom. The "N logged" count
  remains, and a `^`/`v` marker shows when there are more items above or below.

- **Fixed: satellite QSOs uploaded to LoTW were accepted but never posted.** CardSat was
  building the signed `.tq8` in an older format whose *signed data* didn't match what
  LoTW's current server expects, so LoTW accepted the upload but silently dropped the QSO.
  The `.tq8` is now produced in LoTW's current **"V2.0"** signing format — verified field
  by field against the TrustedQSL (tqsl) 2.8.6 source that LoTW itself distributes. This
  affects what gets cryptographically signed (station + contact data, normalized and
  uppercased), the record linkage (`CERT_UID`/`STATION_UID`), and the signature field
  tag. The full format is documented in `docs/design/LOTW_TQ8_FORMAT.md`. CardSat's upload
  endpoint and method were already correct (no change needed there). **If you uploaded
  satellite QSOs with an earlier version and they didn't appear in LoTW, use the new
  re-send option below to upload them again.**
- **New: re-send already-uploaded QSOs.** On the **Sign & upload to LoTW** screen, press
  **`a`** to toggle re-send mode, which includes QSOs already marked as uploaded (normally
  they're skipped). Press **`u`** to upload. This is useful for re-sending contacts that an
  earlier version flagged as uploaded but that never actually posted. Press **`a`** again to
  return to the normal "un-uploaded only" mode.

## Bug fixes

- **Privacy: secrets no longer appear in the USB serial log.** Diagnostic logging printed
  full request URLs, which for QRZ lookups included your username, password, and session
  key in plain text, and the new Cloudlog upload would otherwise expose its API key.
  Sensitive values in logged URLs are now masked (shown as `***`), and the Cloudlog API key
  is never logged. Only relevant for anyone with a serial monitor attached, but worth
  fixing.

- **Orbital analysis: live altitude could read higher than the reported apogee.** On
  the orbital-analysis **Info** page (and the mobile web page), the live **Altitude**
  was a *geodetic* height — measured from the oblate ellipsoid surface directly below
  the satellite — while **Apo/Peri** are computed the conventional way, as a
  *geocentric* distance from the equatorial radius. Because the ellipsoid surface is
  up to ~21 km closer to Earth's center near the poles, the geodetic altitude near a
  pole could exceed the equatorial-radius apogee, which looked wrong. The Info-page
  and web **Altitude** readouts now report a **geocentric** altitude (geocentric
  radius minus the equatorial radius), matching the convention apogee and perigee
  already use, so altitude always sits between perigee and apogee as expected. The
  **footprint** calculation is unchanged — it still uses the true geodetic
  height-above-ground, which is what actually governs which stations can see the
  satellite.
