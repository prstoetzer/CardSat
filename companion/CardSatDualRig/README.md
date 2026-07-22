# CardSatDualRig — two-radio rigctld bridge for the M5StickS3

A companion firmware that lets **CardSat** (or any Hamlib NET-rigctl client) work a
linear-transponder satellite pass with **two half-duplex or receive-only radios** —
one on the downlink, one on the uplink — as if they were a single full-duplex rig.
It does **not** modify CardSat.

## Why

A full-duplex sat rig (IC-9700, FT-847, TS-2000) transmits and receives at once, so
CardSat drives it directly. **Half-duplex and receive-only** radios can't, so a
proper linear-satellite station needs two of them. This firmware is the glue:
CardSat sees one rigctld server and steers two VFOs; the Stick hosts the radios on
USB and translates each one's native CAT. All the dual-radio / dual-USB complexity
lives on the 8 MB-PSRAM StickS3, off the no-PSRAM Cardputer.

## Two control paths (either or both)

- **Wi-Fi / TCP** — CardSat's `rigctl (net)` CAT type points at the Stick's IP:port.
- **Grove UART** — a Grove cable between the Cardputer and the Stick carries the same
  rigctld text protocol, no Wi-Fi. The Stick side is implemented; the CardSat side is
  scoped in `CARDSAT_INTEGRATION_SCOPE.md` and implemented in CardSat since v0.9.62
  (Dual-Rig setup screen + rigctl Grove/TCP backend).

## Runtime configuration — no compile-time flags

Everything (Wi-Fi credentials, which radio is on which leg, CI-V addresses, bauds) is
set at runtime and saved to NVS.

**Enter config mode** when there is no saved config, or by **holding Button A at
boot** (or long-pressing Button A while running). In config mode the Stick:

1. Raises a Wi-Fi **AP** named `CardSatDualRig-XXXX` (password `cardsat123`) and a
   **captive portal** — join the AP and a setup page appears (or browse to
   `http://192.168.4.1`).
2. Runs **USB host**, so the radios you plug into the hub **enumerate live** and show
   up on the page for assignment. Assign each to the downlink (VFOA) or uplink
   (VFOB) leg, pick the model, and optionally set a CI-V address, CAT baud, and pin
   the leg to a specific device by USB serial.
3. Saves to NVS and reboots into **run mode** on "Save & run".

### Direct HTTP API (bypasses the web UI, for scripting)

Same endpoints the page uses — all parameters optional, form-encoded, `GET` or `POST`:

```
GET  /api/status     JSON: mode, ap, sta_ip, tcpPort, wifiOn, groveBaud,
                            downlink{model,civ,baud,serial}, uplink{...},
                            devices[]{addr,vidpid,product,serial}
GET  /api/models     JSON: [{id,name,rxOnly,civ}, ...]
GET  /api/devices    JSON: enumerated USB devices
POST /api/config     ssid, pass, tcpport, wifi(0|1), grovebaud,
                     dl_model, dl_civ(hex), dl_baud, dl_serial,
                     ul_model, ul_civ, ul_baud, ul_serial, save(1), reboot(1)
POST /api/reboot
```

Example (assign an IC-R30 receiver (model 9) at CI-V 0x9C to the downlink and an FT-818
(model 17) to the
uplink, then save & reboot):

```
curl "http://192.168.4.1/api/config" \
  --data "dl_model=9&dl_civ=9C&ul_model=17&save=1&reboot=1"
```

(Model ids come from `GET /api/models`.)

## Supported radios

| Family | Radios | CAT on the wire |
|---|---|---|
| Icom CI-V (transceivers) | IC-705, IC-905, IC-7100, IC-7000, IC-706MKIIG, IC-275, IC-475 | 5-byte BCD freq, per-radio address |
| Icom CI-V (handheld receivers, RX only) | IC-R10, IC-R20, IC-R30 | CI-V; addr 0x52 / 0x6C / 0x9C |
| Icom CI-V (wideband all-mode receivers, RX only) | IC-R7000, IC-R7100, IC-R8500, IC-R8600, IC-R9000, IC-R9500 | CI-V; addr 0x08 / 0x34 / 0x4A / 0x96 / 0x2A / 0x72 |
| Yaesu "old" binary | FT-817, FT-818, FT-857, FT-897, FT-100 | 5-byte block, 4-byte BCD @10 Hz |
| Yaesu receiver (RX only) | VR-5000 | Yaesu 5-byte family — **verify on hardware** |
| Yaesu "new" ASCII | FT-991, FT-991A, FTX-1 | `FA`/`MD` `;`-terminated |
| Kenwood handheld (all-mode RX, RX only) | TH-D74, TH-D75 | `FQ<band>,<10-digit Hz>`+CR |

