# CardSat Web API reference

**Can a third-party UI query the CardSat web API? — Yes, with two caveats.**

CardSat's on-device web server exposes a small **HTTP + JSON API** over WiFi. Any client on
the same network — a script, a native app, another microcontroller, `curl`, a server-side
process — can call it freely: it's **plain HTTP, unauthenticated, returning JSON**. The two
things to know before building against it:

1. **A browser-based UI hosted on a *different origin* will be blocked by CORS.** The server
   sends **no `Access-Control-Allow-Origin` header**, so a web page served from anywhere other
   than the device itself cannot read these endpoints via `fetch`/`XHR` (the browser blocks the
   cross-origin response). **Non-browser clients are unaffected** — CORS is a browser policy,
   not a server restriction. A browser UI *served by the device itself* (same origin) works,
   which is exactly how the built-in page works today.
2. **There is no authentication and no encryption.** Plain HTTP on the LAN, no token, no
   password. Treat the device as fully controllable by anyone who can reach it — keep it on a
   trusted network.

This document is a complete reference to the API **as it currently ships** (it is the same
server the built-in mobile page uses). It is generated from the request router and handlers in
`app.cpp` (`webdHandleRequest` and the `webdSend*` functions). For a forward-looking plan to
extend this API to cover *every* feature including setup, see
`docs/design/WEB_CONTROL_SCOPE.md`.

---

## 1. Enabling the server

The server is **off by default**. Enable it on the device:

- **Settings → Network / data → Web control** — turn on with `,`/`/` or ENTER.
- **Web port** — default **80** (`cfg.webPort`); configurable.

Once enabled and connected to WiFi, the **Web control** settings row shows the device's IP
(e.g. `192.168.1.42`). The base URL is then `http://<device-ip>:<port>/`.

Behavior and limits of the listener (from `serviceWebd()`):

- **Binds all interfaces** (`WiFiServer` on the configured port; effectively `0.0.0.0`).
- **One client at a time.** The server accepts a single connection, reads the request, responds,
  and closes (`Connection: close` — one request per TCP connection). It is a cooperative,
  single-client listener running inside the tracking loop, **not** a concurrent web server.
- **Suspended during outbound fetches.** While CardSat is doing its own TLS download (GP update,
  weather, CloudLog/LoTW upload, etc.), the listener is **freed** to give the outbound
  connection heap headroom, and resumes afterward. A request during that window will simply fail
  to connect; retry. (This is the no-PSRAM heap management described in
  `docs/design/WEB_CONTROL_SCOPE.md` §3.)
- **Request parsing is minimal.** The server reads the request line (`METHOD PATH VERSION`) and
  drains headers until a blank line; it does not require any particular HTTP version or headers,
  and ignores the request body. Header lines are bounded (long lines are truncated). This is
  enough for `GET`/`POST` with query-string parameters — **all parameters are passed in the URL
  query string, not the body.**

---

## 2. Conventions

- **Base URL:** `http://<device-ip>:<port>`
- **Encoding:** responses are JSON (`Content-Type: application/json`), except `/` which returns
  the HTML page (`text/html`). One response per connection.
- **Parameters:** always **query-string** (`?key=value`), even for `POST`. The request body is
  ignored.
- **Frequencies** in JSON are **strings in MHz** formatted to 5 decimals for normal bands, 3 for
  ~1–10 GHz, 1 above 10 GHz (e.g. `"145.95000"`, `"10489.750"`). A frequency that isn't
  available shows as `"--.-----"`. (From `fmtMHz`.)
- **Times** (`aos`, `los`, `tca`, `ascT`, `pAos`, …) are **Unix epoch seconds** (UTC), as JSON
  numbers.
- **Angles** are degrees. **Az/el** are floats; some pass/track azimuths are rounded integers
  (noted per field).
- **Booleans** are JSON `true`/`false`.
- **Methods:** the read endpoints (`/api/status`, `/api/sats`, `/api/passes`, `/api/orbit`)
  respond to **any** method (there is no method gate); the convention is `GET`. The mutating
  endpoints require **`POST`** as noted. `/api/tx` serves JSON on `GET` and acts on `POST`.
