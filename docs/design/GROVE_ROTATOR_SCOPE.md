# Serial rotator on the Grove port

*Design, not built. Answering: "If USB works, I'd like to also be able to choose to drive a serial
rotator over the Grove port."*

**It works, and it's better than it first looks: it doesn't actually depend on USB.**

## The insight

The Cardputer's own Grove port **is G1/G2** — the same pins as wired CI-V. `config.h:78` says so:

> *"Cardputer Grove HY2.0-4P on G1/G2 — SAME pins as the default CI-V, so don't run Grove GPS and
> CI-V together."*

Today that's a conflict. But **the moment CAT stops using G1/G2, they're free** — and
`CivRig::begin()` under `CAT_USB` takes the external-stream path and never touches them.

**And that's true for transports that already exist and are already tested:**

| CAT transport | G1/G2 | Grove rotator possible? |
|---|---|---|
| `CAT_WIRED` | owned by CI-V | **no** |
| `CAT_NET` (Icom LAN) | free | **yes — today, no USB needed** |
| `CAT_RIGCTL` | free | **yes — today, no USB needed** |
| `CAT_USB` | free | yes, once USB is proven |

So this is **not a USB feature**. It's independently useful right now with an IC-9700 over LAN —
which is on your bench. That makes it testable *before* USB proves out, not after.

## Two different Grove ports — worth being precise

There are two, and conflating them would cause real confusion:

| port | pins | bus | what's there today |
|---|---|---|---|
| **Cardputer's own Grove** | G1/G2 | UART | wired CI-V, or Grove GPS |
| **Cap LoRa-1262 Port.A** | G8/G9 | I²C | the **SC16IS750** rotator bridge |

So the rotator *already* uses a Grove port — the cap's I²C one. This proposal moves it to the
Cardputer's own UART Grove port instead.

## What it buys, honestly

- **Drops the SC16IS750 bridge** — one chip and one I²C dependency gone.
- **Frees the cap's Port.A** for something else.
- **Removes an I²C/SPI contention** — the same bus tangle `Store::remount()` exists to work around.
- **Simpler chain**: `UART1 (G1/G2) → MAX3232 → DB-9 → GS-232` instead of
  `Wire1 → SC16IS750 → MAX3232 → DB-9 → GS-232`.

**It does not remove level shifting.** GS-232 is RS-232 (±12 V); a MAX3232 is still required.
The win is one chip, not zero.

## What it costs

- **G1/G2 become unavailable** for wired CAT and for Grove GPS. So it requires a non-wired CAT
  (USB / LAN / rigctl) *and* the Cap LoRa GNSS (G13/G15) rather than a Grove GPS.
- **Three-way pin contention.** G1/G2 are now wanted by CAT, GPS, and the rotator. Two of those
  already conflict and the manual just warns about it; adding a third makes a silent misconfiguration
  more likely.

## The design

The pattern already exists — copy `GPS_PROFILES`:

```c
// app.cpp:521 -- GPS source profiles: { name, UART, RX, TX, baud }.
// "All use UART2 so CI-V keeps UART1."
static const GpsProfile GPS_PROFILES[GPS_SRC_COUNT] = {
  { "Grove 9600",   2,  1,  2,   9600 },   // <- G1/G2
  { "Cap LoRa1262", 2, 15, 13, 115200 },
  ...
};
```

**CI-V owns UART1** (`config.h:90`), GPS owns UART2. A Grove rotator would take **UART1** — free
whenever CAT isn't wired.

Add `ROT_GROVE` to `RotType`, and a `Stream`-backed rotator that opens `HardwareSerial(1)` on
G1/G2. Which is exactly the **`RotIo` abstraction** from `ROTATOR_USB_OPTIONS.md`:

```cpp
class RotIoBridge : public RotIo { /* existing SC16IS750 code, moved verbatim */ };
class RotIoUsb    : public RotIo { /* wraps UsbSerial::stream() */ };
class RotIoGrove  : public RotIo { /* wraps HardwareSerial(1) on G1/G2 */ };   // <- this
```

**One abstraction, three transports.** That is the argument for doing `RotIo` rather than separate
per-protocol USB backends: the third transport costs almost nothing once the seam exists, and this
proposal is the third transport.

## The thing that must not be skipped: refuse the conflict

Three claimants on two pins is a footgun. The setting must be **guarded, not just documented**:

```
Rotator = Grove  AND  CAT type = Wired CI-V     -> refuse, say why
Rotator = Grove  AND  GPS source = Grove 9600/115200 -> refuse, say why
```

CardSat's existing precedent here is a warning in `WIRING.md` and nothing in code — which is how
someone ends up with a rotator that silently doesn't move while the radio silently doesn't tune.
Given three-way contention, the Settings screen should **reject the combination at selection time**
with a message naming the conflict, the way the Station-readiness screen names missing prerequisites.

## Recommendation

**Do it — but as part of `RotIo`, and test it over LAN first.**

1. **Build `RotIo`** (the shim extraction from `ROTATOR_USB_OPTIONS.md`). It's needed for this
   anyway, and the existing SC16IS750 code moves verbatim rather than being rewritten.
2. **Add `RotIoGrove`** — and test it with `CAT_NET` on the IC-9700. **This does not need USB to
   work**, so it can be proven on your bench immediately.
3. **Add `RotIoUsb`** once USB CAT is confirmed.

That order is deliberate: step 2 is testable *today* and validates the `RotIo` seam with a rotator
you own, before anything depends on the unproven USB stack. If `RotIo` is sound, `RotIoUsb` is
nearly free. If it isn't, you find out with hardware you can actually see.

## What I checked, and what I didn't

**Verified in the source:** the Grove/CI-V pin sharing (`config.h:78`), CI-V owns UART1
(`config.h:90`), GPS profiles all use UART2 with Grove at G1/G2 (`app.cpp:521`), the SC16IS750 is
on the *cap's* Port.A at G8/G9 (`config.h:147`), `CivRig::begin()` skips the UART entirely under
`CAT_USB`, and GS-232 needs RS-232 levels (`WIRING.md:94`).

**Not established:** whether the Cardputer's Grove port can supply what a MAX3232 needs. Your
memory notes say the Grove port provides **5 V and no 3.3 V rail**, requiring an LDO or a divider —
that's a wiring detail for `WIRING.md`, and it should be confirmed against the hardware rather than
taken from me. This is electrical, not firmware, and I can't test any of it.
