# CardSat Printing Implementation

*A complete technical description of CardSat's printing subsystem: the three-sink
emitter, eight page-description languages, two network transports, on-device raster
generation (PWG and URF), the IPP client, the capability probe, and the memory model
that makes all of it fit on a no-PSRAM ESP32-S3.*

> **Why this document exists.** As far as we are aware, no other M5Stack Cardputer
> project generates printer raster on the device and drives a driverless/AirPrint
> printer directly. The techniques here — streaming a page-description language with
> zero resident buffering, and rendering a full 300-DPI page one scanline at a time in
> a few kilobytes — are reusable well beyond CardSat, so they are written up in full.

---

## 1. Design goals and constraints

CardSat runs on an M5Stack Cardputer ADV: an **ESP32-S3FN8, no PSRAM**, 8 MB flash, a
240×135 screen. The binding constraint for printing is memory — specifically the
**largest contiguous free heap block**, which on a no-PSRAM part is far smaller than the
total free heap. A letter page at 300 DPI is ~1 MB as a full bitmap; that cannot exist
on this device. Every design decision below follows from that fact.

The goals:

1. **Zero steady-state memory.** Printing must cost nothing when idle and only a
   transient socket-sized amount while active — the same heap class as an ordinary HTTP
   fetch. No report is ever buffered whole.
2. **The field receipt printer is the primary target.** CardSat is built for the
   pocket, battery WiFi thermal printer a rover carries — ESC/POS over raw TCP 9100.
   Everything else is a bonus that must not compromise this path.
3. **Reach as many other printers as possible, for free.** Office printers (PCL,
   PostScript), label printers (ZPL), and — notably — the driverless/AirPrint printers
   that make up the overwhelming majority of home and club printers, all from the same
   streaming core with no libraries and no resident RAM.
4. **No printer required.** Every report also fans out to the USB serial console and to
   a text file on the SD card, so the data is always retrievable.

## 2. Architecture: the three-sink emitter

All printing lives in `src/print.cpp` / `src/print.h`, in `namespace Printer`. (As with
all CardSat code, the same implementation is mirrored byte-for-byte into the monolithic
`CardSat.ino`; see `DEVELOPMENT_METHOD.md`.) The public surface is small:

```
namespace Printer {
  enum Format { FMT_ESCPOS=0, FMT_TEXT=1, FMT_PCL=2, FMT_POSTSCRIPT=3,
                FMT_ESCP2=4, FMT_STAR=5, FMT_ZPL=6, FMT_PWG_RASTER=7,
                FMT_URF_RASTER=8 };
  enum Transport { RAW9100=0, IPP=1 };
  struct Sinks { host; port; printerCols; format; toSerial; toFile;
                 fileTitle; transport; ippResource; };
  bool   begin(const Sinks& s);           // open the sinks, send preamble
  void   line(const String& s);           // one logical report line -> all sinks
  void   blank(); void title(const String&); void rule();
  void   feedCut();                        // finish the page
  void   end();                            // flush + close
  bool   printerOk();                      // did the printer connect this print?
  bool   ippAccepted();                    // did IPP return HTTP 2xx?
  String probeCapabilities(host, port);    // ask a printer what formats it accepts
}
```

A report is written **once**, as a sequence of `title()`, `line()`, `blank()`, `rule()`,
`feedCut()` calls, with no knowledge of the output format. `begin()` fans those calls out
to up to three **sinks** simultaneously:

- **Network printer** — a `WiFiClient` to a TCP printer (raw 9100 or IPP 631).
- **Serial console** — the report echoed to USB serial at 115200.
- **File** — an 80-column text file under `/CardSat/Reports/<title>-<uptime>.txt`.

The serial and file sinks always emit plain text. The network sink emits whichever of the
eight page-description languages is selected. Each sink **hard-wraps to its own column
width**, so a 32-column receipt printer stays inside its paper while the file keeps the
full 80-column layout.

### 2.1 Per-sink wrapping

`wrapTo(s, width, writeLine)` is a word-aware wrapper (template on the line-writer):

- It breaks at the last space that fits within `width`.
- A single word longer than `width` is hard-broken so nothing overflows the column.
- The space at a break point is consumed (no leading-space artifacts on the next line).
- Blank lines are preserved.