- **Errors:** an unparseable request line returns `400`; an unknown path returns `404`. Mutating
  endpoints return `{"ok":true|false}` to indicate whether the action took effect.

---

## 3. Read endpoints

### `GET /` (or `/index…`)
Returns the built-in **HTML/JS control page** (streamed from flash). This is the same page the
device advertises; it is self-contained and same-origin, so its own `fetch` calls to the API
below work without CORS issues.

### `GET /api/status`
The live operating snapshot — the single most useful endpoint for a dashboard. Reflects the
active satellite, current transponder, pointing, and the frequencies CardSat is computing right
now (including the Manual-calculator hold/tune legs).

Response object fields:

| Field | Type | Meaning |
|---|---|---|
| `name` | string | Active satellite name (`""` if none selected) |
| `norad` | number | Active satellite NORAD id (`0` if none) |
| `rx` | string MHz | Current Doppler-corrected **downlink** (receive) frequency, or `"--.-----"` |
| `tx` | string MHz | Current Doppler-corrected **uplink** (transmit) frequency, or `"--.-----"` |
| `dop` | number Hz | Signed downlink Doppler shift applied right now (corrected RX − nominal − cal) |
| `az` | number° | Current azimuth of the satellite |
| `el` | number° | Current elevation (negative = below horizon) |
| `mode` | string | Tuning mode tag: `FULL`, `DL`, `UL`, `TUNE`, or `CAL` |
| `radio` | bool | True if CardSat is actively driving a radio (CAT output on) |
| `rot` | bool | True if CardSat is actively driving a rotator |
| `fixup` | bool | Manual mode: true if the **uplink** is the held (fixed) leg |
| `haveup` | bool | True if the current transponder has an uplink (not receive-only) |
| `txi` | number | Current transponder index (`-1` if none) |
| `txn` | number | Number of transponders on the active satellite |
| `txd` | string | Current transponder description/label |
| `lin` | bool | True if the current transponder is **linear** (has a passband) |
| `pbOff` | number Hz | Passband operating offset from the low edge (linear transponders) |
| `pbBw` | number Hz | Total passband bandwidth (linear transponders) |
| `caldl` | number Hz | Current downlink calibration offset |
| `calul` | number Hz | Current uplink calibration offset |
| `hold` | string MHz | Manual calculator: the **held** leg frequency (parked, no Doppler) |
| `tune` | string MHz | Manual calculator: the **tuned** leg frequency (Doppler-corrected) |

**Stable extension (0.9.62).** The following fields were added as a documented, stable
contract for external tooling. New integrations should prefer these; the keys above grew
organically for the built-in panel and may change.

| Field | Type | Meaning |
|---|---|---|
| `ver` | string | Firmware version (e.g. `"0.9.62"`) |
| `utc` | number | Current time, Unix seconds UTC (`0` if clock unset) |
| `lat` / `lon` | number\|null | Observer latitude °N / longitude °E |
| `grid` | string | Maidenhead grid locator (or `""`) |
| `locsrc` | string | `"gps"`, `"manual"`, or `"none"` |
| `rangeKm` | number\|null | Slant range, km |
| `rangeRate` | number\|null | Range rate, km/s (positive = receding) |
| `vis` | bool | Satellite sunlit **and** observer-dark **and** above horizon |
| `aos` / `tca` / `los` | number\|null | Next/in-progress pass acquisition / closest approach / loss, Unix seconds |
| `maxEl` | number\|null | That pass's maximum elevation, ° |
| `azAos` / `azLos` | number\|null | Azimuth at rise / set, ° |
| `riseDir` | string | Rise direction as a compass point (e.g. `"NW"`) |
| `rxRead` / `txRead` | string MHz | Rig **read-back** downlink / uplink frequency (vs the commanded `rx`/`tx`). When a **transverter LO** is configured (Settings → Radio), these are the rig's **IF** dial as the radio reports it; add the configured LO to recover the real on-air frequency. The `rx`/`tx` panel keys and the Track display already show the real frequency. |
| `catRead` | bool | Rig answered a frequency read this cycle |
| `rotEnable` | bool | Rotator configured/enabled |
| `rotAz` / `rotEl` | number\|null | Rotator **actual** (read-back) az / el |
| `rotTgtAz` / `rotTgtEl` | number\|null | Rotator **commanded** target az / el |
| `sys` | object | Subsystem status (see below) |

