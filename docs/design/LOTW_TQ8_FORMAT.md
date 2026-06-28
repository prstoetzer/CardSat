# LoTW `.tq8` Digitally-Signed Log Format (as implemented by CardSat)

This document is the authoritative reference for the `.tq8` file CardSat produces for
direct upload to ARRL's Logbook of the World (LoTW). It was reverse-engineered from the
**TrustedQSL (tqsl) 2.8.6 source** — the program LoTW itself distributes — because ARRL's
public *developer-tq8* help page is **out of date** (it documents only the legacy
`SIGN_LOTW_1.0` scheme and is wrong about exactly what bytes are signed).

Getting this wrong is silent: LoTW will *accept and queue* a structurally-valid file, then
**drop the QSO during processing** without an error if the signature doesn't match what
LoTW re-derives. CardSat ≤ 0.9.35 produced exactly such a file.

Source of truth: `tqsl-2.8.6/src/location.cpp` (`make_sign_data`,
`tqsl_getGABBItCONTACTData`), `tqsl-2.8.6/src/adif.cpp` (`tqsl_adifMakeField`), and
`tqsl-2.8.6/src/config.xml` (`<sigspecs>`).

---

## 1. File container

A `.tq8` is a **gzip-compressed** text file. CardSat writes a valid gzip using *stored*
(uncompressed) DEFLATE blocks so it needs no compressor memory on the no-PSRAM ESP32-S3;
LoTW decompresses it identically. The decompressed text is four sections in an ADIF-like
syntax.

ADIF field syntax used throughout: `<NAME:LEN>VALUE`, where `LEN` is the byte length of
`VALUE`. An optional **type** annotation is `<NAME:LEN:TYPE>VALUE` (see the signature
field below). Records end with `<eor>`. CardSat ends each line with `\n`.

---

## 2. Section 1 — TQSL_IDENT

A single line identifying the producing software:

```
<TQSL_IDENT:n>TQSL CardSat <ver> Lib(CardSat) Config()
```

Informational; LoTW does not gate on it.

---

## 3. Section 2 — tCERT (the callsign certificate)

```
<Rec_Type:5>tCERT
<CERT_UID:n>1
<CERTIFICATE:n>BASE64_DER_X509
<eor>
```

- `CERT_UID` is an integer that the tSTATION and (indirectly) tCONTACT records reference.
  CardSat uses `1` (a single cert per file).
- `CERTIFICATE` is the user's LoTW callsign certificate as **base64-encoded DER X.509**.

---

## 4. Section 3 — tSTATION (the station location)

```
<Rec_Type:8>tSTATION
<STATION_UID:n>1
<CERT_UID:n>1
<CALL:n>...        (human-readable station fields)
<DXCC:n>...
<GRIDSQUARE:n>...
<US_STATE:n>...    (US: 2-letter, e.g. VA)
<US_COUNTY:n>...   (US: county NAME ALONE, e.g. Arlington -- NOT "ST,County")
<CQZ:n>...
<ITUZ:n>...
<eor>
```

- `STATION_UID` (CardSat uses `1`) is referenced by every tCONTACT.
- `CERT_UID` links this station to the certificate above.
- **Field names matter.** The station record uses TrustedQSL's *internal* field names, which
  for the US state and county are **`US_STATE`** and **`US_COUNTY`** — NOT the ADIF names
  `STATE`/`CNTY`. If you emit a bare `CNTY`, LoTW doesn't recognize it, applies a tiny
  default length limit, reports "ADIF field data length overflow", and rejects the entire
  tSTATION — which then orphans every tCONTACT (`STATION_UID doesn't match any tSTATION`).
- **`US_COUNTY`'s value is the county name only** (e.g. `Arlington`), because TQSL keeps the
  state and county in separate location fields. The state is carried in `US_STATE`. Sending
  the combined `VA,Arlington` is rejected as "US_COUNTY: Invalid value in field". CardSat
  stores the county as `ST,County` (the ADIF `MY_CNTY` convention, entered that way in
  Settings) and strips everything up to and including the comma when building the record.
