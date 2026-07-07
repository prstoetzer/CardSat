# Scoping — Web file transfer, on-device rove-plan viewer, and LoRa object sharing

Status: **scoping only — nothing implemented.** Target: a 0.9.52-ish cycle.
Author aid: Claude (host-side). All device claims below are grounded in the current
0.9.51 source (paths cited inline); anything marked *assumption* needs a bench check.

This covers three requested features:

1. A **file-transfer page** off web control (upload/download without pulling the SD or
   booting a Launcher).
2. **On-device listing + viewing of rove plans.**
3. **LoRa transfer** of notes, rove plans, and manual satellite elements — with manual
   elements *imported into the receiver's GP data*, while notes and rove plans arrive as
   *viewable message attachments*, deliberately **not** written into the receiver's own
   Notes / RovePlans folders.

The three are independent and can ship in any order. Recommended order is **2 → 1 → 3**
(each reuses machinery the previous one adds; #3 is the largest and riskiest).

---

## Shared constraints (read first)

These recur across all three features and drive most of the design decisions.

- **No PSRAM, fragmented heap.** The largest contiguous block hovers in the tens of KB.
  Any feature that wants a multi-KB contiguous buffer competes with the drawing sprite and
  the mbedTLS/BearSSL handshake buffers. **No feature here may allocate a large transient
  arena** (this is the same discipline that killed the PikaScript idea). File and radio
  transfers must **stream** in small fixed buffers.
- **Tiny LittleFS.** The build uses the Arduino *Huge APP* layout (~3 MB app, ~1.5 MB
  filesystem region), shared by GP/TLE cache, transponder cache, notes, logs, favorites,
  rove plans, and config. Prior sessions already hit truncation when temp files competed
  with GP data. **Any inbound file must be size-capped and free-space-checked before
  writing.** `Store::fs()` may be LittleFS *or* an SD card (`Store::onSD()`); on an
  SD-equipped unit the SD is large and this is a non-issue, but the internal-flash case is
  the binding constraint.
- **Filesystem is `Store::fs()`, never raw `LittleFS`/`SD`.** (storage.h: `Store::ready()`,
  `onSD()`, `fs()`.) Everything new uses `Store::fs()` so it works on both back-ends.
- **The Cardputer is single-core-cooperative for our purposes.** The web server, LoRa
  poll, Doppler service, and UI all run from one `loop()`. Nothing may block for more than
  a few ms. Transfers must be **chunked across loop iterations**, exactly like the existing
  rove-planner survey job and the sat-to-sat search job.
- **LoRa is marked UNTESTED in the firmware.** Feature #3 rides on a radio path that has
  never been validated on hardware end-to-end. Its spec must carry the same prominent
  UNTESTED banner the LoRa messaging protocol already does.

---

## Feature 1 — Web file transfer page

### What exists today (grounded)

The web server (`serviceWebd`, `webdHandleRequest`, app.cpp ~3422–3570) is a deliberately
tiny cooperative HTTP/1.1 server: one client at a time, non-blocking, rebuilt each loop
tick, suspended during outbound TLS fetches. The served page is a single self-contained
PROGMEM document streamed in 128-byte chunks (`webdSendPage`). Routing is a chain of
`path.startsWith(...)` tests; every current POST carries its data in the **URL query
string** (`/api/cal?dl=N`, `/api/select?norad=N`, `/api/cmd?k=x`).

**The binding limitation:** the request reader (app.cpp ~3440–3456) consumes only the
request line and headers, then stops at the blank line — **it never reads a request
body**, and it caps accumulated line length at 200 bytes. So *no upload path exists at
all* today, and the read loop discards anything after the headers.

### Proposed design

Add a **Files** tab to the existing web UI (a third mode alongside the current panels, or a
separate `/files` route — a separate route is cleaner and keeps the main status-poll page
lean). New JSON/HTTP endpoints, all under the same one-client server:

- `GET  /api/files?dir=<path>` — list a directory: names, sizes, is-dir, mtime. JSON.
  Restricted to a **whitelist root** (see safety) — e.g. `/CardSat`.
- `GET  /api/file?path=<p>` — download one file. Stream it from `Store::fs()` in a fixed
  buffer (reuse the 128-byte chunk pattern from `webdSendPage`) with
  `Content-Disposition: attachment` and `Content-Length`. No whole-file RAM copy.
- `POST /api/upload?path=<p>` — **the hard part**: receive a file body and stream it to
  disk. Requires teaching the server to read a request body.
- `POST /api/delete?path=<p>` — remove a file (guarded/confirmed in the UI).
- `POST /api/mkdir?path=<p>` — optional; create a subdir.

**Upload body handling (the real work).** Two viable shapes:

- **(A) Raw-body PUT/POST.** The browser sends the file as the raw request body with an
  explicit `Content-Length`; the server reads exactly that many bytes and streams them to a
  temp file, then renames. The client JS uses `fetch(url,{method:'POST',body:file})`.
  *Pros:* trivial to parse (no multipart boundary scanning), minimal RAM. *Cons:* one file
  per request; must read `Content-Length` from headers (server currently ignores headers).
- **(B) multipart/form-data.** Standard HTML form upload. *Cons:* boundary scanning in a
  streaming, small-buffer, cooperative reader is fiddly and error-prone; more code.
  **Recommend (A).**

The read loop must change from "drain headers, ignore rest" to: parse `Content-Length`
during the header scan; after the blank line, switch to a **body-streaming state** that
reads up to N bytes across as many `loop()` ticks as needed, appending to a `File` opened
on `Store::fs()`. This introduces the server's first *multi-tick request* — today every
request completes in one `serviceWebd` call. That's a real state-machine change
(`webdBodyRemaining`, `webdBodyFile`, a `WEBD_BODY` phase) but it's contained.

**Safety (must-haves):**

- **Path whitelist + traversal guard.** Confine all paths under `/CardSat`. Reject any path
  containing `..`, a leading non-`/CardSat` prefix, NUL, or backslashes. This is
  non-negotiable — the web server is unauthenticated on the LAN.
- **Free-space + size cap before writing** (internal-flash case). Reject uploads that would
  overflow LittleFS; surface a clear error. Consider a hard per-file cap (e.g. 256 KB) on
  internal flash, larger/none on SD.
- **Don't clobber live-critical files carelessly.** Uploading over `gp.ndjson`, `cfg`, or
  `favs` while they're in use could corrupt in-memory state. Options: (a) forbid writing the
  known system files; (b) allow but require a reboot note; (c) only allow writes under
  `/CardSat/RovePlans`, `/CardSat/notes*`, `/CardSat/Screenshots`, `/CardSat/RovePlans`,
  and a general `/CardSat/uploads`. **Recommend a curated writable set** for v1, widen later.
- **One transfer at a time.** The server is single-client already; keep it that way. A large
  upload will monopolize the connection — that's acceptable, but the status page's poll will
  stall meanwhile (the browser can open a second connection, but the server serves one).
  *Assumption to verify:* browser behavior when the poll page and an upload compete.
- **TLS-fetch coexistence.** The server is torn down during outbound fetches
  (`suspendNetServers`). An in-flight upload during a GP auto-refresh would be dropped.
  Acceptable (rare), but the UI should fail gracefully and allow retry.

### Effort / risk

Medium. The listing/download endpoints are easy (mirrors existing streaming). The **upload
body reader is the only genuinely new server capability** and carries the most risk
(partial writes, connection resets mid-stream, flash exhaustion). Recommend building
download + listing first (immediately useful: pull screenshots, rove plans, logs off the
device), then upload.

### Out of scope (v1)

Auth/passwords (the LAN page is already unauthenticated by design), rename, chunked/resumable
uploads, directory zip download, progress bars beyond the browser's native upload progress.

---

## Feature 2 — On-device rove-plan listing + viewing

### What exists today (grounded)

`exportRovePlan()` (app.cpp ~20550) writes formatted `.txt` files to
`/CardSat/RovePlans/rove_YYYYMMDD_HHMM.txt` on `Store::fs()`. There is **no way to see them
on-device** — you must pull the card or (with feature #1) download them.

Two clean browser precedents exist to model on:

- **Voice-memo browser** (`buildMemoList`/`drawMemos`/`keyMemos`, app.cpp ~4842+): enumerate
  files newest-first into a fixed `.bss` array, scrollable list, per-row metadata, delete
  confirm.
- **Notes browser + editor** (`buildNoteList`/`drawNotes`, and the `SCR_NOTEEDIT` viewer with
  scrolling): the closest model, since a rove plan is a multi-line text file that needs a
  **scrollable text viewer**, which the notes editor already implements (line-based scroll,
  `noteTopLine`).

### Proposed design

- **New screen `SCR_ROVELIST`** (there's room in the SCR enum, app.h line 26). Reached from
  the Rove planner (a key on `SCR_PLANNER`, e.g. `l` "load/list", complementing the existing
  `w` "save"), and/or from the Tools hub.
- `buildRovePlanList()` — enumerate `/CardSat/RovePlans/*.txt` into a fixed array
  (`ROVEPLAN_LIST_MAX`, ~32) of base names + mtime + size, newest first. Reuse the
  enumeration idiom from `Notes::list` / `VoiceMemo::listMemos`.
- `drawRoveList()` / `keyRoveList()` — scrollable list; each row shows date/time (parsed from
  the filename or mtime) and size. `ENTER` opens the viewer; `d` deletes (with the two-step
  confirm the memo browser uses); `` ` `` back.
- **New screen `SCR_ROVEVIEW`** — a **read-only** scrolling text viewer. Rather than load the
  whole file into RAM (a big plan could be several KB — against heap discipline), stream it:
  keep a small window of lines and a byte offset, seek/re-read on scroll. *Simpler
  alternative:* cap the viewer to the first N KB loaded into a `String` (bounded), with a
  "truncated — download for full" footer when over the cap. **Recommend the bounded-String
  viewer for v1** (far less code; plans are usually small; the cap protects the heap). Revisit
  streaming only if real plans routinely exceed the cap.

### Effort / risk

**Low.** This is the safest of the three — pure local filesystem + UI, no network, no radio,
patterns already in the codebase twice. The only judgment call is viewer memory strategy
(bounded String vs. streaming); bounded String is fine to start.

### Out of scope (v1)

Editing plans on-device (they're generated artifacts), re-running a saved plan's survey from
the file (the plan is a text report, not a serialized query — could be a later "re-plan from
this file's header" nicety), search/filter within a plan.

---

## Feature 3 — LoRa transfer of notes, rove plans, and manual satellite elements

This is the largest and highest-risk item. It rides on the **UNTESTED** LoRa path and
requires a **multi-frame chunked sub-protocol**, because the current LoRa messaging is
single-frame and tiny.

### What exists today (grounded)

- **Frame format** (app.cpp ~15489–15575, 15323–15324): `[0]=MAGIC 0xC5`, `[1]=VER 0x01`,
  `[2..15]=sender callsign (14 bytes, space-padded)`, `[16..]=text (≤ `LORA_TEXT_MAX`=48
  chars)`. Single frame. RX drains into a `uint8_t buf[64]` in `loraPoll` (app.cpp ~15509).
- **Human-readable sigils** decoded by `decodeMsg` (~15438): `@lat,lon` (position),
  `#SAT[/NORAD]` (satellite ref), `!SAT date time` (sked). Satellite refs already carry an
  **invariant NORAD** so two stations that name a bird differently still resolve it — this is
  exactly the property manual-element import needs.
- **Message ring** (`msgRing[MSG_MAX=24]`, app.h ~419): fixed `.bss`, each entry
  from/text/mine/rssi/snr/tMs. Text is capped at 48 chars.
- **GP import** already exists: `db.addGp(SatEntry&)` validates (norad≠0, meanMotion>0) and
  persists as NDJSON; the state-vector fitter already saves fitted elements this way.

**Binding limitation:** 48 text chars per frame. A single set of GP elements is ~7 numbers
plus name+NORAD (comfortably 60–120 chars as text); a note is up to `NOTE_MAX`=120 chars; a
rove plan is multi-KB. **None fit in one frame.** So #3 needs a real chunked-transfer
sub-protocol over LoRa, with sequencing, a payload-type tag, and reassembly — over a
half-duplex, lossy, *untested* radio with no ARQ today.

### Proposed sub-protocol (new frame type, versioned)

Introduce a **second frame type** distinguished by a new magic/type byte so it never
collides with, or is mis-parsed as, a text message. Keep the existing text protocol
untouched.

Proposed object frame layout (all fixed-size fields, small):

```
[0]  MAGIC2  (e.g. 0xC6)          # distinct from 0xC5 text frames
[1]  VER     (0x01)
[2]  OBJTYPE (1=GP elements, 2=note, 3=rove plan)
[3]  XFERID  (random per-object id, ties chunks together)
[4]  SEQ     (chunk index, 0-based)
[5]  COUNT   (total chunks)
[6..]  PAYLOAD (raw bytes of this chunk; chunk size chosen so a frame stays small)
```

- **Sender identity:** either keep a shortened callsign field, or (simpler) require the
  sender to have broadcast an `@position`/text first so the receiver already has them in the
  roster; **recommend embedding a short callsign** so provenance is self-contained.
- **Reassembly:** a *single* in-progress inbound object buffer (one at a time — no PSRAM to
  hold several). Track `XFERID`, a bitmap of received `SEQ`, and a fixed reassembly buffer
  sized to the **maximum object we accept** (see caps). Drop/replace on a new `XFERID`.
  Time out a stalled transfer.
- **No ARQ in v1.** LoRa has no built-in reliability and we have no back-channel protocol
  yet. A missing chunk = failed transfer; show "incomplete (n/m chunks) — ask sender to
  resend". A *manual* resend (sender re-broadcasts the whole object) is the v1 recovery.
  Real selective-repeat ARQ is a big future item and explicitly **out of scope** for v1.
- **Caps (heap + airtime):** GP element set — 1–2 chunks, trivial. Note — `NOTE_MAX`=120
  chars → 1–3 chunks. Rove plan — **cap hard** (e.g. accept only the first ~2–4 KB, or a
  bounded chunk count like 32); a long multi-favorite plan may exceed this and should be
  refused with a clear message rather than exhausting RAM/airtime. At SF12/125 kHz, airtime
  per frame is on the order of ~1 s, so a 32-chunk plan is ~30 s on air — **slow, and a
  reason to keep rove-plan sharing modest or SF-limited.** *Assumption:* exact airtime and
  reliability must be measured on hardware.

### Receive-side behavior (per the requested semantics)

- **OBJTYPE 1 = GP elements → import into GP data.** On a complete, CRC-valid transfer,
  parse into a `SatEntry` and call `db.addGp()` (same validation/persistence the fitter uses;
  NORAD is invariant so it updates in place if already present). Surface a **confirm prompt**
  before writing (don't let a stranger silently inject a satellite): "Import SAT-NAME (NORAD
  n) from CALL? y/n". This is the one type that *does* land in the user's personal data — by
  request — but gated behind explicit acceptance.
- **OBJTYPE 2 = note, 3 = rove plan → viewable in the message area, NOT written to personal
  folders.** Per the request, these are attachments to the conversation, not files. Design:
  add a lightweight **attachment store** parallel to `msgRing` — a small fixed set of
  received-object slots (e.g. `ATTACH_MAX`=4) holding {type, from, title, bounded text
  body}. A message-ring entry references its attachment. In `drawMessages`, an attachment row
  shows e.g. `[note from CALL] title` / `[rove plan from CALL] N passes`; `ENTER` opens a
  **read-only viewer** (reuse the Feature-2 `SCR_ROVEVIEW` bounded viewer for both notes and
  plans). Crucially, there is **no "save to my notes/plans" action** in v1 — this keeps the
  boundary the request asked for (a received note never pollutes the receiver's own Notes).
  If a "save to my folder" action is wanted later it can be added as an explicit, separate,
  opt-in command.

### Send side

- From the **Notes** browser: a key (e.g. `s` "share") on a selected note → chunk + broadcast
  as OBJTYPE 2.
- From the **Rove-plan** list (Feature 2): a key → chunk + broadcast as OBJTYPE 3 (subject to
  the size cap; refuse oversized plans with a clear message).
- From the **Satellites** list / manual-sat detail, or the GP-fit result screen: a key →
  serialize the `SatEntry` and broadcast as OBJTYPE 1. (The fitter already produces exactly
  such a `SatEntry`; sharing a just-fitted pre-launch element set to a rove group is a
  genuinely nice workflow.)
- Sending must be **jobbed across loop ticks** (one frame per tick with a small inter-frame
  gap), never a blocking `for` loop of `sendRaw` — the radio is half-duplex and the loop must
  keep running. Model on the existing planner/sat-to-sat job pattern.

### Integrity

Add a **CRC** (e.g. CRC16) over each object (or per chunk) so a corrupted reassembly is
rejected rather than imported. LoRa has per-packet CRC at the PHY, but an object spanning
chunks needs its own end-to-end check. GP import especially must not accept a corrupted
element set (a garbled mean-motion could produce a nonsense orbit that then drives the rig).

### Effort / risk

**High.** New frame type, chunking, reassembly state machine, per-object buffer sizing under
heap discipline, jobbed TX, CRC, three send entry points, receive-side attachment store +
viewer + GP-import confirm — all over an **UNTESTED** radio. Strongly recommend:

1. Land Features 2 and 1 first (they give the viewer and the off-device transfer paths).
2. Prototype #3 **GP-elements-only** first (smallest object, highest value, 1–2 chunks,
   reuses `addGp`), on the bench with two units, before attempting notes/plans.
3. Treat the whole feature as **UNTESTED** in docs until Paul validates on hardware.

### Out of scope (v1)

ARQ / selective-repeat / automatic resend; encryption or authentication of objects (amateur
radio is unencrypted by regulation anyway — but that means **anyone can inject**, reinforcing
the import-confirm gate); transferring logs, config, or transponder DBs; simultaneous
multi-object reassembly; compression.

---

## Cross-cutting risks & open questions

- **LittleFS exhaustion** (Features 1 & 3-if-ever-persisted): every inbound write must
  free-space-check. Since #3 notes/plans are deliberately *not* persisted to folders, they
  don't hit the filesystem — good, that sidesteps the exhaustion risk for #3 entirely (they
  live in the bounded attachment RAM store). Only Feature 1 uploads touch flash.
- **Unauthenticated LAN web server**: Feature 1 exposes the filesystem to anyone on the LAN.
  Path-whitelist + writable-set are the guardrails. If the threat model ever includes hostile
  LAN peers, an auth token would be needed (out of scope now, but note it).
- **LoRa airtime & duty cycle** (Feature 3): multi-frame objects at high SF are slow and
  consume duty cycle (region-dependent — EU has legal duty-cycle limits). The size caps and
  "GP-first" recommendation partly mitigate. Must measure on hardware.
- **Heap during upload/reassembly**: both Feature 1's upload `File` handle and Feature 3's
  reassembly buffer coexist with the drawing sprite and (for #1) the WiFi stack. Size the
  reassembly buffer to the *smallest* cap that still fits real objects; stream #1 uploads so
  they need only a small I/O buffer.
- **SCR enum / dispatch discipline**: each new screen (`SCR_ROVELIST`, `SCR_ROVEVIEW`,
  `SCR_ATTACHVIEW` if separate) must be added to the enum, the `handleKey` dispatch, and any
  `editHome`/return-path logic, in **both** `src/app.cpp` and `CardSat.ino`, byte-identical
  (the standing dual-apply invariant). Home menu stays locked at 20 — these hang off existing
  screens (Planner, Tools, Messages, Notes), not Home.

## Suggested sequencing

1. **Feature 2** (rove-plan list + viewer) — low risk, immediately useful, and delivers the
   read-only text viewer that Feature 3 reuses.
2. **Feature 1 download + listing** — low risk, high utility (pull plans/screenshots/logs
   off-device); then **Feature 1 upload** as a distinct step (the one new server capability).
3. **Feature 3**, GP-elements-only first on the bench, then notes/plans, all under an UNTESTED
   banner until hardware-validated.

Nothing here changes existing behavior; all three are additive and hang off screens that
already exist.
