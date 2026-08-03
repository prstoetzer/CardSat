# CardSat v0.9.71 — release notes

**A tools release.** Tiny BASIC gains strings, arrays and a way to ask the operator for
input; the calculators gain the satellite and antenna functions that were being worked
out on paper beside the device, plus a full on-screen function list and a printable
card; the MUF tool can answer "can I work *that* entity right now" instead of only
reporting fixed regions.

Underneath, the app partition grew from 3 MB to 4 MB, because the old layout was a
4 MB-part scheme leaving half of the Cardputer ADV's 8 MB flash unused and the firmware
had 97 KB of headroom left.

**Dual CAT is working** and has been flown on real hardware — a Kenwood TH-D75 on USB
as the downlink with an Icom IC-705 over its own Wi-Fi as the uplink. **Multiple USB
devices are not.** See *Known limitations* below; that is stated plainly because it is
the one thing in this release that will disappoint someone.

---

## Tiny BASIC

The interpreter had 26 numeric variables and no way to hold text. It now has:

- **String variables `A$`–`Z$`**, joined with `+`, compared with `=` and `<>`.
- **Text functions**: `LEFT$ RIGHT$ MID$ CHR$ STR$ UCASE$ LCASE$ TRIM$`, and the
  number-returning `LEN ASC VAL INSTR`. **Index rules follow Microsoft BASIC**, which is
  what a ported program expects: `MID$(s, start, len)` counts `start` from **1**, and
  `INSTR` returns a **1-based** position with `0` meaning "not found".
- **Named arrays**: `DIM A(n)`, several per statement, plus the original `@()`.
  `ERASE A` returns the memory early.
- **Constants** `PI TWOPI DEG RAD CLIGHT KBOLT REARTH`, and **maths** `ATN2 ASN ACS
  LOG10 ROUND FRAC HYP`. `ATN2(y,x)` matters: a bearing from `ATN(y/x)` loses the
  quadrant and divides by zero due east.
- **Station and geometry**: `GCDIST` / `GCAZ`, `DXCCLAT` / `DXCCLON` / `DXCC$`,
  `GRID$(lat,lon)`, `TIME$`, `DATE$`. These call the firmware's own routines, so a
  program cannot disagree with the tracker it is running on.

### Asking the operator for input

A program declares what it needs and CardSat asks **once, before the run**:

```basic
110 INPUT "DXCC code"; C
120 INPUT "Callsign"; N$
```

`Fn`+`R` shows a single form with every declared field, then the program runs to
completion with the variables already set. Programs that declare no `INPUT` run
immediately, exactly as before.

Nothing pauses mid-run to ask, and that is deliberate: the interpreter executes inside
one key handler, to completion, which is what makes it safe to run BASIC while a radio
is being tuned.

### Memory, because this is a 76 KB device

The string table and every array are allocated **on demand** and freed with the program.
All arrays together may hold 2048 elements — the budget is shared rather than per-array,
because 26 full-size arrays would be 208 KB of doubles on a device that is also flying a
radio. A subscript outside an array **stops the program** rather than quietly corrupting
memory.

Three new example programs are in `examples/basic/`: `DXPATH.BAS` (input form and DXCC
path work), `CALLPARSE.BAS` (every text function), `PASSTATS.BAS` (named arrays).

---

## Calculators

Six additions, each something that was being computed on paper next to the device:

| | |
| --- | --- |
| `lam(mhz)` | wavelength in metres |
| `dipole(mhz)` | half-wave dipole length, 0.95 velocity factor — the length to cut |
| `dbm2w` / `w2dbm` | power conversions |
| `aorb(minutes)` | the altitude that gives an orbital period — the inverse of `porb` |
| `slant(el, alt)` | slant range at a given elevation |
| `dgain(d, mhz)` | parabolic dish gain, dBi, 55 % efficiency |

`slant()` earns its place: treating altitude as range understates a horizon path by
about **5.6×**, and the horizon is exactly where a link budget is tightest.

**Press `Fn`+`f` on either calculator** for the full 65-name function list on the device.
The new printable **Calculator Card** covers the same ground on paper, with worked
examples.

---

## MUF to a DXCC entity

The MUF tool reported a fixed set of world regions. Press **`d`** to pick a DXCC entity
and the path to it appears pinned above the region table — same model, same units, so it
is directly comparable. **`D`** clears it.

Reference points for all 340 current entities come from the standard country file. The
62 entities without coordinates are exactly the 62 deleted ones, so the tool says "no
location" for those rather than plotting 0,0 — a real place in the Atlantic that would
look like a plausible answer.

---

## Dual CAT

Several fixes, all found on the bench with a TH-D75 on USB and an IC-705 on LAN:

- **A LAN leg never started in a mixed configuration.** The engage path returned early
  whenever *either* leg was USB, so an Icom LAN leg never had its connect state machine
  armed. It sat idle forever with no error.
- **Fine tuning was lost on a mixed rig.** Transponder modes are applied only when every
  leg is ready, and a LAN leg is still handshaking at engage — so the modes were dropped
  and never retried, leaving a TH-D75 on its 5 kHz step. The same early return also cost
  sat mode, band assignment and the CAT read budget; all are now re-applied once the rig
  reports ready.
- **Uplink tuning was unresponsive.** CI-V frames a radio sends when its dial moves are
  broadcast to address `0x00`, and the reader only accepted frames addressed to the
  controller — so exactly the frames carrying the dial position were discarded.
- **The LAN session is now on demand**, like every other socket owner in CardSat. It
  connects when radio control is engaged and closes properly when it is not, instead of
  retrying from boot forever.

---

## Under the hood

- **Partition layout**: the app grew 3 MB → 4 MB and LittleFS 896 KB → 1.5 MB, using
  flash that was simply unused before. The ceiling is deliberate: Launcher lives in the
  same flash and sizes its own partition table to the app, so leaving it room matters.
- **Upgrading is unaffected.** The app still fits an existing 3 MB layout, so a Launcher
  update works exactly as before; the larger filesystem appears after a full
  `CardSat-merged.bin` flash.
- USB host task stack raised for hub enumeration, which needs more than a directly
  attached device.
- Adapter selections now survive a device's USB address changing — moving a radio to a
  hub or a different port no longer silently unbinds it.

---

## Known limitations

**Multiple USB devices do not work.** One device behind a powered hub enumerates; two do
not, and the IC-705 does not enumerate over USB at all. This is understood and is not a
bug we can fix in this firmware.

Arduino's prebuilt ESP-IDF biases the USB FIFO toward isochronous output, which leaves
the non-periodic transmit FIFO — the one carrying **control transfers and bulk OUT**, so
enumeration itself and every CDC write — at **64 bytes, exactly one full-speed packet**.
The IDF default split would give 256 bytes. That split is computed inside the prebuilt
library from a compile-time option; there is no runtime override, so no change to
CardSat or its USB library can reach it.

What works today: **one** USB radio, or a USB radio plus a LAN radio, which is the
configuration dual CAT was tested in.

---

## Verification

20 static gates and 12 host test harnesses. New this release: the built binary is
checked against the real app partition (Arduino reports against the wrong ceiling once a
custom partition scheme is used), every token in the example programs must exist in the
interpreter, and every function named on the calculator card and on-screen reference
must exist in the evaluator — a reference listing something the firmware lacks is worse
than no reference, because the operator assumes the mistake is theirs.
