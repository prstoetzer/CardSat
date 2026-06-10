# CardSat — Cardputer ADV satellite tracker + multi-radio CAT Doppler control

A self-contained, offline-first amateur-radio satellite tracker for the
**M5Stack Cardputer ADV** (ESP32-S3). It downloads GP (orbital element) data and
transponder data over WiFi, predicts passes with SGP4, and drives an Icom, Yaesu, or Kenwood radio over
CAT with real-time Doppler correction — using the AMSAT **"One True Rule"**
(constant frequency *at the satellite*), per-satellite calibration, an all-favorites
pass schedule, an AOS alarm, sun/eclipse status, and more.

> **Status: running on hardware; radio and rotator still unverified.** CardSat
> builds for and runs on the Cardputer ADV, and every feature has been exercised
> on the device **except CAT radio control and the antenna rotator.** Pass
> prediction, the polar and pass-detail plots, mutual-window search, GPS, the AOS
> alarm, deep sleep, and the offline GP/transponder caches are all confirmed
> working on hardware. The per-protocol CAT frequency encoders and both rotator
> backends (GS-232 framing and the rotctld network client) are host-tested but have **not** yet driven a real
> radio or rotator — verify those on the air. See **[Things to verify](#things-to-verify)**.

---

## Highlights

- **Constant-frequency-at-the-satellite Doppler** (KB5MU's *One True Rule*):
  both the uplink and downlink are continuously corrected so your signal never
  walks through the passband. Tune with the device keys **or with the radio's own
  knob** — let go and nothing drifts. A tune-mode cycle (`d`) covers full One True
  Rule, **downlink-only**, **uplink-only** (for an SDR / second-receiver setup),
  and hold-both.
- **Three CAT families, ten radios**, behind one abstract rig interface so the
  Doppler engine is protocol-agnostic: Icom **CI-V** (IC-820/821/910/970/9100/9700),
  Yaesu (**FT-847**, **FT-736R**), and Kenwood (**TS-790**, **TS-2000**). Wire-level
  command sets follow the Hamlib backends. Every frame is traced to the serial monitor.
- **Linear-transponder passband tracking** with correct inversion, and automatic
  sideband selection (USB down / LSB up; USB/USB for HF birds below 30 MHz).
- **Automatic PL/CTCSS tone** on FM uplinks (SO-50, AO-91, ISS, PO-101…): CardSat
  enables the rig's TX tone encoder at the right frequency from a built-in table,
  and clears it when you leave the bird or stop radio output. Tones are also
  **settable per satellite** (`c` on Track) for any FM bird, and persist. Icom /
  FT-847 / TS-2000.
- **Next Passes** — one schedule across *all* your favorites, soonest AOS first.
- **AOS alarm** — countdown beeps + a screen flash before a favorite rises.
- **Deep-sleep until the next pass** — park the unit between passes for big
  battery savings; it wakes ~60 s before AOS.
- **Pass-detail plot** — elevation curve for a pass, colored by sunlit/eclipse,
  plus a **polar view** of that pass (ground track + direction of travel).
- **Polar sky plot with ground track** — the live polar shows the satellite's arc
  across the sky for the current pass (or the next one when it's below the horizon),
  with AOS/LOS markers and a travel-direction arrow.
- **Mutual-window finder** — enter a remote station's grid square and get the
  **co-visibility windows** for a satellite: when you can both see it at once,
  with each window's duration and the peak elevation at both ends.
- **Sun & eclipse** — Sun azimuth/elevation, a Sun glyph on the polar plot, and
  whether the satellite is sunlit or in Earth's shadow.
- **GP age** — element-set age shown and color-graded so you know when elements are stale.
- **Antenna rotator (GS-232 or rotctld)** — point an az/el rotator (Yaesu G-5500 + GS-232B,
  SPID, K3NG/RadioArtisan) through an I²C→UART bridge, so the radio and GPS keep
  their UARTs. Deadband, park-on-LOS, alignment offsets, optional flip mode.
- **QSO logging + ADIF.** Press `l` while tracking to log a contact (UTC, satellite,
  up/downlink, mode, your grid + theirs, RST, notes) to a CSV on the card **without
  interrupting Doppler control**. Review, edit, or delete past entries on-device,
  and **export ADIF** on demand for LoTW/eQSL or your main logger.
- **Auto-refresh, power management, and diagnostics.** If WiFi is configured,
  CardSat connects and NTP-syncs at boot and **auto-refreshes GP when the cached
  elements are over a week old**; the backlight blanks after a configurable idle
  time (any key wakes it); config + favorites **back up / restore to the SD card**;
  and an **About** screen reports version, storage, GP age, battery, and uptime.
- **Fully offline** once GP + transponders are cached. CardSat stores everything in
  a **`/CardSat` folder on the microSD card** by default, falling back to internal
  **LittleFS** if no card is present.
- **Screenshots** — press **`b`** on any screen to save a 24-bit BMP to
  `/CardSat/Screenshots/` on the SD card (handy for documentation).
- **Favorites**, **manual GP / transponder / time entry**, per-satellite
  **calibration**, and a **factory reset**.

---

## Hardware

- **M5Stack Cardputer ADV** (StampS3A = ESP32-S3FN8, 8 MB flash, **no PSRAM**,
  240×135 IPS LCD, 56-key keyboard, microSD, speaker, Grove port, 2×7 header).
- A **CAT interface appropriate to your radio**, between its control jack and the
  3.3 V header (the ESP32-S3 GPIOs are **not** 5 V tolerant — never wire CAT direct):
  **Icom** = a 3.3 V-safe single-wire CI-V interface; **Kenwood** = a MAX3232 RS-232
  level shifter (DB-9); **Yaesu** = a serial CAT interface (verify TTL vs RS-232).
- *(Optional)* a GPS source: the Cardputer **Grove** port, or an **M5Stack Cap
  LoRa** (868 or 1262) whose GNSS UART feeds the header.
- *(Optional)* an **antenna rotator**: a GS-232A/B controller, plus an
  **SC16IS750 I²C→UART bridge** and a **MAX3232** between the bridge and the
  controller's DB-9.

---

## Build & flash

### Arduino IDE (single-file `CardSat.ino`)

Install **M5Cardputer**, **ArduinoJson** (v7), **TinyGPSPlus** from Library
Manager, and the Hopperpop **Sgp4** library via *Add .ZIP Library*
(<https://github.com/Hopperpop/Sgp4-Library>). Then under **Tools**:

| Setting | Value |
|---|---|
| Board | **ESP32S3 Dev Module** (full Tools menu) or **M5StampS3** |
| Flash Size | **8MB (64Mb)** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** — **required** |
| PSRAM | **Disabled** |
| USB CDC On Boot | **Enabled** |

The default ~1.25 MB app partition is too small and the build fails with *"Sketch
too big"*; the 3 MB "Huge APP" layout fits with room to spare and provides the
1 MB SPIFFS region that LittleFS uses for cached data.

### PlatformIO

```
pio run                 # build
pio run -t upload       # flash
pio device monitor      # 115200 baud log
```

The `cardputer_adv` env (`board = m5stack-stamps3`) pins M5Cardputer,
ArduinoJson, TinyGPSPlus, and the Hopperpop SGP4 library. If the keyboard or
display misbehave on the ADV (a very recent variant), switch to the git `master`
of M5Cardputer/M5Unified in `lib_deps`.

---

## Wiring

### CAT (radio)

All three CAT families share the **3.3 V hardware UART** — `UART1`, **RX = G1,
TX = G2** by default — but the **interface hardware between the radio and G1/G2
differs by family**, because the electrical layer is different:

- **Icom CI-V** — single-wire half-duplex bus idling near 5 V. Use a 3.3 V-safe
  CI-V interface (the common one-transistor circuit or a ready-made board) on the
  radio's REMOTE jack. Full build guide: **[CIV_INTERFACE.md](CIV_INTERFACE.md)**.
- **Kenwood (TS-790, TS-2000)** — true **RS-232** on a DB-9 COM port (±12 V). Use a
  **MAX3232-class level shifter** between the DB-9 and G1/G2; do **not** use the CI-V
  circuit. On the TS-2000, a straight 3-wire cable with **CTS/RTS bridged** (or the
  "RTS +12 V" handshake) is the usual fix for the rig's handshake quirk.
- **Yaesu (FT-847, FT-736R)** — 5-byte serial CAT. Verify **TTL vs RS-232** for your
  unit from the CAT manual and use the matching level interface. The FT-736R is most
  reliably driven through an **FT-847-emulating** CAT interface (KA6BFB / HS-736USB);
  select **FT-847** in Settings in that case.

Set the **model** and **CAT baud** in **Settings** to match the radio's menu (the CI-V
**address** field applies to Icom only). Yaesu is 8N2; Icom and Kenwood are 8N1 —
the firmware sets this automatically per backend.

### GPS (optional)

GPS runs on **UART2**, so it never collides with CAT on UART1. The source is
selectable at runtime on the **Location** screen (press `s`):

| Source | UART | RX | TX | Baud |
|---|---|---|---|---|
| Grove 9600 | 2 | G1 | G2 | 9600 |
| Grove 115200 | 2 | G1 | G2 | 115200 |
| Cap LoRa868 | 2 | G15 | G13 | 115200 |
| Cap LoRa1262 | 2 | G15 | G13 | 115200 |

Both Cap LoRa modules carry the same AT6668 GNSS at **115200 8N1** on G15/G13;
the two Grove rows differ only in baud, to match whatever receiver you plug in.

> ⚠️ The **Grove** GPS option uses **G1/G2 — the same pins as the default CAT
> port.** Don't run Grove GPS and CAT at the same time on those pins. The two Cap
> LoRa sources use G15/G13 and coexist with CAT fine.

---

### Antenna rotator (GS-232 over I²C, or rotctld over WiFi)

CardSat drives an az/el rotator through one of two interchangeable backends,
chosen in **Settings → Rot type**. Only one is active at a time, and the pointing
logic — alignment offsets, deadband, flip mode, park-on-LOS — is shared, so
switching transports doesn't change behaviour. Enable the rotator in Settings and
press **`o`** on the Track screen to start/stop pointing; it parks at the
configured azimuth on LOS.

**Wired — GS-232.** All three UARTs are spoken for (USB, CAT, GPS), so the
rotator's serial port is made with an **I²C→UART bridge**. Chain:

**Wire1 → SC16IS750 → MAX3232 → DB-9 → GS-232 controller.**

- The bridge runs on the Cardputer-ADV expansion I²C bus — **G8 = SDA, G9 = SCL**
  (`ROT_I2C_SDA`/`ROT_I2C_SCL`), on **Wire1**, separate from the keyboard bus.
  These are confirmed from the Cap LoRa-1262 pinmap and clear of CAT (G1/G2), the
  GPS UART (G13/G15), the LoRa SPI (G3/G4/G5/G6/G14/G39/G40), and SD (CS G12).
- On the Cap LoRa-1262, G8/G9 is the bus broken out to its **HY2.0-4P Grove
  Port.A**, so a Grove SC16IS750 plugs straight in. It's shared with the cap's
  PI4IOE5V6408 expander (~0x43/0x44, LoRa RF switch only); keep `ROT_I2C_ADDR`
  (default `0x4D`) clear of that, or add a TCA9548A mux.
- GS-232 uses RS-232 levels, so a **MAX3232** sits between the bridge's TTL pins
  and the controller's DB-9 (TXD / RXD / GND). Set **Rot baud** to match the
  controller (commonly 9600).

**Networked — rotctld.** No extra wiring: set **Rot type → rotctld (net)** and
point CardSat at a **Hamlib `rotctld`** server on the same WiFi network — enter
its **Rotctld host** (an IP is simplest) and **Rotctld port** (Hamlib default
**4533**). CardSat is the TCP client, the same role Gpredict plays: it sends
`P <az> <el>` about once a second to track and `S`/park on LOS. The socket opens
when you enable the rotator and reconnects on its own (throttled) if the server
drops; pressing `o` re-attempts the link on the spot. Because the rotator sits
behind Hamlib, anything `rotctld` can drive works over the LAN.

- Bench-test it end-to-end before trusting motors: run
  `rotctld -m 1 -T 0.0.0.0 -t 4533` (Hamlib's dummy rotator) on a PC and watch
  CardSat's `P`/`S` commands arrive without moving anything.
- `rotctld` has **no authentication** — keep it on a trusted LAN, never exposed
  to the internet.

Full details for both backends: **[MANUAL.md §17](MANUAL.md)**.

---

## Radios: bands, satellite mode, and read-back

CardSat drives **two independent VFOs** and Doppler-corrects both. The default
convention is **"Sub" = downlink/RX, "Main" = uplink/TX**; the **VFO Type** setting
swaps the roles (*Main Dn/Sub Up*) when your rig's satellite-mode band layout needs
it. The **CAT rate** setting sets how often updates are sent (default **500 ms**,
adjustable in 10 ms steps, soft-floored to what the CAT baud can service). How that
maps to each family:

- **Icom** — CardSat drives MAIN/SUB directly. By default it leaves the rig's own
  satellite mode **off**; the **Sat mode** setting commands it on/off when you
  engage CAT (a no-op on rigs without one). MAIN/SUB select uses CI-V `0x07 D0/D1`
  (verified vs the IC-821H manual). If a radio mistunes the wrong VFO, edit
  `selMain[]`/`selSub[]` in `radio_profiles.h`.
- **Yaesu** — the FT-847 sat opcodes set the SAT-RX (downlink, `0x11`) and SAT-TX
  (uplink, `0x21`) VFOs directly, and read the downlink back with `0x13` (firmware-
  dependent). CAT is enabled at startup (`00 00 00 00 00`).
- **Kenwood** — downlink on **VFO A** (`FA`), uplink on **VFO B** (`FB`).

> **Shared limitation of the Yaesu/Kenwood sat rigs:** CAT **cannot switch the band
> pair**. The operator selects the uplink/downlink bands and engages the rig's own
> satellite / full-duplex mode **by hand**; CardSat only Doppler-tunes within that
> setup. (This is exactly how SatPC32 drives these radios.) Icom is the exception —
> CardSat manages its MAIN/SUB bands over CI-V.

**Frequency read-back** powers the radio-knob *One True Rule* mode (Icom and Kenwood,
plus the FT-847 on updated firmware). Where it isn't available, the device **TUNE** keys
move the passband instead:

| Family | Radios | Protocol | Interface | Read-back | Knob tuning |
|---|---|---|---|---|---|
| Icom | IC-820/821/910/970/9100/9700 | CI-V (binary) | CI-V 5 V single-wire | ✅ `0x03` | ✅ |
| Yaesu | FT-847 | 5-byte (binary) | serial (TTL/RS-232) | ✅ `0x13` ¹ | ✅ ¹ |
| Yaesu | FT-736R | 5-byte (binary) | serial (TTL/RS-232) | ❌ | ❌ (TUNE keys) |
| Kenwood | TS-790, TS-2000 | ASCII `;` | RS-232 (MAX3232) | ✅ `FA;` | ✅ |

¹ **FT-847 read-back** uses the "read freq & mode" command (`0x03`, patched to `0x13`
for the SAT-RX/downlink VFO): 4 big-endian BCD bytes + mode. It works only on
**firmware-updated** FT-847s — early units have no read capability and stay silent
(CardSat times out and holds steady). In satellite mode the radio occasionally returns
the uplink VFO instead (Hamlib #1286); CardSat rejects any read that jumps > 1 MHz from
the commanded downlink, so a stray wrong-VFO reply holds the passband rather than jerking
it. The **FT-736R** cannot report frequency at all, so it uses the device **TUNE** keys.
Both Yaesu rigs track fully under software control regardless.

### CAT serial trace

Every frame the firmware sends is printed to the **serial monitor at 115200
baud**, decoded, so you can watch exactly what reaches the radio. The Icom (CI-V)
trace looks like:

```
[CI-V TX] FE FE A2 E0 07 D1 FD  sel-band SUB
[CI-V TX] FE FE A2 E0 05 00 60 58 45 14 FD  set-freq 145456000 Hz
[CI-V RX] radio ACK (FB)
[CI-V TX] FE FE A2 E0 07 D0 FD  sel-band MAIN
[CI-V TX] FE FE A2 E0 05 00 00 56 35 14 FD  set-freq 435356000 Hz
[CI-V RX] radio ACK (FB)
[CI-V TX] FE FE A2 E0 03 FD  read-freq req
[CI-V RX] FE FE A2 E0 03 FD FE FE E0 A2 03 00 60 58 45 14 FD
[CI-V] SUB freq read: 145456000 Hz
```

Set-freq frames are decoded to Hz, modes to LSB/USB/CW/FM, band selects to
MAIN/SUB, and the radio's reply is reported as **ACK (FB)** or **NAK (FA)** — the
quickest way to confirm wiring, address, and baud. Set `CIV_DEBUG 0` at the top of
`civ.cpp` to silence the trace.

The Yaesu and Kenwood backends emit the same kind of trace tagged **`[CAT TX]`**
(decoded set-freq/mode and CAT-on for Yaesu; the literal `FA…;`/`MD…;` strings and
the `FA;` read reply for Kenwood). Silence any of them with `CIV_DEBUG` / `YAESU_DEBUG`
/ `KW_DEBUG 0` at the top of `civ.cpp` / `yaesu.cpp` / `kenwood.cpp`.

---

## Quick start

Navigation uses the legends printed on the Cardputer keys:
`;` up · `.` down · `,` left · `/` right · **ENTER** select · `` ` `` or **DEL** back.

1. **Settings** — WiFi SSID/password (or press `s` on the SSID row to **scan**
   and pick a network), radio model, **CAT baud** (and the CI-V
   address for Icom), minimum pass elevation, AOS alarm on/off. Once a network is
   saved, CardSat **auto-connects and NTP-syncs the clock at every boot**
   (best-effort; it falls back to GPS or the cached/manual clock).
2. **Location** — set your grid or lat/lon, or enable GPS; set the UTC clock if
   you have no network/GPS.
3. **Update** — download GP data (and NTP time-sync); optionally cache *all*
   transponders for full offline use.
4. **Satellites** — pick a bird (`f` to favorite it); transponders load from
   cache or SatNOGS.
5. **Next Passes** — see what's coming up across all favorites.
6. **Passes → Track** — live az/el and Doppler; `m` switches TUNE/CAL, `d`
   toggles radio-knob tuning.

See **[MANUAL.md](MANUAL.md)** for the complete guide.

---

## Data sources

AMSAT publishes **GP (General Perturbations / OMM) element sets as JSON**. The app
reads `https://newark192.amsat.org/gpdata/current/daily-bulletin.json` (configurable
in **Settings → GP URL**) and caches it, using each record's **`AMSAT_NAME`** for the
satellite name. GP replaces the legacy TLE text format, which is being retired as the
5-digit NORAD catalog field runs out. Transponder
frequencies come from the **SatNOGS DB** as JSON
(`https://db.satnogs.org/api/transmitters/`). The GP file (~75 KB) is **streamed
straight to flash and parsed one element set at a time**, so the full catalog
loads on the no-PSRAM S3 without ever needing a large contiguous buffer. Up to
**220 satellites** are held in RAM, and **up to 64 transponders** per active
satellite.

---

## Things to verify

Confirmed working on the Cardputer ADV: display and keyboard, GP download + parse,
SGP4 pass prediction, the polar / pass-detail / mutual screens, GPS (auto-refresh
on fix/sat-count change), the AOS alarm and speaker, deep sleep, and the offline
GP/transponder caches. Still **unverified on real equipment**:

- **CAT radio control.** Watch the serial monitor to confirm the rig ACKs (`FB`)
  rather than NAKs (`FA`), that the correct VFO tunes, and that model/baud/address
  match. For radio-knob (One True Rule) tuning, the 20 Hz operator-move threshold
  in the Doppler loop is the knob to adjust if your rig quantizes coarsely.
- **Antenna rotator.** Two interchangeable backends, both host-tested only. For
  **GS-232**, the I²C pins (G8/G9) are confirmed from the Cap LoRa-1262 pinmap, but
  the SC16IS750 I²C→UART bridge and command path are host-tested for baud math and
  framing only — confirm the bridge address (`ROT_I2C_ADDR`) and the controller
  baud **before keying real motors**. For **rotctld (net)**, the Hamlib TCP client
  follows the published protocol and can be exercised end-to-end against
  `rotctld -m 1`, but it hasn't driven a physical rotator either.
- **SD-card storage** — CardSat now stores its data in `/CardSat` on the microSD card
  by default (SCK 40 / MISO 39 / MOSI 14 / CS 12, in `config.h`), falling back to
  internal LittleFS only if no card is present. The SD path hasn't been exercised on
  hardware yet; verify the pins if your card isn't detected, and seat the card before
  power-up (the filesystem is chosen once at boot).
- **TLS** uses `WiFiClientSecure::setInsecure()` (no cert validation) — fine for
  public GP data; pin a CA root if you care.

---

## File map

```
platformio.ini          board, libs, build flags
CardSat.ino             single-file Arduino build (generated from src/)
src/main.cpp            entry point (instantiates App)
src/app.{h,cpp}         UI state machine, rendering, Doppler service loop
src/config.h            URLs, UART/pin assignments, limits, file paths
src/storage.{h,cpp}     filesystem layer: microSD (/CardSat) first, internal LittleFS fallback
src/settings.{h,cpp}    persisted config (WiFi, location, radio, rotator, alarm, calibration)
src/satdb.{h,cpp}       GP/OMM element store + TLE rebuild + streaming parse + transponder cache
src/net.{h,cpp}         WiFi, NTP, HTTPS GET, GP stream-to-file, SatNOGS fetch
src/location.{h,cpp}    manual / grid / GPS position, Maidenhead conversion
src/predict.{h,cpp}     SGP4 wrapper: look angles, passes, Doppler, Sun/eclipse, polar path, mutual windows
src/rig.{h,cpp}         abstract Rig interface (keeps the Doppler engine protocol-agnostic)
src/civ.{h,cpp}         Icom CI-V framing, freq/mode set + read, MAIN/SUB select
src/yaesu.{h,cpp}       Yaesu 5-byte CAT (FT-847 / FT-736R)
src/kenwood.{h,cpp}     Kenwood ASCII CAT (TS-790 / TS-2000)
src/rotator.{h,cpp}     rotator backends: GS-232 (SC16IS750 I²C→UART) or rotctld (TCP)
src/radio_profiles.h    per-model address, baud, band-select, capabilities
```

---

## Credits & license

- SGP4 propagation: [Hopperpop/Sgp4-Library](https://github.com/Hopperpop/Sgp4-Library).
- GP data: [AMSAT](https://www.amsat.org/). Transponders: [SatNOGS DB](https://db.satnogs.org/).
- "One True Rule" Doppler tuning: Paul Williamson **KB5MU**,
  [AMSAT](https://www.amsat.org/the-one-true-rule-for-doppler-tuning/).

License: add your preferred license here (e.g. MIT). Built for amateur-radio use;
respect your local licensing and band plans.
