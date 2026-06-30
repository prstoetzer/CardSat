# CardSat Annotated Code Reference

A file-by-file, interface-level annotation of the CardSat source, written for porters and
contributors. For each source file this gives: **what it is**, its **portability tier**
(A–D, per [PORTING.md](PORTING.md) §2), its **dependencies**, its **public interface**,
the **key functions** with line anchors, and **porting notes**.

This is a map, not a transcription — the source itself is heavily commented
(`app.cpp` alone carries ~1,560 comment lines), so this document points you *to* the code
and explains how the pieces fit, rather than repeating every line. Line numbers are
accurate as of firmware **0.9.31**; treat them as "near here," since edits shift them.

**How CardSat is laid out.** Three layers (see PORTING.md §1):

```
app.{h,cpp}      UI + orchestration + service loop      ~12.3k lines   Tier D
<modules>        the actual capabilities                ~6.0k lines    Tier A–C
config/settings  pins, constants, persisted model       ~0.4k lines    config
main.cpp         thin entry shell                        19 lines      —
```

The modules are independently reusable; `app.cpp` is the board-specific glue. Read the
modules first.

---

## Table of contents

- [Platform & configuration](#platform--configuration): `main.cpp`, `config.h`, `settings.{h,cpp}`, `storage.{h,cpp}`
- [Prediction core](#prediction-core): `predict.{h,cpp}`, `satdb.{h,cpp}`, `location.{h,cpp}`
- [Radio (CAT) layer](#radio-cat-layer): `rig.{h,cpp}`, `radio_profiles.h`, `civ.{h,cpp}`, `yaesu.{h,cpp}`, `kenwood.{h,cpp}`, `icomnet.{h,cpp}`
- [Rotator layer](#rotator-layer): `rotator.{h,cpp}`
- [Connectivity & data](#connectivity--data): `net.{h,cpp}`, `lotw.{h,cpp}`, `lotw_subdiv.h`
- [Optional peripherals](#optional-peripherals): `lora.{h,cpp}`, `voicememo.{h,cpp}`, `irbeacon.{h,cpp}`, `notes.{h,cpp}`
- [Application layer](#application-layer): `app.{h,cpp}`
- [Cross-cutting data flows](#cross-cutting-data-flows)

---

## Platform & configuration

### `main.cpp` (19 lines) — Tier —

The entire entry point. Instantiates a single static `App` and forwards Arduino's
`setup()`/`loop()` to `App::setup()`/`App::loop()`. **All** hardware bring-up lives inside
`App::setup()`, so this file never changes in a port. If you target a non-Arduino host,
this is the file you replace with your own `main()` that calls the equivalent.

### `config.h` (211 lines) — configuration

**The single most important file for any port.** Centralizes every pin, bus, URL, and
hard limit — there are no pin numbers hidden in `app.cpp`. Reconcile this file first on new
hardware.

Key contents:
- **Physical constants** — `C_LIGHT` (used by the Doppler math).
- **Data sources** — `AMSAT_GP_URL` (the GP/OMM JSON feed), `SATNOGS_TX_URL` (transponder
  DB). Change these to point at a mirror or a different catalog.
- **File paths** — `DATA_DIR = "/CardSat"` and the per-file paths under it (settings, cal,
  notes, favorites, caches).
- **CI-V UART** — `CIV_UART_NUM = 1`, `CIV_RX_PIN = 1` (G1), `CIV_TX_PIN = 2` (G2).
- **SD card SPI** — `SD_SCK/MISO/MOSI/CS_PIN` (40/39/14/12), `SD_FREQ_HZ`.
- **Rotator I²C bridge** — `ROT_I2C_SDA/SCL` (G8/G9), `ROT_I2C_ADDR` (0x4D, the SC16IS750),
  `ROT_XTAL_HZ`, `ROT_I2C_HZ`.
- **IR beacon** — `IR_LED_PIN` (44), `IR_CARRIER_HZ` (38 kHz), burst/gap timing, and the
  `IR_N_*` flash-count table for each pass event.
- **Limits** — `MAX_SATS` (220), `MAX_TX_PER_SAT` (64), `GP_STALE_DAYS` (7), and the
  `CAT_BYTES_PER_UPDATE` throttle. These embody the no-PSRAM frugality; relax them freely
  if your target has RAM to spare.
- **`FW_VERSION`** — the version string (shown on the About screen; the manual/PDF build
  greps it from here).
- **`GpsSource` enum** — Grove 9600/115200 vs Cap-LoRa GNSS, with the pin/baud mapping in
  comments.

**Porting note:** on another board, edit the pins/buses to match. On non-ESP32, the pin
constants still compile as plain ints; only the code that *uses* them (UART/SPI/I²C setup)
changes.

### `settings.{h,cpp}` (198 + 168 lines) — Tier B

The **persisted configuration model**. `struct Settings` (settings.h) is the complete
saved state: location, radio model/address/baud/`civPinMode`, CAT type + LAN host/port/creds,
VFO roles, Doppler tuning thresholds and lead, rotator type/host/port/ranges/offsets,
visible-pass and decay parameters, WiFi (two networks), web/LoRa/QRZ config, alarm/IR flags.

Enums that classify behavior live here too: `VfoType`, `RxOnlyVfo`, `CatType`
(`CAT_WIRED`/`CAT_NET`/`CAT_RIGCTL`), `RotType` (the eight rotator backends), `RotAzRange`,
`SolarActivity`, `WxUnits`.

`settings.cpp` is **load/save as JSON** (`d["key"] | default` pattern) onto the active
filesystem via `Store`. Defaults are applied inline, so a missing or corrupt file yields a
sane config.

**Porting note:** the struct is pure data — it ports as-is. Only `save()`/`load()` touch
the filesystem (through `Store`), so they follow `storage` (below). On a host port, you can
back this with a JSON file via stdio with minimal change.

### `storage.{h,cpp}` (21 + 76 lines) — Tier B

**The single filesystem seam.** Everything that persists funnels through `Store`:
- `begin()` — mount: **prefer microSD** (FAT) if a card is present, else fall back to
  internal **LittleFS** (formatting on failure). The choice is made once at boot.
- `fs()` — returns the active `fs::FS&` (LittleFS or SD); all other modules call this.
- `onSD()` — true if running off the card (this is why flashing never loses SD-stored data).
- `formatInternal()` — factory reset of internal flash only; never touches the SD card.

**Porting note:** reimplement these four methods against your platform's filesystem (SD-SPI
on another MCU, or `fopen` under a directory on Linux) and the entire persistence layer
follows. This is the cleanest seam in the codebase.

---

## Prediction core

### `predict.{h,cpp}` (134 + 420 lines) — **Tier A (lift as-is)**

The orbital-mechanics engine and the most reusable module. Pure math over the Hopperpop
SGP4 library; the only Arduino API it touches is `min`/`max`/`PI`/`radians`/`degrees` (and
no `String`).

**Data structures (predict.h):**
- `PassPredict` — `aos`/`los`/`tca` (unix UTC), `maxEl`, `azAos`/`azLos`.
- `LiveLook` — the live snapshot: `az`/`el`, `rangeKm`, `rangeRate` (km/s, + = receding,
  this is what Doppler keys off), sub-point lat/lon, `satAltKm`, `sunlit`, and Sun az/el.
- `MutualWindow` — co-visibility bounds for two ground stations.

**`class Predictor`:**
- `setSite(const Observer&)` — set the ground station (predict.cpp:35 calls `_sat.site()`).
- `setSat(SatEntry&)` — point at a satellite. Internally renders the elements to a TLE via
  `SatDb::gpToTle()` and calls `_sat.init(name, l1, l2)` (predict.cpp:40–44). **This is the
  GP→TLE bridge** that lets the SGP4 library ingest AMSAT's GP/OMM data.
- `look(time_t)` → `LiveLook` — the per-tick call (predict.cpp:~98, via `_sat.findsat`).
- `azelAt(t, az&, el&)`, `rangeRateAt(unixSec)` — lighter single-value queries.
- `predictPasses(from, minEl, out*, maxN, horizonEnd=0)` → count — AOS/LOS/peak search.
- `mutualWindows(from, dx, minEl, out*, maxN)` — sat-to-sat co-visibility.
- **Sun/eclipse:** `sunlitAt(t)`, `eclipseDepthDeg(t)`, `betaAngleDeg(...)` — the basis of
  the illumination, visible-pass, and Sun/Moon features.
- **Static Doppler helpers (no instance state):**
  - `dopplerFreqs(dlNom, ulNom, rangeRateKmS, calDlHz, calUlHz, rxHz&, txHz&)` — the core
    correction; writes corrected RX/TX through reference params.
  - `uplinkForFixedDownlink(...)` / `downlinkForFixedUplink(...)` — the **One True Rule**
    asymmetric corrections used by radio-knob tuning.
  - `passbandFreqs(Transponder, pbOffsetHz, ...)` — linear-transponder passband mapping
    with inversion.
  - `elevationFromSubpoint(...)`, `jdToUnix(...)` — geometry/time utilities.

**Porting note:** copy `predict.{h,cpp}`, the `SatEntry`/`Transponder` structs from
`satdb.h`, the `Observer` struct from `location.h`, the few constants from `config.h`, and
link the SGP4 library. See PORTING.md §4 and §9d for a working desktop example.

### `satdb.{h,cpp}` (105 + 639 lines) — Tier B

The **satellite element store** and GP/transponder ingestion.

**Structures:** `Transponder` (downlink/uplink low+high in Hz, `invert`, `isLinear`,
`toneHz`, plus a `bandwidth()` helper) and `SatEntry` (the orbital elements:
`epochUnix`, `incl`, `ecc`, `raan`, `argp`, `ma`, `meanMotion`, `bstar`, `ndot`, `nddot`,
plus identity/status fields).

**`class SatDb`** holds up to `MAX_SATS` entries and the active satellite's transponders:
- `begin()`, `count()`, `indexOfNorad()`, lookups.
- **GP ingestion** — `saveGpJson()`/`loadGpFromFs()` cache the downloaded blob;
  the **streaming parser** (satdb.cpp) walks the GP JSON element-by-element so the full
  catalog loads without a large buffer on the no-PSRAM S3.
- **`gpToTle(const SatEntry&, l1[72], l2[72])`** (satdb.cpp:490) — the static GP→TLE
  renderer the predictor relies on. Reconstructs a well-formed TLE line-pair purely to feed
  SGP4 (the catalog number is cosmetic; identity is kept separately).
- `gpEpochToUnix()` — converts a GP epoch string to unix seconds.
- **Transponders** — `parseTransmittersJson()` ingests the SatNOGS DB JSON; manual
  add/remove (`addGp`, `loadManualGpFile`, `removeManualGp`) for hand-entered sats.

**Porting note:** the parsing logic is portable C++; the I/O (LittleFS reads, the streaming
download) is the part to swap. If you feed elements from TLEs instead of GP JSON, you can
use `gpToTle` in reverse conceptually — or just fill `SatEntry` directly and skip most of
this file (see PORTING.md §9a's `tleToEntry` helper).

### `location.{h,cpp}` (64 + 163 lines) — Tier A

Ground-station position and GPS. Mostly portable math.

- `struct Observer` — `lat`/`lon`/`altM` + `valid`/`fromGps`. The type the predictor takes;
  copy it for any predictor-only port.
- `struct GpsSat` — a satellite in view (PRN, az/el, SNR) for the GPS sky plot.
- **`class Location`:**
  - `setManual(lat, lon, altM)`, `setFromGrid(String)` — Maidenhead grid → lat/lon (pure
    math; portable).
  - `beginGps(uartNum, rxPin, txPin, baud)`, `pollGps()` — the only hardware part: read
    NMEA from a UART. `feedNmeaChar()` / `parseGsv()` do the parsing (portable); replace the
    UART read for another platform.
  - Accessors: fix status, sat count, speed/course/HDOP, and the in-view list.

**Porting note:** the Maidenhead and NMEA-parse code ports unchanged; only the serial read
in `pollGps()` is platform-specific.

---

## Radio (CAT) layer

> For an exhaustive prose walkthrough of the CAT wire protocols (CI-V framing, Yaesu 5-byte,
> Kenwood ASCII, Icom LAN, rigctl), every rotator backend, **and** the full Doppler tuning model
> and One True Rule loop, see **`docs/guides/CAT_ROTATOR_DOPPLER.md`**. This section is the
> file-by-file index; that document is the subsystem reference.

### `rig.{h,cpp}` (172 + 142 lines) — Tier B

**The CAT abstraction.** `class Rig` (rig.h) is a pure-virtual interface that hides every
radio protocol behind one contract, so the Doppler engine never knows which radio it drives.

`enum RigMode { RM_LSB, RM_USB, RM_CW, RM_FM, RM_AM, RM_DATA }`.

Pure-virtual / overridable methods:
- Lifecycle: `begin(baud, uartNum, rxPin, txPin)`, `ready()`, `service()`.
- Set: `setMainFreq(hz)` (uplink/TX), `setSubFreq(hz)` (downlink/RX), `setMainMode`,
  `setSubMode`.
- Read (for One True Rule knob tracking): `readSubFreq(hz&)`, `readMainFreq(hz&)`,
  `readPtt(tx&)`.
- Radio state: `enableSatMode(on)`, `selectMainBand()`/`selectSubBand()`,
  `assignBands(mainHz, subHz)`, `setCtcss(on, toneHz)`.
- **Capability flags** — `canReadFreq()`, `hasSatMode()`, `hasTone()`, `canAssignBand()`,
  `selVerified()`, `name()`. *Honor these instead of assuming features exist*; this is how
  one code path spans an IC-820 to an IC-9700.
- Timing knobs: `setCmdDelay(ms)`, `setReadBudgetMs(ms)`.

`rig.cpp` holds **`makeRig(model, catType, host, port, user, pass)`** (rig.cpp:30) — the
factory that picks a backend from the model/CAT-type — and **`RigctlRig`**, the Hamlib
network client backend (talks to a remote `rigctld`; needs only `WiFiClient`).

**Porting note:** to add a radio, subclass `Rig`, implement the virtuals, register it in
`makeRig()` and `radio_profiles.h` — nothing else changes. To port the layer, the framing
in the backends is portable; replace `HardwareSerial`/`WiFiClient` with your transport.

### `radio_profiles.h` (153 lines) — Tier A

The **per-model capability table**. `enum RadioModel`, `enum RigProtocol`
(`PROTO_CIV`/`PROTO_YAESU`/`PROTO_KENWOOD`), and `struct RadioProfile` — one row per
supported radio giving its name, protocol, CI-V address, default baud, the CI-V band-select
byte sequences for MAIN/SUB, sat-mode command bytes, tone-encoder sub-command, and the
capability booleans (`canReadFreq`, `hasSatMode`, `hasTone`, `canAssignBand`,
`selVerified`). Pure data; ports unchanged. Add a radio by adding a row.

### `civ.{h,cpp}` (72 + 446 lines) — Tier B

**Icom CI-V backend** (`class CivRig : public Rig`). Implements CI-V frame build/parse,
frequency set/read, mode, MAIN/SUB select, sat mode, and CTCSS. ~100 comment lines explain
the framing.

**The one ESP32-specific wrinkle:** single-pin CI-V. `CivRig::begin()` can drive the whole
half-duplex bus on **one open-drain GPIO** using register-level ESP32 calls —
`gpio_set_pull_mode`, `uart_set_line_inverse`, `GPIO.pin[pin].pad_driver = 1` (open-drain at
the pad), `esp_rom_gpio_connect_in_signal` (re-assert RX on the shared pad). This is the
only genuinely non-portable code in the CAT layer.

**Porting note:** for a two-wire (separate TX/RX) interface, none of that applies — delete
or `#ifdef`-guard the single-pin branch and the `<driver/gpio.h>`/`<driver/uart.h>`
includes, and the rest is portable C++. `setAddress()`/`address()` carry the CI-V address.

### `yaesu.{h,cpp}` (68 + 175 lines) — Tier B

**Yaesu 5-byte CAT backend** (`class YaesuRig`) for the FT-847 / FT-736R. BCD frequency
encoding, mode bytes, band/sat handling per the Hamlib backends. **No ESP32-specific
code** — only `HardwareSerial`. Ports cleanly by swapping the UART.

### `kenwood.{h,cpp}` (59 + 132 lines) — Tier B

**Kenwood ASCII CAT backend** (`class KenwoodRig`) for the TS-790 / TS-2000. Human-readable
`FA…;` style commands over RS-232 levels. No ESP32-specific code; swap the UART to port.

### `icomnet.{h,cpp}` (160 + 513 lines) — Tier C

**Icom LAN (RS-BA1) backend** (`class IcomNetRig`) — drives a network-capable Icom over
WiFi/Ethernet using Icom's own UDP protocol (control/serial streams; CardSat never opens
the audio stream). **Confirmed controlling an IC-705** on real hardware; **intended for the
IC-9700** (the only model in CardSat's list with the full-duplex MAIN/SUB satellite
architecture — the IC-9700 itself is not yet bench-tested over LAN). Implements the
connect/auth/keepalive handshake plus the CI-V frames
tunneled inside. Socket-bound (`WiFi`/`WiFiUDP`).

**Porting note:** portable in spirit but tied to UDP sockets; on Linux it would become
ordinary Berkeley sockets. The protocol is documented in
[docs/interfaces/ICOM_LAN_PROTOCOL.md](../interfaces/ICOM_LAN_PROTOCOL.md).

---

## Rotator layer

### `rotator.{h,cpp}` (220 + 642 lines) — Tier B

**Eight interchangeable rotator backends** behind one pointing interface, selected by
`RotType` (settings.h). The pointing logic (alignment offsets, deadband, flip-at-180°,
park-on-LOS) is shared; only the transport differs:

- **GS-232 / Easycomm I/II/III / SPID Rot2Prog** — ASCII or binary framing over an
  **SC16IS750 I²C→UART bridge** (used because all three ESP32 UARTs are occupied). `Wire`.
- **rotctl (net)** (`ROT_NET`) — Hamlib `rotctld` **client** over TCP (`WiFiClient`).
- **PstRotator (net)** (`ROT_PST`) — UDP datagrams to PstRotator (`WiFiUDP`).
- **Yaesu (direct)** (`ROT_YAESU`) — a Yaesu rotator with **no GS-232 box**: an I²C
  **ADS1115** reads position-feedback voltages and an I²C **PCF8574** drives the direction
  lines; CardSat closes the bang-bang loop itself (built in `app.cpp`, which supplies live
  calibration).

`rotator.h` documents the type codes; the header notes that the Yaesu-direct backend is
constructed by the app (it needs calibration) rather than by the rotator factory.

**Porting note:** the framing for each protocol is portable; the transports are not. On a
board with a spare UART, drop the I²C bridge and use a real serial port; on Linux, use
sockets for the network paths and termios for GS-232. Wire protocols are documented in
[docs/interfaces/ROTOR_INTERFACE.md](../interfaces/ROTOR_INTERFACE.md).

---

## Connectivity & data

### `net.{h,cpp}` (79 + 378 lines) — Tier C

WiFi, NTP, and HTTPS fetching. Includes a **streaming GP download** that writes/parses the
feed without a large contiguous buffer, and uses `esp_heap_caps_get_largest_free_block()` to
watch for fragmentation on the no-PSRAM S3. TLS via `WiFiClientSecure::setInsecure()` (fine
for public GP data; pin a CA root if you care).

**Porting note:** on another ESP32 this works as-is. On non-ESP32, **don't port this file** —
replace it with your platform's HTTP client (`libcurl` on Linux, a W5500/ATWINC library on
an MCU) and hand the downloaded text to `satdb`'s parser. The data-source URLs are in
`config.h`.

---

### `lotw.{h,cpp}` (70 + 400 lines) — Tier C

**LoTW (Logbook of the World) direct upload.** Builds a cryptographically-signed `.tq8` from
logged satellite QSOs and uploads it to ARRL's self-authenticating LoTW web service. **Requires
a microSD card:** the user's callsign-certificate private key + cert live there as PEM files
(`/CardSat/lotw_key.pem`, `/CardSat/lotw_cert.pem`), and the staged `.tq8` is written there
(`/CardSat/lotw_upload.tq8`) before upload.
- `buildTq8(qsos, n, station, keyPass, err, *gzippedBytes)` — build + sign the `.tq8`; returns
  false (and sets `err`) on any failure (missing card/key, parse, sign, gzip).
- `signData(qso, station)` — the **normalized SIGNDATA** for one QSO, in exact developer-tq8
  field order with **no trailing spaces** (public so it can be unit-checked).
- `credentialPresent()` — are the credential PEM files on the card?
- `LotwStation` carries the station fields LoTW needs beyond per-QSO data (DXCC, grid, CQ/ITU
  zones, US state/county, non-US primary/secondary subdivision codes + field names, IOTA).
  `LotwResult` reports signed/accepted/dupe counts + a message.

The load-bearing subtlety: the **outer ADIF record uses spaced fields** (`<NAME:len>val `) while
**SIGNDATA and structural tags are tight** (no trailing space) — LoTW's signature won't verify
otherwise. Signing is `mbedtls_pk_sign(SHA1)` (RSA-PKCS1v15-over-SHA1), gzip CRC via ROM
`miniz` (`mz_crc32`, no allocation). The byte format, SIGNDATA order, and signature were
validated host-side against ARRL's developer-tq8 spec and OpenSSL's verifier — see
`docs/proto/lotw/` and `docs/design/LOTW_TQ8_FORMAT.md`. This is an **upload** feature, not
enrollment: first-time certificate issuance is gated by ARRL (TQSL + a mailed postcard) and
can't happen on-device; the user exports their existing credential to the card once. The
gzip step is also the one place the firmware deliberately frees the display sprite for a
contiguous heap block (see `WEB_CONTROL_SCOPE.md` §3.2).

**Porting note:** Tier C for the mbedTLS/miniz dependency, but the byte-format logic is
portable; on Linux, OpenSSL + zlib are the natural substitutes and the host-verification
harness in `docs/proto/lotw/` already exercises that path.

### `lotw_subdiv.h` (2201 lines, header-only) — data table

**Auto-generated DXCC + administrative-subdivision tables** for the LoTW station-location UI,
extracted from `tqsl-2.8.6 config.xml`. Flash-resident `const` tables (the ESP32-S3 maps
`const` to flash — zero heap cost). The model follows LoTW's `dependsOn` chain: **DXCC →
primary** (state/province/oblast/prefecture/…) **→ secondary** (US county, or Japanese
city/gun/ku); IOTA is free-form. The `DXCC_LIST[]` is ordered so entities **with** a primary
subdivision come first (alphabetical), then all remaining current entities — so the picker can
surface subdivision-bearing entities first. `SubdivEntry{code,name}` and
`DxccEntry{id,name,primaryField,secondaryField}` are the row types. **Do not hand-edit**;
regenerate from `config.xml` if LoTW updates its enums. (This is the single largest source
file and is consumed by the LoTW settings screens in `app.cpp`.)

---

## Optional peripherals

These are gated and can be omitted entirely; removing a module means deleting its calls from
`App` and its `#include`. Nothing else depends on them.

### `lora.{h,cpp}` (84 + 139 lines) — Tier C

LoRa text messaging over an **SX1262** via **RadioLib** (`Wire` for the module). Marked
UNTESTED in firmware. RadioLib is cross-platform, so the logic ports; you supply the radio
and pins. Guarded by `CARDSAT_HAS_LORA`. Protocol in
[docs/interfaces/](../interfaces/) and the LoRa scope doc under
[docs/design/](../design/).

### `voicememo.{h,cpp}` (90 + 412 lines) — **Tier D (board-specific)**

SD-card voice recorder/player using the ADV's **ES8311** codec via **M5Unified**
(`M5.Mic`/`M5.Speaker`). The most board-tied module after `app.cpp`. Expect to rewrite or
drop it on any other hardware.

### `irbeacon.{h,cpp}` (42 + 75 lines) — Tier A

An IR-LED pass beacon: a 38 kHz carrier flashed in per-event counts (T-60/T-30/T-10/AOS/
TCA/LOS, from the `IR_N_*` table in `config.h`). Pure timing over one GPIO; trivial to keep,
re-target, or cut.

### `notes.{h,cpp}` (39 + 148 lines) — Tier B

**Plain-text note storage.** Notes are `.txt` files under `/CardSat/notes/` on the active
filesystem (LittleFS **or** SD — so notes work even with no card). This module is *just the
file I/O*; the browser + editor UI lives in `app.cpp`. The `Notes` namespace:
- `ensureDir()` — create `/CardSat/notes` if absent.
- `list(out[][32], times, max, nameCap)` — enumerate note base names (no dir, no `.txt`),
  **newest-first by mtime**, with parallel last-write times.
- `read(base, dst, maxBytes)` / `write(base, text)` / `remove(base)` / `exists(base)` — the
  obvious per-note operations (base name only — no path, no extension).
- `sanitizeName(name, cap)` — in-place clean of a user-entered name: keep `[A-Za-z0-9 _-]`,
  collapse the rest to `_`, trim, cap length; false if the result is empty.

**Porting note:** Tier B — pure filesystem; reimplement against your platform's FS (it already
goes through `Store::fs()`, so on most ports it follows `storage` for free).

---

## Application layer

> The analysis and visualization screens are documented in technical detail across two
> companion guides: **`docs/guides/ORBITAL_VIEWS.md`** (3D globe, OSCARLOCATOR/EQX,
> illumination, 10-day progression, mutual finder, DX Doppler) and
> **`docs/guides/ANALYSIS_VIEWS.md`** (pass detail/polar, visible-pass list, multi-sat
> schedule, Sun/Moon tracking, sky sources, transits, simulation, sat-to-sat, footprint
> coverage, space/surface weather, AMSAT status). The logging and QSO services (the log,
> CloudLog, LoTW, callsign lookup, voice memos, notes) are in
> **`docs/guides/LOGGING_AND_QSO.md`**.

### `app.h` (858 lines) — Tier D

Declares **`class App`** — the whole program state. Worth skimming to understand the model:

- **`enum Screen`** — every screen in the UI (the state-machine states). Ends with
  `…SCR_CATMON, SCR_TRANSIT` (newest last).
- **`enum TuneMode`**, **`struct SchedEntry`** (a row in Next Passes, including the
  `visible` flag), **`struct PendingQso`** (a logbook entry being edited).
- Hundreds of member fields: per-screen cursors/scroll offsets, the predictor, the rig and
  rotator pointers, the satellite DB, the schedule, calibration, the CAT-monitor ring
  buffer, the QSO log, build-job state for the incremental scans, etc.
- The public surface is just `setup()` and `loop()`; everything else is private.

### `app.cpp` (11,435 lines) — Tier D

The orchestrator and UI. **238 methods**, of which **56 are `draw*`** (M5GFX rendering —
the least portable code) and **52 are `key*`** (one input handler per screen — the portable
state-machine logic). The dominant pattern: most screens have a **`build*` / `draw*` /
`key*`** trio — compute, render, handle input.

**Lifecycle & service (the heart):**
- `setup()` (app.cpp:140) — all hardware bring-up: M5 init, `Store::begin()`, load config,
  init radio/GPS/rotator, restore favorites/cal.
- `loop()` (app.cpp:2693) — the main service loop: read keys, run the Doppler/CAT update,
  poll GPS, service the AOS alarm, tilt tuning, and the network servers, then redraw.
- `handleKey(c, enter, back)` (app.cpp:3075) — dispatches a logical key to the current
  screen's `key*` handler. **The entire UI is driven by single `char` codes**, so a port
  that synthesizes those chars from its own input device reuses every handler unchanged.
- `draw()` (app.cpp:5636) — dispatches to the current screen's `draw*`.

**Radio/Doppler orchestration:**
- `applyRadioFromCfg()` (246), `applyTransponderModes()` (340), `applyCtcssForCurrentTx()`
  (379) — push config/transponder state to the rig.
- `dopplerThreshAndLead()` (2645), `serviceTiltTune()` (542) — the tuning loop's helpers;
  the knob-tracking / One True Rule logic lives in `loop()` (search `lastRxSet`,
  `knobMoveThreshHz`).

**Rotator orchestration:**
- `applyRotatorFromCfg()` (264), `passNeedsFlip()` (288), `rotPoint(az, el)` (311).

**Network servers (CardSat as a server):**
- `serviceRigctld()`/`rigdHandleLine()` (1729/1754) — CardSat's own **rigctld** TCP server.
- `serviceRotctld()`/`rotdHandleLine()` (1816/1838) — its **rotctld** TCP server.
- `serviceWebd()`/`webdHandleRequest()`/`webdSendPage()` + the `webdSend*Json()` family
  (2113…) — the mobile web control server.

**Data acquisition:**
- `doUpdateGp()` (606), `doFastUpdate()` (642), `fetchAmsatStatus()` (775),
  `fetchSpaceWeather()` (793), `fetchWeather()` (927), `qrzLookup()` (8768) — the
  download/refresh entry points.
- `cacheTxBatch()`/`doCacheAllTransponders()`/`resumeCacheIfPending()` (1142…) — the
  incremental transponder cache.

**Per-satellite persistence:**
- `loadCalForSat()`/`saveCalForSat()` (1234/1254), `loadNoteForSat()`/`saveNoteForSat()`/
  `satHasNote()` (1283…), `toneOverrideHz()`/`saveToneOverride()` (1355/1371),
  `loadFavs()`/`saveFavs()`/`toggleFav()` (1409…).

**Prediction-driven screen builders (the incremental, watchdog-safe scans):**
- `buildSchedule()` (1488) + `visEvalPass()` (1467) — Next Passes + the visible-pass flag.
- `buildOrbit()` (6864), `buildEqx()` (7312), `buildVis()` (4194, the 10-day overview),
  `buildIllum()` (4270), `buildGrids()`/`buildStates()`/`buildDxcc()` (workable maps),
  `computeMutual()` (3859).
- `satsatJobTick()` (7953), `transitStartJob()`/`transitJobTick()` (8032/8044) — chunked
  sat-to-sat and Sun/Moon-transit scans that yield to the loop to avoid the watchdog.
- `decayLevelFor()` (6844) — the decay/reentry flag.

**Self-test & diagnostics:**
- `runCatTest()`/`drawCatTest()` (6328/6436) — the CAT self-test screen.
- `catMonPush()`/`drawCatMon()`/`catMonSendHex()` (6479…) — the on-device CAT serial
  monitor (with the 700 ms active poll).

**Logbook:** `beginQso()`/`saveQso()`/`exportAdif()`/`loadLog()`/`rewriteLog()` (5220…).

**Porting note:** this file is the port's main labor. Reuse the **structure** (the
Screen-enum state machine, the build/draw/key trio per screen, the service-loop ordering)
and the **logic** in the `key*`/`build*` methods; rewrite the `draw*` methods for your
display and the input read in `loop()` for your keys. Everything below `app.cpp` —
prediction, CAT, rotator — it merely calls, and that you keep.

---

## Cross-cutting data flows

Understanding these five flows explains how the modules cooperate:

1. **Boot.** `main` → `App::setup()` → `Store::begin()` (pick SD or LittleFS) → load
   `Settings` → `makeRig()` + rotator + GPS init → load favorites/cal/notes → restore the
   cached GP catalog (`SatDb::loadGpFromFs`).

2. **Element refresh.** `doUpdateGp()` → `net` streams the GP JSON → `SatDb` parses it
   element-by-element into `SatEntry[]` and caches the blob → NTP sets the clock. SatNOGS
   transponders load per active satellite (`ensureTransponders`).

3. **Live tracking + Doppler (every `catRateMs`).** `loop()` → `Predictor::look(now)`
   gives `rangeRate`/`sunlit` → `Predictor::dopplerFreqs()` (or the One True Rule helpers
   if the operator is tuning the dial) → `Rig::setSubFreq`/`setMainFreq` → optionally
   `Rig::readSubFreq` to honor manual tuning → rotator `rotPoint(az, el)`.

4. **Prediction screens.** A `key*` handler calls a `build*` (e.g. `buildSchedule`), which
   loops `Predictor::predictPasses`/`look` over the favorites/catalog, filling an array;
   the matching `draw*` renders it. Long scans (`*JobTick`) run incrementally from `loop()`
   so the watchdog never fires.

5. **Network control.** *As client:* `RigctlRig`/`ROT_NET` connect out to a remote
   `rigctld`/`rotctld`. *As server:* `serviceRigctld`/`serviceRotctld`/`serviceWebd` accept
   connections so a PC or phone drives CardSat (which still does the Doppler/pointing). See
   PORTING.md and MANUAL §18 for the client-vs-server distinction.

---

*For the porting strategy, tiers, and worked examples per platform, see
[PORTING.md](PORTING.md). For wire-protocol specifications, see
[docs/interfaces/](../interfaces/).*
