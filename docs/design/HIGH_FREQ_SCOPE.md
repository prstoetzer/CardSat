# Scoping: frequencies higher than 4.2 GHz

*Status: proposal / scope only. No code. Written against the 0.9.62 source.*

## 1. What the limit actually is

CardSat stores every frequency as **`uint32_t` Hz**. The maximum value of a `uint32_t` is
4,294,967,295 — i.e. **~4.294 GHz**. "4.2 GHz" is the round-number practical ceiling below
that hard limit. Anything at or above it cannot be represented: the value wraps modulo 2^32.

This is not a display bug or a band-plan omission — it is the width of the integer type used
for frequency throughout the codebase. The relevant bands above the limit that hams actually
use for satellite / weak-signal / EME work:

- **5.7 GHz** (C-band uplinks, some experimental)
- **10 GHz** — the big one: QO-100's downlink is **10489.75 MHz** (1.049 × 10^10 Hz),
  10368 MHz terrestrial/EME
- **24 GHz** and up

Today, a QO-100 downlink ingested from SatNOGS (`downlink_low` ≈ 10,489,750,000) is assigned
into `Transponder.downlink` (a `uint32_t`) and **silently truncates** — the stored value is
that number mod 2^32 (≈ 1.9 GHz), which is meaningless. So higher-band birds are effectively
broken in the catalog path even for display. (The reference *band-plan tables* list QO-100
correctly because they are hard-coded text, not computed from stored frequencies.)

A revealing tell: `fmtMHzN()` already has a formatting branch for values ≥ 10000 MHz (1
decimal place) — someone anticipated displaying 10 GHz frequencies, but the storage type
can't hold them.

## 2. Two ways to support high frequencies

**Option A — widen the frequency type to 64-bit.** Change `Transponder.downlink/uplink`
(and the high-edge fields), the `Predictor` Doppler signatures, the `Rig` CAT interface, and
the app tune path from `uint32_t` to `uint64_t`. Then the real frequency is stored and driven
directly.

**Option B — transverter offsets (see [`TRANSVERTER_SCOPE.md`](TRANSVERTER_SCOPE.md)).** Keep
the rig on a sub-GHz IF and shift to the real band in external hardware. The rig side stays
32-bit; only *display/storage* of the real frequency needs to exceed 4.29 GHz.

These are complementary, not competing. **Even Option B needs part of Option A**: to *show*
the operator "10489.750 MHz" the stored/displayed frequency must exceed uint32, while the
value sent to the rig (the IF) stays 32-bit. The minimal correct design is:

- Widen the **stored / computed / displayed** frequency to 64-bit.
- Leave the **rig CAT calls** at whatever the wire needs (CI-V already carries 10 digits;
  Yaesu IF is sub-GHz), converting at the boundary.

## 3. Surface of the change (Option A, the storage/compute widening)

From the 0.9.62 source, the frequency type touches roughly:

- **`Transponder` struct** (`satdb.h`): 4 frequency fields (`downlink`, `downlinkHigh`,
  `uplink`, `uplinkHigh`) + the `bandwidth()`, `isTwoWay()`, `freqIsAmateur()` helpers and
  the amateur-band table (~35 references in that header alone).
- **SatNOGS ingest** (`txFillFromDoc`, `txBuildFilter` in `satdb.cpp`): the `| 0u` default
  coercions must become 64-bit reads (ArduinoJson can return `uint64_t`). The tx **cache**
  stores raw JSON re-parsed through the same filter, so widening the parse fixes the cache
  automatically — no cache format change.
- **`Predictor`** (`predict.h`): `dopplerFreqs`, `uplinkForFixedDownlink`,
  `downlinkForFixedUplink` signatures (5 points). Doppler is a *ratio* applied to the
  frequency; the math already promotes to `int64_t` internally in the hot path, so widening
  the interface is mostly mechanical, but every callsite passes/receives 64-bit now.
- **`Rig` CAT interface** (`rig.h` + `civ/yaesu/kenwood/icomnet.cpp`): `setMainFreq`,
  `setSubFreq`, `readMainFreq`, `readSubFreq`, `assignBands` (~42 signature points across the
  four backends). Here the wire format matters: CI-V's `freqToBcd` can take a wider value (5
  BCD bytes already), Yaesu's 4-byte BCD caps ~1 GHz (fine — it's an IF rig).
- **App tune path** (`app.h`): `lastRxSet`, `driveDownlink/driveUplink`, `calDl`/`calUl`
  arithmetic, the manual-tune stepping. The int64 promotions already sprinkled here become
  the native width.
