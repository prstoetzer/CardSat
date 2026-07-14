# CardSat — What's Verified, and What to Check on the Air

CardSat is developed and tested host-side (x86 logic simulations plus brace/parity
checks); the firmware author flashes and confirms behavior on real hardware. This page
records what is confirmed on the Cardputer ADV versus what still needs verification
against real radios and rotators.

## Confirmed working on hardware

Display and keyboard, GP download + streaming parse, SGP4 pass prediction, the polar /
pass-detail / mutual-window screens, GPS (auto-refresh on fix / satellite-count
change), the AOS alarm and speaker, deep sleep, the visual-pass / decay /
Sun-Moon-transit / per-satellite-note features, and the offline GP/transponder caches.

**v0.9.52–0.9.53 additions confirmed:** on-demand speaker power (audio buffers up only while
sound plays, released after — including game exit via any path), the **4bpp display sprite**
(colours verified unchanged on hardware), and the **multi-batch LoTW upload fix** — three
back-to-back signed uploads with zero send stalls, confirmed via the on-device heap log
(largest block recovering to its ceiling before every batch). The one 0.9.53 addition **not**
yet exercised on hardware is multi-file download from a phone browser (the Files page's
sequenced downloads; browser behavior varies by platform).

**Added in 0.9.54, not yet on hardware:** favorites-first loading and the "Loaded X of
Y" truncation status with an oversized CelesTrak group; the storage preflight refusal
("file too big for storage") on an internal-flash unit; the USB serial console
(115200: help / ver / heap / sats / fav / next / net); and the Tools → CubeSatSim C2C
reference screen (render, scroll, backtick exit); and the Help → `a` AMSAT Fox
anatomy animation (spin smoothness, callout cycling, leader tracking, Orbit-zoo
regression on the shared 66 ms tick), plus its two companion text screens (`i`
primer from the anatomy; Help `c` Simulator intro).

**Added in 0.9.55, not yet on hardware:** the entire TCP:9100 printing path — each of
the seven reports against a real printer (or a host running `nc -l 9100`), the
Settings → Network printer IP/port entry and persistence, the `p` Print menu on a
satellite's Passes screen (and the rove viewer's `p`), the serial `print` commands, and the error paths (unreachable printer fails
fast; blank IP reports "No printer set").

**Single-pin CI-V is confirmed on an IC-821** — the full bidirectional CI-V exchange
(frequency reads and ACKs) works over one shared open-drain GPIO, including **Doppler
compensation and full radio-knob tuning**. See
**[interfaces/CIV_SINGLE_PIN.md](interfaces/CIV_SINGLE_PIN.md)**.

**LoRa text messaging is confirmed on hardware** — two-way messaging between CardSat and a
LilyGo T-LoRa unit running the companion CardSat Pager firmware works (on-air frame format,
sync word, and CRC interoperate). See
**[design/LORA_MONITOR_SCOPE.md](design/LORA_MONITOR_SCOPE.md)**.

**The Icom LAN (RS-BA1 UDP) CAT path is confirmed able to control an Icom radio** — CardSat
successfully controlled an **IC-705** over the network once the CI-V address was set
correctly (connect / auth / keepalive handshake and CI-V framing all work). Practical
satellite use needs a radio with proper satellite mode: on the IC-705 the two VFOs just
swap back and forth (single-RX limitation), so it isn't usable for live satellite work, but
the path is proven and **should work with an IC-9700**, which has true satellite (dual-RX)
operation. Still unverified specifically against an IC-9700.

## Network commands verified, but not yet tested against a physical device

These send correct, protocol-accurate traffic on the wire (confirmed by the author), but
have **not** been driven against a real rotator or radio on the far end:

- **rotctl / rigctl network clients.** The **rigctl** client and the **rotctld**-protocol
  rotator client send accurate Hamlib TCP commands over the network. Exercise the client
  against `rigctld -m 2` / `rotctld -m 1`; the on-air commands look correct but the
  device side (a real rig or rotor actually moving) is untested.