The `sys` object:

| Field | Type | Meaning |
|---|---|---|
| `radio` | bool | Radio CAT output engaged |
| `catProto` | string | Active CAT backend name (`"icom"`, `"yaesu"`, `"kenwood"`, `"rigctl"`, `"none"`, …) |
| `gps` | string | `"fix"`, `"nofix"`, or `"off"` |
| `gpsSats` | number | Satellites used in the last GPS fix |
| `sd` | bool | Running from an SD card |
| `wifi` | string | `"up"` or `"down"` |
| `ip` | string | IPv4 address when connected, else `""` |
| `rssi` | number | Wi-Fi RSSI, dBm |
| `batt` | number\|null | Battery level, percent |
| `charging` | bool | Battery charging |
| `heapFree` / `heapMin` / `heapBlk` | number | Free heap now / minimum since boot / largest free block, bytes |

Fields are additive across firmware versions: new keys may appear, but the ones above keep
their names, types, and meanings.

### `GET /api/sats`
The selectable-satellite list. **Favorites first**; if no favorites are marked, falls back to
the catalog. Capped at **60** entries.

Returns a JSON **array** of:

| Field | Type | Meaning |
|---|---|---|
| `n` | number | NORAD id |
| `name` | string | Satellite name |
| `fav` | bool | Whether it's marked a favorite |

```json
[{"n":43017,"name":"AO-91","fav":true},{"n":7530,"name":"AO-7","fav":true}]
```

### `GET /api/passes`
Upcoming passes for the **active** satellite (up to 8), plus an azimuth/elevation **arc** of the
next pass for drawing an approaching-pass sky plot. Empty `passes` if no satellite is selected
or the clock isn't set.

```json
{ "passes": [ {"aos":1719700000,"los":1719700600,"tca":1719700300,
               "el":42,"azaos":210,"azlos":95,"vis":false}, … ],
  "arc": [ [210,0], [215,5], …, [95,0] ] }
```

`passes[]` fields:

| Field | Type | Meaning |
|---|---|---|
| `aos` / `los` / `tca` | number (epoch s) | Acquisition / loss / time of closest approach |
| `el` | number° | Max elevation (rounded) |
| `azaos` / `azlos` | number° | Azimuth at AOS / LOS (rounded) |
| `vis` | bool | True if the pass is optically visible (sunlit sat, dark site) |

`arc` is an array of `[az, el]` integer-degree samples (24 points) across the first upcoming
pass.

### `GET /api/orbit`
A comprehensive orbital data set for the active satellite — the data behind the on-device Orbit
screens. Returns `{"ok":false}` if no satellite is selected, the clock isn't set, or mean motion
is invalid. Otherwise `{"ok":true, …}` with the fields below (grouped here by the on-device page
they feed):

**Identity & elements:** `name`, `norad`, `incl`°, `ecc`, `bstar`, `sma` (semi-major axis km),
`argp`°, `raan`°.

**Size & shape:** `alt` (current geocentric altitude km), `footprint` (footprint diameter km),
`period` (minutes), `apo` / `peri` (apogee/perigee altitude km).

**Decay:** `decayDays`, `decayLo`, `decayHi` (estimated days to decay, with range).

**Live state:** `az`°, `el`°, `range` (km), `rangeRate` (km/s, + = receding), `subLat`°,
`subLon`°, `sunlit` (bool), `eclDepth`° (eclipse depth).

**Ascending node:** `ascLon`° (longitude), `ascT` (epoch s of next ascending node).

**Next pass:** `hasPass` (bool); if true: `pAos`, `pLos` (epoch s), `pMaxEl`°, `pAzAos`°,
`pAzLos`°, `pSunPct` (% of pass sunlit).

