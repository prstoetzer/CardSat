# CardSat v0.9.35 — Release Notes

**0.9.35** adds a built-in **Notes** editor — a free-form, multi-page text editor
with a file browser, reachable from the **Log** menu — and fixes a memory bug that
could make **LoTW uploads fail** with "Sign failed: gzip/write failed" on the
no-PSRAM Cardputer.

## Upgrading

Two prebuilt binaries ship with the release. **`CardSat.bin`** installs through
**[Launcher](https://github.com/bmorcelli/Launcher)** and **preserves your saved data**
(settings, calibration, per-satellite notes, favorites, cached elements) — the
recommended in-place update. **`CardSat_Merged.bin`** is a complete standalone image
for **M5Burner** or a **direct flash** at `0x0`; it carries an empty filesystem, so
flashing it erases **internally-stored** data. CardSat prefers a **microSD card** for
storage, so **if you run with an SD card in, your configuration persists across any
flash**. Confirm the new version on the **About** screen after flashing.

## What's new

### Notes — a free-form text editor

A new **Notes** entry on the **Log** menu opens a browser of your saved notes, each
stored as a plain `.txt` file under `/CardSat/notes/` (on the SD card if present,
otherwise internal flash — so notes work with or without a card). From the browser
you can create a new note (`n`), open one (Enter), or delete one (`d`, press Enter
to confirm).

The editor is a full multi-line text editor. Type normally; **Enter** inserts a new
line and **Backspace** deletes. Because the Cardputer's `;` `.` `,` `/` keys are
needed as ordinary punctuation, all editor commands use the **Fn** modifier, so
every plain key types literally: **Fn+`,`** / **Fn+`/`** move the cursor left and
right, **Fn+`;`** / **Fn+`.`** move up and down, and **Fn+`s`** saves (you'll be
asked for a name the first time). Press **`` ` ``** to exit — unsaved changes are
written automatically so nothing is lost. The browser shows each note's last-edited
date and time (UTC), newest first. Notes can be up to 4 KB each.

## Bug fixes

- **LoTW upload "Sign failed: gzip/write failed" / "low memory (gzip)".** Building
  the signed `.tq8` was compressing it with miniz, whose compressor needs a single
  ~160 KB working block (a 32 KB dictionary plus large hash tables) that simply
  cannot be allocated contiguously on the no-PSRAM ESP32-S3 — so the gzip step
  failed regardless of how much total heap was free. The `.tq8` is now written with
  *stored* (uncompressed) gzip framing, which is a valid gzip stream that LoTW
  accepts and which needs no working memory at all. The drawing sprite stays
  resident the whole time, so the screen keeps updating during the upload. A typical
  batch is only a few kilobytes, so skipping compression costs nothing meaningful.

## Notes for testers

The Notes editor's **Fn-key cursor movement** is new and hardware-dependent; if Fn
behaves unexpectedly on your unit, that's the first thing to report. The LoTW gzip
fix is best confirmed by watching the serial log during an upload — the new
`[lotw] heap before compressor: free … largest …` line shows whether the ~32 KB
block was available.
