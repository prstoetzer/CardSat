# CardSat — Road to 1.0

*Status as of **v0.9.56** (July 2026). This is the single place to look for what stands between
CardSat and a 1.0 release: what's deliberately deferred, what's blocked on hardware
verification, and what the author has decided not to do. Each item links to the scoping
document that sized it, where one exists.*

CardSat is developed host-side (logic simulations, brace/parity gates, byte-for-byte validation
against reference implementations) and flashed, compiled, and confirmed on real hardware by the
author. That split defines most of what follows: the code is verified as far as a host can verify
it, and the remaining risk is concentrated in **things only real hardware and real radios can
answer**.

---

## 1. Blockers for 1.0

These are the items that a 1.0 label should not ship without.

### 1.1 Hardware verification of unconfirmed interfaces — **the largest blocker**

Most of CardSat's radio and rotator backends are host-tested but have never been exercised
against the hardware they target. A 1.0 that claims broad radio support without that
confirmation would be dishonest.

| Interface | Status |
|---|---|
| **Icom CI-V (single-pin)** | **Confirmed on IC-821H** — bidirectional exchange, Doppler compensation, knob tuning |
| **Icom LAN (IC-9700)** | Host-tested only |
| **Yaesu CAT** | Host-tested only |
| **Kenwood CAT** | Host-tested only |
| **GS-232 rotator** | Host-tested only |
| **rotctl / rotctld** | Host-tested only |
| **PstRotator** | Host-tested only |
| **Yaesu direct rotator** | Host-tested only |
| **LoRa (SX126x)** | **UNTESTED** — marked as such in firmware |

The authoritative, continuously-updated list is **[THINGS_TO_VERIFY.md](THINGS_TO_VERIFY.md)**.
Everything hardware-facing carries an untested / at-your-own-risk banner in the manual and the
interface docs.

**Outstanding IC-821H work items** (identified in earlier sessions, pending bench confirmation):
a higher default `catDelayMs`; MAIN-read as reference with push-only defaulting on; and PTT
polling defaulting off.

**IC-820H command table** is asserted (by the author) to behave identically to the IC-821H for
the MAIN/SUB band-select commands `D0`/`D1`, but this has not been verifiable from the IC-820H
manual directly. It remains an unverified claim in the code comments.

### 1.2 TLS certificate validation — *security, deferred by decision*

**Status: consciously deferred.** CardSat's HTTPS connections do not validate certificates.

The author's reasoning: CardSat runs on trusted LANs, is a hobbyist device, its users are
informed by the documentation, it is severely memory-constrained (a full CA bundle is not
affordable next to a 31 KB largest-free-block ceiling), and the device is active only a few hours
a day.

This is a defensible position for public-data fetches. It is **less** defensible for the
credential-bearing services (QRZ, Cloudlog, LoTW), and a 1.0 should make a deliberate call rather
than inherit the default.

**Scope:** [design/TLS_VALIDATION_SCOPE.md](design/TLS_VALIDATION_SCOPE.md) — recommends pinning
the specific root CAs for CardSat's fixed hosts (not a full CA bundle) plus an explicit opt-in
insecure valve, rather than all-or-nothing.

### 1.3 LAN control hardening — *security, deferred by decision*

**Status: consciously deferred.** The web-control and rotctld server interfaces are
**disabled by default**, which is the single most important mitigation and is already in place.
When enabled, they accept commands from any host on the LAN without authentication.

These actuate real antennas and radios, so the bar is higher than for an information page. A 1.0
should decide whether default-off is sufficient.

**Scope:** [design/LAN_HARDENING_SCOPE.md](design/LAN_HARDENING_SCOPE.md) — recommends a layered
approach: client subnet restriction plus an on-device "listening" and "last command" indicator,
both cheap; a token and read-only mode if more is wanted.

### 1.4 Build pinning — *process*

The PlatformIO platform, every library version, and Git dependencies should be pinned by commit,
with the resolved graph, linker map, and size report archived per release. Low risk, high value:
it makes releases reproducible and makes any future memory measurements comparable.

**Not started.**

---

## 2. Deferred by decision (revisit triggers noted)

These were scoped, measured, and consciously set aside. They are not blockers; they are
judgments that can be revisited if the facts change.

### 2.1 RAM lifecycle refactor — **deferred on evidence**

The proposal: a `ScreenScratch` union/arena so mutually-exclusive foreground screens occupy the
*largest* screen's RAM rather than the *sum* of all of them — plausibly reclaiming 25–35 KB.

**Why it's deferred:** a live memory baseline (v0.9.56, SD card, 92/150 satellites loaded) showed
**no memory pressure**:

- Free heap ~55 KB at idle; **min-ever 43,936 bytes** across a full session including GP refresh,
  seven data fetches, a 14-QSO LoTW upload cycle, and active CI-V + rotator control.
- **Largest free block rock-steady at 31,732 bytes** through every screen transition — zero
  variance, no fragmentation creep. The TLS handshake gate is 28,000, leaving ~3.7 KB of margin
  that never erodes.
- The one screen doing meaningful transient allocation (a game, ~8.3 KB) **fully recovers on
  exit** — proving the on-entry/on-exit pattern already works on this device.