- **Formatting / display**: ~42 `fmtMHz`/`fmtMHzN` callsites (already `double`-based, so they
  handle 10 GHz once the input is 64-bit) plus the status/web JSON (frequencies are emitted
  as strings via `fmtMHz`, so no JSON-number precision issue).

This is a **wide but shallow** change: many callsites, each a mechanical type change, with a
few genuinely tricky spots (Doppler ratio, CAT BCD width, calibration arithmetic).

## 4. Memory cost

- **RAM (the important number): the `activeTx[]` array.** `Transponder` currently has 4 ×
  `uint32_t` frequency fields = 16 bytes of frequency. Widening to `uint64_t` makes that 32
  bytes — **+16 bytes per `Transponder`**. With `MAX_TX_PER_SAT = 64`, `activeTx[]` grows by
  **64 × 16 = 1,024 bytes (~1 KB)** of `.bss` (the array lives in the global `App`, the
  dominant static consumer at ~96 KB). Struct alignment may round this slightly. That is the
  main RAM cost of Option A.
  - *Mitigation if 1 KB matters:* keep the high-edge fields (`downlinkHigh`/`uplinkHigh`) as
    32-bit **deltas from the low edge** (a passband is at most tens of MHz wide, well within
    uint32), widening only the two low-edge fields. That halves the cost to ~512 bytes and
    is a clean representation. Recommended.
- **Config:** none required (no new settings for Option A alone).
- **Flash:** the widening itself adds little code, but touching ~40 callsites and the CAT
  backends will add some. Estimate **~2–4 KB flash**. Flash is the tighter budget (91%
  used); this fits but should be watched.
- **Doppler / SGP4 arrays:** unaffected — those store positions/angles, not frequencies.

Net: **~0.5–1 KB RAM** (halvable with the delta trick), a few KB flash.

## 5. Risks

- **Wide mechanical change, many callsites (medium).** The type touches storage, prediction,
  four CAT backends, and the app path. Low *conceptual* risk but high *surface* — easy to
  miss a callsite, and a missed one is a truncation bug that only shows on 10 GHz birds
  (which the bench radios can't exercise). Mitigated by: the parity/defines gates catching
  signature drift between `src/` and the `.ino`, and by the host orbit-audit harness being
  able to check computed dials at 10 GHz.
- **CAT wire width (medium).** CI-V's 5-BCD-byte path must be confirmed to encode a 10-digit
  frequency correctly (the code path exists; the `uint32` argument was the limit). Yaesu and
  the sub-GHz IF case are fine. Icom LAN (IC-9700 only) needs the same check. A wrong BCD
  width sends the rig to the wrong frequency silently.
- **Doppler ratio at high frequency (medium).** Doppler is ~10× larger at 10 GHz than at
  1 GHz for the same range-rate; the int64 promotions must be complete so nothing overflows
  mid-calculation, and rounding at 1 Hz resolution over a 10 GHz carrier must stay stable.
  The existing int64 hot-path promotions suggest this was partly anticipated.
- **No hardware to validate the high-band path (medium-high).** Neither bench radio operates
  at 10 GHz natively; end-to-end verification needs a QO-100-capable station (which in
  practice means a transverter — reinforcing that the two proposals ship together). The
  computation is harness-verifiable; the CAT-to-real-rig-at-10-GHz path is owner-tested.
- **Truncation is the current silent failure (informational).** Because high-band
  transponders already truncate silently today, *any* fix is strictly an improvement — but
  it also means there is no current "known-good" behavior to regress against; correctness
  must be established fresh (harness + a QO-100 owner).
- **Flash headroom (low-medium).** At 91% flash, a few KB is fine, but this and the
  transverter feature together should be watched against the ceiling; the huge-app partition
  gives ~3.1 MB and each cycle has been adding.

## 6. Recommendation

Support > 4.2 GHz by **widening the low-edge frequency fields to 64-bit** and keeping the
high-edge fields as 32-bit deltas (≈ 0.5 KB RAM), fixing the silent truncation in the
SatNOGS/cache path and letting the catalog, display, and status API carry true 10 GHz
frequencies. This is the prerequisite for a useful transverter feature (which shows the real
band) and is worth doing first. The work is wide but shallow; the risk is *missed callsites*
and *unverified high-band CAT/rig behavior*, both mitigated by the gates + host harness now,
and finished by a QO-100/transverter owner on hardware. Do this before, or together with,
[`TRANSVERTER_SCOPE.md`](TRANSVERTER_SCOPE.md); driving a rig at those bands realistically
requires a transverter, so the two land as a pair for the microwave-satellite use case.
