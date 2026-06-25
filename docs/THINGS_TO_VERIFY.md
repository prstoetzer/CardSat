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

**Single-pin CI-V is confirmed on an IC-821** — the full bidirectional CI-V exchange
(frequency reads and ACKs) works over one shared open-drain GPIO. See
**[interfaces/CIV_SINGLE_PIN.md](interfaces/CIV_SINGLE_PIN.md)**.

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
  adjusting.
- **Icom LAN (RS-BA1).** The network CAT backend is host-tested only: the connect /
  auth / keepalive handshake and CI-V framing follow the protocol spec but haven't run
  against a real radio. Open question: whether the radio tolerates the audio stream
  never being opened (CardSat is CAT-only). There is no transmit retransmit buffer (a
  dropped CAT frame re-sends next cycle). Watch the `[NET]` serial trace. **Icom LAN is
  IC-9700 only.**
- **Antenna rotator.** Backends are host-tested only. For **GS-232**, the I²C pins
  (G8/G9) are confirmed from the Cap LoRa-1262 pinmap, but the SC16IS750 I²C→UART
  bridge and command path are host-tested for baud math and framing only — confirm the
  bridge address (`ROT_I2C_ADDR`) and controller baud **before keying real motors**.
  **rotctld (net)** follows the Hamlib TCP protocol and can be exercised against
  `rotctld -m 1`. **PstRotator (net)** is host-verified for UDP formatting against the
  PstRotator manual (Rev. 7.5). The **direct-Yaesu** I²C backend (ADS1115 feedback +
  PCF8574 direction) is host-tested only.
- **Network control surfaces.** The **rigctl** client, the **rigctld server**, and the
  **rotctld server** are host-tested only. Exercise the client against `rigctld -m 2`
  and the servers against `rigctl` / `rotctl` or Gpredict; keep both servers on a
  trusted LAN (no auth).
- **LoRa messaging** — the SX1262 path is marked UNTESTED in firmware; verify before
  relying on it. See **[design/LORA_MONITOR_SCOPE.md](design/LORA_MONITOR_SCOPE.md)**.
- **TLS** uses `WiFiClientSecure::setInsecure()` (no cert validation) — fine for public
  GP data; pin a CA root if you care.

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
src/icomnet.{h,cpp}     Icom LAN (RS-BA1 UDP) CAT backend — IC-9700 only, no wiring
src/yaesu.{h,cpp}       Yaesu 5-byte CAT (FT-847 / FT-736R)
src/kenwood.{h,cpp}     Kenwood ASCII CAT (TS-790 / TS-2000)
src/rotator.{h,cpp}     rotator backends: GS-232 / Easycomm / SPID (I²C→UART), rotctl (TCP), PstRotator (UDP), Yaesu direct (I²C)
src/voicememo.{h,cpp}   SD-card voice memo recorder + playback (ADV ES8311 mic via M5Unified)
src/irbeacon.{h,cpp}    optional IR-LED pass beacon (38 kHz carrier, per-event flash counts)
src/lora.{h,cpp}        optional LoRa text messaging (Cap LoRa SX1262 via RadioLib; CARDSAT_HAS_LORA)
src/radio_profiles.h    per-model address, baud, band-select, capabilities
tools_make_cheatcard.py generates the printable 4×6 key-reference card (front + back)
```