It is shared by every sink and every text format, so improving it improves all outputs at
once (receipt, PostScript, ZPL, serial, file, and the raster renderer's line collection).

## 3. The eight page-description languages

The network sink branches on `s_fmt` only where the wire format actually differs —
`begin()` (preamble), `feedCut()` (page finish), and the per-line emit. Everything else is
format-agnostic.

| # | Format | MIME / language | Notes |
|---|---|---|---|
| 0 | ESC/POS (receipt) | ESC/POS control codes | Default. Centered/bold titles, paper cut. Reference: Epson TM-P20II, GZM8022. |
| 1 | Plain text | none | No control bytes; universal raw-9100 fallback. |
| 2 | PCL (HP) | `application/vnd.hp-PCL` | PJL-wrapped: UEL + `@PJL ENTER LANGUAGE=PCL` + `ESC E` + fixed-pitch Courier + form-feed + closing UEL. |
| 3 | PostScript | `application/postscript` | Minimal Courier document, automatic page breaks, validated under Ghostscript. |
| 4 | ESC/P2 (Epson) | ESC/P2 | Reset, text, form-feed. *Untested on hardware.* |
| 5 | Star Line | Star Line Mode | Reset, text, Star feed + cut. *Untested on hardware.* |
| 6 | ZPL (Zebra) | ZPL II | Each line -> positioned `^FO/^FD` field in an `^XA…^XZ` label; new label on overflow. *Untested on hardware.* |
| 7 | PWG raster | `image/pwg-raster` | On-device raster render; streamed over IPP. **Confirmed on a real AirPrint printer.** |
| 8 | URF raster | `image/urf` | Apple Raster; same render, Apple container. |

Formats 0–6 are **streaming text**: each `line()` writes characters (with format-specific
control bytes) straight to the socket. Formats 7–8 are fundamentally different — see §5.

### 3.1 PostScript and ZPL page state

PostScript and ZPL are not line-at-a-time streams; they carry a **cursor**. The emitter
keeps minimal page state:

- **PostScript**: Courier 10 pt on US Letter — 72 lines fit in a 720 pt column from a 750
  pt top with 12 pt leading. `psShowLine()` emits a `moveto … show`, decrements the
  baseline, and ejects with `showpage` + a new `%%Page` when the column fills.
- **ZPL**: each line becomes `^FO(x,y)^A0N,h,w^FD(text)^FS`. `y` advances down the label;
  when it passes the label height the label is closed (`^XZ`) and a new one opened.

Both escape their control characters (`\ ( )` for PostScript; `^ ~` for ZPL) and drop
non-ASCII so a stray byte cannot corrupt the job.

## 4. The two transports

`enum Transport { RAW9100, IPP }` selects how bytes reach the printer.

### 4.1 Raw 9100 (JetDirect)

The default. `begin()` opens a `WiFiClient` to port 9100 and every emit writes straight to
the socket. This is what virtually every receipt printer implements, and it is the
lowest-overhead path.

### 4.2 IPP (Internet Printing Protocol)

IPP wraps the page-description bytes in an HTTP POST to `:631/ipp/print`. It exists for two
reasons: office printers that expose IPP but not raw 9100, and — critically — driverless
printers, which **only** accept jobs over IPP.

The problem IPP poses for a streaming design: the HTTP body needs a length, but the report
is generated line by line and its length is unknown up front. The solution is **HTTP
chunked transfer-encoding**, which sends the body as a series of length-prefixed chunks
with no total declared. This preserves the zero-buffer model exactly:

```
POST /ipp/print HTTP/1.1
Host: <printer>
Content-Type: application/ipp
Transfer-Encoding: chunked
Connection: close

<hex-len>\r\n <IPP operation header>          \r\n   <- first chunk
<hex-len>\r\n <page-description bytes>         \r\n   <- one chunk per emit
…
0\r\n\r\n                                              <- terminating chunk
```

`begin()` writes the HTTP request line + headers (unchunked), then sets `s_chunkOpen`.
From that point, `sockWrite()` wraps every write as its own chunk: `<hex length>CRLF <bytes>
CRLF`. `end()` writes the `0\r\n\r\n` terminator and reads the HTTP status line; a 2xx sets
`s_ippAccepted`. The **IPP operation header** (a Print-Job operation carrying
`printer-uri`, `requesting-user-name`, `job-name`, and `document-format`) is built by
`ippHeader()` and sent as the first body chunk.

The `document-format` attribute is set from `s_fmt`: `application/postscript`,
`application/vnd.hp-PCL`, `image/pwg-raster`, `image/urf`, or `application/octet-stream`.

Raster formats (7, 8) **force IPP** regardless of the transport setting and default the port
to 631, because raster is only meaningful to a driverless printer.

## 5. On-device raster generation

This is the part with no precedent on the Cardputer. Formats 7 and 8 do not stream text —
they render the report to a **raster image of the page** and stream that. The whole point is
that this is possible in a few kilobytes.

### 5.1 Why band/scanline rendering is the only option

A US-Letter page at 300 DPI is 2550 × 3300 pixels. As an 8-bit grayscale bitmap that is
~8.4 MB; even 1-bit it is ~1 MB. Neither fits. But the raster formats are **line-oriented**:
the file is a page header followed by scanlines, top to bottom, each independently
compressed. So the device never needs the whole page — only **one scanline at a time**
(2550 bytes) plus a one-scanline hold buffer for run-length coalescing. Total working set:
under 10 KB.

The renderer (`RasterGen::renderTextPage`) produces the page as a pure function of
(page text, y):

```
for y in 0 .. height-1:
    fill scanline with white
    R  = (y - marginY) / rowPitch          # which text row
    gy = (y - marginY) % rowPitch          # pixel row within that text row's cell
    if R < line_count and gy < glyph_height:
        srcY = gy / scale                  # font pixel-row (integer scale)
        for each char in text[R]:
            blit that glyph's row `srcY` into the scanline (scaled)
    emit(scanline)                         # -> encoder -> IPP chunk
```

Because the page is produced strictly top-to-bottom and each scanline independently pulls
the correct pixel-row from each glyph, there is **no glyph-straddle problem** — a glyph that
spans a band boundary is simply drawn into whichever scanlines cover it, automatically.

### 5.2 The font

`src/font16x32.h` is a 16×32 monospace bitmap font covering printable ASCII (32–126), about
6 KB of flash as a `uint16_t[95][32]` table (one 16-bit word per glyph pixel-row). It is
integer-scalable: the renderer chooses a scale so the report's column width fits the printable
area (typically scale 2 → 32-px glyphs → ~64 columns across a letter page inside a safe
2400-px imageable width).

### 5.3 Report text → raster page

In raster mode, `line()`, `title()`, `blank()`, and `rule()` do **not** stream — they
**collect** the (word-wrapped) report lines into `s_rasLines[]`. Then `feedCut()` renders the
whole collected page through `renderTextPage()`, which streams the encoded raster over the
already-open IPP chunked connection. This keeps the report builders completely unaware of
raster: they call the same `line()`/`title()` API as every other format.

## 6. The raster encoders (PWG and URF)

Both formats share the **same run-length-encoded scanline data** and differ only in the file
magic and page header. Both encoders in `RasterGen` were validated **byte-for-byte identical
to the reference `ppm2pwg` / `ppm2pwg -f urf` tools** (github.com/attah/ppm2pwg) and their
output decodes pixel-perfect through the independent `pwg2ppm` decoder. See §8.

### 6.1 PWG Raster (`image/pwg-raster`, PWG 5102.4)

- **File magic**: the 4 bytes `RaS2`.
- **Page header**: exactly **1796 bytes**, big-endian, with a fixed field layout. The first
  64-byte field is the literal sync word `PwgRaster` (a common source of `document-format-error`
  when omitted). The header CardSat emits is 8-bit sGray (`ColorSpace = 18`, `BitsPerColor =
  8`, `BitsPerPixel = 8`, `BytesPerLine = width`, chunky order), `CrossFeedTransform = 1`,
  `FeedTransform = 1`, `TotalPageCount = 0`, `PrintQuality = 0`, `AlternatePrimary =
  0x00FFFFFF`, `PageSizeName = na_letter_8.5x11in`. Every field was matched against the
  authoritative `pwgpghdr.codable` serialization.

### 6.2 URF / Apple Raster (`image/urf`)

- **File header**: 8-byte magic `UNIRAST\0` followed by a big-endian page count (0 =
  unspecified/streaming).
- **Page header**: a compact **32 bytes** — `BitsPerPixel = 8`, `ColorSpace = 0` (sGray),
  `Duplex = 1`, `Quality = 0`, `MediaType = 0`, `MediaPosition = 0`, then big-endian `Width`,
  `Height`, `HWResolution`. Matched against `urfpghdr.codable`.

### 6.3 The run-length encoding (shared)

Both formats use the PWG PackBits variant, an exact port of ppm2pwg's `compress_line` with
one-byte pixels (sGray). Per line:

1. A **line-repeat count** byte: how many *additional* identical scanlines follow this one
   (0–255). Identical consecutive lines (e.g. the white top margin) coalesce into a single
   encoded line — this is why a mostly-white text page compresses ~40–50×.
2. Then the packed pixel runs:
   - **Repeat run**: a byte `0..126` meaning *(count − 1)* identical pixels, then the pixel.
   - **Verbatim run**: a byte `257 − N` (i.e. `129..255`) meaning *N* literal pixels, then
     the bytes.
   - **Single pixel**: byte `0`, then the pixel.

This is *not* standard PackBits (repeats use a positive count-minus-one, not `257−N`); getting
it wrong produces a file that a printer accepts and then discards. The exact scheme was
reverse-engineered against the reference and locked by byte-diff.

## 7. The capability probe

`probeCapabilities(host, port)` answers "what formats does this printer accept?" It sends an
IPP **Get-Printer-Attributes** operation (op `0x000B`) requesting
`document-format-supported`, reads the response, and scans it for the format tokens
(`image/pwg-raster`, `image/urf`, `application/pdf`, `image/jpeg`, `application/postscript`,
`PCL`), returning a short human summary like `"PWG URF PDF JPEG"`.

**Implementation note that matters:** the IPP response is *binary* and full of null bytes
(every attribute has 2-byte length prefixes). It must be read into a **raw `uint8_t` buffer**
and searched with a binary-safe `memcmp` scan — an Arduino `String` is null-terminated and
would truncate at the first `\0`, missing every format token. This is exposed in the UI as
**Settings → Network → Test printer (probe formats)**; it is a read-only query and sends no
print job, so it is always safe to run.

## 8. Validation methodology

The raster work established a discipline worth stating explicitly, because it is the reason
the encoders are trustworthy:

- **Validate against an independent reference, never your own decoder.** Early raster
  attempts were tested by round-tripping through CardSat's *own* decoder, which shared the
  encoder's bugs and hid them. The fix was to diff CardSat's output byte-for-byte against the
  reference `ppm2pwg` encoder, and to decode CardSat's output with the independent `pwg2ppm`
  — two separate programs, neither written by us.
- **Never send an experimental job to a real printer.** A malformed raster job can crash a
  printer (this happened once during development and required a power cycle). Every raster file
  is proven byte-valid on a PC before any printer sees it.
- **HTTP 200 means *accepted*, not *rendered*.** A raster-only printer will accept (200) and
  silently discard a PCL or PostScript job. Only a physical page — or a `job-state` of
  `completed` — confirms success. The capability probe exists so a user does not have to
  discover this by trial.

Host-side tools used during development (not shipped in firmware) live in `tools/`:
`ipp_probe.py` (send test jobs), `pwg_probe.py` / `pwg_diag.py` (query attributes), and the
reference `ppm2pwg` / `pwg2ppm` build.

## 9. Memory model summary

| Component | Resident cost |
|---|---|
| Idle (no print in progress) | **0** |
| Text formats (0–6), during print | one `WiFiClient` socket; lines stream, nothing buffered |
| Raster formats (7–8), during print | ~4 KB encoder hold-row + ~2.5 KB scanline compose buffer + socket ≈ **< 10 KB** |
| Font table | ~6 KB flash (not heap) |
| Capability probe | one 4 KB response buffer + socket, transient |

No path holds a whole report, a whole page, or a whole IPP response in a way that scales with
content. The largest single allocation anywhere in the subsystem is the 4 KB probe buffer.

## 10. Status and honesty

- **Confirmed on hardware**: ESC/POS receipt printing; PWG raster printing a legible page on
  a real AirPrint printer (the on-device raster was captured off the wire and verified
  byte-valid through `pwg2ppm`).
- **Validated on host, not yet bench-confirmed on a matching printer**: URF output (byte-
  identical to reference, but not yet sent to a URF-only printer); the capability probe's
  parse (validated against representative IPP responses).
- **Structurally validated, untested on hardware**: ESC/P2, Star Line, ZPL. These carry
  at-your-own-risk banners in the manual; the `/CardSat/Reports` file is the safe fallback.

The host gates (`tools/check_balance.py`, `check_parity.py`, `check_screen_text.py`) verify
brace balance and `src/` ↔ `.ino` parity but **not** compilation or semantics — the Arduino
compiler and on-device testing remain the backstop.
