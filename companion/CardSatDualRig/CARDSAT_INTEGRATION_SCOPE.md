# CardSat-side integration — scope (no CardSat changes made yet)

Two future CardSat features are scoped here. **Neither is implemented** — CardSat is
unchanged. This documents what each would take so it can be picked up later.

---

## A. Configure the Stick directly from CardSat (no phone)

Today the Stick is configured from a phone/laptop via its captive portal or HTTP
API. The goal here is to drive that same configuration from the Cardputer's own UI,
so a field operator needs no second device.

### What the Stick already exposes (reuse, don't rebuild)

The Stick's HTTP API is the configuration surface and is transport-agnostic in
spirit — every setting is a flat key/value:

```
GET  /api/status         -> JSON: mode, AP, sta_ip, tcpPort, wifiOn, groveBaud,
                             downlink{model,civ,baud,serial}, uplink{...},
                             devices[]{addr,vidpid,product,serial}
GET  /api/models         -> JSON: [{id,name,rxOnly,civ}, ...]
POST /api/config         -> set any subset: ssid,pass,tcpport,wifi,grovebaud,
                             dl_model,dl_civ,dl_baud,dl_serial, ul_model,ul_civ,
                             ul_baud,ul_serial, save, reboot
```

### Option A1 — CardSat as an HTTP client (Wi-Fi path)

When CardSat is already on the same network as the Stick (the normal Wi-Fi control
case), add a small CardSat screen "Dual-Rig setup" that:

1. `GET http://<stick>/api/models` once to populate a model picker.
2. `GET /api/status` to show the live `devices[]` list and current assignments.
3. Let the operator pick downlink/uplink model, CI-V address, baud, and (optionally)
   pin to a device serial.
4. `POST /api/config` with `save=1` to persist.

CardSat already has an HTTP client (it fetches TLE/weather and speaks RS-BA1/rigctl),
so this is a new screen plus a handful of requests — no new subsystem. Estimated
scope: one settings screen, a JSON reader (CardSat already has lightweight JSON
handling for CelesTrak GP), ~1 new `SCR_*` screen and a fetch helper. Because the
Stick's config API is plain form-encoded key/values, CardSat can build requests with
its existing string tooling; no JSON *writer* is required.

### Option A2 — config over the rigctl link itself (IMPLEMENTED on the Stick)

The Stick's rigctld dispatcher (`handleRigctlLine`) now includes a vendor escape so
config travels over the *same* link CardSat already uses — **TCP or Grove** — with no
second HTTP connection and no phone:

```
\csdr_get                one JSON line: full status (config + enumerated USB devices)
\csdr_devices            one JSON line: enumerated USB devices
\csdr_set k=v k=v ...      apply config keys (same names as the HTTP API);
                         optional save=1 / reboot=1              -> "RPRT 0"
\csdr_save               persist config to NVS                    -> "RPRT 0"
```

Key names match the HTTP API exactly (`dl_model`, `dl_civ`, `dl_baud`, `dl_serial`,
`ul_*`, `ssid`, `pass`, `tcpport`, `wifi`, `grovebaud`). Both the HTTP handler and the
escape funnel through one `applyConfigKV()` function, so they can't drift. Because USB
host runs in **run mode as well as config mode**, CardSat can enumerate
(`\csdr_devices`) and reassign radios live over Grove without forcing the Stick into
AP/portal mode.

**The CardSat side is implemented** (since v0.9.62): a Cardputer "Dual-Rig
setup" screen that (1) sends `\csdr_get` and parses the JSON to show current
assignments and the device list, (2) lets the operator pick models/addresses/bauds,
and (3) sends `\csdr_set … save=1`. Over Grove this reuses the Grove rigctl transport
from section B; over Wi-Fi it reuses the existing rigctl TCP socket. Real Hamlib
clients never send `\csdr_*`, so adding it was harmless to standard interop.

Estimated CardSat scope: one screen + a small JSON reader (CardSat already parses
CelesTrak GP JSON) + a line sender over whichever transport is active. No JSON writer
needed — requests are `key=value` tokens built with existing string tooling.

### CI-V-address note (from the request)

The IC-R10/R20/R30 and any Icom whose address was changed are handled by the Stick's
**per-leg CI-V address** field (`dl_civ` / `ul_civ`, hex). So "allow setting the CI-V
address" is satisfied on the Stick; the CardSat setup screen just needs to expose
that field. CardSat itself never needs to know the CI-V address — the Stick is the
CI-V master and CardSat only speaks rigctl to the Stick.

---

## B. CardSat → Stick over a Grove serial cable (no Wi-Fi)

The Stick side is **implemented**: it runs a second rigctld transport on its Grove
UART (`Serial1` on GPIO9/GPIO10) that speaks the exact same protocol as the TCP
server. This section scopes the **CardSat side**, which is *not* built.

### Wiring

Both devices are 3.3 V, so a plain Grove cable works — no level shifter. Cross TX/RX:

```
Cardputer Grove TX  ->  Stick Grove RX (GPIO9)
Cardputer Grove RX  <-  Stick Grove TX (GPIO10)
Cardputer GND       <-> Stick GND
```

(If nothing is received, swap the two signal wires — Grove pin order varies.)

The Stick's Grove baud is configurable (default 115200) and must match CardSat's.

### What CardSat needs (new transport, not a new protocol)

CardSat already has a rigctl **client** that speaks the VFO-mode protocol over TCP
(`RigctlRig`). The Grove path reuses that exact protocol; only the byte transport
changes from `WiFiClient` to a `HardwareSerial`/Grove UART. Concretely:

1. **New CAT type** — add a `CAT_RIGCTL_GROVE` (or reuse "rigctl" with a transport
   sub-setting) alongside the existing Wired CI-V / Icom LAN / rigctl (net) / USB.
2. **New backend** — a near-clone of `RigctlRig` whose `xchg()` writes/reads a
   `Stream*` bound to the Cardputer's Grove UART instead of a socket. Everything
   above the transport (the `V`/`F`/`f`/`M`/`t` VFO-mode command set that was just
   made compliant) is **identical** and should be factored so both share it.
3. **Pin/UART config** — a setting for the Cardputer's Grove UART pins and baud
   (matching the Stick). CardSat already manages a Grove UART for GPS/rotator, so it
   has the plumbing; the constraint is the mutual-exclusion rules already in the
   codebase (Grove can host wired CI-V, Grove GPS, or a Grove rotator — the Grove
   rigctl transport joins that exclusion set).
4. **No level shifting / no TLS / no Wi-Fi** — this is the low-power, no-network
   field mode. It should keep working when Wi-Fi is off on both ends.

### Suggested factoring (to avoid protocol drift)

Pull the rigctl-client command formatting/parsing out of `RigctlRig` into a helper
that operates on a `Stream&` (both `WiFiClient` and `HardwareSerial` are `Stream`s).
Then the TCP backend and the Grove backend are each ~30 lines of transport glue over
one shared protocol implementation. This mirrors exactly what the Stick side does
(`handleRigctlLine(Stream&)` serves both TCP and Grove), so the two ends stay in
lock-step and the VFO-mode fix applies everywhere at once.

### Estimated scope

Small-to-moderate: one new CAT-type enum value + settings rows (pins/baud), one new
backend class sharing the existing protocol code, and inclusion in the Grove
mutual-exclusion logic. No new protocol, no new parser — the Stick already answers
this protocol on its Grove port, verified to compile.
