# CardSat v0.9.38 — release notes

Small, focused release on top of 0.9.37: logging-workflow polish and an
upload-reliability failsafe extended to LoTW. See
[RELEASE_NOTES_0.9.37.md](RELEASE_NOTES_0.9.37.md) for the larger LoTW-location and
Cloudlog-reliability work that precedes this.

## Logging

- **Call field is pre-selected when you log from a live screen.** Pressing `l` to log a
  QSO from the Track, Big, Polar, or Manual tracking screens now opens the log form with
  the **Call** field already highlighted, so you can press ENTER and start typing the
  callsign immediately — no scrolling down past Date/Time/Sat first. Opening a new QSO
  from the Log menu still starts at the top (Date), since that isn't a "work them now"
  context.

- **The QSO log lists newest-first.** Viewing the log now shows entries in date/time
  order with the most recent contact at the top (the cursor lands there on open). Entries
  are sorted by their actual timestamp rather than just file order, so a QSO whose time
  was edited or backdated still sorts correctly. Editing and deleting continue to target
  the right entry.

## Upload reliability

- **Reboot-to-upload is now an automatic, confirmed failsafe — for both Cloudlog and
  LoTW.** If an upload hits the `-1` "connection refused" wall that a long session's heap
  fragmentation can cause, CardSat offers to reboot and complete the upload from a clean
  boot: **ENTER** reboots and uploads, **`` ` ``** cancels and leaves the QSOs pending.
  - It only offers this for a genuine **connect-level** failure. A real server response
    (e.g. a bad API key, or LoTW rejecting a record) is reported as-is — no pointless
    reboot.
  - **LoTW** is now covered too (it previously had no reboot path). Because the LoTW
    certificate passphrase is never stored, the reboot re-prompts for it after boot rather
    than persisting it anywhere.
  - The separate manual **`R`** key on the Cloudlog screen has been **removed** — the
    automatic prompt makes it redundant.

- **Fixed: the upload screen now updates when the upload finishes.** Previously the
  Cloudlog screen could stay showing "Uploading QSOs…" after the upload had actually
  completed (the result only appeared once you pressed a key). The result — success,
  rejection, or the reboot prompt — now paints immediately on every path, including the
  after-reboot upload.

## Interface

- **Space Wx, Weather, and Activations now refresh the same way.** All three data screens
  behave consistently: opening one shows its **cached data immediately**, then — if WiFi
  is up — it fetches an update in the background with an **"Updating Space Wx / Updating
  Weather / Updating Activations"** banner on the bottom status bar. When the fetch
  finishes it briefly shows the result (**updated**, **update failed**, or **WiFi not
  connected**) and the banner clears itself. Previously each screen behaved differently:
  Space Wx never auto-fetched (it sat on a blank screen asking you to refresh), Weather
  fetched before switching screens (so the banner showed on the menu), and the Activations
  banner could linger until you pressed a key. The on-screen **`r`** refresh uses the same
  flow.

## Notes

- This release contains no orbital-engine, CAT, or rotator changes; existing station
  settings, logs, and cached data carry over untouched.
- As with 0.9.37, the LoRa messaging path is built into the standard binaries but remains
  untested on real hardware.