**Nodal / J2 drift:** `nodeDrift` (°/day RAAN precession), `perigDrift` (°/day), `sunSync` (bool —
near sun-synchronous).

**Sun / beta:** `beta`° (beta angle), `betaStar`° (beta at which eclipses cease), `eclFrac`
(% of orbit in eclipse).

**Outlook (multi-day):** `outlookN` (pass count), `outlookHi` (count above a high-el threshold),
`bestEl`°, `bestT` (epoch s of best pass), `longestMin`, `avgGapH` (avg gap hours).

**Orbit position:** `meanAnom`°, `trueAnom`°, `argLat`° (argument of latitude), `toPeri` /
`toApo` (seconds to perigee/apogee).

**Arrays:**
- `track` — `[lat, lon]` sub-satellite points over ~2 orbits (37 samples) for a ground track.
- `doppler` — Doppler shift (Hz) at the beacon reference frequency across the next pass (40
  samples); `beaconMHz` gives the reference frequency used.

### `GET /api/tx`
The transponder list for the active satellite.

```json
{ "cur": 0, "list": [ {"i":0,"d":"FM Voice","m":"FM","lin":false},
                      {"i":1,"d":"Linear V/U","m":"LSB","lin":true} ] }
```

| Field | Type | Meaning |
|---|---|---|
| `cur` | number | Current transponder index (`-1` if none) |
| `list[].i` | number | Transponder index |
| `list[].d` | string | Description/label |
| `list[].m` | string | Mode (e.g. `FM`, `USB`, `LSB`, `CW`) |
| `list[].lin` | bool | Linear transponder (has a passband) |

---

## 4. Control endpoints (POST)

All take query-string parameters and return `{"ok":…}`. They have **side effects on the radio
and device state** — this is where the no-auth caveat matters most.

### `POST /api/select?norad=<id>`
Select a satellite by NORAD id and make it active (mirrors picking it on-device). Returns
`{"ok":true}` if the satellite was found and selected, else `{"ok":false}`.

### `POST /api/fav?norad=<id>`
Toggle a satellite's favorite flag. Returns `{"ok":true,"fav":true|false}` with the new state
(`ok:false` if no/invalid id).

### `POST /api/tx?i=<index>`
Select transponder `<index>` on the active satellite (mirrors the on-device `t` cycle: applies
sat mode / modes, re-arms tuning). Returns `{"ok":true}` if the index was valid. `GET /api/tx`
(no body) returns the list instead.

### `POST /api/cal?dl=<Hz>&ul=<Hz>`
Set the **downlink** and/or **uplink** calibration offset directly, in Hz (either parameter may
be omitted to leave that leg unchanged). Values are **clamped to ±100000 Hz**. Persists to config
and to the current satellite's per-sat calibration. Returns `{"ok":true}`.

### `POST /api/cmd?k=<key>[&man=1]`
Inject a **single control key** into CardSat's UI, as if pressed on the device — the bridge into
the existing key-handling state machine. Returns `{"ok":true}`.

