# CardSat v0.9.34 — Release Notes

**0.9.34** adds **direct Logbook of the World (LoTW) upload**: CardSat can now sign
your satellite QSOs and send them straight to ARRL's LoTW over WiFi — no PC, no
TQSL, no separate upload step. It builds the same cryptographically-signed `.tq8`
file TQSL would, signs each contact with your callsign certificate's private key,
and posts it to LoTW's self-authenticating upload service. This is the biggest
single workflow addition since the rotator backends.

## Upgrading

Two prebuilt binaries ship with the release. **`CardSat.bin`** installs through
**[Launcher](https://github.com/bmorcelli/Launcher)** and **preserves your saved data**
(settings, calibration, per-satellite notes, favorites, cached elements) — the
recommended in-place update. **`CardSat_Merged.bin`** is a complete standalone image
for **M5Burner** or a **direct flash** at `0x0`; it carries an empty filesystem, so
flashing it erases **internally-stored** data. CardSat prefers a **microSD card** for
storage, so **if you run with an SD card in, your configuration persists across any
flash**. Confirm the new version on the **About** screen after flashing.

## Direct LoTW upload (new)

- **Sign & upload from the device.** A new **Sign & upload to LoTW** action on the
  **Log** menu signs every un-uploaded satellite QSO into a `.tq8` and uploads it
  directly to LoTW. The signed payload is self-authenticating, so no login is needed.
- **Requires a microSD card and your LoTW key.** This is an *upload* feature, not
  enrollment — ARRL still issues first-time certificates only through TQSL plus a
  mailed postcard. You enroll once on a computer the normal way, then copy your
  credential to the card as two PEM files (`lotw_key.pem` + `lotw_cert.pem` in
  `/CardSat/`, produced from your TQSL `.p12` with a one-line `openssl pkcs12`
  command — see the manual). **Your private key lives on the SD card; use a card you
  control.** CardSat loads the key only at upload time, keeps it in RAM only, and
  never copies or transmits it except as the signature inside the `.tq8`.
- **Station location in Settings.** New **LoTW DXCC**, **LoTW CQ zone**, and **LoTW
  ITU zone** fields under *Settings → Station / display* fill out the `.tq8` station
  record; your callsign and grid come from the values CardSat already has.
- **No duplicate uploads.** LoTW rejects re-uploads, so CardSat tracks what it has
  sent (a new `uploaded` column in `qso_log.csv`) and flags QSOs once they've reached
  LoTW so they're never sent twice. Large backlogs go in batches of up to 50.
- **Satellite names handled.** The same `/CardSat/lotw_sats.csv` map used by the ADIF
  export translates each bird to its six-character LoTW `SAT_NAME`.

The `.tq8` byte format and the RSA-PKCS#1-v1.5-over-SHA-1 signature were validated
host-side against ARRL's published `.tq8` specification and OpenSSL's verifier before
this shipped; the on-device signing uses the mbedTLS library already built into the
firmware. The one thing that can only be confirmed against the live service is the
exact set of station fields LoTW accepts — verify your first upload landed by checking
your LoTW account's activity page.

## Upcoming activations feed (new)

- **Activations** on the main menu shows the **upcoming satellite activations**
  scheduled on **[hams.at](https://hams.at)** — roves, grid activations, and special
  operations announced ahead of time. CardSat downloads the feed over WiFi and shows
  a scrollable list (date, callsign, satellite, grid); **ENTER** opens a detail view
  with the start/end times (UTC), mode, frequency, and the activator's comment, and
  **`r`** refreshes. A quick way to know who's planning to be on which bird, from
  where, and when. The feed parser was validated host-side against the live feed.

## Notes

- The printed **cheat card** stays **2 pages**; the font was reduced slightly and
  line spacing tightened to fit the new LoTW reference. The 4×6 layout is otherwise
  unchanged.
- No changes to the radio, rotator, Doppler, or pass-prediction paths in this release.
