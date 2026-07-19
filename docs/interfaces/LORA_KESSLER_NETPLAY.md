# LoRa frame type 0xC7 — KESSLER two-player netplay (0.9.60)

CardSat's LoRa link carries broadcast text chat (magic `0xC5`) and chunked object
transfer (`0xC6`). Two-player KESSLER adds a third magic byte, `0xC7`. All frames
are fixed-layout and under 16 bytes, so they clear the 64-byte RX buffer with room
and never allocate. Frames not beginning `0xC7` are ignored by the game; `0xC7`
frames are skipped by the messaging path.

## Design: deterministic lockstep

A half-duplex radio can't stream a projectile's path. Instead both stations hold
the **same RNG seed** and run the **same physics**, so each independently builds
byte-identical terrain, station placement, and per-round wind, and simulates every
shot to the same pixels. Only three control events cross the air.

## Frames

| Byte | HELLO (`0x01`) | FIRE (`0x02`) | SYNC (`0x03`) |
|------|----------------|---------------|---------------|
| 0 | `0xC7` | `0xC7` | `0xC7` |
| 1 | `0x01` | `0x02` | `0x03` |
| 2 | seed low | shooter (0/1) | winner (0/1) |
| 3 | seed high | angle low | P1 score |
| 4 | gravity (0–2) | angle high | P2 score |
| 5 | play-to (rounds) | velocity low | — |
| 6 | — | velocity high | — |
| 7 | — | wind (`int8`) | — |

## Exchange

- **HELLO** host→guest, beaconed once a second (~15 tries) until answered. The guest
  echoes the same HELLO as its acknowledgement and both sides build round 1 from the
  seed. An unsolicited HELLO received outside a game is treated as an invite.
- **FIRE** shooter→peer, sent when a player launches. Carries the shooter's chosen
  wind so the peer's local simulation matches to the pixel. The peer replays the shot
  with re-broadcast suppressed.
- **SYNC** shooter→peer after a scoring round: authoritative winner and both scores.
  The seed stream advances one step on both ends for the next round; a reached
  play-to ends the match on both.

## Roles

Host is P1 (`mePlayer = 0`), guest is P2 (`mePlayer = 1`). Input is accepted only
when `turn == mePlayer`; the idle station shows "waiting for peer's shot". Loss of
the peer leaves the survivor on the last state rather than hanging. Range equals the
LoRa link's range; frequency/SF/BW are the shared messaging settings.