**Only a whitelist of safe *live* controls is accepted** (anything that would navigate menus or
isn't a safe live control is silently ignored — by design):

- **Track context (default):** `,` `/` `t` `d` `r` `o` `m` `x` `s` `y`
  (e.g. `,`/`/` step the passband or tuning, `t` cycles transponder, `d`/`o`/`m`/`x`/`s`/`y` are
  the live Track toggles).
- **Manual-calculator context** (`man=1`): `u` `m` `,` `/` `t` `x` `s` (adds `u` to swap the held
  leg).
- `,` and `/` may be sent URL-encoded (`%2C`, `%2F`) or raw.

The command runs in the relevant context and **restores the device's own current screen** after,
so a web client driving live controls does not yank the on-device view. **Navigation and setup
keys are intentionally not accepted** — extending the API to those is the subject of
`docs/design/WEB_CONTROL_SCOPE.md`.

---

## 5. Examples

Assuming the device is at `192.168.1.42:80`:

```sh
# Live status (poll this for a dashboard)
curl http://192.168.1.42/api/status

# Satellite list (favorites first)
curl http://192.168.1.42/api/sats

# Upcoming passes + next-pass arc for the active satellite
curl http://192.168.1.42/api/passes

# Full orbital data for the active satellite
curl http://192.168.1.42/api/orbit

# Transponder list
curl http://192.168.1.42/api/tx

# --- control (POST; parameters in the URL) ---
# Select AO-91 (NORAD 43017)
curl -X POST "http://192.168.1.42/api/select?norad=43017"

# Pick transponder index 1
curl -X POST "http://192.168.1.42/api/tx?i=1"

# Cycle to the next transponder (live Track control 't')
curl -X POST "http://192.168.1.42/api/cmd?k=t"

# Set downlink calibration to +1200 Hz, leave uplink unchanged
curl -X POST "http://192.168.1.42/api/cal?dl=1200"

# Toggle a favorite
curl -X POST "http://192.168.1.42/api/fav?norad=43017"
```

A minimal third-party poller (Python), which works because it is **not** a browser (no CORS):

```python
import time, requests
BASE = "http://192.168.1.42"
while True:
    s = requests.get(f"{BASE}/api/status", timeout=2).json()
    if s["norad"]:
        print(f'{s["name"]:10s} az {s["az"]:5.1f} el {s["el"]:5.1f}  '
              f'RX {s["rx"]}  TX {s["tx"]}  dop {s["dop"]:+d} Hz  [{s["mode"]}]')
    time.sleep(2)
```

---

## 6. Building a browser-based third-party UI (the CORS workaround)

Because the device sends no CORS header, a browser page **served from another host** cannot read
the API directly. Options, in order of simplicity:

1. **Serve your UI from the device's origin** — not possible to add files to the device, but the
   built-in page already is same-origin; if you only need the built-in features, use it.
2. **Use a non-browser client** — a native app, a desktop tool, or a script (as above) has no CORS
   restriction at all. This is the path of least resistance for a custom UI.
3. **Proxy through a server** — run a tiny reverse proxy (on a Pi, a NAS, a PC) that fetches from
   the device and re-serves with `Access-Control-Allow-Origin`. Your browser UI then talks to the
   proxy, which talks to the device. The proxy is also the natural place to **add the
   authentication the device itself lacks**.
4. **A future device-side change** — adding an `Access-Control-Allow-Origin: *` header to the
   responses would let browser UIs read the API cross-origin. That is a firmware change, not
   something a client can do; it's noted in `docs/design/WEB_CONTROL_SCOPE.md` as part of opening
   the API up, and would want to be paired with the authentication discussed there given the
   control endpoints' side effects.

---

## 7. Summary table

| Endpoint | Method | Parameters | Returns |
|---|---|---|---|
| `/` | GET | — | HTML control page |
| `/api/status` | GET | — | Live snapshot (see §3) |
| `/api/sats` | GET | — | Satellite list (favorites first, ≤60) |
| `/api/passes` | GET | — | Upcoming passes + next-pass arc |
| `/api/orbit` | GET | — | Full orbital data set |
| `/api/tx` | GET | — | Transponder list |
| `/api/select` | POST | `norad=<id>` | `{"ok":…}` — select satellite |
| `/api/fav` | POST | `norad=<id>` | `{"ok":…,"fav":…}` — toggle favorite |
| `/api/tx` | POST | `i=<index>` | `{"ok":…}` — select transponder |
| `/api/cal` | POST | `dl=<Hz>&ul=<Hz>` | `{"ok":true}` — set calibration (±100 kHz) |
| `/api/cmd` | POST | `k=<key>[&man=1]` | `{"ok":true}` — inject a whitelisted live key |

**Access model:** plain HTTP · **no authentication** · **no CORS header** (browsers blocked
cross-origin; non-browser clients fine) · single client at a time · LAN-only by intent ·
off by default. For extending this to full control including setup, see
`docs/design/WEB_CONTROL_SCOPE.md`.
