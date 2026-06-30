# CardSat CAT, rotator, and Doppler reference

An exhaustive reference to how CardSat controls radios (CAT) and antenna rotators, and the
Doppler-tuning model that ties them to the orbital prediction. Generated from the current
source (`rig.h`/`civ`/`yaesu`/`kenwood`/`icomnet`, `rotator.{h,cpp}`, `predict.{h,cpp}`, and
the service loop in `app.cpp`).

This is the *internals* reference. For wiring/hardware see `docs/interfaces/` (CIV_INTERFACE,
CIV_SINGLE_PIN, ICOM_LAN_PROTOCOL, RS232_INTERFACE, ROTOR_INTERFACE, RADIO_SETTINGS); for the
HTTP control surface see `docs/interfaces/WEB_API.md`; for moving any of this to another
platform see `docs/guides/PORTING.md`.

---

## Part 1 — Radio control (CAT)

### 1.1 The `Rig` abstraction

The entire application talks to the radio through one narrow interface, `Rig` (`rig.h`), so
the predictor, Doppler loop, calibration, and UI are all protocol-agnostic. The pervasive
convention — kept regardless of how a given radio labels its own VFOs — is:

> **"Sub" = downlink / RX. "Main" = uplink / TX.**

Key interface methods:

| Method | Purpose |
|---|---|
| `begin(baud, uartNum, rxPin, txPin)` | Open the CAT port; the backend already knows its model/params |
| `service()` | Pumped every loop tick (network backends advance their connection/keepalive here; wired backends do nothing) |
| `setMainFreq(hz)` / `setSubFreq(hz)` | Set uplink (TX) / downlink (RX) frequency |
| `setMainMode(m)` / `setSubMode(m)` | Set uplink / downlink mode (`RigMode`) |
| `readSubFreq(&hz)` / `readMainFreq(&hz)` | Read back a leg's frequency (false if unsupported) |
| `readPtt(&tx)` | Read transmit state (false if the backend can't report it) |
| `enableSatMode(on)` | Toggle the rig's own satellite mode (see per-backend semantics below) |
| `selectSubBand()` / `selectMainBand()` | Leave band access on RX / TX (meaningful for Icom; no-op elsewhere) |
| `assignBands(mainHz, subHz)` | Put the right *band* on MAIN vs SUB (only IC-9100/9700; others false) |
| `setCtcss(on, toneHz)` | Set the transmit CTCSS/PL tone (FM birds; false if no CAT tone) |
| `setAddress(a)` / `setPinMode(m)` | CI-V address / CI-V wiring mode (Icom only) |
| `sendRaw(bytes, n)` | Write raw bytes to the CAT port (the serial-terminal diagnostic) |
| Capability flags | `canReadFreq` `hasSatMode` `hasTone` `canAssignBand` `selVerified`, read from the model's `RadioProfile` |

**Operating modes** are protocol-neutral; each backend maps them to its own codes:

```
enum RigMode { RM_LSB, RM_USB, RM_CW, RM_FM, RM_AM, RM_DATA };
```

`Rig::modeFromString()` maps an AMSAT/SatNOGS mode string (`"FM"`, `"USB"`, `"CW"`, `"DATA"`…)
to a `RigMode`.

**Pacing knobs** every backend inherits:
- `setCmdDelay(ms)` — pause this many ms after each CAT frame (the **CAT Delay** setting;
  default 70 ms) so a slow radio keeps up.
- `setReadBudgetMs(ms)` — upper bound on a single blocking CAT read, so slow I/O (the LAN
  backend especially) can't stall the cooperative main loop.

**Five concrete backends** implement `Rig`:

| Backend | File | Protocol | Radios |
|---|---|---|---|
| `CivRig` | `civ.cpp` | Icom CI-V | IC-820/821/910/970/9100/9700 |
| `YaesuRig` | `yaesu.cpp` | Yaesu 5-byte CAT | FT-847, FT-736R |
| `KenwoodRig` | `kenwood.cpp` | Kenwood ASCII CAT | TS-790, TS-2000 |
| `IcomNetRig` | `icomnet.{h,cpp}` | Icom RS-BA1 over UDP | IC-705, (intended) IC-9700 |
| `RigctlRig` | `rig.h` | Hamlib NET rigctl (TCP client) | any radio behind a `rigctld` |

The CTCSS tone helpers `ctcssToneIndex(hz)` / `ctcssToneHz(index)` share one 39-tone EIA list
across the Yaesu code table and Kenwood tone numbers (Icom encodes the tone frequency directly).

### 1.2 CI-V backend (`CivRig`) — the reference backend

CI-V is the hardware-proven path (the IC-821 is CardSat's bench reference). 

**Frame format** (`sendFrame`): every CI-V message is

```
FE FE <addr> E0 <payload…> FD
```

`FE FE` preamble, `<addr>` = the radio's CI-V address (from the model profile, settable via
`setAddress`), `E0` = the controller's address, `<payload>` = opcode + operands, `FD` = end of
message. Built into a `buf[20]`.

**Frequency encoding** (`freqToBcd`): 5-byte **little-endian BCD**, two decimal digits per
byte, least-significant pair first. (E.g. 145 900 000 Hz → bytes `00 00 90 45 01`.)

**Commands used:**

| Op | Meaning | Notes |
|---|---|---|
| `05` + 5-byte BCD | **Set frequency** | `setFreqCiv`: selects the band first, then sends `05`+BCD |
| `06` + mode + filter | **Set mode** | `setModeCiv`: filter byte defaults to `0x01`; `CivMode` enum maps `RM_*` |
| `07` (+ profile bytes) | **Select band/VFO** | `selectMain()`/`selectSub()` send the model profile's `selMain`/`selSub` byte sequence so subsequent set/read hits the right VFO |
| `07 D0` | **Swap VFO A/B** | used inside `assignBands` on IC-9100/9700 |
| `07 D2 00 <band>` / `07 D2 01 <band>` | **Assign band to MAIN / SUB** | `assignBands`, IC-9100/9700 only; **untested on hardware** |
| `1A 07` … | **IC-910 satellite mode** | the IC-910 keeps sat mode under `1A 07` per its command table |
| `1C 00` | **Read PTT/transmit state** | `readPtt`; frame `FE FE <addr> E0 1C 00 FD` |
| `1B 00` + tone, then `16` + enc | **CTCSS** | `setCtcss`: `1B 00` sets the tone (4 BCD digits, tenths of Hz), `16` enables/disables the encoder; `off` sends the `16` disable |

**CivMode** maps the modes to CI-V data bytes: `LSB=00 USB=01 AM=02 CW=03 RTTY=04 FM=05
CWR=07 RTTYR=08`.

**Set-frequency remembers what it commanded.** `setFreqCiv` stores `_lastMainHz`/`_lastSubHz`
after a successful send, because **the IC-821 in particular often won't answer a SUB-band read
(`03`)** — so a flaky read can fall back to the last commanded value instead of returning
nothing.

**Echo draining** (`drainEcho`): CI-V is a *shared bus* — the controller reads back its own
transmitted frame. After sending, CardSat drains that echo (looking for the `FD` terminators)
before reading a genuine reply, and recognizes a reply by the `FE FE E0 <addr>` header
(reversed addressing — radio→controller).

**`enableSatMode` semantics (Icom):** Icom sat mode is **actively driven OFF** — CardSat
controls MAIN (uplink) and SUB (downlink) itself rather than relying on the radio's sat mode.
The actual command bytes come from the model profile and differ by rig: **IC-9100/9700 use
`16 5A`**, while the **IC-910 uses `1A 07`** (per its control-command table).

#### Single-pin CI-V — the bespoke open-drain trick

`setPinMode` selects the CI-V wiring (call **before** `begin`):
- `0` = separate TX/RX (G2 = TX push-pull, G1 = RX) — the **recommended, default** path.
- `1` = single shared wire on the TX pin (G2); `2` = single shared wire on the RX pin (G1).

The single-pin mode emulates a real CI-V one-wire bus on **one GPIO**, and is the firmware's
most hardware-specific routine (verified on the bench step by step, documented in
`CIV_SINGLE_PIN.md` — **and flagged UNVERIFIED on-air with 5 V/3.3 V cautions**). The sequence
in `begin`:

1. `hs->begin(baud, SERIAL_8N1, pin, pin)` — put **both** UART TX and RX on the one pad (same
   call shape as the known-good two-wire begin; a scope confirmed real UART data comes out).
2. `uart_set_line_inverse(…INV_DISABLE)` — clear UART signal inversion so the idle/mark state
   is **HIGH**, not LOW.
3. `gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY)` — add a pull-up so the line idles high.
4. **`GPIO.pin[pin].pad_driver = 1`** — enable open-drain **at the pad register**. This is the
   crucial bit: `gpio_set_direction(…OD)` would re-run the pad's direction config and
   **detach the UART output matrix**, parking the pad LOW (the original "idle = 0" bug).
   Flipping only the pad's open-drain bit leaves the UART TX matrix attached, so the pin idles
   HIGH via the pull-up and is pulled low only for data — while still letting the radio pull it
   low (shared bus).
5. Re-assert the UART RX input on the same pad with `esp_rom_gpio_connect_in_signal(pin,
   rxSig…)` — a shared-pad TX binding can leave the input path disabled, and without this
   CardSat may not even hear its own echo.

Because the `HardwareSerial` peripheral is static (survives rig delete/recreate), `begin`
tracks the last-attached pins and calls `gpio_reset_pin` on them before reconfiguring — a stale
pad otherwise keeps its old UART routing when the wiring mode changes.

### 1.3 Yaesu backend (`YaesuRig`) — 5-byte CAT

Yaesu's classic CAT uses fixed **5-byte commands**: four parameter bytes followed by an opcode
(`{P1, P2, P3, P4, opcode}`), written straight to the UART.

| Operation | Command | Notes |
|---|---|---|
| **Set frequency** | `freqToBcd → P1..P4`, `cmd[4]=opcode` | 10 Hz BCD; opcode `0x11` = **SAT RX (downlink)**, `0x21` = **SAT TX (uplink)** |
| **Set mode** | `{modeCode, 0,0,0, opcode}` | opcode `0x17` = RX, `0x27` = TX; `modeCode` maps `RM_*` (e.g. `RM_USB → 0x01`) |
| **Read SAT-RX frequency** | `{0,0,0,0, 0x13}` | FT-847 "read freq & mode"; reply is BCD freq + a mode byte (`bcdToFreq`) |
| **CTCSS** | tone via opcode `0x2B`, encoder enabled with `{0x4A,…,0x2A}` | FM uplink PL tone |

**`enableSatMode` is a no-op** for Yaesu (and Kenwood): their full-duplex/satellite mode is set
up by the operator on the radio and **must not** be disturbed by CAT.

### 1.4 Kenwood backend (`KenwoodRig`) — ASCII CAT

Kenwood uses human-readable **ASCII commands terminated by `;`**, printed to the UART (`sendCmd`
does a `print` + `flush`).

- **Serial framing depends on baud:** `SERIAL_8N2` at ≤ 4800 baud, else `SERIAL_8N1`.
- **Mode characters** map `RM_*` to ASCII digits: `LSB='1' USB='2' CW='3' FM='4' AM='5'
  DATA='6'`.
- Frequency/mode/sat commands follow the TS-2000/TS-790 ASCII grammar (`FA`/`FB` set VFO A/B
  frequency, `MD` mode, `SA` satellite, etc.), each `;`-terminated.
- `enableSatMode` is a no-op (as with Yaesu).

### 1.5 Icom LAN backend (`IcomNetRig`) — RS-BA1 over UDP

`IcomNetRig` (`icomnet.{h,cpp}`) speaks Icom's **RS-BA1 network protocol over UDP** to a
LAN-connected radio (confirmed controlling an **IC-705**; intended for the **IC-9700**). It is
**packetized, not a byte stream**, so `sendRaw` returns false (the serial terminal can't drive
it). Its `service()` advances the connect/auth/keepalive state machine each tick. The CI-V
*payloads* it carries are the same opcodes as §1.2 — the difference is the transport
(connection handshake + retransmit + keepalive) layered underneath. The detailed packet format
is documented in `docs/interfaces/ICOM_LAN_PROTOCOL.md`.

> Hardware status: the LAN path is proven to control an IC-705, but the IC-705 itself isn't
> usable for a live satellite QSO (its VFOs just swap, no true sat mode) — it proves the
> transport. The IC-9700 over LAN is intended but untested.

### 1.6 rigctl backend (`RigctlRig`) — Hamlib network client

`RigctlRig` (`rig.h`) is a **TCP client to a Hamlib `rigctld` server** elsewhere on the LAN
(default port **4532**) — so CardSat can drive any radio Hamlib supports, with the remote
`rigctld` owning the radio (model-agnostic).

To carry **both legs of a full-duplex satellite QSO over one link**, it uses Hamlib **split
semantics**:
- **Downlink (Sub/RX)** = the **main VFO**: `F`/`f` (set/get_freq), `M` (set_mode).
- **Uplink (Main/TX)** = the **split/TX VFO**: `I`/`i` (set/get_split_freq), `X` (set_split_mode).

The socket **opens lazily and reconnects (throttled)** so a missing/!slow server never hangs
the Doppler loop.

> Hardware status: emits correct rigctl commands (network-verified), but not exercised against
> a physical radio behind a real `rigctld`.

---

## Part 2 — Antenna rotator control

### 2.1 The `Rotator` abstraction

Mirroring `Rig`, the app points the rotator through a narrow interface (`rotator.h`):

| Method | Purpose |
|---|---|
| `begin()` / `ready()` | Bring up the backend / report ready |
| `point(az, el)` | Command an absolute position (degrees) |
| `readPos(&az, &el)` | Read current position (false if no/invalid reply) |
| `stop()` | All-stop |
| `service()` | Fast closed-loop step for self-driven backends (default no-op) |
| `rawPos(&azCnt, &elCnt)` | Uncalibrated position counts, for calibration (default unsupported) |

Convention: **degrees**; azimuth 0–360 (0–450 with overlap), elevation 0–90 (0–180 in flip
mode). "Sub"/"Main" don't apply.

Several backends (GS-232, Easycomm, SPID) reach their controller through an **SC16IS750/752
I²C→UART bridge on `Wire1`**, because all three ESP32-S3 hardware UARTs are already used by the
radio CAT path. The network backends use TCP/UDP; the direct-Yaesu backend uses I²C ADC/GPIO
expanders.

**Backend selection** (`makeRotator(type, …)`): `type 0` = GS-232 (`ROT_GS232`), `1` = rotctld
(`ROT_NET`, TCP), `2` = PstRotator (`ROT_PST`, UDP). The direct-Yaesu backend (`ROT_YAESU`) is
built by the app itself because it must be handed calibration data.

### 2.2 GS-232 (`Gs232Rotator`)

Yaesu's **GS-232A/B**, the de-facto rotator standard (also emulated by SPID, K3NG,
RadioArtisan, ERC, …). 8N1, no handshake, commonly 9600 baud, over the I²C→UART bridge.

