# Scoping: Uploading Logs to Cloudlog (and Wavelog)

**Status:** IMPLEMENTED in 0.9.36. This document captures the API research the
implementation was built from. The feature is live: Settings → Station / display holds the
Cloudlog URL / API key / station ID, and **Log → Upload to Cloudlog** does the upload. See
the user-facing writeup in MANUAL.md (§8 → Cloudlog / Wavelog upload). The notes below are
retained as the design/reference record.

## Why this is attractive

Cloudlog/Wavelog is self-hosted, so unlike LoTW there's **no certificate, no signing, no
gzip** — just an authenticated HTTPS POST of an ADIF record as JSON. CardSat already
produces the exact ADIF a QSO needs (see `adifField()` and `beginAdifExport()` in
`app.cpp`), so most of the work is transport + a small settings/UI surface, not new log
logic. This is substantially simpler than the LoTW path.

## The Cloudlog QSO API (verified against source)

**Endpoint:** `POST {base_url}/index.php/api/qso`
(Cloudlog runs CodeIgniter; depending on the user's web-server rewrite config the path may
also work as `{base_url}/api/qso`. The scheme is whatever the user's instance uses —
typically `https://`. Because it's self-hosted, the base URL is **user-supplied**.)

**Content-Type:** `application/json` (the controller also accepts
`application/x-www-form-urlencoded` as a fallback, but JSON is the clean path).

**Request body (JSON):**
```json
{
  "key": "<API key>",
  "station_profile_id": "<numeric id>",
  "type": "adif",
  "string": "<one ADIF record, e.g. <CALL:5>N9EAT...<EOR>>"
}
```

- `key` — the user's Cloudlog **API key**. Must have **read-write (`rw`)** rights;
  `Api_model::authorize()` returns 2 only for `rw`. A read-only (`r`) key is rejected for
  QSO upload. Created in the Cloudlog UI under the account's API keys page.
- `station_profile_id` — which station location/profile to file the QSO under. The API
  **enforces** that this profile belongs to the key's owner, and that the record's
  `STATION_CALLSIGN` (if present) matches the profile's callsign — otherwise it returns
  401 with `"station callsign does not match..."`. So CardSat should emit
  `STATION_CALLSIGN` = the user's call (it already knows `cfg.myCall`).
- `type` — always `"adif"`.
- `string` — the ADIF. The server feeds this to its ADIF parser and loops records, so a
  batch upload could send multiple `<...><EOR>` records in one `string`. CALL is required
  per record (empty CALL → 401).

**Responses (observed in source):**
- Success: HTTP **201**, body `{"status":"created","imported_count":N,"messages":[...]}`.
  `imported_count` is the authoritative success count to show the user.
- Bad/!rw key: HTTP **401**, `{"status":"failed","reason":"missing api key"}`.
- Wrong station profile: **401**, reason about station id / callsign mismatch.
- Malformed JSON: `{"status":"failed","reason":"wrong JSON"}`.

**Discovering `station_profile_id`:** `GET {base_url}/index.php/api/station_info/<key>`
(needs only an `r` key) returns a JSON array of the user's stations, each with
`station_id`, `station_profile_name`, `station_callsign`, `station_gridsquare`,
`station_active`. CardSat could fetch this and let the user pick, or just have them type
the numeric id from their Cloudlog UI.

## What CardSat already has vs. what's missing

Already present (reusable):
- **ADIF record construction** — `adifField()` builds `<NAME:LEN>VALUE`; `beginAdifExport()`
  already assembles full per-QSO records (CALL, QSO_DATE, TIME_ON, BAND/FREQ, MODE,
  SAT_NAME, PROP_MODE, etc.). A Cloudlog upload reuses this verbatim for the `string`.
- **HTTPS GET** (`Net::httpsGet`) — usable for `station_info`.
- The `q.uploaded` flag pattern and the per-QSO log parsing used by the LoTW path are a
  ready template for "which QSOs still need uploading to Cloudlog."

Missing (the actual work):
1. **A JSON-body HTTPS POST** in `net.{h,cpp}`. Today there's `httpsPostMultipart` (for
   LoTW) and `httpsGet`, but no generic `httpsPostJson(url, body, resp)`. This is the main
   new transport primitive. Small and self-contained.
2. **Settings fields**: Cloudlog base URL, API key, station_profile_id. The URL and key are
   long free-text — fine with the existing edit-target mechanism, but the key is sensitive
   (see security note). New `cfg` members + Settings rows + editTargets.
3. **A minimal UI**: either a new "Upload to Cloudlog" item, or fold it into the existing
   flow. NOTE: the Log menu was just reorganized to put core functions first; a Cloudlog
   uploader is a core logging function and would sit with Export/LoTW, not with Voice
   Memos/Notes.
4. **An "uploaded to Cloudlog" flag.** `q.uploaded` is currently a single bit used by LoTW.
   To track Cloudlog independently, use a **second bit** of the same `uploaded` byte (e.g.
   bit 0 = LoTW, bit 1 = Cloudlog) rather than overloading bit 0 — otherwise a QSO uploaded
   to one service would look uploaded to the other. The CSV already stores `uploaded` as an
   integer, so widening the bitmask is backward-compatible.
5. **Response handling**: parse the 201 / `imported_count`, surface failures (esp. the
   401 reasons, which are actionable: wrong key rights, wrong station id).

## Effort estimate

Roughly: one new net primitive (`httpsPostJson`), ~3 settings fields, one upload routine
modeled closely on `doLotwUpload` (minus all the crypto/gzip), and a bitmask widening for
the uploaded flag. Materially smaller than the LoTW work because there's no signing.

## Open questions / things to verify before building

- **Exact route on a default install.** Confirm whether `/api/qso` works or whether
  `/index.php/api/qso` is required (CodeIgniter rewrite-dependent). Probably expose the full
  base URL as a setting and let the user include `/index.php` if their install needs it, or
  try both.
- **Batch vs. per-QSO.** The API loops records in one `string`, so batching is possible and
  reduces round-trips. Confirm there's no practical size limit on a self-hosted instance
  (there generally isn't, but the POST body still lives in CardSat RAM while building).
- **TLS on self-hosted instances.** Many Cloudlog installs use Let's Encrypt; some use
  self-signed certs or plain HTTP on a LAN. CardSat currently does `setInsecure()` for its
  TLS (no CA verification) which would accept either — note this in the UI rather than
  silently. Plain-HTTP LAN instances would need the POST to allow `http://` too.
- **Wavelog parity.** Wavelog's `/api/qso` is reported to be compatible; if we want to
  claim support, test against a Wavelog instance too. Don't claim it in docs until verified.

## Security note

The Cloudlog API key grants read-write access to the user's logbook. Treat it like the QRZ
password: store it in `cfg`, and be aware CardSat's existing pattern of logging request
URLs/bodies to serial would expose it. If `httpsPostJson` logs the body for diagnostics
(as the LoTW path does), **redact the `key` field** before printing, or gate that logging.
