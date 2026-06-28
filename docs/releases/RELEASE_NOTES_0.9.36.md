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
- **Fixed: LoTW rejected the station record on US uploads (county field).** The signed
  `.tq8`'s station record used the ADIF field names `STATE`/`CNTY`, but inside a `.tq8` the
  station record must use TrustedQSL's internal field names — `US_STATE`/`US_COUNTY`. LoTW
  didn't recognize the bare `CNTY`, applied a tiny default length limit, reported a "data
  length overflow", and discarded the whole station record — which then orphaned every
  contact ("STATION_UID doesn't match any tSTATION"). Both errors are fixed by emitting the
  correct field names. If you have a US county set and your QSOs were rejected, re-send them
  with the new re-send option.
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

- **Orbital analysis: live altitude could still read higher than the reported apogee.**
  An earlier fix made the live **Altitude** geocentric (matching how apogee/perigee are
  measured), which removed the large near-pole discrepancy. But a smaller one remained: the
  displayed **Apo/Peri** were computed from the satellite's *mean* orbital elements, while
  the live altitude comes from the full perturbed (SGP4) orbit, whose instantaneous height
  oscillates a few km around the mean orbit. Near apogee the live altitude could therefore
  edge a few km above the mean-element apogee — most visibly on near-circular orbits where
  that wobble is larger than the whole apogee-to-perigee spread. The Info page and web view
  now derive **Apo/Peri** by sampling that same perturbed altitude across a full orbit, so
  the live **Altitude** is always within the displayed apogee/perigee. The **footprint**
  calculation is unchanged — it still uses the true geodetic height-above-ground.

- **More robust uploads when memory is tight.** A failed upload (for example, after
  mistyping the Cloudlog URL) could leave the heap fragmented, and a subsequent upload would
  then fail to start its secure connection — a frustrating cascade that only cleared with a
  reboot. Before each secure upload CardSat now reclaims fragmented memory and, if there
  still isn't enough contiguous space for the TLS handshake, declines cleanly with a "low
  memory; try again" message instead of failing in a way that makes things worse. In
  practice a retry now succeeds without a reboot.
