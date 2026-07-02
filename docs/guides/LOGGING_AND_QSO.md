# CardSat logging & QSO services — technical reference

A technical reference to CardSat's logging and QSO-record features: the on-device log, the two
upload services (CloudLog and LoTW), callsign lookup, voice memos, and plain-text notes.
Generated from `app.cpp`, `lotw.{h,cpp}`, `voicememo.{h,cpp}`, and `notes.{h,cpp}`.

This is the companion to `docs/guides/ORBITAL_VIEWS.md` (the orbital-analysis screens) and
`docs/guides/ANALYSIS_VIEWS.md` (the remaining analysis screens). For the per-module index see
`docs/guides/CODE_REFERENCE.md`; for the LoTW byte format specifically see
`docs/design/LOTW_TQ8_FORMAT.md` and `docs/proto/lotw/`.

Contents: [The log](#1-the-log-scr_log--scr_logentry--scr_loglist) ·
[CloudLog upload](#2-cloudlog-upload-scr_cloudlog) · [LoTW upload](#3-lotw-upload-scr_lotw--scr_lotwsub) ·
[Callsign lookup](#4-callsign-lookup-scr_qrz) · [Voice memos](#5-voice-memos-scr_memos) ·
[Notes](#6-notes-scr_notes--scr_noteedit)

---

## 1. The log (`SCR_LOG` / `SCR_LOGENTRY` / `SCR_LOGLIST`)

CardSat keeps a flat **CSV log** of satellite QSOs on the active filesystem (SD or LittleFS),
at `FILE_LOG`. `writeQsoCsv` / `parseQsoCsv` / the log/entry/list screens.

**The record** (`PendingQso`) carries everything a satellite QSO needs:

| Field | Meaning |
|---|---|
| `utc` | QSO time (epoch seconds) |
| `call` | worked station callsign |
| `sat` | satellite name |
| `mode` | mode (FM/USB/CW/…) |
| `dlHz` / `ulHz` | downlink (RX) / uplink (TX) frequency in Hz |
| `rstS` / `rstR` | RST sent / received |
| `myGrid` / `grid` | my grid / worked station's grid |
| `myCall` | my station callsign (for multi-call operation) |
| `notes` | free-text note |
| `uploaded` | bitfield — **bit0** = sent to LoTW, **bit1** = sent to CloudLog (absent ⇒ 0) |

**The file format** is a CSV with a header row written on creation:

```
utc,call,sat,mode,dl,ul,rsts,rstr,mygrid,grid,mycall,notes
```

New QSOs are **appended** (`open(FILE_LOG, "a")`); the file survives a firmware flash because it
lives on the SD card (or LittleFS, which a flash also preserves). The `uploaded` column is the
13th field — its absence is read as `0`, so logs written before upload tracking existed still
parse. Up to `LOG_VIEW_MAX = 60` recent entries are loaded into RAM (`logRecs[]`) for the
view/edit screens; the file itself is unbounded.

The three screens: **`SCR_LOG`** logs the QSO in progress (callsign, RST, grid entry tied to the
active satellite/transponder, auto-filling frequencies from the live Doppler-corrected values),
**`SCR_LOGLIST`** browses recent entries (newest-first), **`SCR_LOGENTRY`** views/edits one
record (including the 2-press delete). The same on-disk QSOs feed both upload paths below — the
`uploaded` bitfield is what keeps each QSO from being double-sent.

**Editing re-arms upload — with a manual override.** Editing any content field of a logged
QSO (call, RST, grid, frequency, date/time, mode, or satellite) clears that QSO's `uploaded`
bitfield, so a corrected record is re-sent to both LoTW and CloudLog on the next upload —
otherwise the services would keep the stale copy they already accepted. The clear happens the
moment the field is committed, and the edit screen shows it: when editing an existing QSO, two
extra rows — **LoTW** and **Cloudlog** — display the current upload state ("uploaded" / "not
uploaded") and **ENTER toggles each flag**. So the operator can either let an edit re-arm the
upload (the default), or, after fixing something cosmetic like a `notes` typo, mark the QSO back
as already-uploaded so it isn't re-sent. The rows always show exactly the state that will be
saved, and the flags can also be toggled directly without editing any other field. (The two
rows appear only when editing an existing QSO; a brand-new QSO starts un-uploaded with nothing
to override.)

---

## 2. CloudLog upload (`SCR_CLOUDLOG`)

Uploads logged QSOs to a **CloudLog** (or Wavelog) instance over its HTTP API.
`doCloudlogUpload()` (+ `cloudlogEnter` / `cloudlogRebootUpload` / `resumeCloudlogIfPending`);
reached from the Log menu.

**What it sends:** up to `CAP = 50` QSOs per upload, read from the log CSV, each emitted as an
**ADIF record** with the full satellite-QSO field set:

```
CALL, QSO_DATE, TIME_ON, MODE, SAT_NAME, PROP_MODE=SAT,
FREQ + BAND (uplink), FREQ_RX + BAND_RX (downlink),
RST_SENT, RST_RCVD, GRIDSQUARE, MY_GRIDSQUARE, STATION_CALLSIGN, <EOR>
```

`STATION_CALLSIGN` is included so CloudLog can match each QSO to the chosen **station profile**
(it rejects a mismatch). `SAT_NAME` is normalized via `lotwSatResolve`, bands via `bandFor()`.
By default only QSOs not yet sent to CloudLog go up (the `uploaded & 0x2` flag); `clResend`
re-sends everything.

**The request:** the ADIF string is wrapped in JSON
(`{"key":…,"station_profile_id":…,"type":"adif","string":…}`) and POSTed to
`<clUrl>/index.php/api/qso` via `net.httpsPostJson` (the API key is redacted from the serial
trace). The response is parsed for accepted/duplicate counts, and accepted QSOs get their CloudLog
bit set so they aren't re-sent.

**Two implementation details** (shared with the LoTW path's hard-won lessons):
- **Heap-fragmentation-aware body construction.** The JSON body is built in **one pre-sized
  buffer** with JSON-escaping done inline (`jsonEscapeAppend`), *not* via chained `String` +
  `jsonEscape()` temporaries — on the no-PSRAM heap that concatenation churn fragments the
  free-list right before the TLS handshake, which is what made the connect fail (`-1`) once the
  heap had seen activity. The ADIF buffer is reserved per-record (~64 B each) rather than as a
  flat 8 KB block, for the same reason.
- **Reboot-into-upload fallback.** A **negative** `net.lastCode` is a transport/connect failure
  (the `-1` a churned heap can cause), not a server rejection — so CloudLog offers a **reboot to
  a clean heap** and retries after boot (`cloudlogRebootUpload` / `resumeCloudlogIfPending`,
  behind a confirmation since a reboot drops a live pass/CAT link). A **positive** code
  (401/500…) is a real server response a reboot wouldn't fix, so it's just reported. LoTW has the
  same user-confirmed recovery prompt. Note that this is a *failure recovery* only — as of
  v0.9.43 the normal multi-batch path no longer reboots at all (uploads and the transponder
  cache run entirely in one session on the BearSSL TLS stack; see the network TLS migration
  postmortem). A reboot is offered solely when a connection genuinely fails.

---

## 3. LoTW upload (`SCR_LOTW` / `SCR_LOTWSUB`)

Builds a **cryptographically-signed `.tq8`** from logged QSOs and uploads it to ARRL's
**Logbook of the World**. `doLotwUpload()` + the `Lotw` class (`lotw.{h,cpp}`); reached from the
Log menu. **Requires a microSD card** — the credential and the staged `.tq8` live there.

**Credential model.** This is an **upload** feature, not enrollment: first-time certificate
issuance is gated by ARRL behind TQSL + a mailed postcard and cannot happen on-device. The user
exports their existing credential **once** to two PEM files on the card —
`/CardSat/lotw_key.pem` (the RSA private key) and `/CardSat/lotw_cert.pem` (the certificate) —
extracted from their `.p12`. (The repo ships a browser-based `.p12`→PEM converter to make this
step easy.) The key passphrase is entered at upload time and **never stored**.

**The `.tq8` build** (`Lotw::buildTq8`): each QSO is emitted as an ADIF record and **signed
individually**. The load-bearing subtlety is in the ADIF emission:
- The **outer record uses spaced fields** (`<NAME:len>val `).
- The **SIGNDATA and structural tags are tight** (no trailing space) — LoTW's signature won't
  verify with inter-field spaces. `Lotw::signData` builds the normalized SIGNDATA in exact
  developer-tq8 field order.
- Each QSO is signed with **`mbedtls_pk_sign(SHA1)`** (RSA-PKCS1v15-over-SHA1) using the
  SD-card key, and the whole `.tq8` is gzip-wrapped (CRC via ROM `miniz`'s `mz_crc32`, no
  allocation). The byte format, SIGNDATA order, and signature were validated host-side against
  ARRL's developer-tq8 spec and OpenSSL's verifier (`docs/proto/lotw/`); the signing path is
  proven byte-identical to OpenSSL.

**Station location** (`LotwStation`) carries the fields LoTW needs beyond per-QSO data: DXCC
entity, grid, CQ/ITU zones, and — depending on entity — US **state**/**county**, or a non-US
**primary** subdivision (province/oblast/prefecture/…) and **secondary** (Japanese
city/gun/ku), each as the LoTW enum **code** plus the LoTW field **name** it fills, plus optional
IOTA. These come from the `SCR_LOTWSUB` settings screens, which are driven by the
**`lotw_subdiv.h`** tables (DXCC → primary → secondary `dependsOn` chain, subdivision-bearing
entities surfaced first).

**The upload** (`doLotwUpload`): the staged `.tq8` is POSTed to LoTW via
`net.httpsPostMultipart(LOTW_UPLOAD_URL, "upfile", …)` as a file field. The same negative/positive
`lastCode` reboot-or-report logic as CloudLog applies (the key passphrase is re-prompted after a
reboot, never stored). The response is parsed for accepted/duplicate counts and accepted QSOs get
their LoTW bit set.

**The one heap subtlety** unique to LoTW: its gzip step originally needed a contiguous block that
didn't exist with the display sprite resident, so the upload path **frees the sprite explicitly
and locally** for the gzip, then restores it — the single place in the firmware that still does
so (everything else keeps the sprite resident). The current code uses **stored (uncompressed)
gzip framing** so the gzip needs no working memory and the TLS upload fits the same contiguous
block the other HTTPS fetches use — so in practice the screen now stays live throughout. (See
`docs/design/WEB_CONTROL_SCOPE.md` §3.2 for the full sprite-memory history.)

---

## 4. Callsign lookup (`SCR_QRZ`)

A **QRZ.com callsign lookup**: enter a callsign and fetch the operator's name/QTH/grid over
QRZ's XML data API, for filling in a QSO or just identifying who you're hearing. `drawQrz()` /
`keyQrz()`; reached from the logging flow (and standalone). Requires WiFi and QRZ credentials in
settings; the lookup opens a session against QRZ's XML endpoint and parses the returned fields.
The result can seed the log entry's grid/name. (No WiFi or no credential ⇒ it says so and does
nothing.)

---

## 5. Voice memos (`SCR_MEMOS`)

A **voice-memo recorder** using the Cardputer ADV's built-in **PDM microphone** (via
M5Unified's `M5.Mic`), streaming to a **16-bit mono WAV** on the SD card. `voicememo.{h,cpp}` +
the memos browser; reached from the Log menu, and recordable in-pass with `v`.

- **SD-card required** — memos are written under `AUDIO_DIR` (`/CardSat/audio`); there is no
  internal-flash fallback (audio is too large for LittleFS on this part).
- **Format:** 16-bit mono WAV. The WAV header is **patched on finalize** (`stop()` rewrites the
  size fields, closes the file, and restores the speaker) — so a memo interrupted by a power cut
  still leaves a mostly-valid file.
- **Recording** auto-stops at **`MEMO_MAX_SECS = 30`** or when `stop()` is called (a second
  `v`). The intent is a quick spoken note during a busy pass ("worked W1ABC, missed his grid")
  rather than a long recording.
- **Retrieval** is by reading the SD card on a computer; the on-device **memos browser**
  (`SCR_MEMOS`) lists files newest-first (parsed from the WAV filename + size), plays them back,
  and supports delete. A memo can be recorded standalone (`n` in the browser) or attached to the
  moment during a pass.

**Porting note:** this is the one **Tier D (board-specific)** logging feature — it's tied to the
ADV's PDM mic through M5Unified, so any port to other hardware rewrites the capture path (or
drops the feature). The browser/file-management half is portable.

---

## 6. Notes (`SCR_NOTES` / `SCR_NOTEEDIT`)

Plain-text **notes** stored as `.txt` files under `/CardSat/notes/` on the active filesystem.
`notes.{h,cpp}` (file I/O) + the browser/editor in `app.cpp`. Unlike voice memos, notes work
**with no SD card** (they fall back to LittleFS).

- **`Notes` API:** `list()` (base names, newest-first by mtime), `read`/`write`/`remove`/`exists`
  (per-note, base name only — no path, no `.txt`), `sanitizeName()` (keep `[A-Za-z0-9 _-]`,
  collapse the rest to `_`, trim, length-cap — so a user-typed name is always a safe filename).
- **The browser** (`SCR_NOTES`) lists notes newest-first with their save date/time; the
  **editor** (`SCR_NOTEEDIT`) is a simple text editor. As with all CardSat text editors, the
  editor deliberately treats **DEL as backspace** (so DEL can delete a character) and only
  `` ` `` as back/exit — with an unsaved-changes prompt on exit.

**Porting note:** Tier B — pure filesystem, already routed through `Store::fs()`, so on most
ports it follows the `storage` module for free.

---

## Where these live in the code

| Feature | Screen(s) | Key functions | Files |
|---|---|---|---|
| Log | `SCR_LOG` / `SCR_LOGLIST` / `SCR_LOGENTRY` | `writeQsoCsv`, `parseQsoCsv` | `app.cpp` |
| CloudLog | `SCR_CLOUDLOG` | `doCloudlogUpload`, `resumeCloudlogIfPending` | `app.cpp` |
| LoTW | `SCR_LOTW` / `SCR_LOTWSUB` | `doLotwUpload`, `Lotw::buildTq8`/`signData` | `app.cpp`, `lotw.{h,cpp}`, `lotw_subdiv.h` |
| Callsign lookup | `SCR_QRZ` | `drawQrz`, QRZ XML session | `app.cpp` |
| Voice memos | `SCR_MEMOS` | `VoiceMemo` (record/finalize/list) | `voicememo.{h,cpp}` |
| Notes | `SCR_NOTES` / `SCR_NOTEEDIT` | `Notes::list/read/write/remove` | `notes.{h,cpp}` |

The common thread: the log CSV is the source of truth, the `uploaded` bitfield prevents
double-sending to either service, and both upload paths share the same no-PSRAM heap discipline
(single pre-sized buffers, reboot-on-transport-failure) developed in the CloudLog/LoTW work — see
the postmortems in `docs/design/CLOUDLOG_UPLOAD_POSTMORTEM.md`.