- These human-readable fields are **separate** from the bytes that get signed (Section 6).
  They are what LoTW displays/uses for the location; the *signature* is computed over a
  different normalized string.

---

## 5. Section 4 — tCONTACT (one per QSO)

```
<Rec_Type:8>tCONTACT
<STATION_UID:n>1
<CALL:n>...        (worked station)
<BAND:n>...
<MODE:n>...
[<FREQ:n>...]      (optional fields emitted only when present)
[<FREQ_RX:n>...]
[<PROP_MODE:n>...] (SAT for satellite QSOs)
[<SAT_NAME:n>...]  (required when PROP_MODE=SAT; format cc-nn, e.g. FO-29)
[<BAND_RX:n>...]
<QSO_DATE:n>...    (TEXT date "YYYY-MM-DD", UTC -- e.g. 2026-06-28)
<QSO_TIME:n>...    (TEXT time "HH:MM:SSZ", UTC -- e.g. 01:18:00Z)
<SIGN_LOTW_V2.0:LEN:6>BASE64_SIGNATURE
<SIGNDATA:n>NORMALIZED_SIGNED_STRING
<eor>
```

- `STATION_UID` ties the QSO to the station location (and thus the certificate). **Without
  it LoTW cannot resolve the station location and drops the QSO.**
- **Date/time are TEXT, not compact ADIF.** `QSO_DATE` is `YYYY-MM-DD` (with dashes) and
  `QSO_TIME` is `HH:MM:SSZ` (colons, trailing `Z`), matching `tqsl_convertDateToText` /
  `tqsl_convertTimeToText`. The compact ADIF forms `20260628` / `011800` are rejected as
  "Invalid Date/Time in tCONTACT record". The time field is named **`QSO_TIME`**, not the
  ADIF `TIME_ON`. The same text-formatted strings appear in the signed data (Section 6),
  because tqsl signs the converted-to-text values.
- The signature field name is literally `SIGN_LOTW_V2.0` (built by tqsl as
  `"SIGN_" + sigspec_name + "_V" + sigspec_version`). The **`:6`** is the ADIF type
  annotation emitted by `tqsl_adifMakeField(name, '6', ...)` — its presence is required;
  the format is `<SIGN_LOTW_V2.0:LEN:6>` where `LEN` is the base64 signature's length.
- `SIGNDATA` stores the exact normalized string that was signed (Section 6). For LoTW V2.0
  this is the **values-only, uppercased** station+contact string — **not** a tagged,
  mixed-case copy of the QSO fields.

---

## 6. The signed data — THE critical part

LoTW re-derives this string on its end and verifies the signature against it. It must match
**byte for byte**, or the QSO is silently dropped.

Build one string by concatenating field **values only (no `<adif:tags>`)** in this exact
order, then **uppercase the whole thing**, then SHA-1 hash and RSA-sign it:

### 6.1 Station portion first (LOTW V2.0 `tSTATION` sigspec order)

Only non-empty fields, in this order (from `config.xml`):

```
AU_STATE, CA_PROVINCE, CA_US_PARK, CN_PROVINCE, CQZ(int), DX_US_PARK,
FI_KUNTA, GRIDSQUARE, IOTA, ITUZ(int), JA_CITY_GUN_KU, JA_PREFECTURE,
RU_OBLAST, US_COUNTY, US_PARK, US_STATE
```

For a typical US station that reduces to: **CQZ, GRIDSQUARE, ITUZ, US_COUNTY, US_STATE**.
`US_COUNTY`'s signed value is the **county name alone** (e.g. `ARLINGTON` after
uppercasing) — *not* the combined `ST,County`. This must match the county-name-only value
emitted in the tSTATION record. **CALL and DXCC are NOT part of the signed data.**

### 6.2 Contact portion appended (LOTW V2.0 `tCONTACT` sigspec order — alphabetical)

```
BAND, BAND_RX, CALL, FREQ, FREQ_RX, MODE, PROP_MODE, QSO_DATE, QSO_TIME, SAT_NAME
```

