# CardSat v0.9.20 — Release Notes

Hardening for the network layer's socket-failure recovery, plus the Easycomm and
SPID rotator backends.

> **Hardware status.** Host-verified (tokenizer-balanced; the recovery logic, the
> rotator wire-formats, and the new fetch-suspend behaviour checked off-device and
> compiled with mocks) but not run on a device this release. The rotator protocols
> especially want confirmation against a physical controller.

---

## Network: stronger socket-failure recovery

Building on the consecutive-failure WiFi reset and inter-fetch settle delay, two
further mitigations make the `-1`-class "connection refused" cascade much less
likely to strand the device:

- **LAN listeners stay freed for the whole update burst.** A full update fires
  several downloads back-to-back (GP, AMSAT, space weather, weather). The rigctld /
  rotctld / web listeners are now held released for the *entire* sequence — not
  rebuilt between fetches — so every outbound TLS connect in the burst gets maximum
  socket headroom. They rebuild automatically once the update finishes.
- **A reboot prompt when recovery is exhausted.** If hard-resetting WiFi repeatedly
  fails to bring the connection back, CardSat now shows a clear **Network problem**
  prompt offering a reboot (ENTER/`y` to reboot, `` ` ``/`n` to keep running) rather
  than ever rebooting silently underneath you. A reboot is the last-resort cure once
  the socket stack is wedged; the choice stays with the operator.

## Rotators: Easycomm (I/II/III) and SPID Rot2Prog

New **Rot type** choices, all over the same I2C->UART bridge the GS-232 backend
uses:

- **Easycomm I, II, and III** — the open, plain-ASCII tracking protocol used by
  SatNOGS, K3NG, ERC, and most homebrew rotator controllers.
- **SPID Rot2Prog** — the binary protocol of SPID MD-01/MD-02 controllers.

Settings → Rotator → **Rot type** cycles all eight backends. Both new protocols are
host-verified only — verify against a physical controller before trusting them.

> **GS-232A and GS-232B are both supported** (and always have been): the GS-232
> backend sends the `W` point command both accept, and parses both the GS-232A
> (`+0aaa+0eee`) and GS-232B (`AZ=aaaEL=eee`) position-reply formats.

---

## Voice memo (SD card required)

Press `v` on the **Track**, **Manual**, **large-font readout**, or **Polar** screen
to record a short spoken note during a pass — handy for "worked W1ABC, strong
signal" without breaking from tracking. A red **REC** badge with a countdown shows
top-right while recording, and **radio, rotator, and web control keep running** the
whole time (the memo is captured a small block at a time between tracking updates).
Press `v` again to stop, or it stops at the 30-second cap.

Memos save as 16 kHz mono WAV files under **`/CardSat/audio/`** on the SD card, named
by UTC timestamp. **An SD card is required** — with no card, `v` reports "SD card
required" and does nothing. Retrieve memos by reading the card on a computer.

Host-verified only — the WAV writing and cooperative capture were checked off-device,
but the mic/SD interaction wants confirmation on real hardware before you rely on it.

---

## Notes

- All host-verified only. The socket-recovery hardening, the listener-suspend
  behaviour, and the rotator wire formats were checked off-device (balance + g++
  semantic compiles + round-trip tests for the SPID frame encode/decode), but
  on-device confirmation is still needed — especially the rotator protocols against
  real hardware and the reboot prompt against an actually-wedged socket pool.
