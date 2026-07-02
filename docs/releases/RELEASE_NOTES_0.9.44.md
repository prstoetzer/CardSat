# CardSat v0.9.44 — release notes

A LoRa-focused release that builds on 0.9.43's networking work, plus a documentation
audit. Nothing changes the on-air message format — everything here is backward-compatible
with 0.9.43 CardSats.

## LoRa: a station roster and presence pings

Press **`o`** on the Messages screen to open a new **station roster** — a live list of every
station heard reporting a position, newest first, showing:

- **callsign** and its **Maidenhead grid** (computed locally from the reported lat/lon),
- **distance and bearing** from your location (when you have a fix),
- last **signal** (RSSI) and how long ago it was heard.

Scroll with `;`/`.`, press **ENTER** to open a **bearing compass** to the selected station,
or press **`p`** to broadcast your own position — a **presence ping** — so other operators
add you to their rosters. The roster is built from ordinary `@lat,lon` messages, so a single
`p` press both announces you and populates everyone else's list.

## LoRa: optional automatic position reply

A new **Auto position reply** setting (Settings → Network/data, **off by default**) makes
CardSat answer a received position report with its own `@lat,lon` automatically — useful for
a net where everyone wants to see who's where without each operator pressing `p`. Because it
broadcasts your location, it's opt-in, and it's loop-guarded so two auto-replying units can't
flood the channel: a short random delay before replying, at most one reply to a given station
every few minutes, and never more than one auto-reply every 30 seconds overall.

## LoRa: NORAD IDs for satellite references

`#SAT` and `!SAT` messages now include the satellite's **NORAD catalog number**
(`#name/norad`, e.g. `#RS95S/57173`). The number is invariant across every station's
database, so a satellite reference resolves correctly even when two stations use different
display names for the same object — the case that failed before, where one CardSat calls a
bird **RS95S** and another calls it **QMR-KWT2**. The name still comes first (human-readable,
and older receivers still see it), and a bare name with no NORAD still works by name. The
`@lat,lon` position format is unchanged.

## LoRa: grid on the bearing compass

The bearing compass (opened from a received `@position`, or from the roster) now shows the
peer's **Maidenhead grid** alongside the distance, bearing and lat/lon.

## Fixes

- **Sked callsign.** Hardened the sked pre-fill so a received `!SAT` proposal reliably
  carries the sender's callsign into the editor's Call field (a latent buffer-termination
  issue on that path is fixed).

## Audio plays through network fetches now

Earlier firmware **stopped the speaker around every HTTPS fetch**. That was a workaround
for the old mbedTLS stack, which drew its handshake buffers from the same internal
DMA-capable memory the audio driver uses — so audio being on could starve a connection.
The BearSSL migration in 0.9.43 removed that conflict, and on-device measurement confirms
it: HTTPS fetches (downloads and LoTW/Cloudlog uploads) all succeed with the speaker
enabled, and the internal DMA pool stays flat right through each handshake because BearSSL
doesn't draw from it. The speaker teardown is therefore **removed** — audio (a playing tone,
a game sound, voice-memo playback) now continues uninterrupted while CardSat is on the
network.

## Documentation

The user manual, README, and features list had drifted; this release audits them: the
**reboot-between-batches** descriptions for LoTW/Cloudlog uploads and the transponder cache
are corrected (those run in a single session as of 0.9.43), the **six built-in mini-games**
(reached with `z` from About) are now documented — they were previously missing or only
partly covered — and the new LoRa roster / presence / auto-reply / NORAD features are
described. The manual and cheat-card PDFs are regenerated.

## Compatibility

Fully interoperable with 0.9.43 CardSats. A 0.9.43 unit receiving a `#name/norad` message
will read the whole `name/norad` as the name (it doesn't split on the `/`), so it resolves
that particular reference no better than before — but no worse, and everything else,
including all `@lat,lon` positions and the roster they feed, works across versions.