Only non-empty fields; required = BAND, CALL, MODE, QSO_DATE, QSO_TIME. **The worked
station's CALL IS included here.** Note this order differs from the on-disk tCONTACT field
order — the *signed* order is fixed by the sigspec, independent of emission order.

### 6.3 Uppercase

The entire concatenation is upper-cased (`string_toupper`). e.g. `fm18lu` → `FM18LU`,
`FO-29` stays `FO-29`, frequencies unchanged.

### 6.4 Worked example

Station: CALL N8HM, GRID FM18LU, US_STATE VA, US_COUNTY Arlington, CQZ 5, ITUZ 8.
QSO: worked N9EAT/VE3, 2M/70CM, FREQ 145.9500 / FREQ_RX 435.8500, SSB, SAT, FO-29,
2026-06-28 01:18:00Z.

Signed string (before SHA-1) — this is the exact string that produced a QSO LoTW accepted
and posted:

```
5FM18LU8ARLINGTONVA2M70CMN9EAT/VE3145.9500435.8500SSBSAT2026-06-2801:18:00ZFO-29
```

Note the county is `ARLINGTON` (name only, not `VA,ARLINGTON`) and the date/time are the
text forms `2026-06-28` and `01:18:00Z` (not `20260628` / `011800`).

Then `SHA-1` → `RSA PKCS#1 v1.5 sign` → base64 → goes in `SIGN_LOTW_V2.0`, and the string
above goes in `SIGNDATA`.

### 6.5 Verifying (matches ARRL's recipe, applied to V2.0 bytes)

```
openssl x509 -in cert.der -inform DER -outform PEM -out cert.pem
openssl x509 -in cert.pem -noout -pubkey > pubkey.pem
# write the base64 SIGN_LOTW_V2.0 value to sig.b64, base64 -d > signature
# write the SIGNDATA value (no trailing newline) to signdata
openssl dgst -sha1 -verify pubkey.pem -signature signature signdata
```

---

## 7. Upload transport (no authentication)

Verified against `tqsl-2.8.6/apps/tqsl.cpp` (`UploadFile`):

- **Endpoint:** `https://lotw.arrl.org/lotw/upload` (`DEFAULT_UPL_URL`).
- **Method:** HTTPS `multipart/form-data` POST, one part, field name **`upfile`**
  (`DEFAULT_UPL_FIELD`), `Content-Type: application/octet-stream`, filename ending `.tq8`.
- **No login, password, cookie, or session.** The `.tq8` is self-authenticating via its
  embedded signature. (The only place tqsl sends `login=&password=` is the *download/query*
  API — `lotwreport.adi` — which is unrelated to uploading.)
- **Success detection:** the HTML response contains a comment marker
  `<!-- UPL_<status> -->` (regex `<!-- .UPL.\s*([^-]+)\s*-->`); the captured status must
  contain **`accepted`** (`DEFAULT_UPL_STATUSOK`). A human-readable message may appear in
  `<!-- UPLMESSAGE_<text> -->`. **Matching the bare word "accepted" anywhere in the page is
  wrong** — that yields false positives from login/landing pages. Look for the comment
  marker.

---

## 8. CardSat ≤ 0.9.35 vs this spec (what was fixed in 0.9.36)

| Aspect | CardSat ≤ 0.9.35 (wrong) | Correct (this spec) |
|---|---|---|
| Signed bytes | QSO fields only | Station values + contact values |
| ADIF tags in signed data | Yes (`<BAND:2>2M…`) | No (values only) |
| Case | Mixed | **Uppercased** |
| Worked CALL in signed data | Omitted | Included |
| Field order (signed) | Emission order | Fixed sigspec order |
| Signature field | `<SIGN_LOTW_1.0:n>` | `<SIGN_LOTW_V2.0:n:6>` |
| CERT_UID / STATION_UID | Absent | Present, linked |
| Response success check | "accepted" anywhere | `<!-- UPL_… -->` marker |

The upload URL (`/lotw/upload`), POST field (`upfile`), no-auth model, and gzip container
were already correct in CardSat ≤ 0.9.35.
