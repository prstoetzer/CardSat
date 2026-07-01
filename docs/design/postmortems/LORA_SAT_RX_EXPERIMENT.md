# Post-mortem: satellite LoRa reception experiment (the "tinyGS" arc)

**Status: experiment concluded and removed from CardSat. Archived here for
reference.** This records an exploration into receiving LoRa **satellite**
telemetry on the Cardputer — initially framed around the [tinyGS](https://tinygs.com)
network — that was ultimately **descoped and fully reverted** in favour of a
general-purpose LoRa RX / hex monitor (`docs/design/LORARX_IMPLEMENTATION.md`),
which is what shipped.

It is kept because a **separate, dedicated tinyGS firmware for the Cardputer** may
be worth building in future, and the findings below (especially about how tinyGS
actually works) were expensive to obtain and would otherwise have to be
rediscovered. Nothing here is in the shipping build.

---

## 1. The single most important finding

**tinyGS is an MQTT system, not an HTTP one. There is no REST API that returns a
satellite list, tuning parameters, or TLEs.** This was discovered by reading the
actual tinyGS firmware source, after a guessed HTTP endpoint
(`https://api.tinygs.com/v2/satellites`) returned **404** on-device.

Concretely, from the tinyGS firmware:
- A station connects to **`mqtt.tinygs.com`** over TLS.
- It **subscribes** to a global topic and a per-station command topic.
- The network **pushes** the current satellite's parameters (frequency,
  modulation, SF/BW/CR) and its **TLE** to the station as MQTT messages
  ("autotune"), typically minutes before a pass.
- On a received packet, the station **publishes** the raw bytes + metadata (RSSI,
  SNR, frequency, time) back to an MQTT topic.
- **Decoding is server-side.** tinyGS's `tinygs-decoders` project holds a
  *per-satellite* decoder for each bird; there is no universal on-device decoder.
- MQTT credentials are provisioned out-of-band via the tinyGS **Telegram bot**
  (`/mqtt`), and the only `HTTPClient` uses in the firmware are OTA updates and
  that credential/OTP provisioning — **not** satellite data.

Implication: contributing to tinyGS requires a **persistent MQTT-over-TLS
connection** held open across a whole pass, plus autotune handling. That is a
fundamentally different (and heavier) thing than a one-shot HTTPS fetch.

---

## 2. Why it did not fit CardSat (the heap reality)

CardSat runs on an **ESP32-S3 with no PSRAM**. A companion post-mortem
(`docs/design/UPLOAD_AND_AUDIO_TLS_POSTMORTEM.md`) established that the binding
constraint on this part is the **internal/DMA-capable RAM pool** that the TLS
handshake draws from: a ~5 KB swing there decided success or failure for ordinary
one-shot uploads, and multiple cold TLS handshakes cannot even run in one session
once the heap fragments (which is why uploads had to reboot between batches).

A tinyGS **station** needs the opposite of one-shot: a **long-lived** TLS/MQTT
socket kept open while the radio receives continuously and the SD card logs. That
pushes directly against the very constraint that dominated the upload work, and
the "reboot between operations" escape hatch cannot apply to a connection that
must persist. The realistic conclusion was that a robust tinyGS station on this
hardware is **PlatformIO-territory** — it likely needs the sdkconfig levers
(shrinking the mbedTLS record buffers, raising the lwip buffers) documented in
`docs/design/LOTW_UPLOAD_SIZE_WORKAROUNDS.md` — and is a poor fit for the
single-`.ino` Arduino build CardSat ships as.

---

## 3. The three architectures the experiment went through

The exploration iterated as reality was discovered — each step is a lesson:

1. **HTTP satellite list (fictional).** Fetch a satellite/parameter list from a
   tinyGS REST endpoint over HTTPS. **Failed at 404**: the endpoint does not
   exist. Reading the firmware showed why (§1). This was the clearest instance of
   the session-long lesson: *stop guessing about the black box; read the source.*

