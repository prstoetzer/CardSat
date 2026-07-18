# CardSat v0.9.58 — release notes

*USB CAT, proven on the bench and then made to survive being turned off and on again. Rotators
that run over any wire you have. Diagnostics that outlive the console they used to need.*

---

# New

## CAT over USB — now bench-proven, and it survives a disengage

A fourth CAT transport: a **USB↔serial adapter** (FTDI / CP210x / CH34x) on the USB-C port instead
of the G1/G2 UART and its level shifter. It works for **every protocol and every radio** — CI-V,
Yaesu, Kenwood — because the three wire-level backends already talk through a `Stream*` and only
their `begin()` binds a UART. Handing them a USB stream instead changes the transport and nothing
else; the Doppler loop, calibration and UI are untouched.

The point is not the modern rigs. It is that **every pre-USB radio** — IC-821, TS-790, FT-847 and
the rest — could use a $5 adapter instead of the MAX3232 harness `WIRING.md` documents today.

- **Engaging USB takes the serial console for the rest of the session.** USB host and
  the USB CDC console share the S3's **one internal USB PHY**: the console is released
  on engage and — by design — **does not come back until reboot** (see *The USB host
  now stays up* below for why the restore-on-disengage model was tried and abandoned).
  Diagnostics move to disk instead: `/CardSat/Logs/usb.log` and the new *Console to
  file* capture.
- **RAM is allocated on engage and freed on disengage**, driven by a **reconciliation in `loop()`**
  rather than a hook on the toggle: `radioOut` is *set* in one place but *cleared* in six (the
  emergency stop, charge mode, losing the tracked satellite...), and hanging teardown off the
  toggle would miss five of them and strand both the host stack and the console. Same lesson as
  `basicFree()`.
- **Composite devices**: a USB↔CI-V cable is a single-interface device — the simple case. A modern
  rig plugged in directly (IC-9100/9700 USB-B) is composite (serial + audio), and EspUsbHost warns
  the S3 "has a small number of USB host channels" which composite devices "can exhaust quickly".
  Only the serial interface is bound; audio is never claimed. **Whether that suffices on a real
  composite rig is unverified.**
- **Which adapters work**: CDC-ACM devices are found by interface class (`0x02`/`0x0A`) — any
  compliant device. **Vendor bridges are found by a hardcoded VID:PID allow-list** in EspUsbHost,
  because they are class `0xFF` with no standard descriptor to detect them by: **FTDI** `0403:6001/
  6010/6011/6014/6015`, **CP210x** `10c4:ea60/ea70/ea71`, **CH34x** `1a86:5523/55d3/7522/7523`,
  **PL2303** `067b:2303/23a3`. A clone with an unlisted PID enumerates fine and is simply not
  recognized — so the status line now distinguishes `No USB device detected` (nothing there) from
  `Not a known serial adapter: <name> <vid:pid>` (there, but unlisted), which need entirely
  different fixes.

**A stock build needs nothing new** — no extra library, no new flags, exactly as before.

**To enable USB CAT: install [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) from the
Library Manager and change `#define CARDSAT_HAS_USBCAT 0` to `1`.** No `-D` build flags — the
Arduino IDE has no field for them, and requiring one would have made this feature PlatformIO-only
by accident. Two build notes apply (see `docs/BUILD_AND_FLASH.md`): EspUsbHost 2.x needs a
one-line patch to compile against arduino-esp32 3.2.1, and the single-file `.ino` needs a
`build_opt.h` containing `-mtext-section-literals`. PlatformIO users can uncomment two ready-made
lines in `platformio.ini`.

RAM is handled by design rather than by a slot flag: the `EspUsbHost` object (whose default
**8** device slots make it ~10–20 KB) is **heap-allocated on engage and freed on disengage**, so
it costs nothing at rest — on a board where a stranded 6 KB once broke a LoTW upload, that
matters. An earlier wip pinned `ESP_USB_HOST_MAX_DEVICES=1` per-file instead; that is an ODR
violation (the slot array is a member of the object, and the library's own translation unit kept
the 8-slot layout) and froze the firmware the moment USB CAT was enabled. The fix and full
analysis live in the comment block at the top of `src/usbserial.cpp`.

