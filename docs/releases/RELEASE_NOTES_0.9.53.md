# CardSat v0.9.53 — release notes

This is a **memory and reliability** release. It has one small user-facing feature
(multi-file download in web control); the substance is under the hood, and it fixes a
real bug: **LoTW/Cloudlog uploads failing partway through a multi-batch session** on this
no-PSRAM board.

Upgrading from 0.9.52 is drop-in — no settings, log, or on-air format changes.

---

## The upload fix (why this release exists)

On a no-PSRAM ESP32-S3 the number that governs a TLS handshake isn't total free memory —
it's the largest *contiguous* free block. Recent releases had eaten into that headroom, and
a LoTW upload session (which uploads QSOs in batches, one TLS connection each) could stall
on the second or third batch: the connection established, then the send stalled partway
through the body and never recovered. The device had enough total memory but not enough
contiguous room for the TCP send path to work while the TLS buffers were resident.

The fix is a set of memory reductions that raise the contiguous-block ceiling back up, the
largest of which is the display sprite change below. With them in place, on-device testing
shows the largest block holding steady at its ceiling before every upload and recovering
fully after each one — three back-to-back LoTW batches, all accepted, with zero send stalls.

### Half-size display sprite (the big one)

The screen is drawn into an off-screen sprite that stays resident in RAM the whole time the
unit is running. It was an 8-bits-per-pixel palette sprite (~32 KB). CardSat only ever uses
a 16-colour palette, so it now uses a **4-bits-per-pixel** sprite (~16 KB) — **colours are
byte-for-byte identical**, only the storage per pixel halves. That frees ~16 KB and, more
importantly, widens the largest contiguous free block that TLS depends on. The depth is a
single build-time switch, so it can be reverted in one line if ever needed.

### Smaller working buffers

A scratch buffer in the pass-overview builder was sized for a whole 10-day window when it
only ever holds one day of passes at a time; right-sizing it reclaims a few more kilobytes
with no change to what you see.

### On-demand audio (carried in from 0.9.52, worth restating)

The speaker's ~8 KB of audio buffers are now allocated only while sound is actually playing
— around games, voice memos, alarm beeps, and settings previews — and released afterward, so
they're not sitting in the middle of the heap during an upload. Between sounds that memory is
free.

Taken together, the unit now boots and idles with substantially more free memory and a solid
contiguous block, which is what makes uploads reliable again.

---

## Multi-file download in web control

The web **Files** page (the `/files` browser in web control) now lets you download **several
files at once**: tick the checkbox beside each file and press **Download selected**. The
browser saves them one after another.

This is deliberately implemented with **no extra memory cost on the device** — the unit still
streams a single file per request exactly as before; the multi-select and the sequencing live
entirely in the browser. (Because a page is handing the browser several downloads in a row,
some browsers ask permission the first time; just allow it.) The page remains download-only
and confined to `/CardSat`, same as before.

---

## Diagnostics

This build adds two low-cost log lines around uploads — a heap reading after each POST, and a
one-shot reading if a send ever stalls — that make the memory behaviour visible over the USB
serial console. They cost no runtime memory and exist to confirm the fix in the field; they
may be removed in a later release once the fix has proven out in normal use.

---

## Upgrade notes

Drop-in from 0.9.52. No changes to settings, the log format, the on-air/CAT behavior, wiring,
or any existing screen. The only user-visible additions are the **Download selected** control
on the web Files page and, indirectly, more free memory. If you flash this and the screen
looks wrong in any way (colours, artifacts), that would point at the 4bpp sprite on your
display/library combination — it can be reverted in one build-time line; please report it.