2. **GP sources + SatNOGS transmitter lookup (worked, but off-mission).** Populate
   a satellite list from CardSat's own GP data (main DB / SD file / manual entry),
   then resolve each satellite's LoRa **frequency** by reusing CardSat's existing
   **SatNOGS** transmitter code (`SATNOGS_TX_URL`, `SatDb::loadTxCache`,
   filtering transmitters whose `mode == "LoRa"`), and propagate with the existing
   SGP4 for Doppler/AOS/LOS. This was technically sound and used only one-shot
   cached GETs (no persistent connection) — but:
   - It was **not actually tinyGS** (no network contribution, no autotune).
   - SatNOGS publishes the downlink frequency and mode but usually **not** the
     LoRa SF/BW/CR, so reception still required per-sat parameters from a file or
     manual entry.
   - The satellite framing added UI overlap and complexity for a niche result.

3. **Descope to a general LoRa RX monitor (shipped).** The satellite framing was
   dropped entirely. What remained useful — set the full SX1262 RX parameters and
   watch incoming frames as a scrolling hex/ASCII dump with live tuning — became
   the `lorarx` feature, which works for **any** LoRa signal, not just satellites,
   and has no network or orbital machinery. See
   `docs/design/LORARX_IMPLEMENTATION.md`.

---

## 4. What was reused vs. removed

**Kept (generally useful, now serves the LoRa RX monitor):**
- `LoraRadio::setRadioRx(freq, sf, bw, cr, sync, preamble, crc)` — full LoRa RX
  reconfiguration with the shared-SPI/SD bus discipline. Written during the
  experiment; retained.

**Removed (satellite-specific, reverted):**
- The `tinygs` module (`src/tinygs.{h,cpp}`) in all its forms.
- `Predictor::setTle(name, l1, l2)` — a raw-TLE entry added so the experiment could
  propagate satellites that were not in the GP table. Removed with the module.
- All app wiring (a `SCR_TINYGS` screen, dispatch, a launcher key, a `friend`
  declaration, a member), the SD list-file format, and the satellite parameter
  overrides.

---

## 5. Hard-won specifics worth keeping (for a future tinyGS firmware)

If a **dedicated tinyGS Cardputer firmware** is built later (as its own project,
not inside CardSat), start from these facts rather than rediscovering them:

- **Transport is MQTT over TLS** to `mqtt.tinygs.com`. Plan the whole design around
  a persistent connection, not request/response.
- **The wire format has evolved** over the project's life. Do **not** implement the
  topic/payload shapes from memory — read the current tinyGS firmware
  (`ConfigManager` for the broker host, `MQTT_Client` for the rx-publish payload,
  `Radio.cpp` for how the pushed satellite/TLE message is parsed) and match it.
- **Credentials** come from the Telegram bot (`/mqtt`); provisioning is an
  `HTTPClient`/OTP flow in the firmware, separate from satellite data.
- **Autotune** drives the radio: the station subscribes and retunes to the
  satellite/parameters the network sends, scheduled around passes.
- **Decoding is server-side** (`tinygs-decoders`, per-satellite). An on-device
  station only needs to **capture and forward** a well-formed packet; human-readable
  telemetry is produced in the cloud. On-device decode of a specific bird is a
  separate, per-satellite effort.
- **The hardware limiter is internal RAM, not RF.** A persistent TLS/MQTT session
  co-existing with continuous radio RX and SD logging on a no-PSRAM S3 is the core
  engineering risk. Budget it explicitly, and expect to need a project-owned
  `sdkconfig` (PlatformIO) to shrink the TLS buffers — the Arduino core's fixed
  buffers are what make it tight. (The Cap LoRa's SX1262 has **no band-pass
  filter**, so 433 MHz — where most tinyGS birds are — is reachable with a 70 cm
  antenna; the antenna, not the front end, is the limit.)
- **A workable structure** for a dedicated build: one MQTT/TLS client task, an
  autotune handler that calls a `setRadioRx`-style full-parameter reconfiguration,
  a receive path that publishes raw frames, and the shared-SPI/SD discipline
  CardSat already proved (`Store::remount()` after every RF SPI burst) since the
  SX1262 and the SD card share one bus.

---

## 6. The meta-lesson

Every wrong turn in this arc came from reasoning about what tinyGS or the API
"should" do; every correction came from reading the actual firmware or watching the
device fail. The 404 was not a wrong URL to fix — it was the source telling us the
entire HTTP premise was fictional. When the next tinyGS attempt begins, begin by
reading the current tinyGS source.