| Command | Meaning |
|---|---|
| `Waaa eee\r` | Point to azimuth `aaa` (000–360/450) + elevation `eee` (000–180) |
| `C2\r` | Read position → `+0aaa+0eee` (GS-232A) **or** `AZ=aaaEL=eee` (GS-232B) — CardSat parses **both** |
| `S\r` | All stop |

The backend includes the SC16IS750 register access (`wreg`/`rreg`/`bridgeInit`) and byte-level
UART through the bridge (`putc_`/`puts_`/`getc_`/`flushIn`).

### 2.3 Easycomm I/II/III (`EasycommRotator`)

The open, plain-ASCII tracking protocol used by **SatNOGS, K3NG, ERC**, and most homebrew
controllers, over the same I²C→UART bridge. One backend covers all three versions (selected by
`ver`):

| Version | Set | Query | Stop |
|---|---|---|---|
| **II / III** | `AZ<az.a> EL<el.a>\r` (0.1° decimal) | `AZ EL\r` → `AZ<az.a> EL<el.a>` | `SA SE\r` |
| **I** | `AZ<az> EL<el>` (integer) | — | — |

II and III share the positioning grammar (III only adds velocity/config commands CardSat
doesn't need); I is the integer-format variant.

### 2.4 SPID Rot2Prog (`SpidRotator`)

SPID **MD-01/02 / ROT2PROG** controllers, over the same bridge. SPID uses a **binary** protocol
(not ASCII) for set/read/stop — the backend frames those packets. (Many SPID boxes can also be
put in GS-232 mode, in which case §2.2 applies instead.)

### 2.5 rotctld (`RotctlRotator`) — Hamlib network client

A **TCP client to a Hamlib `rotctld` server** (default port **4533**) — the rotator analogue of
`RigctlRig`. CardSat is the client; a `rotctld` somewhere on the LAN drives the physical
rotator. Sends `P <az> <el>` (set position) and `p` (get position) per the rotctl grammar, with
lazy/throttled reconnect.

### 2.6 PstRotator (`PstRotator`) — UDP

Controls a **PstRotator** instance on the LAN (default UDP port **12000**) by sending
`<PST>…</PST>` datagrams: set-position is
`<PST><AZIMUTH>az</AZIMUTH><ELEVATION>el</ELEVATION></PST>`, stop is `<PST><STOP>1</STOP></PST>`,
and position is queried with `<PST>AZ?</PST>`/`<PST>EL?</PST>` (PstRotator replies `AZ:xxx.x` /
`EL:yy.y` on **port+1**).

### 2.7 Direct Yaesu (`YaesuRotator`) — closed-loop, no GS-232 box

Drives a Yaesu az/el rotator **directly**, with no external GS-232 controller:

- **Feedback:** an **ADS1115** ADC (on `Wire1`) reads the controller's two position-feedback
  voltages (AIN0 = az, AIN1 = el, via dividers).
- **Drive:** a **PCF8574** I²C expander drives four opto-isolated/relay **direction lines**
  (az CW/CCW, el up/down), handling active-low.
- **Control:** CardSat runs the closed loop **itself** in `service()` — read position,
  bang-bang drive toward the target within a **deadband**, stop — with a **stall watchdog**
  (tracks whether the counts are making progress) and **soft limits**.
- **Calibration:** ADC counts at each axis endpoint are supplied from settings;
  `cnt2deg`/`rawPos` map counts↔degrees and expose raw counts for the calibration UI.

> **⚠ Untested hardware — use entirely at your own risk; the author accepts no liability for
> damage.** See `ROTOR_INTERFACE.md`. The GS-232 bridge and the direct-Yaesu backend both live
> on `Wire1` and are **mutually exclusive**.

---

## Part 3 — The Doppler tuning model

This is what makes CardSat a satellite controller rather than a frequency display: it predicts
the Doppler shift from the orbit and tunes the radio so signals stay put. Two layers: the
**Doppler math** (`predict.cpp`) and the **tuning state machine / One True Rule** (`app.cpp`).

### 3.1 Range rate → Doppler factor

All Doppler flows from the **range rate** — how fast the satellite's distance to the observer
is changing (`LiveLook.rangeRate`, km/s, **positive = receding**), from the SGP4 prediction.
The fractional Doppler factor is

```
beta = rangeRate_m_per_s / C        (C = 299792458 m/s, config.h C_LIGHT)
```

### 3.2 The fundamental asymmetry (`dopplerFreqs`)

Downlink and uplink are **not** treated symmetrically, and getting this right is the core of
the model. Given nominal downlink `dlNominal`, nominal uplink `ulNominal`, and the calibration
offsets:

```
RX (downlink, what you receive) = dlNominal * (1 - beta) + calDl
TX (uplink,   what you transmit) = ulNominal / (1 - beta) + calUl
```

- The **downlink** is emitted by the satellite and arrives Doppler-shifted, so the observer
  tunes RX to `dl*(1-beta)` to receive it.
- The **uplink** must arrive *at the satellite* on its nominal frequency, so the ground must
  transmit `ul/(1-beta)` to pre-compensate for the shift on the way up.

(When receding, `beta>0`: RX tunes **down**, TX tunes **up** — they move in opposite
directions, which is the signature of correct full-duplex Doppler.) Calibration offsets
`calDl`/`calUl` (per-satellite, settable on-device or via `/api/cal`) are added last to absorb
fixed radio/LO error.

### 3.3 Linear transponders and passband tuning (`passbandFreqs`)

On a **linear** transponder you also choose *where in the passband* to operate. `pbOffset` (Hz
from the low edge of the downlink passband) is the operating point, **clamped to
`[0, bandwidth]`**:

```
dlOp = downlink + pbOffset
```

The uplink that pairs with that downlink point depends on whether the transponder **inverts**:

```
non-inverting:  ulOp = uplink + pbOffset           (uplink tracks downlink)
inverting:      ulOp = uplink + ulBw - pbOffset    (uplink moves opposite)
```

(Inverting is the common case for linear birds: the bottom of the uplink passband maps to the
top of the downlink passband, so tuning the downlink **up** moves the uplink **down**.) For a
non-tunable (FM/single-channel) transponder, the offset is ignored and `dlOp/ulOp` are just the
nominal pair.

### 3.4 Fixed-leg solving (`uplinkForFixedDownlink` / `downlinkForFixedUplink`)

For **manual** operating (hold one leg, let the other track) and the One True Rule, CardSat
solves the opposite leg from a *fixed* one. `uplinkForFixedDownlink` answers: *the operator has
parked RX at a ground frequency — what uplink must I transmit so the same conversation stays on
the bird?* The chain it computes:

1. The parked ground RX is `dlOp + calDl`. For the ground to hear that, the satellite must
   **emit** a downlink of `Fdl_sat = (dlOp + calDl) / (1 - beta)`.
2. `delta = Fdl_sat - dlOp` is how far that emit sits above the nominal downlink.
3. Map through the transponder with the **same inversion sense** as `passbandFreqs`:
   `Ful_sat = ulOp ∓ delta` (− if inverting, + if not).
4. Doppler-compensate so the bird actually **hears** it: `TX = Ful_sat / (1 - beta) + calUl`.

`downlinkForFixedUplink` is the mirror image (park TX, solve RX), used when the **uplink** is
the held leg. A downlink-only bird collapses to the plain Doppler-shifted downlink.

### 3.5 Tuning modes (`TuneMode`)

The active tuning behavior is one of four modes (cycled with `d` on the Track screen):

| Mode | Behavior |
|---|---|
| `TM_HOLD` | Hold the passband point; Doppler-correct **both** legs. Tune the passband with the **device keys**. |
| `TM_FULL` | **One True Rule:** the **rig's own knob** sets the passband; correct **both** legs around it. |
| `TM_DL` | One True Rule on the **downlink only** (uplink left untouched). |
| `TM_UL` | Doppler-correct the **uplink only** (downlink left untouched). |

`TM_FULL`/`TM_DL` require a radio that can read its frequency back (`canReadFreq`); the mode
cycle skips them when the backend can't.

### 3.6 The One True Rule loop (the heart of `app.cpp`'s service)

The **One True Rule** (credited to KB5MU): *hold a constant frequency **at the satellite** while
the operator tunes the rig's knob; let go and nothing drifts.* It runs each service tick when
`tuneMode ∈ {FULL, DL}`, the transponder is linear, and the rig can read frequency. The
sequence:

1. **Read the dial the operator is on** (`rigReadDownlinkFreq`), unless this is a (re)sync
   (`lastRxSet == 0`, in which case CardSat **pushes** its current point to the rig instead of
   adopting whatever it's parked on — *push-then-track*) or the rig is **transmitting**
   (`readPtt` true — the rig reports the TX VFO then, which would look like a wild jump).
2. **Detect a deliberate knob move:** compare the read-back to `lastRxSet`. A difference beyond
   `knobMoveThreshHz(t)` is a real move, not rig rounding or jitter. The threshold is
   **mode-aware** — `KNOB_MOVE_SSB_HZ = 30` for SSB/CW, `KNOB_MOVE_FM_HZ = 250` for FM (coarse,
   to avoid chasing channelized jitter) — and **floored at `RIG_STEP_HZ = 10`** so quantization
   never reads as a move.
3. **Back out Doppler to recover the operator's chosen satellite point:**
   `dlSat = (rxNow - calDl) / (1 - beta)`, then `pbOffset = round(dlSat - downlink)`.
4. **Clamp to the passband edges.** If the operator tuned past an edge, clamp `pbOffset` to
   `[0, bandwidth]` and arm the transient **out-of-passband banner** (`pbOobDir`/`pbOobUntilMs`)
   in the overrun direction; tuning back inside clears it. The pull-back to the edge happens via
   the normal downlink drive once the grace window ends.
5. **Recompute both legs** around the (possibly updated) point: `passbandFreqs` → `dopplerFreqs`.
6. **Write the legs with a tuning-grace window.** While the operator is actively tuning (within
   `TUNE_GRACE_MS = 400` of the last detected move), **don't write Doppler back to the
   downlink** — that would tug against the knob (the new point was already adopted in step 3).
   The **uplink is not on the operator's knob**, so it follows the new point immediately
   (suppressing it would just make the uplink lag the downlink, which it must not).

`lastRxSet` is set to the **actual read-back** after a downlink write (`driveDownlink`'s
`readback`), so the rig's own rounding can't later masquerade as a knob move.

### 3.7 Write deadband, TCA-adaptive threshold, and predictive lead

CardSat avoids hammering the CAT bus while still tracking tightly, via three mechanisms
(`dopplerThreshAndLead`, OscarWatch-inspired):

1. **Mode-aware write deadband** — only push a leg when it moved more than a per-mode threshold:
   `DOPP_THRESH_FM_HZ = 300` (FM tolerates kHz of Doppler in its passband, so a loose value
   avoids needless chatter) vs `DOPP_THRESH_LINEAR_HZ = 50` (linear SSB/CW needs tight tracking).
   Enforced by `driveDownlink`/`driveUplink`, which skip a re-send within `threshHz` of the last
   value (floor `FREQ_GUARD_HZ = 2`).
2. **TCA-adaptive tightening** — near closest approach the Doppler **slew** (Hz/s) is high, so
   the threshold is tightened (linearly interpolated between `DOPP_SLEW_START_HZS = 15` and
   `DOPP_SLEW_FULL_HZS = 35`, down to a floor `DOPP_THRESH_MIN_HZ = 25`) to keep the rig up;
   when slew is low the deadband stays loose.
3. **Predictive CAT lead** — Doppler is computed for `now + lead` (a TCA-tapered lead time) to
   mask CAT latency, so the frequency the rig lands on is right for when the command actually
   takes effect rather than when it was computed.

### 3.8 Uplink deferral and band parking

- **Uplink defer** (`driveUplinkDeferred`, OscarWatch "defer uplink after a dial move"): after a
  downlink write or a knob move, the uplink write is held off **one tick**
  (`UPLINK_DEFER_TICKS = 1`) so the SUB read and RX settle first. The re-arm is guarded so that
  during a fast slew (downlink writing every tick) the uplink still services **every other
  tick** instead of starving.
- **Band parking** (`driveUplink` → `rigSelectDownlink`): after moving the uplink, CardSat
  leaves the radio's **active band on the downlink**, so the operator's knob and the read-back
  stay on RX.
- **CTCSS** (`applyCtcssForCurrentTx`): for FM birds whose uplink needs a subaudible PL tone, the
  tone is (re)sent only when it changes.
- **CAT rate floor** (`effectiveCatRateMs`): CardSat never sends CAT faster than the configured
  baud can comfortably service one update — `max(configured rate, baud-derived minimum)`, from
  `CAT_BYTES_PER_UPDATE = 80`.

### 3.9 How it all fits together (per service tick, tracking a linear bird in FULL)

```
range rate (SGP4)
   └─> beta = rr/C
        ├─ read rig downlink ──> knob move? ──> back out Doppler ──> pbOffset (clamped)
        ├─ passbandFreqs(t, pbOffset) ──────> dlOp, ulOp
        ├─ dopplerFreqs(dlOp, ulOp, lead-adjusted rr, calDl, calUl) ─> rx, tx
        ├─ driveDownlink(rx)   [skipped during tuning grace; deadband; read-back]
        └─ driveUplinkDeferred(tx)  [one-tick defer after a DL write/knob move; then park band on RX]
```

The same `rx`/`tx` numbers are exactly what `/api/status` reports (`rx`, `tx`, `dop`,
`pbOff`, `pbBw`, `mode`), so a web client sees precisely what the radio is being told.

---

## Hardware verification status (per the source's own caveats)

| Path | Status |
|---|---|
| IC-821 single-pin CI-V (Doppler + full knob tuning) | **Confirmed on hardware** (the bench reference) |
| Icom LAN (RS-BA1 UDP) | **Controls an IC-705** (proves the transport; IC-705 not usable for live sat; IC-9700 intended, untested) |
| rigctl / rotctl, PstRotator | **Network-verified** (correct commands emitted; not driven against a physical rig/rotator) |
| Separate-pin CI-V; Yaesu (FT-847/736R); Kenwood (TS-790/2000) | **Host-tested, not hardware-confirmed** |
| GS-232 / Easycomm / SPID / direct-Yaesu rotator backends | **Untested on hardware** (direct-Yaesu carries an explicit liability warning) |

These reflect the caveats embedded in the code itself; treat any "untested" path as
at-your-own-risk until verified against the actual radio/rotator.
