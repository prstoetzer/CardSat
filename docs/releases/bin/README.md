# Archived release binaries

This directory holds **historical** CardSat firmware images kept for reference and
rollback. They are **not** the current release.

**For the current firmware, use the binaries in [`/firmware/`](../../../firmware/)**
(and see [`/firmware/README.md`](../../../firmware/README.md) for flashing instructions
and checksums).

Contents here:

- `CardSat-0_9_61-merged.bin` — v0.9.61 single-image full flash (0x0)
- `CardSat-0_9_61.bin` — v0.9.61 app-only image (0x10000 / OTA)
- `CardSat-0_9_61-bootloader.bin` — v0.9.61 bootloader (0x0)
- `CardSat-0_9_61-partitions.bin` — v0.9.61 partition table (0x8000)
- `FLASHING.txt` — v0.9.61 flashing notes

All files here are prefixed with their version so they can't be mistaken for the
current release images.
