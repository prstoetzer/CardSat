# CardSat v0.9.43 — release notes

A substantial reliability-and-features release. Three things headline it:

1. **HTTPS is now reliable at scale.** All network paths moved from mbedTLS to BearSSL,
   which fixed the residual "first download works, the next fails" problem on the
   no-PSRAM Cardputer. The device now runs **91 back-to-back TLS handshakes in one
   session with zero failures**.
2. **Every reboot-between-batches workaround is gone.** Because many handshakes now run
   cleanly in a single session, "cache all transponders" and the LoTW/Cloudlog uploads no
   longer reboot partway through. They just work, in one session.
3. **LoRa messages became actionable.** A received message can now carry a position, a
   satellite, or a schedule proposal, and CardSat decodes it into a bearing compass, a
   satellite detail screen, or a pre-filled sked — with matching send keys.

It also carries the **Activations footprint** feature (documented below), and it
introduces **one new build dependency** you must install to compile from source (see the
box at the end).

> **New build dependency:** building from source now requires the **`ESP_SSLClient`**
> library (by *Mobizt*), installed from the Arduino Library Manager. The firmware will
> not compile without it. Prebuilt binaries are unaffected — flash as usual.

---

## HTTPS reliability — mbedTLS to BearSSL

The Cardputer ADV has no PSRAM, and a full-screen display sprite stays resident so the UI
can repaint during fetches. That left the largest contiguous heap block pinned at 31732
bytes — about a kilobyte short of what an mbedTLS handshake needs (~32 KB contiguous). The
result was the maddening pattern where the first HTTPS download of a session succeeded and
a later one failed with a misleading "out of memory," even though total free heap looked
fine.

The fix was to **change the TLS stack**, not chase kilobytes: all five HTTPS paths now use
**`ESP_SSLClient` (BearSSL)**, whose largest allocation is a sizeable record buffer rather
than a monolithic handshake arena. Sized with `setBufferSizes(16384, 512)`, it fits in the
block we have — **with the sprite resident, so the screen never freezes.**

What you'll notice: downloads and uploads that were occasionally flaky are now dependable,
and the screen stays live throughout. The full technical story — six disproven theories,
the real root cause, and why the RX buffer must be 16 KB — is in
**[docs/design/NETWORK_TLS_MIGRATION_POSTMORTEM.md](../design/NETWORK_TLS_MIGRATION_POSTMORTEM.md)**.

## No more reboots mid-operation

Several features used to **reboot the device between batches** of network work, because
the old TLS stack couldn't run many handshakes per session. That's no longer true, so the
reboots are gone:

- **Cache all transponders** now fetches every satellite in **one session** — no reboots,
  no resume marker, no interruption. (Verified: 91 of 91 in a single run, zero failures.)
- **LoTW uploads** of a large log continue **in-session** between batches instead of
  rebooting and re-prompting for your key passphrase. The log is still split into
  size-bounded batches; it just doesn't reboot between them.
- **Cloudlog uploads** get the same in-session batching.
- The old **"Network cooling down — wait Ns"** message is gone. It was a 90-second lockout
  built to prevent a socket-pool problem that turned out not to exist.

The only reboots that remain are the ones you explicitly confirm: factory reset, and the
"reboot to upload?" recovery prompt that appears **only** if a connection genuinely fails.

## LoRa messages that do something

LoRa messages are still plain text on the air (the wire format is unchanged, so this is
fully compatible with other CardSats), but CardSat now **reads three sigils** out of a
message's text and turns them into actions:

- **`@lat,lon`** — a position. Selecting the message opens a **north-up bearing compass**
  showing distance and bearing to the sender, plus the message's age. (It's a bearing
  display, not a magnetometer compass — the Cardputer ADV has no magnetometer — so it
  reads out the bearing in degrees for you to orient by.)
- **`#SAT`** — a satellite name. Opens a **satellite detail** screen with the NORAD ID and
  the next pass computed for your location.
- **`!SAT YYYY-MM-DD HH:MM`** — a schedule proposal. Opens the **sked editor pre-filled**
  with the proposal so you can review and save it.

And matching **send keys on the Messages screen**, all for the currently selected
satellite / your own position:

- **`p`** — send your position (`@lat,lon` from your location/GPS).
- **`s`** — send the current satellite (`#name`).
- **`k`** — propose a sked: prompts for date, then time, then sends `!SAT date time`.

On the Messages list, scroll to select a message (the newest sits on the bottom line,
highlighted); if it carries a sigil, a hint shows what **ENTER** will open.

> **Note:** the *receive* side of these features (opening a compass/detail/sked from a
> message someone else sent) needs a second LoRa-equipped device to exercise fully. The
> *send* side works standalone.

## Activation footprint

(Carried in this release.) The **Activations** page answers the question that matters
before a rove: *do I actually have a footprint with this operator, and when?*

- **Footprint check on the activation detail.** Opening an activation checks whether the
  listed satellite is above the horizon for **both you and the activator** near the listed
  time, searching **±30 minutes** around the listed start so a pass that has drifted since
  the alert was posted is still found. The detail shows the mutual window's start–end and
  duration, "No footprint near listed time," or a short reason when it can't be computed.
- **Scrollable comment** with `;`/`.` on the detail screen.
- **Mutual-window polar plot** — press **`w`** to see the satellite's track from your site
  (cyan) and the DX site (orange) on one sky dome, with AOS/LOS/duration/peak elevation
  for each station. The same view is reachable from the existing **Mutual** schedule
  (ENTER on a window).
- **Tailored DX Doppler** — **`d`** from the mutual-window screen opens a DX Doppler table
  pre-set to the activation's own frequency, locking the dial to the listed frequency as a
  fixed downlink or uplink when it matches a real two-way transponder.

## Under the hood

- All five HTTPS paths (two GETs, three POSTs) run on BearSSL; the GET paths are
  hand-rolled over the raw client (with chunked-transfer decode, one redirect hop, and
  Content-Length handling) because `HTTPClient` requires a `NetworkClient` the SSL client
  doesn't provide.
- Upload batch continuation is **iterative, not recursive** — an earlier in-session
  version recursed one call per batch and overflowed the stack on the third batch of a
  large upload; it now loops at constant stack depth.
- The retired workarounds (the cooldown lockout, `SO_LINGER`, PCB/SRAM probes, TLS error
  diagnostics, reboot-per-batch machinery) are removed.

## Notes

- The LoRa message features, the single-session transponder cache, and the in-session
  uploads are new; the receive-side LoRa screens should be checked with a second device.
- As always, CardSat is amateur-radio software provided as-is; verify frequencies and
  operating details against the operator's own information before transmitting.

---

## Building from source — read this

If you build from source (Arduino IDE single-file `CardSat.ino`, or PlatformIO), you must
now install one additional library:

- **`ESP_SSLClient`** by **Mobizt** — Arduino Library Manager, search "ESP_SSLClient".

Alongside the existing dependencies: **M5Cardputer**, **ArduinoJson** (v7),
**TinyGPSPlus**, **RadioLib** (by Jan Gromes), and the Hopperpop **Sgp4** library. Full
setup in **[docs/BUILD_AND_FLASH.md](../BUILD_AND_FLASH.md)** and
**[docs/guides/ARDUINO_SETUP.md](../guides/ARDUINO_SETUP.md)**.
