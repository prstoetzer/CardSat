# CardSat v0.9.63 — release notes

A bug-fix and memory-optimization release. The BASIC fixes came from testing the bundled
example programs on hardware; the under-the-hood work substantially lowers permanent RAM use
and steadies the heap during network uploads on the no-PSRAM ESP32-S3.

# Fixes

### Graphics programs now stay on screen until you press a key

A BASIC program that draws and calls `SHOW` is meant to hold its frame until the next
keypress. It was vanishing instantly instead. The cause was in the main draw loop: it
clears the offscreen canvas and re-pushes it on every tick, and the periodic redraws
(status-banner expiry, the AOS-alarm check) fired one of those ticks immediately after
the program finished — blanking the display before you could see it. The draw loop now
leaves the display untouched while a `SHOW`n frame is being held, so the picture stays
up until you press a key to return to the console.

A follow-up fix for the same symptom: because a program runs synchronously when you press
Fn+r, that key is often still down (or its release / auto-repeat lands) on the very next
keyboard scan after the program's final SHOW -- which was being read as "the keypress that
dismisses the frame," so the picture vanished the instant it appeared. The console now
ignores keys for a short settle window (~0.45 s) right after a SHOW, so the launch key can't
dismiss the frame; a genuine press after that still returns you to the text console.

### `SATSEL` no longer crashes on larger catalogues

Walking the satellite catalogue from BASIC with `SATSEL` — as the `SKYDOME` example
does — could hard-fault the device. Decoding the crash showed a **loop-task stack
overflow**, not a logic error: a BASIC program runs the whole interpreter *inside* the
key handler, and `SATSEL` then calls down through the predictor into SGP4, whose
routines each carry very large stack frames. That combined call depth overran the
default 8 KB task stack. The loop task now gets a 16 KB stack, and the predictor keeps
its `Sgp4` scratch object off the stack, so the deep BASIC-to-SGP4 path has ample room.
(Ordinary tracking reached SGP4 from a much shallower stack and was never affected.)

### Loading a program reports "Loaded", not "Saved"

Picking a program to load worked, but the status line read "Saved". The load path fell
through to the generic settings-commit code that saves config and prints "Saved",
overwriting the correct "Loaded <name>" message. Load and save now each report their own
result.

### `SATSEL` in BASIC no longer aborts on a dead satellite

Selecting a catalog satellite whose element set can't be propagated right now (a decayed
object, or a stale TLE that SGP4 rejects) used to stop the whole program with a "bad sat"
error. That defeated the documented `SATOK` flag, which is meant to let a program branch
instead of halt. Now only a truly out-of-range index is fatal; a valid index that can't be
propagated sets `SATOK` to 0 and the program continues, so a full-catalogue scan
(`FOR I = 0 TO NSAT-1 : SATSEL I : IF SATOK = 0 THEN GOTO skip`) skips the dead bird
instead of aborting. The bundled SKYDOME example relies on exactly this.

### The BASIC program buffer is now reclaimable

Clearing a program with **Fn+n** now actually frees its memory (Arduino's `String` keeps
its buffer on `= ""`; only an explicit destroy releases it), and leaving BASIC with an empty
editor releases the program buffer too. A real program is still preserved if you leave and
come back, so nothing is lost — but an empty editor no longer holds a few KB for the rest of
the session.

# New

### A file browser for loading programs

**Fn+l** in the BASIC editor now opens a **list of your saved programs** instead of
asking you to type a name. Scroll with `;`/`.`, press **Enter** to load, or press `d`
twice to delete the highlighted program. The editor footer also spells out the keys it
was missing: `Fn+r run · s save · l load · n new · h help`. The list is allocated only
while the browser is open and freed when you leave it, so it costs no permanent RAM —
the same heap-on-demand approach the Dual-Rig screen uses (this matters on the
no-PSRAM ESP32-S3).

# Under the hood

### Less permanent RAM

A systematic heap-on-demand campaign moved buffers that are only needed while one screen is
open out of always-resident memory: they now allocate on entry and free themselves on exit
(the same approach the Dual-Rig screen uses). Converted: the BASIC file-browser list, the
CAT self-test log, the polar-arc samples, the **QSO-log view cache** (~8.9 KB, the biggest
single item), the illumination and equator-crossing tables (which rebuild before printing so
the About > Print reports still work off-screen), and the Notes / Wi-Fi-scan / CelesTrak-
search / AMSAT-report lists. Altogether about **17.9 KB** of permanent RAM (`sizeof(App)`
dropped from ~98.5 KB to ~80.6 KB, confirmed on-device) — which matters on the no-PSRAM
ESP32-S3, where the free contiguous block is what large TLS uploads and the audio path
compete for. No feature changed. A full audit, including the buffers that *looked*
convertible but were rejected (read off-screen by the serial/print-menu reports or the
world-map renderer, or shared across screens), is in `docs/design/RAM_AUDIT_0.9.63.md`.

### Steadier heap during uploads

The 16.2 KB drawing canvas, previously malloc'd once at boot and never freed, is now a static
buffer instead of a permanent hole in the middle of the heap. On-device tracing showed this
doesn't raise the largest-contiguous-block ceiling (that's a fixed property of the ESP32-S3
DRAM map, ~31.7 KB) but it does stop that block collapsing during LoTW / Cloudlog uploads —
the heap now holds steady through a full round of TLS POSTs instead of dropping low. The
`mem` serial command also gained a per-capability (8BIT / INTERNAL / DMA) heap summary to
make this measurable.

---

*The BASIC fixes were found and fixed while testing the bundled examples
(`examples/basic/`); the example programs themselves are unchanged and run within the
interpreter's limits. The RAM work is transparent — no behaviour changes, only lower
resident memory and a steadier heap.*