- **PstRotator (UDP).** Sends accurate PstRotator UDP commands over the network
  (host-verified against the PstRotator manual). Not yet tested driving a real rotator
  through PstRotator.
- **Icom LAN against an IC-9700 specifically.** The LAN path is confirmed controlling an
  IC-705 (see above); the IC-9700 — the radio it's actually intended for, with real
  satellite mode — has not been tested directly.

For all of these, keep the network servers/clients on a trusted LAN (no auth).

## Still to verify on real equipment

- **CAT radio control (other paths).** Separate-pin CI-V, Yaesu, and Kenwood encoders
  are host-tested but not yet confirmed against those specific radios. Watch the CAT
  serial monitor to confirm the rig ACKs (`FB`) rather than NAKs (`FA`), that the
  correct VFO tunes, and that model / baud / address match. For radio-knob (One True
  Rule) tuning, each cycle reads the dial back after a set and only re-sends a leg when
  it actually moved, so coarse tuning steps don't masquerade as knob moves; while the
  rig reports PTT it skips the knob read. The knob-move threshold is **mode-aware**
  (≈30 Hz SSB/CW, 250 Hz FM, floored at the rig's tuning step), with a short grace
  window that holds off downlink writes while you're turning — tune
  `KNOB_MOVE_SSB_HZ` / `KNOB_MOVE_FM_HZ` / `TUNE_GRACE_MS` in `app.h` if the feel needs
  adjusting. (The IC-821 single-pin path above is the one CAT backend that **is**
  hardware-confirmed.)
- **Antenna rotator (hardware paths).** The motor-driving backends are host-tested only.
  For **GS-232**, the I²C pins (G8/G9) are confirmed from the Cap LoRa-1262 pinmap, but
  the SC16IS750 I²C→UART bridge and command path are host-tested for baud math and
  framing only — confirm the bridge address (`ROT_I2C_ADDR`) and controller baud
  **before keying real motors**. The **direct-Yaesu** I²C backend (ADS1115 feedback +
  PCF8574 direction) is host-tested only. (The network rotator surfaces — rotctld and
  PstRotator — are covered in the section above: their commands are verified, only the
  physical rotor is untested.)
- **Network *server* surfaces (CardSat as the server).** Separate from the client
  commands above: CardSat can also *act as* a **rigctld server** and a **rotctld server**
  for Gpredict / `rigctl` / `rotctl` to connect into. Those inbound paths are host-tested
  only — exercise them with `rigctl` / `rotctl` or Gpredict pointed at CardSat, on a
  trusted LAN (no auth).
- **TLS** uses `WiFiClientSecure::setInsecure()` (no cert validation) — fine for public
  GP data; pin a CA root if you care.

## Implemented in 0.9.36

- **DONE & CONFIRMED WORKING ON A REAL LoTW ACCOUNT — LoTW `.tq8` rewritten to LOTW V2.0.**
  A satellite QSO built by CardSat was uploaded and **successfully posted to the operator's
  LoTW account** (N8HM, the FO-29 QSO). Getting there took several fixes beyond the initial
  signing rewrite, each found from LoTW's server-side processing log:
  - `signData()` emits the V2.0 normalized string (station VALUES + contact VALUES, no adif
    tags, sigspec order, UPPERCASED, worked CALL included, station CALL/DXCC excluded).
    Host-verified the byte string and that it signs+verifies with `openssl dgst -sha1`.
  - Records carry `CERT_UID`/`STATION_UID` linkage; signature field is `<SIGN_LOTW_V2.0:LEN:6>`.
  - **Station record field names:** the tSTATION uses TQSL's internal names `US_STATE` /
    `US_COUNTY`, NOT the ADIF `STATE`/`CNTY`. A bare `CNTY` is rejected ("data length
    overflow"), which discards the whole tSTATION and orphans the tCONTACT.
  - **County value:** `US_COUNTY` is the county NAME ALONE (`Arlington`), not the combined
    `VA,Arlington` (which LoTW rejects as "Invalid value"). CardSat stores `ST,County` and
    strips the prefix when building both the record and the signed data. The state is in
    `US_STATE`.
  - **Date/time format:** the tCONTACT uses TEXT forms `YYYY-MM-DD` and `HH:MM:SSZ` under the
    field name `QSO_TIME` (not the compact `20260628`/`011800`, not `TIME_ON`) — otherwise
    "Invalid Date/Time in tCONTACT record". The signed data uses the same text forms.
  - **Response parser:** keys off LoTW's REAL marker `<!-- .UPL. accepted -->` (dots + space),
    not `<!-- UPL_ -->` (underscore) which never matched — that mismatch made an accepted
    upload report as a failure. Reports "Queued N at LoTW" on success (acceptance = queued
    for processing; per-QSO results are server-side).
  - Upload transport verified against tqsl 2.8.6 apps/tqsl.cpp: endpoint
    `https://lotw.arrl.org/lotw/upload`, multipart field `upfile`, **no login/cookie/auth**.
  - Opt-in **re-send** toggle (`a` on the LoTW screen) re-uploads QSOs already marked
    uploaded -- needed because pre-fix QSOs were flagged uploaded but never posted.
  - Full, corrected format spec: `docs/design/LOTW_TQ8_FORMAT.md`.

## Reference: how the root cause was found (kept for context)

- **ROOT CAUSE CONFIRMED + FULL FIX SPEC (reverse-engineered from tqsl-2.8.6 source).**
  The user's `.tq8` uploaded via the LoTW website was accepted/queued but the QSO never
  posted. Direct inspection proved the file is cryptographically self-consistent (sig
  verifies against the cert with `openssl dgst -sha1`), cert valid 2026-2029, all fields
  well-formed, date in range. **The problem: CardSat builds the SIGNED DATA completely
  differently from real TQSL, so LoTW re-derives a different hash and silently drops the
  QSO.** CardSat implements ARRL's *developer-tq8* doc, but that doc is STALE (documents
  only `SIGN_LOTW_1.0` and is wrong about what's signed). The authoritative definition is
  tqsllib's `make_sign_data()` + `tqsl_getGABBItCONTACTData()` in `src/location.cpp` and
  the `<sigspecs>` in `src/config.xml` (downloaded from SourceForge, tqsl-2.8.6).

  **The correct LOTW V2.0 signed-data algorithm (what LoTW actually verifies):**
  1. Build a single string = concatenation of STATION field VALUES then CONTACT field
     VALUES — **values only, NO `<adif:tags>`**.
  2. **tSTATION** fields, in this exact `config.xml` sigspec order, non-empty only:
     `AU_STATE, CA_PROVINCE, CA_US_PARK, CN_PROVINCE, CQZ(as int), DX_US_PARK, FI_KUNTA,
     GRIDSQUARE, IOTA, ITUZ(as int), JA_CITY_GUN_KU, JA_PREFECTURE, RU_OBLAST, US_COUNTY,
     US_PARK, US_STATE`. For a US station that's effectively: CQZ, GRIDSQUARE, ITUZ,
     US_COUNTY (value `ST,County`), US_STATE. **NOTE: CALL and DXCC are NOT signed.**
  3. **tCONTACT** fields appended, in this exact order (alphabetical, per sigspec /
     `tCONTACT_sign` loop): `BAND, BAND_RX, CALL, FREQ, FREQ_RX, MODE, PROP_MODE,
     QSO_DATE, QSO_TIME, SAT_NAME`. **The worked station's CALL IS included here** (CardSat
     currently omits it from signdata). Required: BAND, CALL, MODE, QSO_DATE, QSO_TIME.
  4. **UPPERCASE the entire concatenated string** (`string_toupper`) — CardSat keeps mixed
     case. e.g. `145.9500`->unchanged, `FO-29`->`FO-29`, `fm18lu`->`FM18LU`.
  5. SHA-1 hash that, RSA-sign (PKCS#1 v1.5) -> base64. (CardSat's sign primitive is fine;
     only the INPUT bytes are wrong.)
  Worked example (N8HM, this QSO), as understood at this *initial* stage — signed string =
  `5FM18LU8VA,ARLINGTONVA2M70CMN9EAT/VE3145.9500435.8500SSBSAT20260628011800FO-29`
  **(SUPERSEDED — this intermediate string is wrong: later real-account testing showed the
  county must be name-only and the date/time must be text-formatted. The corrected,
  confirmed-working string is `5FM18LU8ARLINGTONVA2M70CMN9EAT/VE3145.9500435.8500SSBSAT2026-
  06-2801:18:00ZFO-29`. See `docs/design/LOTW_TQ8_FORMAT.md` §6.4 for the authoritative
  version.)**

  **File STRUCTURE changes (also required):**
  - tCERT: add `<CERT_UID:n>1` after the Rec_Type (before CERTIFICATE).
  - tSTATION: add `<STATION_UID:n>1` and `<CERT_UID:n>1`.
  - tCONTACT: add `<STATION_UID:n>1` right after `<Rec_Type:8>tCONTACT`.
  - The signature field tag is `<SIGN_LOTW_V2.0:N:6>base64sig` where N = base64 length
    and `6` is the ADIF "type 6" annotation (verified: `tqsl_adifMakeField` emits
    `<name:len:type>value`, so type '6' -> trailing `:6`). NOT CardSat's
    `<SIGN_LOTW_1.0:N>`. The sigspec name string is built as `SIGN_` + name + `_V` +
    version = `SIGN_LOTW_V2.0`. (The wavelog reference file's `<SIGN_LOTW_V2.0:1:6>` had
    a 1-char placeholder value; real value is the ~172-char base64 sig.)
  - The SIGNDATA field stored in the file is the SAME uppercased station+contact value
    string from step 1-4 (not CardSat's tagged contact-only blob).
  - tSTATION record itself still lists the human fields (CALL, DXCC, GRIDSQUARE, US_STATE
    as `STATE`, US_COUNTY as `CNTY`, CQZ, ITUZ) as it does now — those are separate from
    the signed string.

  **Implementation plan:**
  1. Rewrite `signData()` in `src/lotw.cpp` to emit the station+contact UPPERCASED
     values-only string in the orders above (this is the load-bearing change).
  2. Add CERT_UID/STATION_UID to the three records; change the sig tag to
     `SIGN_LOTW_V2.0` with the `:6:` type annotation.
  3. Include the worked CALL in the signed data; drop the adif tags from signdata.
  4. Verify against tqsl: ideally sign the same one-QSO ADIF with desktop TQSL 2.7.2+ and
     byte-compare; at minimum confirm a CardSat file's stored SIGNDATA matches the
     step-1-4 reconstruction and the signature verifies, THEN test-upload ONE QSO and
     confirm it posts before shipping.
  5. Update the stale references in code comments/docs that cite the developer-tq8 page.

  Source refs in /tmp (this session): tqsl-2.8.6/src/location.cpp lines ~752 (make_sign_data),
  ~3760 (tqsl_getGABBItCONTACTData), config.xml <sigspecs>. The string_toupper at
  location.cpp:3827 is the easy-to-miss key step.

- **`.tq8` vs `.tq7` (DEMOTED — do NOT switch yet).** Earlier theory: our stored
    (uncompressed) gzip should use `.tq7` (the documented uncompressed extension) rather
    than `.tq8` (documented as compressed). Re-examined and the evidence now argues
    *against* switching: our `.tq8` got "Accepted: 1", and a genuine format/compression
    rejection produces an upload error (`400 Bad Request` / "bad file format"), not an
    acceptance — so the file was NOT rejected for being uncompressed. There is also
    *conflicting information* on whether LoTW still accepts `.tq7` at all, so switching
    risks trading a file that demonstrably uploaded for one that may not. Keep `.tq8`.
    Only revisit if the serial response specifically shows a compression/format error.
    (A stored gzip is a valid gzip stream — gunzip-verified — and "stored" is a legal
    DEFLATE method, so `.tq8` is defensible.)

## Investigated and intentionally left as-is — do NOT "fix"

- **Two `SAT_NAME` occurrences in a `.tq8` tCONTACT record are correct.** A grep of a
  signed `.tq8` shows `<SAT_NAME:n>` twice in one QSO record, which looks like a
  duplicate field but is not. One is the QSO's own ADIF field at the record level; the
  other is *inside* the `<SIGNDATA:n>` value, where `n` is the byte length of the whole
  normalized-and-signed blob. A length-tag-driven ADIF parser (LoTW's) consumes exactly
  `n` bytes of SIGNDATA as one opaque value, so at the record level there is exactly one
  `SAT_NAME`. The copy inside SIGNDATA is load-bearing: SIGNDATA is the normalized field
  sequence that gets SHA-1-hashed and signed, and SAT_NAME is field 9 of that sequence
  (per the ARRL developer-tq8 sigspec) — removing it would break signature verification.
  This is exactly how TQSL builds the file, and LoTW's "Accepted: 1" confirms the
  signature verified. Verified by reparsing a generated record host-side (one record-level
  `SAT_NAME`). Leave both emitters in `src/lotw.cpp` (`signData()` and `contactRec()`) as
  they are.

## Source file map

```
platformio.ini          board, libs, build flags
CardSat.ino             single-file Arduino build (generated from src/)
src/main.cpp            entry point (instantiates App)
src/app.{h,cpp}         UI state machine, rendering, Doppler service loop
src/config.h            URLs, UART/pin assignments, limits, file paths
src/storage.{h,cpp}     filesystem: microSD (/CardSat) first, internal LittleFS fallback
src/settings.{h,cpp}    persisted config (WiFi, location, radio, rotator, alarm, calibration, notes)
src/satdb.{h,cpp}       GP/OMM element store + TLE rebuild + streaming parse + transponder cache
src/net.{h,cpp}         WiFi, NTP, HTTPS GET, GP stream-to-file, SatNOGS fetch
src/location.{h,cpp}    manual / grid / GPS position, Maidenhead conversion
src/predict.{h,cpp}     SGP4 wrapper: look angles, passes, Doppler, Sun/eclipse, polar path, mutual windows
src/rig.{h,cpp}         abstract Rig interface + rigctl (rigctld) network client backend
src/civ.{h,cpp}         Icom CI-V framing, freq/mode set + read, MAIN/SUB select, single-pin
src/icomnet.{h,cpp}     Icom LAN (RS-BA1 UDP) CAT backend — confirmed controlling an IC-705; intended for the IC-9700
src/yaesu.{h,cpp}       Yaesu 5-byte CAT (FT-847 / FT-736R)
src/kenwood.{h,cpp}     Kenwood ASCII CAT (TS-790 / TS-2000)
src/rotator.{h,cpp}     rotator backends: GS-232 / Easycomm / SPID (I²C→UART), rotctl (TCP), PstRotator (UDP), Yaesu direct (I²C)
src/voicememo.{h,cpp}   SD-card voice memo recorder + playback (ADV ES8311 mic via M5Unified)
src/irbeacon.{h,cpp}    optional IR-LED pass beacon (38 kHz carrier, per-event flash counts)
src/lora.{h,cpp}        optional LoRa text messaging (Cap LoRa SX1262 via RadioLib; CARDSAT_HAS_LORA)
src/radio_profiles.h    per-model address, baud, band-select, capabilities
tools_make_cheatcard.py generates the printable 4×6 key-reference card (front + back)
```