**Reversible by construction.** With the flag off (the default) `EspUsbHost.h` is never included,
the Settings row cannot reach `USB serial`, and the lifecycle code vanishes — a default build is
byte-for-byte what it was.

**The rotator was NOT done.** It looks like the same job and is not: the seven rotator backends
drive the SC16IS750 bridge registers directly over I²C and share no `Stream`. USB rotator support
means refactoring seven bench-verified backends first — and with only one USB port, it is mutually
exclusive with USB CAT anyway. Worth doing only once the CAT path is proven. See
`docs/design/USB_CAT_IMPLEMENTATION.md`, and `docs/design/ROTATOR_USB_OPTIONS.md` for the
three ways it could be done (and a correction: it is **three** serial backends, not seven —
the other three are network- or relay-driven and could never use USB).

## The USB host now stays up — and that is the fix, not a workaround

The first USB CAT build engaged fine and then could not be turned off. Eight revisions tried to
tear the IDF host stack down on disengage. **It cannot be done from outside EspUsbHost v2.3.0**,
and the bench diagnostic that settled it took five numbers:

```
clients=1 devices=1 polls=160 flags=0x00 freeAll=259
```

The client was never deregistered and its device never closed, so `usb_host_device_free_all()`
refused (it needs `num_clients == 0`) and `usb_host_uninstall()` refused after it. 160 event polls
saw **no flags at all** — there was nothing to drain, and every event-flag theory was wrong. The
cause is the library's own `end()` ordering: it kills the **client task** first, then lets the
daemon run a cleanup (`releaseInterfaces` / `usb_host_device_close` / `usb_host_client_deregister`)
whose every call is client-scoped and needs the client's event queue serviced — and that queue is
already dead. `clientHandle_` is private; there is no public hook to drive it ourselves.

So `end()` now **detaches the CDC port and leaves the host running**. A re-engage rebinds it: no
allocation, no enumeration wait, and `usb_host_install()` runs **exactly once per boot**, which
makes the `ESP_ERR_INVALID_STATE` (259) failure structurally unreachable.