**Receive-only radios** (all the IC-R sets, the VR-5000, and the TH-D74/D75 here) are
marked as such: they tune on the downlink leg and are never keyed. **PTT is handled
manually** by the operator — the Stick never sends a transmit command over CAT to any
radio; it only tracks PTT state a client may set/read. Put your transmit radio on the
uplink leg and key it by hand (front panel / footswitch).

CI-V addresses in the table are factory defaults and are **editable per leg**, so a
radio whose address you changed (or any receiver) still works.

### Notes on the receivers

- **Kenwood TH-D74/D75:** the DSP all-mode receiver (SSB/CW/AM) is on **Band B** only
  (Band A is FM/DV), so the firmware drives Band B (`FQ1,…` / `MD1,…`). Mode values
  follow the CHIRP TH-D74 order.
- **Icom wideband receivers (IC-R7000/R7100/R8500/R8600/R9000/R9500):** all cover
  VHF/UHF in SSB/CW/AM and reuse the proven CI-V code unchanged — the cleanest adds.
  Older sets (R7000/R9000) default to 1200 baud; adjust in the portal if yours differs.
- **Yaesu VR-5000:** an all-mode wideband receiver that uses the Yaesu 5-byte CAT
  family. It's wired to that dialect, but its exact opcodes differ from the FT-817's
  in places — **confirm frequency/mode on the bench** and tweak the encoder if needed.
- **Kenwood/other Yaesu receivers:** no other dedicated VHF/UHF all-mode receiver with
  a documented, reliable CAT command set turned up (Kenwood's are HF-only or
  undocumented). Easy to add later if a spec surfaces — the dialect layer is modular.

## Wiring

- A **powered USB hub**: the ESP32-S3 OTG port has 8 channels and no root hub, so two
  devices need a hub (hub = 2 channels, each serial device = 3, so 2+3+3 = 8 — at the
  ceiling; use a simple compliant hub).
- Each radio to the hub by its **native USB CAT** port, or via a **USB-serial adapter**
  (CP210x / FTDI / CH34x) to its CAT jack. EspUsbHost handles both.
- **Grove path:** Cardputer Grove TX → Stick Grove RX (GPIO9), Cardputer RX ← Stick TX
  (GPIO10), GND↔GND. Both are 3.3 V (no level shifter). Swap the two signals if
  nothing is received. Grove baud (default 115200) must match on both ends.

### Power: can the StickS3 feed the hub + two adapters itself?

**Don't count on it — use a powered hub or feed 5 V into the Stick.** The StickS3 runs
from a small **250 mAh** battery through a custom **M5PM1** PMIC. It has a
software-switched 5 V boost (`M5.Power.setExtOutput(true)`), but that boost feeds the
**Grove / Hat2 EXT_5V / IR rails — not the USB-C VBUS**, and M5Stack's docs frame the
USB-C port as a power *input*. So in host mode the Stick isn't built to source much
VBUS, and the small boost + tiny cell can't reliably carry a hub plus two serial
adapters (a few hundred mA, more on inrush). Options, best first:

1. **Powered USB hub** — the hub's supply runs the adapters; the Stick only does data.
   Most reliable, and what the wiring above assumes.
2. **Feed 5 V into the Stick** and let a bus-powered hub draw from it — via Grove
   5 V-input mode or the Hat2-Bus **5VIN**. The Grove port is 5 V *input by default*;
   enabling EXT_5V **output** repurposes those pins, and you must never feed 5 V into
   an output (short-circuit risk).
3. **Bus-powered hub straight off the Stick's USB-C** — only if you've measured it on
   the bench with *your* adapters and confirmed it's stable. Unverified until then.

For a portable field build, bring a small powered hub or a 5 V feed for the Stick; the
battery is for the Stick, not for two radios' adapters.

