# Rotator over USB — separate paths, or refactor?

*Answering: "Instead of refactoring the existing I2C bridge rotator code, would creating a separate
path for USB rotator control for each of the protocols be worth it?"*

**Short answer: your instinct is right that a refactor sounds risky — but I overstated the risk,
and the numbers point to a third option that has neither problem.**

## First, a correction to what I told you

I said "seven backends, none `Stream`-based." Wrong twice: there are **six** concrete backends,
not seven (I counted the abstract `Rotator` base class), and **only three are serial protocols:**

| backend | transport | relevant to USB? |
|---|---|---|
| `Gs232Rotator` | SC16IS750 I²C→UART | **yes** |
| `EasycommRotator` | SC16IS750 I²C→UART | **yes** |
| `SpidRotator` | SC16IS750 I²C→UART | **yes** |
| `RotctlRotator` | TCP | no — already networked |
| `PstRotator` | UDP | no — already networked |
| `YaesuRotator` | I²C ADC + relay expander | no — not serial at all |

So the job is **three backends, not seven**. That materially changes the calculus, and I should
have counted before framing it as a big refactor.

## The measurements

From `src/rotator.cpp`, counting each method:

| backend | plumbing | protocol |
|---|---|---|
| `Gs232Rotator` | 42 lines | 55 lines |
| `EasycommRotator` | 33 lines | 47 lines |
| `SpidRotator` | 26 lines | 72 lines |
| **total** | **~101 lines, triplicated** | **~174 lines, unique** |

"Plumbing" = `wreg` / `rreg` / `bridgeInit` / `putc_` / `puts_` / `getc_` / `flushIn` — the
SC16IS750 register shim. **The same shim, written three times.**

And it is already almost an interface: `Gs232Rotator` and `EasycommRotator` have **identical
seven-method shims**. `SpidRotator` has only four, because SPID is binary — it builds a 13-byte
frame and writes registers directly.

## The three options

### A. Your proposal — a separate USB backend per protocol

`Gs232UsbRotator`, `EasycommUsbRotator`, `SpidUsbRotator` alongside the existing classes.

- **Good:** the bench-verified I²C backends are **not touched at all**. Zero risk to working
  hardware code — which is exactly the right instinct, and the reason to take this seriously.
- **Bad:** it duplicates the **~174 lines of protocol logic**. Two GS-232 parsers, two Easycomm
  grammars, two SPID frame builders — each pair having to stay in step forever. A protocol bug
  would need fixing twice, and the second one is the one that gets forgotten.

The asymmetry is the problem: it protects the ~101 lines that are *plumbing* by duplicating the
~174 lines that are *the actual value*.

### B. Full refactor onto `Stream`

Replace the SC16IS750 shim with a `Stream`, so both transports fall out.

- **Good:** kills the triplication; protocol logic exists once.
- **Bad:** the SC16IS750 is not a `Stream` and would need wrapping; it rewrites the innards of code
  you have verified on a real rotator, and neither of us can test the result today.

### C. The middle — extract only the byte-level shim

Give the three backends an I/O interface, leave every line of protocol logic exactly where it is:

```cpp
struct RotIo {                        // byte-level transport only
  virtual bool begin() = 0;
  virtual void write(const uint8_t* b, size_t n) = 0;   // covers SPID's frames
  virtual int  read() = 0;                              // -1 if nothing
  virtual void flushIn() = 0;
  virtual ~RotIo() {}
};
class RotIoBridge : public RotIo { /* the EXISTING SC16IS750 code, moved verbatim */ };
class RotIoUsb    : public RotIo { /* new: wraps UsbSerial::stream() */ };
```

Each backend changes `putc_(c)` → `_io->write(&c,1)` and `getc_()` → `_io->read()`. **The protocol
methods are not rewritten, not duplicated, and not moved** — only the call target changes.

- **Good:** one copy of the protocol logic; ~101 lines of triplication become ~40; USB works for
  all three at once; and `Stream` already provides exactly these primitives, so `RotIoUsb` is
  nearly free.
- **Risk:** still touches the existing backends — but *mechanically*, and the existing SC16IS750
  code moves **verbatim** into `RotIoBridge` rather than being rewritten. A diff you can read.

## A third transport strengthens the case for C

Paul has since asked for a **serial rotator on the Grove port** — see
`GROVE_ROTATOR_SCOPE.md`. That is a *third* transport for the same three protocols, and it changes
the arithmetic:

- Under **option A** (a separate backend per protocol per transport), three protocols × three
  transports = **nine classes**, with the ~174 lines of protocol logic copied three times.
- Under **option C**, it is one more `RotIo` implementation — `RotIoGrove` alongside `RotIoBridge`
  and `RotIoUsb` — and the protocol logic stays at one copy.

And the Grove case does **not depend on USB at all**: it only needs G1/G2 free, which is already
true today under `CAT_NET` (an IC-9700 over LAN) or `CAT_RIGCTL`. So it is testable on Paul's bench
*now*, which makes it the right way to validate the `RotIo` seam before anything rests on the
unproven USB stack.

## Recommendation

**C, but not yet.**

Not yet, because of the constraint that dominates everything here: **there is one USB port**. A USB
rotator and USB CAT are mutually exclusive. A station wanting both still needs the SC16IS750 for
one of them — so USB rotator support is worth *less* than USB CAT, not more, and it should not be
built on top of an unproven USB stack.

**The order that makes sense:**

1. **Prove USB CAT on the bench** (IC-821 + FTDI cable). That answers the questions that gate
   everything: does EspUsbHost work here, does the console handover behave, does the RAM come back.
2. **If it works**, do C. The rotator refactor becomes a known-value proposition instead of two
   unknowns stacked.
3. **If it doesn't**, C was never worth doing — and you have lost nothing by waiting.

If you would rather have A anyway — because not touching working rotator code is worth more to you
than avoiding duplication — that is a legitimate call and I would build it. The 174 duplicated
lines are a real cost, but "don't touch the thing that works" is a real principle, and you are the
one who has to live with the drift.

## Honest note

I told you "seven backends" without checking. There are six (I counted the abstract base class),
and three of those are network- or relay-driven and could never use USB. That is the same pattern as the USB-host research
earlier this session: a confident framing built on an unchecked count. The numbers in this document
are read out of `src/rotator.cpp`, not recalled.