The cost is stated plainly: **~11.8 KB held from the first engage until reboot, and the serial
console does not come back** (the host owns the S3's one USB PHY). Both are bounded and visible.
The destroy-and-recreate alternative produced, in order: a heap corruption, an IDLE0 panic, a
use-after-free in the rig layer, and a permanently wedged stack. Operators who need the console
during CAT can run the radio on **Grove** instead — that combination is unaffected.

### Four bugs found getting there, each worth naming

- **A use-after-free in the rig layer, latent for as long as the caching existed.** Every backend's
  `begin()` copies the external `Stream*` into its own `_stream`. Disengage cleared
  `Rig::extStream` but not the backend's copy, and once `end()` started actually deleting the CDC
  object, the next keypress dereferenced freed memory. It never bit before because the old `end()`
  deleted nothing. `setExternalStream()` is now virtual and clears both.
- **The stack headroom numbers were nonsense** — `free=28208` on an 8,192-byte stack. Two causes,
  both mine: the report raced the dying tasks, *and* `uxTaskGetStackHighWaterMark` returns **bytes**
  on ESP-IDF, not words (the Xtensa port defines `portSTACK_TYPE uint8_t`, so the vanilla
  divide-by-`sizeof(StackType_t)` is a divide by one). The ×4 was wrong. Corrected, the real
  numbers arrived: **EspUsbHost peaks at 1,132 B and the client task at 1,824 B of 8,192**.
- **`kTaskStack` is now 4096**, on two agreeing bench runs — 2.3× the measured peak, **+8 KB of
  heap**. Both readings were with a CDC adapter, which is also what a USB rotator is.
- **A task-watchdog reset while uploading to LoTW.** `enableLoopWDT()` was armed at engage to catch
  a `begin()` that never returned — but loopTask feeds the TWDT **once per `loop()` pass**, so it
  was really asserting "no pass may exceed 5 s". `Net::postFile` allows a 30 s no-progress budget
  inside one pass. The board reset for doing exactly what it was asked. Removed: the span it
  guarded is already watched precisely by a TWDT *user* subscription scoped to `begin()`.

## Rotators over any wire: Grove, USB, or the I2C bridge

The three serial rotator protocols — **GS-232**, **Easycomm I/II/III**, **SPID Rot2Prog** — now run
over any of three transports. Protocol and wire are **separate settings**, so any protocol works on
any wire without a class-per-combination explosion:

| Setting | Values |
|---|---|
| `Rot type` | GS-232, rotctl (net), PstRotator (net), Yaesu (direct), Easycomm I/II/III, SPID |
| `Rot wire` | I2C bridge (default), Grove G1/G2, USB adapter |

The mechanism is the one `rig.h` already used: give each backend a `Stream*` and let it stop caring
what it is talking through. That also deleted **three copies** of the SC16IS750 register plumbing in
favour of one `BridgeStream`.

**Grove is contested and CardSat enforces it both ways.** The Cardputer has one Grove port; wired
CI-V and the Grove GPS use the same two pins. Selecting a Grove rotator while either holds G1/G2 is
refused with the reason on screen. Selecting Grove CAT or GPS while a Grove rotator holds it makes
the **rotator yield** to the I2C bridge and say so — CAT and GPS are primary, a rotator transport is
an accessory choice, and blocking the CAT row would strand you on a setting that silently refuses to
move.

**Fixed in passing, and it has been eating settings for several releases:** `rotType` was clamped to
`ROT_PST` on load, silently resetting **Yaesu, Easycomm and SPID** configs to GS-232 at every boot.
The clamp predated those types. If your rotator setting kept reverting to GS-232, that was why.

## Radio *and* rotator on USB at once — experimental

The host has 4 device slots and 4 CDC slots, so two adapters are supported by design. The hazard is
*which adapter each port binds*. `EspUsbHostCdcSerial` defaults to `ANY_ADDRESS`, which the library
resolves to **the first enumerated device with a bulk-OUT endpoint** — so two default-bound ports
grab the *same* adapter, and the radio's Doppler writes can land on the rotator. "First" is
enumeration order, which changes across a replug.

CardSat therefore **never leaves a port at `ANY_ADDRESS`**. Each binds an explicit device address,
re-resolved on every engage, and the exclusion is symmetric: neither port will take the other's wire.

With two adapters, **Settings → Radio (or Rotator) → `Scan USB adapters`**, then pick each in
`Radio USB:` / `Rot USB:`. The other port's adapter is **skipped while cycling** — visible in the
list but not selectable, so you can see it is taken rather than wonder where it went. Selections
persist by a stable key (serial number where the adapter reports one; else VID:PID + address).
`Auto` means "the only adapter that is not the other port's" — and with two adapters and neither
nominated, engage **refuses** rather than guessing.

The scan is a deliberate keypress, not automatic on entering the screen: it brings the host up,
which closes the console for the session. That should be a choice, not a side effect of opening a
menu.

**IC-9100 / IC-9700 (untested here, so guarded rather than assumed).** Those radios present an
internal hub carrying both a serial interface and a USB Audio device. Audio **cannot** be mistaken
for the CAT port, and that is structural: the library claims only the CDC-data/vendor-serial
interface, and serial candidacy requires a **bulk OUT** endpoint — audio streaming endpoints are
isochronous, never bulk. Device slots are USB **addresses**, not interfaces, and a 9700 is a
*composite* device (one address, two interfaces), so **IC-9700 + USB rotator is 3 devices** — hub,
radio, adapter — inside the 4-slot budget. Each extra slot costs **2,048 B** (measured: 8 slots →
20,040 B at ALLOC, 4 slots → 11,848 B), and slot exhaustion is reported as its own error rather
than a vague "no device".

## Logs that outlive the console: `/CardSat/Logs`

Engaging USB takes the PHY, and the console does not come back. That is exactly when a field problem
needs a trace, so the trace now lands in a file — retrievable through the existing web files portal,
identical on SD and on bare flash.

- **`/CardSat/Logs/usb.log`** — the full USB story for both ports, in one file, in order. CAT lines
  are prefixed `cat:` and rotator lines `rot:`, because an unprefixed line is ambiguous the moment
  both are in play.
- **`/CardSat/Logs/console.log`** — **Settings → Station / logging → `Console to file`** (default **off**) mirrors
  the entire serial console. `Serial` is a *macro* in arduino-esp32 (`#define Serial HWCDCSerial`),
  so CardSat redefines it to a `Print`-derived tee that forwards to the hardware and captures the
  text: **~181 call sites covered, none edited.**

**Everything is capped.** ~32 KB on flash (2 × 16 KB with rotation), ~512 KB on SD — the honest
ceiling is 2× cap **plus one line**, because the size is checked before the write. An uncapped log
on a flash-only Cardputer would fill the filesystem the GP cache and config live on.

Console capture is **buffered** (512 B; flushed on fill, after 1 s idle, before every
`ESP.restart()`, and before a USB engage). That is ~10× fewer flash operations: CardSat ships
`CIV_DEBUG=1`, so Doppler tracking emits ~8 console lines/sec, and unbuffered that would be ~48 ms/s
of **blocking** I/O on the task that also runs Doppler, the UI, WiFi and LoRa — ~5% of wall time,
versus ~0.5% buffered. The tradeoff is real: **a hard crash loses whatever is still buffered.** The
USB engage trace deliberately does *not* go through the buffer — `Logstore` writes it unbuffered, so
the trace that has to survive a freeze still does.

**On-screen, USB CAT and the USB rotator now behave identically**: one `USB CAT: starting...` /
`USB rotator: starting...` line, then the bound device name, then `USB CAT: <reason>` on failure.
The old build painted all 20-odd setup stages to the screen at 8 s each — `bind: set DTR` tells an
operator nothing they can act on, and it buried the one line that mattered. Every stage still goes
to the log, which is where they were actually useful: every USB bug this cycle was found by reading
them in a file, never off the screen.

## "Never rises from your QTH" — the empty pass list now says why

Ported from PREDICT's `AosHappens()`, which has answered this for thirty years.

Ask CardSat for passes of a low-inclination satellite from a mid-latitude site and it used to run
its full 200-step search and return an empty list under *"No passes >= min elev."* — which reads
like a temporary condition worth waiting out. It isn't: the satellite **cannot ever rise**.

**IO-86** is the case: 6.0° inclination, and from FM18LU (38.85°N) it peaks at **−7.4°** — verified
by brute-forcing real SGP4 over ten days against its live AMSAT elements. The Passes screen now
says so:

```
Never rises from your QTH:
incl 6.0 too low for 38.9N
```

The geometry: `acos(Re/r)` is the Earth-central half-angle of the visibility cap — how far the
horizon reaches at orbital radius `r`. Add the inclination and you have the highest latitude the
footprint edge can ever touch. Retrograde orbits fold (98° behaves like 82°).

**Applied to every single-satellite empty state**, after an audit of all sixteen "no passes"
messages in the app: the **Passes** screen, the **10-day chart**, **Orbital analysis** pages 4
(Doppler) and 7 (Outlook), and the **10-day** and **visible-pass reports**. Deliberately *not*
applied to the favorites-list screens (Next Passes, Sky glance, Rove planner) — those aggregate
many satellites, so an empty list there genuinely does mean "none soon".

### And a real bug in the Doppler page, found while auditing

The Orbital-analysis **Doppler page had no `if (orbitPage == 4)` guard at all** — it was a bare
`{ ... }` block that ran only because every other page had already returned. Correct by accident of
ordering, and unreadable next to its siblings: I initially read page 4 as unimplemented. Add a page
11 and it would have silently rendered the Doppler plot. Now explicitly guarded.

Its empty state also said `No upcoming pass.` while its footer dropped `f freq` — even though the
key handler still accepted `f` on that page. The key works; the footer now says so.

**It is conservative by construction**: it uses *apogee* and the *footprint edge*, i.e. the best
case, so it can only ever be generous. A false "never rises" is geometrically impossible — which is
the property that makes it safe to act on. Degenerate elements (zero or absurd mean motion) return
"rises" rather than blocking.

## Deep-space orbits are labeled SDP4

An audit against **PREDICT 3.0.0** (KD2BD, released 13-Jul-2026) prompted a look at the propagation
model, and turned up a correction worth stating plainly: **CardSat already handles deep-space
orbits, and an earlier draft of these notes said it did not.**

CardSat delegates propagation to the **Hopperpop Sgp4-Library**, which is the Vallado unified
SGP4 — deep-space model included. It selects automatically at `sgp4unit.cpp:1523`:

```c
if ((2*pi / satrec.no) >= 225.0) satrec.method = 'd';
```

— the identical 225-minute boundary PREDICT uses. Verified by compiling that library and driving an
AO-40-class HEO (2.05 rev/day, ecc 0.72, perigee 951 km) through CardSat's exact path
(`gpToTle` → `Sgp4::init` → `twoline2rv` → `sgp4init`): SDP4 selected, resonance initialized,
propagates with no error. QO-100 likewise.

So the **Orbital analysis** now simply *labels* it: the title leads with **`SDP4`** on a deep-space
orbit and the printed report names the model. Useful information — a GEO bird's geometry is nothing
like a LEO pass — but not a caveat. **A future amateur HEO needs no propagator work**; what it
would need is everything downstream that assumes a 90-minute orbit (pass search budgets, the
10-day chart's day strips, Doppler loop timing). See `docs/design/SDP4_SCOPE.md`.

## ~11.8 KB of RAM given back

Two resident arrays were costing **11,776 bytes of `.bss`** for data that is rebuilt every time
it is used. This matters more than the raw number suggests: `App` is a **`static`** object, so it
lives in `.bss` — and on the ESP32-S3 the linker lays `.bss` down first and *what remains becomes
the heap*. Shrinking it moves the heap's floor down, raising **free heap *and* the largest
contiguous block by the same amount**, un-fragmentably. The largest contiguous block is exactly
what a TLS upload needs, and what the 0.9.57 heap bug starved.

- **The two pass arrays are now one.** `visPasses[128]` (the 10-day chart) and `vlPasses[128]`
  (the visible-pass list) were separate `PassPredict` arrays, 5,120 B each. They hold different
  data — but they are **never live at the same time**: both are reached only from the Passes
  screen (`v` and `V`), each rebuilds on entry, and neither navigates to the other. They are now
  one `passScratch[128]`, **saving 5,120 B**. `buildOrbit()` already used the 10-day array as
  scratch informally; the merge turns that undocumented reuse into a stated contract — *the buffer
  belongs to whoever built into it last* — verified against every navigation path, including that
  `buildPassDetail()` copies rather than holding a reference.
- **The voice-memo list is heap-allocated on its screen.** `memos[64]` was 6,656 B of `.bss` — a
  directory listing for one rarely-visited screen, read by nothing else, rebuilt on every entry.
  It is now allocated in `buildMemoList()` and released by the screen-transition hook in `loop()`
  (the same hook that fixed the 0.9.57 BASIC leak, and for the same reason: a key handler only
  knows the exits it implements). **Saving 6,656 B.**

**Expected:** free heap ~55,376 → ~67,152, largest block ~31,732 → ~43,508.

### A bug found while doing this

**`printVisList()` did not rebuild before printing.** It read `vlN`, which is 0 unless you had
visited the Visible-pass screen — so printing that report from **About → Print** said *"(none in
the window)"* for a satellite that has passes. `printTenDay()` calls `buildVis()` for exactly this
reason; the visible-pass report (added in 0.9.57) missed it. Fixed — and the fix is what makes the
shared buffer safe, since now every reader builds its own data first.

## Performance monitor (About → `m`)

`mem` and `memtrace` have found two real bugs — but only over **USB serial**, with a laptop
attached. The 0.9.57 heap bug (a runaway BASIC program stranding its 6 KB output buffer, which
then starved the contiguous block a TLS upload needs) was caught because a console happened to be
connected. In the field there is no console.

The performance monitor puts those numbers **on the device**:

| | |
|---|---|
| **Largest block** | the figure that decides whether a TLS upload can run, color-coded (red under 12 KB, amber under 20 KB) |
| **min ever** | the worst it has been since boot — the number that catches a leak you weren't watching |
| **Free heap** | and its own min-ever |
| **Loop avg / worst** | slow loops mean a starved watchdog and a sluggish UI |
| **Uptime, storage, catalog size** | context for the rest |

Largest block leads deliberately: on this no-PSRAM board it, not free heap, is what fails first —
fragmentation can starve TLS while "free" still looks healthy. The thresholds are anchored to
measured behavior (a GP fetch runs with ~25 KB free and the largest block dipping to ~8 KB).

`x` resets the peak trackers, `p` prints the snapshot, `` ` `` goes back. It is also report **29**
in the About → Print menu, so a heap snapshot can go on paper next to whatever else you're
printing.

Sampling runs **every loop on every screen** — min-ever values are only meaningful if they can't
be missed — and reuses the same two heap queries `memtrace` already makes.

---

## A new gate: `check_ino_dupes.py`

Answering "what do I need to build this?" turned up a real bug: the USB CAT code had been inlined
into `CardSat.ino` **twice**, and one copy was **stale** — it predated the adapter-detection fix.
Two `EspUsbHost s_host;` definitions in one translation unit is a redefinition error the moment the
flag is turned on.

**Both existing gates passed it.** Braces stayed balanced, so `check_balance.py` was happy; every
declaration was present, so `check_parity.py` was happy. The failure only shows up at compile time,
and only with a feature flag most builds never set.

The cause is the dual-representation invariant itself: every `src/` edit has to be re-inlined into
the `.ino`, and a re-sync that *inserts* instead of *replacing* leaves two copies. `check_ino_dupes.py`
now checks that no `src/` file is inlined more than once, that single-instance definitions really
are single, and that guarded includes appear once. Verified by re-injecting the exact bug and
confirming the gate fails.

### `check_settings_rows.py` — the gate for the hand-counted array bound (0.9.58)

`drawSettings()` builds its labels into a **stack array of Strings** sized by a hand-
maintained constant:

    const int N = 99;      // must exceed the highest rows[] index used below

Adding the USB picker and scan rows at 99, 100 and 101 wrote three `String` objects **past
the end** of that array. It was reported as "the new settings rows have no visible label" —
but the missing label was the harmless symptom. The real behavior was undefined:
constructing Strings over whatever followed on the stack.

Nothing structural catches it. Braces balance. Parity matches (both representations carry
the same overflow). `check_compiles` cannot reach `app.cpp`. Nor can the compiler — once `N`
is a variable, `rows[101]` is not a diagnosable constant-index error. The comment said "must
exceed the highest index used below" and was wrong the moment a row was added, which is
exactly the kind of invariant a human maintains badly and a script maintains perfectly.

`check_settings_rows.py` parses `drawSettings()`, finds `N`, finds every `rows[i]`, and
checks. It also verifies every id in a `SET_*` menu list has a matching `rows[]` assignment —
a menu entry with no row renders as a blank line, the other way a row goes invisible.
Verified against both bugs by re-injecting them.

### `check_defines.py` — the gate for constants that vanish (0.9.58)

A script removed one retired `#define` from `config.h` by cutting "from the define to the
next blank line". The define was followed by a **comment block**, not a blank — so the cut
swallowed 26 lines: the AMSAT URLs, the space-weather paths, the Open-Meteo API, the QSO log
and ADIF paths. **87 compiler errors from one bad edit**, every one of them "not declared".

All eight gates passed first, and each was right to: braces balanced, parity matched (both
representations were identically gutted — parity's blind spot again), and `check_compiles`
does not reach `config.h`'s consumers in `app.cpp`. Deleting a constant is invisible to every
*structural* check: the code still parses, it just names something that is gone.

`check_defines.py` compares the **constant surface**. Every `#define NAME` and
`static constexpr T NAME` in `src/*.h` must appear in `CardSat.ino` — catching a constant
lost from one representation. With `--baseline DIR` it also diffs against a known-good tree,
catching one lost from **both**, which is what happened here. Deliberate removals go in a
`RETIRED` set, so "I meant to delete that" is an explicit claim with a reason attached rather
than an assumption. Verified by re-injecting the exact 26-line deletion: it names every lost
constant and exits 1.

### `check_switch_dupes.py` — the gate for IDs that collide (0.9.58)

The rotator work added a "Rot wire" settings row and gave it ID **48** — which `cfg.bright`
already owned. `error: duplicate case value`; the build died on the bench. All seven gates
passed first: braces balanced, parity matched (both representations were identically wrong,
which is parity's blind spot by definition), and `check_compiles.py` does not reach `app.cpp` —
30k lines pulling in the whole M5/WiFi/SGP4 world, which is not realistically stubbable.

So this gate does not use a compiler. It parses each `switch` body and checks that no case
value repeats within it, tracking brace depth so nested switches are attributed correctly.
That is a pure text property and needs no toolchain. Verified by re-injecting the exact bench
bug: it reports the same collision the compiler did, in both `app.cpp` and the `.ino`, and
passes once fixed.

Settings rows are hand-assigned integer IDs shared across a `switch`, a `rows[]` array and
per-screen ID lists — a namespace with no compiler-enforced allocation, so collisions are a
matter of when, not if.

### `check_compiles.py` — the gate that asks a compiler (0.9.58-wip fix31)

Same lesson, one turn of the screw tighter. A `usbserial.cpp` edit called a function ~470 lines
before its definition and shipped to the bench as a hard build error. **All six gates passed**, and
they were right to: braces balanced, symbols existed (further down the file), and `check_parity`
confirmed `src/` and the `.ino` agreed — they agreed *on identical broken code*. That is the trap
worth naming: parity proves the two representations MATCH, which says nothing about whether the
shared content is correct. Nothing in the toolchain was asking a compiler.

`check_compiles.py` does. It stubs the Arduino/IDF/EspUsbHost world (structure only, no behavior),
then compiles **and links** the real `src/usbserial.{h,cpp}` bodies with the host `g++`. Linking is
not optional: declaring the forward reference inside the anonymous namespace instead of at
`UsbSerial` scope compiles fine and then fails as an ambiguous overload — the second bug this gate
caught, in the first attempt at fixing the first. Verified both ways: re-inject either bug and the
gate reproduces the bench compiler's exact error; fix it and the gate passes. Skips cleanly where
`g++` is absent.

# Post-release: 0.9.58.1

**A compiler audit found a real bug that 0.9.58 shipped with.** The Arduino build passes **`-w`**,
which silences every warning GCC has. Compiling the same sources with `-Wall -Wextra` produced 13
warnings — and four of them were the same defect:

```
deleting object of polymorphic class type 'UsbRotStream'
which has non-virtual destructor might cause undefined behavior
```

`freeRotator()` deletes its transport through a **`Stream*`**, and Arduino's `Stream` has **no
virtual destructor** — so `~UsbRotStream()`, which calls `rotEnd()` to release the USB CDC port,
**never ran**. The "disabling the rotator permanently binds the adapter" bug reported against the
previous build was therefore *still live in 0.9.58*: the RAII fix was correct and unreachable.
Host-proven both ways — delete via `Stream*`, destructor skipped; via a base with a virtual dtor,
destructor runs.

Fixed with a `RotWire` base that has a virtual destructor, plus an explicit ownership split
(`s_rotOwned` is what we allocated; the Grove UART is borrowed and never freed). Renaming was
forced: `RotTransport` was already the enum name in `settings.h` — a collision that would have
broken the device build, caught by the same audit.

**The compile gate now fails on `-Wdelete-non-virtual-dtor`**, verified by re-injecting the shipped
bug. It is the only place in the project where the compiler is allowed to speak, and it earned that
on its first outing.

Also from the audit: the adapter trace buffers were widened (96 → 160 B) because a long device name
could clip the **key** — the one field an operator has to copy into Settings. `snprintf` truncates
safely, so this was never a crash, only a lost value.

**The `-Os` switch is confirmed in the artifacts**: `.flash.text` 2,061,244 → **1,843,116** (−10.6%)
and the `.bin` 2,921,408 → **2,661,040**, taking the app partition from **92.9% → 84.6%** full.
`.bss` rose 1,208 B — the new `Logstore`/`ConsoleLog` buffers, accounted for symbol by symbol.

# Verification status

- **Host-validated:** the loop-timing exponential moving average. The first version was
  **wrong** — `avg + (dt - (int32_t)avg) / 16` promotes the subtraction back to unsigned, so
  every sample below the average wrapped to a huge positive; a host test showed it running away
  to ~1.4×10⁹ within six samples. The shipped version computes the delta in signed arithmetic
  first. Printed report widths are checked against 32-column (58 mm) paper; the footer fits the
  39-char budget; `PA_ITEMS` and the dispatch `MAP` are 29/29 and the `static_assert` holds.
- **Needs bench confirmation:** the screen's layout and color thresholds on real hardware, and
  whether the loop-timing figures are useful in practice or just noise. The heap numbers should
  agree with what `memtrace` reports over serial — worth checking they match on the first run,
  since that cross-check is free.
- **The RAM work is confirmed.** The bench reports **~85.4 KB free at first engage** (was ~62.7 KB),
  and the linker agrees: `.bss` fell from 145,904 to **122,336 B**, lifting the boot heap ceiling
  from ~104 KB to **~129 KB**. The `.bss`-sits-below-the-heap reasoning held. USB CAT costs ~11.8 KB
  of that while resident, which is the trade the resident-host section describes.
- **The shared pass buffer wants exercising**, since its safety is an argued invariant rather than
  a compiler-enforced one: Passes → `v` (10-day chart) → back → `V` (visible list) → ENTER (pass
  detail) → back, then print both reports from **About → Print** without visiting either screen.
  Every one of those should show correct data.
- **The memo list**: open the browser, play and delete a memo, leave, and confirm `mem` shows the
  ~6.6 KB returning. As with the 0.9.57 `String` bug, the API returning success is not proof the
  memory came back.
- **USB CAT is bench-proven on an IC-821 + FTDI.** Engage, disengage, re-engage, and Doppler
  tracking all confirmed on hardware over many cycles. The console does **not** come back on
  disengage, and that is now by design rather than a defect — see the resident-host section.
- **Two USB adapters at once is NOT proven.** It is built, guarded, and reasoned about carefully,
  but it has never had a radio and a rotator plugged in together. The misbind hazard is real and
  the explicit-address binding is the defense; treat it as experimental. If both adapters are the
  *same model with no serial numbers*, they can only be told apart by address — re-check the
  selection after a replug.
- **IC-9100 / IC-9700 has never been tested.** The composite-device reasoning (audio takes an
  *interface*, not a device slot; isochronous endpoints can never look like a bulk-OUT serial port)
  is drawn from the library and IDF sources, not from a radio. If a 9700 ever reports slot
  exhaustion, that is the evidence to raise `ESP_USB_HOST_MAX_DEVICES` from 4 to 5 (+2,048 B).
- **The console-capture cost figure is modeled, not measured.** ~6 ms per `Logstore` write on
  LittleFS is an estimate; the ratios hold but the absolute could be off 2× either way. The first
  bench run with capture on will settle it — every line is timestamped, so the log profiles itself.
  Watch for: does tracking stay smooth with it on, and does `console.log` stop growing at the cap.
- **The Grove rotator has not been driven by a rotator.** The transport, the conflict rules and the
  yield behavior are exercised; an actual GS-232/Easycomm/SPID controller on G1/G2 is not.
- **`rotType` was silently reverting to GS-232 on every boot** for anyone using Yaesu, Easycomm or
  SPID (a stale clamp). Worth confirming your rotator setting now survives a reboot.