## Prebuilt firmware

If you just want to run it, precompiled binaries for the M5StickS3 are in
[`firmware/`](firmware/) — flash `CardSatDualRig-merged.bin` at offset `0x0` with
esptool or M5Burner. See [`firmware/README.md`](firmware/README.md) for exact
commands and offsets. To build it yourself instead, read on.

## Build

Board **ESP32S3 Dev Module**; **USB Mode = Hardware CDC and JTAG**; USB CDC On Boot =
Disabled; Flash 8 MB; PSRAM enabled; Partition "8M with spiffs"; **Core Debug Level =
Error** (EspUsbHost only defines its log `TAG` at level ≥ Error).

```
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled,DebugLevel=error" \
  --build-property "compiler.cpp.extra_flags=-DESP_USB_HOST_MAX_DEVICES=4 -DCORE_DEBUG_LEVEL=1" \
  CardSatDualRig
```

> Put the extra defines in `compiler.cpp.extra_flags` (which **appends**).
> `build.extra_flags` would overwrite the core's `CORE_DEBUG_LEVEL` and bring back a
> `TAG` compile error.

Files: `CardSatDualRig.ino` + `catradio_types.h` (the header holds the type/struct
definitions so Arduino's prototype generator sees them; all logic is in the `.ino`).

## rigctld commands

VFO model (primary): `V`/`v`, `F`/`f`, `M`/`m`, `T`/`t`, plus `\chk_vfo`,
`\dump_state`, `\get_powerstat`, `_`, `q`. Legacy split (`S`/`s`, `I`/`i`, `X`/`x`) is
also accepted and mapped onto the two legs. Each transport (TCP and Grove) has its
own VFO selection state so two clients never collide. `T` (set_ptt) is accepted but
**never keys a radio** (PTT is manual) — it only updates tracked state.

### Configuring the Stick over the control link (`\csdr_*`)

So CardSat can configure the Stick without a phone — over Wi-Fi **or the Grove cable**
— the rigctld dispatcher understands a vendor escape (works on every transport):

```
\csdr_get                one line of JSON: full status (config + USB devices)
\csdr_devices            one line of JSON: enumerated USB devices
\csdr_models             one line of JSON: this build's radio catalogue
                         [{id,name,rxOnly,civ}, ...]
\csdr_set k=v k=v ...     apply config keys (same names as the HTTP API);
                         optional save=1 / reboot=1        -> "RPRT 0"
\csdr_save               persist current config to NVS      -> "RPRT 0"
```

Example over the link (assign an IC-R8600 to the downlink, an IC-705 to the uplink,
save and reboot): `\csdr_set dl_model=13 ul_model=0 save=1 reboot=1`. Because USB host
runs in **run mode too**, CardSat can enumerate (`\csdr_devices`) and reconfigure
without forcing the Stick into AP/portal mode. **CardSat drives this from its own UI:**
Settings → Radio → *Dual-Rig setup (Stick)* connects over the active rigctl transport
(net or Grove), shows the live USB enumeration, and lets you bind a device to each leg
and pick the model — no phone or portal needed. `\csdr_models` exists so CardSat shows
*this* firmware's radio list rather than a hard-coded copy. Real Hamlib clients never
send `\csdr_*`, so the escape is harmless to them.

## Status / honesty

Compiles clean against ESP32-S3 + EspUsbHost 2.3.2 + M5Unified (37% flash, 18% RAM).
**Not yet hardware-tested against physical radios.** All four CAT frequency encoders
were byte-verified against the specs / CardSat's proven CI-V codec. Bring-up order:

1. Config mode first — confirm your radios enumerate and appear in the portal.
2. Prove **one** radio tunes on USB before adding the hub + second radio (channel
   budget is tight).
3. Per-radio quirks most likely to need a tweak: FT-817 CAT-enable state; native-USB
   radios that expose two CDC interfaces (only one is CAT); the TH-D74/D75 mode-digit
   map and PTT-band behavior; exact CI-V addresses (all editable in the portal).

See `CARDSAT_INTEGRATION_SCOPE.md` for the two not-yet-built CardSat-side features:
configuring the Stick directly from the Cardputer, and the CardSat Grove transport.
