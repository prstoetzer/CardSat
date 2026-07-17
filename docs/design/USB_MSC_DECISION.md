# USB mass storage — DECLINED

**Decision (Paul, 0.9.58): not building it. The web interface already gets data out of CardSat.**

That closes the question. The analysis below is kept because it establishes a fact that matters
for the *other* USB work: **MSC exposes a raw block device**, and there is no "expose one folder"
primitive in the protocol — so the `/CardSat` sandbox that CardSat's storage posture demands
("never formats your card") would have required synthesising a FAT filesystem. Not worth it for a
problem the web UI already solves.

---

## Original analysis (retained for the record)

## The requirement

> "USB mass storage mode should only be active when on the mass storage screen to allow users to
> add or remove files. The sandbox should be the /CardSat folder on whichever file system is
> active LittleFS or SD. The serial monitor should be active otherwise."

The **mode-switching** half is straightforward and matches Mini-FT8's `C` key: CDC console
normally, MSC only while the screen is open, back to CDC on exit. No problem there.

**The sandbox half collides with how USB MSC works**, and I would rather say so than build the
wrong thing.

## The conflict

`USBMSC::begin(block_count, block_size)` with `onRead(sector, ...)` / `onWrite(sector, ...)`
callbacks. MSC exposes a **raw block device**. The PC runs *its own* filesystem driver over those
sectors — CardSat just serves blocks and has no idea which files are being touched.

**There is no "expose one folder" primitive.** It does not exist in the protocol.

So "sandbox to `/CardSat`" cannot be done by forwarding the real volume's sectors: the PC mounts
the whole card and sees everything on it.

This matters more for CardSat than it would for most projects, because of a deliberate existing
decision — the manual says CardSat **"never formats your card"**. The SD is *the user's card*;
CardSat is a polite guest in `/CardSat`. Exposing the whole volume over USB puts the user's
unrelated files in the blast radius of a PC that believes it owns the disk.

## The options

### A. Whole-volume MSC, clearly labelled

Forward sectors straight to `SD.readRAW`/`writeRAW` (both exist, along with `numSectors()` and
`sectorSize()`).

- **Effort:** small — perhaps 150 lines including the screen.
- **Gets you:** files off without pulling the card. Which is the actual goal.
- **Costs:** the PC sees the entire card, not just `/CardSat`. Not what you asked for.
- **Mitigation:** say so plainly on the screen — *"exposes your whole SD card"* — and note that
  the user already owns everything on it.
- **LittleFS case:** doesn't work at all. LittleFS is not FAT; a PC can't mount it. So SD-only.

### B. Virtual FAT12 image containing only `/CardSat`

Synthesise a filesystem: boot sector, FAT tables, directory entries, and translate the PC's sector
writes back into file operations.

- **Gets you:** exactly what you asked for, on both filesystems.
- **Costs:** this is **writing a FAT driver**. Reads are tractable. **Writes are the hard part** —
  the PC writes sectors in whatever order it likes, updates FAT and dirents independently, and
  expects the result to be coherent. Getting this subtly wrong corrupts files silently.
- **Honest estimate:** read-only ~400 lines and testable; read-write is a different category of
  work, and you specifically asked for *"add or remove files"*.

### C. Read-only virtual FAT of `/CardSat` now

Option B minus the writes.

- **Gets you:** the sandbox, and the common case — pulling reports, logs, memos, ADIF off the
  device. Works on both SD and LittleFS. No corruption risk: the PC literally cannot write.
- **Costs:** dropping files *in* (`gp.json`, `calib.txt`, `.bas` programs) still needs the card
  reader or the web UI.

### D. Mini-FT8's answer: a dedicated FAT partition

Mini-FT8 sidesteps all of this by keeping its files on a **dedicated `fatfs` partition** it
exposes wholesale. Naturally sandboxed, no synthesis, real FAT.

- **Costs:** CardSat's internal storage is **LittleFS**, and its primary storage is the **user's
  SD card**. Adopting this would mean a partition-table change plus data migration, and it would
  still do nothing for SD-card users — who are the majority.

## What I'd recommend

**C, then B's writes later** — if the sandbox requirement is firm.

**A, if the real goal is "stop making me pull the SD card"** and you're comfortable telling users
the whole card is exposed. It's a fraction of the work and delivers the actual benefit this week.

My read: you asked for the sandbox because CardSat's whole storage posture is *"this is the
user's card, we're a guest."* That instinct is right and A violates it. But C delivers 80% of the
value at 20% of B's risk, and the "add files" case already has two working answers (card reader,
web UI).

## What I'm not doing

Guessing. This is a fork with real consequences — a corrupted SD card is a much worse outcome than
a missing feature, and option B is exactly the sort of code where I'd be writing a filesystem
driver against a spec I'd be recalling rather than checking. Given this session's track record on
recalled hardware facts, that deserves a deliberate decision rather than my confidence.

**Which way do you want it?**
