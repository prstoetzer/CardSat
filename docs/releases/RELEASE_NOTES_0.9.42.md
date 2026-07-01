# CardSat v0.9.42 — release notes

A reliability and quality-of-life release for the no-PSRAM ESP32-S3. The headline is that
**large LoTW and Cloudlog uploads now work** — they are split into small batches, each
uploaded in its own clean reboot, so a full log goes up from a single prompt. Along the
way this release fixes a subtle **audio-vs-TLS memory conflict** that could make HTTPS
fail after playing a sound or a voice memo, adds a **persistent speaker-volume setting**
with live feedback, adds a **QRZ grid-backfill** utility for the log, and includes a
small **games** menu. The deep debugging behind the upload and audio fixes is written up
in `docs/design/UPLOAD_AND_AUDIO_TLS_POSTMORTEM.md`.

## Uploads

- **Large LoTW uploads now succeed (batched).** Previously a LoTW `.tq8` upload with more
  than a handful of QSOs would stall part-way through the body and fail. The cause was a
  fixed ESP32 TCP send-buffer ceiling (~5744 bytes) that a large `.tq8` overran — not the
  server, which accepts the exact same request from a PC. CardSat now uploads LoTW in
  **6-QSO batches**, each well under the ceiling. When more QSOs remain after a batch, the
  device **reboots and continues automatically**, carrying your key passphrase across the
  reboots so **you only enter it once** for the whole run. This applies to both "upload
  un-uploaded only" and "upload ALL" (re-send) modes. A 14-QSO upload, for example, goes
  up as 6 + 6 + 2 across three quick reboots and stops on its own.
- **Large Cloudlog uploads are batched the same way.** Cloudlog uploads in **15-QSO
  batches** (Cloudlog's records are smaller), rebooting to continue if more remain.
  Cloudlog needs no passphrase, so the continuation is fully automatic.
- **Why reboots?** On this no-PSRAM part, a single TLS handshake needs a large contiguous
  block of RAM, and a *second* handshake in the same session can't get it once the heap is
  fragmented — the same reason the "cache all transponders" feature reboots between
  batches. Each upload batch therefore runs in a fresh boot. Each reboot takes only a
  couple of seconds, and the upload screen shows progress ("Batch sent; N left,
  rebooting…").
- **Passphrase handling is safe.** During a multi-batch LoTW run the key passphrase is
  held only in volatile RTC memory, is validated against the reset reason on each boot,
  and is erased the moment the run finishes or fails. It is never written to the SD card or
  flash.
- **The upload screen now refreshes immediately when finished** instead of waiting for a
  keypress to repaint the final result.

## Audio & networking

- **Fixed: HTTPS could fail after playing audio.** After a game sound effect — or, more
  subtly, after recording or playing a **voice memo** — the next HTTPS request could fail
  with an out-of-memory error even though free memory looked fine. The ES8311 speaker's
  audio buffer and the TLS handshake draw from the **same** small pool of internal RAM, and
  the speaker's buffer is claimed on first sound and left resident. CardSat now releases the
  speaker's audio buffer before each secure connection and restores it afterward, so
  updates and uploads work reliably regardless of prior audio use. (Full writeup:
  `docs/design/UPLOAD_AND_AUDIO_TLS_POSTMORTEM.md`.)

## Settings

- **Speaker volume is now adjustable and persistent.** A new **Volume** row in Settings
  (just below Brightness) sets the speaker level from 0–100%. Adjust it with left/right;
  as you do, CardSat plays a short blip at the new level so you can hear it, and the
  setting is saved and survives reboots. Volume applies to the AOS alarm, game sounds, and
  voice-memo playback.
- **Game settings now persist.** The game **tilt-steering**, **sound**, and **Morse
  key-swap** toggles are now saved to the config and survive a reboot (previously they
  reset to off on every boot).

## Logging

- **QRZ grid backfill.** A utility to fill in missing `GRIDSQUARE` fields in your log by
  looking up callsigns on QRZ (requires QRZ credentials in Settings). It gathers the
  callsigns that need grids, looks them up with the log file closed (so the lookups have
  full memory), then rewrites the log with the grids filled in — de-duplicating repeated
  callsigns so each is fetched once.

## Games

- **Games menu.** A small collection of satellite-themed mini-games reachable from the
  About screen. They are written to use **zero heap** (fixed state, no per-frame
  allocation) so they don't affect the memory available for tracking and uploads.

## Under the hood

- The debug instrumentation added while diagnosing the upload and audio issues has been
  removed for release, except for a small amount of **operational logging** kept
  deliberately (heap-before-TLS, upload byte counts, and per-batch progress) so that field
  issues can be diagnosed from a shared serial console log.
- New/updated design docs: `docs/design/UPLOAD_AND_AUDIO_TLS_POSTMORTEM.md` (the full
  three-cause postmortem) and `docs/design/LOTW_UPLOAD_SIZE_WORKAROUNDS.md` (the batching
  workaround and the PlatformIO route that would remove the size ceiling entirely).

---

**Note on reliability testing.** LoTW upload batching (normal and re-send) is confirmed
working on-device. Cloudlog uses the identical batching design and is confirmed for small
uploads; its large-log (>15 QSO) multi-batch path is logically verified but had not yet
been exercised on real large data at release. If you upload a large Cloudlog log, the
`[cloudlog] batch …` lines in the serial console will show each batch's progress.
