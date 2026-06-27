# CardSat — Direct LoTW Upload: Prototype Results & Integration Plan

**Status:** Format and signing **proven host-side**. Not yet integrated into firmware.
Target firmware version: **0.9.34** (a feature release; out of scope for 0.9.33).

---

## 1. What was proven

The hard part of LoTW upload is producing a byte-correct, correctly-signed `.tq8`.
That is now fully validated against ARRL's own spec (`developer-tq8`) and, crucially,
against the **exact crypto library the ESP32-S3 already links (mbedTLS)**:

| Step | Proven by | Result |
|---|---|---|
| `.tq8` four-section byte format | `cardsat_lotw_reference.py` round-trip | builds + parses |
| SIGNDATA normalization order | matched to `developer-tq8` field list | exact |
| RSA-PKCS1v15-over-SHA1 signature | `openssl dgst -sha1 -verify` (LoTW's own command) | **Verified OK** |
| On-device signing path | `sign_mbedtls.c` via `mbedtls_pk_sign` | **byte-identical** to OpenSSL |
| `.p12`-exported key signs the same | mbedTLS over a key extracted from a real PKCS#12 | **Verified OK** |
| gzip framing (miniz raw-deflate) | `gunzip -t` on the manually-framed stream | valid |

**Bottom line:** the firmware can produce a `.tq8` LoTW will accept, using only
primitives it already has (mbedTLS RSA/SHA-1, base64, miniz deflate, WiFiClientSecure).

### The `.tq8` format (verified)

A gzip-compressed text file, four ADIF-like sections:

1. `<TQSL_IDENT:n>...` — TQSL/lib/config versions.
2. `<Rec_Type:5>tCERT<CERTIFICATE:n>{base64 DER X.509}<eor>` — the callsign cert.
3. `<Rec_Type:8>tSTATION...<eor>` — station-location fields (DXCC, grid, zones…).
4. One `<Rec_Type:8>tCONTACT...<SIGNDATA:n>{norm}<SIGN_LOTW_1.0:n>{b64 sig}<eor>`
   per QSO.

**SIGNDATA** = normalized ADIF fields in this exact order (skip absent optionals):
`BAND, BAND_RX, FREQ, FREQ_RX, MODE, PROP_MODE, QSO_DATE, QSO_TIME, SAT_NAME`.
**Signature** = RSA-sign the SHA-1 digest of SIGNDATA (verifies with
`openssl dgst -sha1 -verify pubkey -signature sig signdata`).

---

## 2. The one unavoidable constraint

First-time **certificate enrollment cannot happen on the device** — ARRL gates it
behind TQSL + a mailed postcard. So this is an **upload** feature, not an enrollment
feature. The user does the one-time PC setup, then puts their credential on the SD card.

**Key delivery to the device (decided):** the user supplies **PEM key + PEM cert**
on the SD card, produced once from their existing TQSL `.p12`:

```
openssl pkcs12 -in CALL.p12 -nocerts -nodes | openssl rsa -out /CardSat/lotw_key.pem
openssl pkcs12 -in CALL.p12 -clcerts -nokeys -out /CardSat/lotw_cert.pem
```

Rationale: mbedTLS has **no full PKCS#12 parser** (only PBE primitives), but parses
PEM natively (proven). PEM also lets the user keep the key password-encrypted on the
card and unlock it on the device with `mbedtls_pk_parse_keyfile(..., password)`.

**Security posture:** the private key is the user's identity credential. The feature
must: load it only when the user invokes an upload, keep the decrypted key in RAM
only, never log it, never copy it off the card, and carry an explicit
"your LoTW private key will live on the SD card — use a card you control" banner
(same untested/at-your-own-risk treatment as the hardware-interface docs).

---

## 3. What already exists in CardSat (reuse, don't rebuild)

- `exportAdif()` / `adifField()` already emit **exactly** the LoTW-needed fields
  (`BAND`, `BAND_RX`, `FREQ`, `FREQ_RX`, `MODE`, `PROP_MODE`, `QSO_DATE`, `TIME_ON`,
  `SAT_NAME`) plus `bandFor()` for the band mapping. The `.tq8` builder reuses these.
- `PendingQso` / `logRecs[]` / `loadLog()` / `rewriteLog()` — the log store the
  uploader reads from and flags.
- `Net` already does TLS via `WiFiClientSecure` with the `freeCanvasForTls()` heap
  dance and a retry wrapper. Upload adds an HTTPS **POST** alongside the existing GETs.
- The existing **LoTW SAT_NAME prompt** flow (`promptNextLotw`, `FILE_LOTW`) already
  handles per-sat name mapping for export — the uploader hooks the same data.

---

## 4. New pieces required

### 4a. `Net::httpsPostMultipart()` (net.cpp / net.h)
LoTW's upload web service takes a `multipart/form-data` POST of the `.tq8` to the
documented self-authenticating endpoint (no login/session — the payload authenticates
itself). Mirror `httpsGetToFile`'s structure: same TLS client, same canvas-free guard,
read back the HTML/text result for the success/failure string. ~60–80 lines.

### 4b. `buildTq8()` (new lotw.cpp/.h, or a section in app.cpp)
Port `cardsat_lotw_reference.py` → C:
- emit the three header sections + one tCONTACT per selected QSO,
- for each QSO: build SIGNDATA (reuse `adifField`), `mbedtls_sha1` it,
  `mbedtls_pk_sign(MBEDTLS_MD_SHA1)`, base64 the 256-byte signature,
- gzip the whole text with miniz raw-deflate + the 10-byte gzip frame (proven).
Cert is read from `lotw_cert.pem`, converted PEM→DER→base64 once (mbedTLS
`mbedtls_pem`/`mbedtls_x509` give the DER directly). ~150–200 lines.

### 4c. Duplicate tracking (app.h / log store)
Add an `uploaded` flag to the per-QSO log record (extra CSV column, default 0). The
uploader signs only un-flagged QSOs and sets the flag on a confirmed-accepted POST.
LoTW penalizes re-uploads and puts this burden on the client. Small but mandatory.

### 4d. Station Location entry (Settings)
LoTW needs DXCC + grid + CQ/ITU zones. CardSat already has the grid (from GPS/
Location) and `myCall`. Add a small Settings group: **LoTW DXCC**, **CQ zone**,
**ITU zone** (state/county optional). Derive what we can; prompt for the rest once.

### 4e. UI: a Log-menu action
"Sign & upload to LoTW" on the Log menu → counts un-uploaded sat QSOs → unlock-
password entry (if the key is encrypted) → build `.tq8` → POST → show
accepted/rejected counts parsed from the response. One new screen, reusing the
text-editor for the password and the existing status-line patterns.

---

## 5. Risks & mitigations

- **mbedTLS version drift.** Host proved on 2.28; ESP-IDF ships 3.x. `mbedtls_pk_sign`
  signature is stable; `mbedtls_sha1` is renamed (`mbedtls_sha1` one-shot still
  present, deprecated). Trivial. Verify the produced signature on-device by
  round-tripping against the embedded public key before the first real upload.
- **Heap during TLS + sign + gzip.** Payloads are tiny (a few QSOs → ~1–2 KB), but
  all three happen inside the canvas-freed window. Build/sign/gzip the `.tq8` to an
  SD file *first*, then free the canvas and POST the file (mirrors `httpsGetToFile`).
- **Exact `tSTATION` field set.** TQSL is picky about which fields LoTW expects.
  Test against the live endpoint with one throwaway QSO before shipping; keep the
  station fields minimal (DXCC + grid + zones) as the satellite-QSO doc recommends.
- **Endpoint/URL changes.** ARRL has moved LoTW URLs before. Put the endpoint in
  config so it's updatable without a reflash.

---

## 6. Suggested build order

1. **Port `buildTq8()`** and host-verify its output with the same OpenSSL round-trip
   used here (run on the dev box from the firmware's actual byte emitter, e.g. a
   small `--lotw-selftest` host build, before any device flash).
2. **Add `httpsPostMultipart()`**, test against the live endpoint with one signed
   throwaway QSO from a real (consenting) cert; confirm "accepted" in LoTW Activity.
3. **Wire duplicate flag + Station Location settings + the Log-menu UI.**
4. **Docs:** new MANUAL.md §, cheat-card line, the security banner, the one-time
   `openssl pkcs12` extraction recipe.

---

## Files in this folder

- `cardsat_lotw_reference.py` — verified host reference for the build+sign+gzip
  pipeline. The C port mirrors this; keep them in sync.
- `sign_mbedtls.c` — the on-device signing path proven byte-identical to OpenSSL.
  Compile: `gcc -o sign_mbedtls sign_mbedtls.c -lmbedcrypto`.