The reclaimable RAM is real, but it is *comfort*, not *need*, and every KB of it carries
use-after-free risk. Measurement instrumentation shipped in 0.9.56 (`mem`, `memtrace`) so this can
be re-measured at any time.

**A note on 0.9.56's own footprint.** The new Tools features prompted a boot-RAM check. The Tiny
BASIC interpreter's ~3.8 KB working state was moved out of `.bss` and is now **heap-allocated only
while a program runs**, and the graphing calculator's default expression is seeded lazily. What
remains of the boot-heap change across 0.9.56 is compiled code footprint (`.data`/`.bss` from the
new features), not runtime allocation — it is the cost of the code existing, and is not
lazy-able. This is the pattern to follow for any future screen-local working memory.

**Revisit triggers:** catalog scaling past ~150 satellites, or a larger TLS trust store (see 1.2)
changing the block-size picture.

**Scope:** [design/RAM_LIFECYCLE_SCOPE.md](design/RAM_LIFECYCLE_SCOPE.md) — sequence the safe,
isolated pieces first (share the two 128-entry pass arrays; allocate the memo directory only while
browsing; allocate transponders only when loaded).

### 2.2 Frequency storage ceiling — *disclosed, migration deferred*

Frequencies are stored as `uint32_t` Hz, giving a **4.294 GHz ceiling**. This is documented in
`satdb.h` and the code reference. Migrating to 32-bit kHz storage would raise the ceiling but is a
data-format change; the **disclosure** shipped in 0.9.56, the **migration** is deferred.

Practical impact: the amateur satellite service's microwave allocations above 4.294 GHz (5.6 GHz
and up) cannot be represented.

### 2.3 what3words support — *declined, by design*

Deliberately excluded from the location converter. It is a proprietary, network-only wordlist
lookup rather than an offline algorithm: it cannot be computed on-device, the wordlist is
licensed, and it would break the "all Tools math is local" contract every other tool honors.
Documented in the manual so the omission is explained rather than looking like a gap.

### 2.4 UX items

Scoped but not scheduled: a task-hub navigation model, a screen registry, accessibility modes, and
a two-tier disk catalog.

---

## 3. Feature-completeness assessment for 1.0

**What's solid.** The core mission — track satellites, predict passes, tune radios for Doppler,
point rotators, log and upload QSOs, plan roves, and work offline — is complete and, for the
CI-V path, hardware-confirmed. Printing is comprehensive (nineteen menu-listed reports plus
context-only ones, three sinks, nine page-description formats including on-device PWG/URF
raster). The Tools hub is a genuine offline bench (35 tools). The documentation is thorough: a
138-page manual, a features list, per-interface wiring guides, and a design-decision archive.

**What a 1.0 needs beyond the blockers above.** Nothing structural. The gap between 0.9.56 and
1.0 is mostly *confidence*, not *scope*: hardware confirmation of the radio/rotator matrix, a
deliberate security decision, and reproducible builds.

---

## 4. Honest notes on verification method

The distinction matters for anyone evaluating a 1.0 claim:

- **Host-validated** means: logic simulated on x86, algorithms checked byte-for-byte against
  reference implementations (the UTM/MGRS/Plus Code projections, the Maidenhead subsquare math,
  the PWG raster encoder, the Tiny BASIC interpreter), brace/parity/screen gates green, and the
  final in-firmware source re-extracted and re-run to catch transcription errors.
- **Bench-confirmed** means: flashed to a real Cardputer ADV and exercised against real hardware
  by the author.

Host validation has repeatedly caught real bugs (a Maidenhead subsquare error, a Plus Code
float-drift error, a PRINT newline bug in the BASIC interpreter, all found by validating against
reference implementations or by testing the documented examples). It has also repeatedly *missed*
a whole class of bug that only the compiler catches — **anonymous-namespace scoping and
definition-ordering errors** — because the host harnesses flatten code structure into a single
scope. Five such bugs were caught by `arduino-cli` during 0.9.56 development: private class
constants unreachable from an anonymous namespace; a helper defined outside the namespace of the
type it took; and three use-before-declaration errors where a method was placed earlier in the
file than the file-scope helpers it called.

**The resulting rule:** a method that calls tool/screen helpers belongs *after* them in the
translation unit, not grouped with its logical siblings. The gates cannot see this; only the
compiler can.

**Neither substitutes for the other, and neither substitutes for on-air use.**

---

## 5. Where the details live

- **[THINGS_TO_VERIFY.md](THINGS_TO_VERIFY.md)** — the authoritative, per-feature verified /
  unverified list. The most important companion to this document.
- **[design/REVIEW_0.9.55_ASSESSMENT.md](design/REVIEW_0.9.55_ASSESSMENT.md)** — the codebase
  review that produced this sequencing. All five of its 0.9.56 items shipped.
- **[design/TLS_VALIDATION_SCOPE.md](design/TLS_VALIDATION_SCOPE.md)**,
  **[design/LAN_HARDENING_SCOPE.md](design/LAN_HARDENING_SCOPE.md)**,
  **[design/RAM_LIFECYCLE_SCOPE.md](design/RAM_LIFECYCLE_SCOPE.md)** — the three deferred items,
  scoped in full.
- **[releases/](releases/)** — per-release notes and test checklists.
