# 0.9.67 — Telnet client shipped, SSH shelved (memory)

*Decision record. Grounds why 0.9.67 ships a **Telnet** terminal in Tools and defers **SSH**
to a later release, and — importantly — corrects a wrong assumption about which resource
unblocks SSH.*

## The decision

Paul's bench numbers at the time of this decision: **max free heap ~75 KB, largest contiguous
block ~31 KB** with CardSat's normal state resident. On those numbers:

- **SSH is unsafe to ship.** An SSH handshake (LibSSH-ESP32 over mbedTLS) needs one or more
  **large single contiguous allocations** in the 16–32 KB class — the same weight as the TLS RX
  buffer that the entire 0.9.53 release existed to make fit, and heavier, because LibSSH keeps
  its own session + per-channel buffers resident *on top of* mbedTLS. With a 31 KB largest block,
  the first big allocation is a coin flip and the second has nowhere to go: the realistic outcome
  is an allocation failure mid-handshake and a crash. The bare reference sketch
  (`Cardputer_ADV_ESC_POS_SSH_Terminal_Settings.ino`) runs SSH only because it has almost the
  whole ~180 KB DRAM region unfragmented — no 89 KB `App`, no resident 4bpp sprite, no satellite
  arrays. CardSat is the opposite environment.

- **Telnet is comfortable.** Plain Telnet over `WiFiClient` needs **no crypto, no handshake, no
  large contiguous buffer** — a socket plus a few-hundred-byte line buffer. It runs in the memory
  available now, with margin, and covers much of the real field use case: rotator controllers,
  APRS boxes, DX-cluster nodes, serial-to-network bridges, and anything on a trusted LAN.

So 0.9.67 ships the **full terminal** (config screen, ≤10 saved connections on `Store::fs()`,
modal line-oriented terminal with ANSI sanitizer, keyboard mapping, and printer streaming to all
sinks except IPP/raster) driven over **Telnet**. SSH is the same terminal with a crypto transport
swapped in — a small addition — gated behind a memory proof.

## The correction that matters for the next session

**The 8 MB partition (Proposal B) does NOT free the RAM that SSH needs.** It is easy to write
"shelve SSH until the 8 MB partition frees RAM," and that is **wrong**:

- The 8 MB partition change recovers **flash** (app partition 95% -> ~76%). Flash and DRAM heap
  are independent address spaces. More flash lets a bigger binary *link*; it does not add one
  byte of contiguous DRAM at runtime.
- SSH's blocker is **DRAM largest-contiguous-block**, not flash. The lever that unblocks SSH is a
  **heap/fragmentation win**: freeing resident DRAM (the sprite is the big one but can't be
  dropped/recreated safely on this M5GFX/IDF combo), or otherwise raising the largest-block
  ceiling above a measured SSH floor with the app resident.

If a future session wants SSH, the gate is an **on-device heap proof**, not the partition work:
bring up LibSSH-ESP32, attempt a real handshake + held session over `WiFiClient`, and measure
peak + resident largest-block **with the sprite and normal state resident**. Only if that clears
a measured floor (with margin) does SSH ship — behind a runtime guard that refuses to open a
session below that floor, mirroring the TLS-reclaim posture.

## What 0.9.67 leaves in place for SSH later

- The terminal, sanitizer, keyboard mapping, connection store, and printer path are all
  transport-agnostic. Adding SSH is: a second connection `type`, a LibSSH transport behind the
  same `beginSession()/readBytes()/writeBytes()/endSession()` seam Telnet uses, and the memory
  guard. No UI rework.
- LibSSH-ESP32 (already a resident M5 dependency) stays uninlined — the documented exception to
  the single-file mirroring model still applies when SSH is added.
- The `SSH_CLIENT_SCOPE.md` doc's recommendation #1 ("prove the hard part first") is now the
  literal gate, with the flash/RAM correction above folded in.
