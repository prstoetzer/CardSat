# Scope: Full web control of the Cardputer ADV

**Status: forward-looking design scope — proposes extending an existing, shipping feature.**
Unlike the porting documents (which target hardware CardSat doesn't run on yet), this builds
on the **on-device web server that already exists** in the current firmware. Code references
are to the present `app.cpp`; nothing here changes code.

The goal: make **every** CardSat feature operable from a phone/laptop browser over Wi-Fi —
**including the on-device setup screens** (WiFi, radio/CAT, rotator, location, station/LoTW,
calibration, preferences) — not just the read-out and live-tuning the web page exposes today.
The central engineering question is **memory**: the ADV is a no-PSRAM ESP32-S3, and a richer
web surface competes for the same scarce heap the display sprite and TLS already fight over.
The user's instinct — that the **display sprite could be freed while the device is in a
"web control" mode** — is exactly the right lever, and there is direct precedent for it in the
codebase. This document evaluates that and the rest.

---

## 1. What already exists (the starting point)

The firmware is **not** starting from zero. Today it ships a small but real on-device web
stack, gated behind a `webEnable` setting on `cfg.webPort`:

- **A non-blocking HTTP listener** (`serviceWebd()`), one client at a time, reading the
  request line + headers without blocking the tracking loop.
- **A served HTML/JS page** (`webdSendPage()`), streamed from **PROGMEM in chunks** so the
  page is never copied whole into RAM — already the right pattern for a no-PSRAM part.
- **A read API**: `/api/status`, `/api/sats`, `/api/passes`, `/api/orbit`, `/api/tx` — live
  state, the satellite list, upcoming passes, the ground track, transponder list.
- **A control API, but deliberately narrow**: `POST /api/select` (pick a satellite by NORAD),
  `/api/fav` (toggle favourite), `/api/tx` (pick a transponder), `/api/cal` (set RX/TX
  calibration in Hz, clamped), and **`/api/cmd?k=`** — which **injects a single key code into
  the existing `char` state machine**. Crucially, `/api/cmd` accepts only a **whitelist of
  live Track/Manual controls** (`, / t d r o m x s y`, plus a few Manual-context keys) and the
  comment says it explicitly: *"ignore anything that would navigate or that isn't a safe live
  control,"* and it restores `screen` so it *"don't yank the device's own view."*

**So the gap is precise.** CardSat already has (a) a web server, (b) a page, (c) live read,
(d) a key-injection bridge into the UI state machine. What it does **not** expose is
**navigation and setup** — moving between screens, and especially the configuration editors.
"Full web control" is mostly about **safely widening `/api/cmd` (and the page) to the whole
feature set, including setup**, and paying the memory cost of doing so.

> **The current API is fully documented** in `docs/interfaces/WEB_API.md` — every endpoint,
> parameter, and response field as it ships today, plus the access model (no auth, no CORS,
> single-client). That reference is the baseline this document proposes to extend.

---

## 2. The goal: every feature, including setup

"Fully controllable over the web" means a browser can do anything the device's own keyboard
can, including the things the current API deliberately refuses:

- **Navigation:** move between all screens (home menu, satellite list, passes, schedule,
  maps, sun/moon, transponder DB, log, messages, settings) — the things the `/api/cmd`
  whitelist currently blocks.
- **Setup / configuration:** the on-device editors — **WiFi** (scan, pick AP, enter password),
  **radio/CAT** (type, model, baud, CI-V address, pin mode, PTT), **rotator** (backend, pins,
  calibration), **location** (GPS or manual lat/lon/grid), **station/LoTW** (callsign,
  certificate handling), **preferences** (units, tones, display, etc.). These involve **text
  entry** and **multi-field forms**, which is a different interaction than injecting a Track
  hotkey.
- **Actions with side effects:** trigger a GP update, a CloudLog/LoTW upload, add/edit a log
  QSO, send a LoRa message, manage the satellite database.
- **Everything read-only** the device shows: full status, all list views, calibration state,
  diagnostics.

The two genuinely new interaction classes versus today are **navigation** and **text-entry
forms** — the rest is more of the existing key-injection and read pattern.

---

## 3. The memory reality (the crux)

This is the section that decides feasibility. The ADV is an **ESP32-S3 with no PSRAM**; heap
is the binding constraint, and the firmware already spends real effort managing it.

### 3.1 What the heap looks like today

- The **display sprite** is an **8bpp `M5Canvas` of 240×135 ≈ 32 KB**, kept **resident at all
  times**. With it allocated, roughly **~85 KB of heap remains free** (per the in-code notes).
- **TLS fetches** (GP, weather, QRZ, hams.at, CloudLog, LoTW) need a **contiguous block** for
  the mbedTLS handshake. This is why the firmware keeps the baseline DRAM footprint low
  (bounded `MAX_SATS = 150`, `LOG_VIEW_MAX = 60`) — so that contiguous block exists *with the
  sprite resident*.
- The firmware **already frees the network listener sockets around every outbound fetch**
  (`suspendNetServers()` / `s_fetchDepth`, via `Net::onTlsBusy`) to hand the TLS connect
  maximum headroom — and **the web listener is one of those suspended sockets**. So an
  outbound fetch and the web server already contend, and the firmware already arbitrates it.

### 3.2 The hard-won sprite-freeing history (read before proposing anything)

The codebase has **already tried freeing the sprite to reclaim heap**, and the comments record
exactly how it went — this is the most important input to the design:

- Freeing the **16bpp 64 KB** sprite **could not be reliably reallocated on IDF 5.4.x** and
  **froze the screen**. (This is why the sprite was dropped to 8bpp/32 KB.)
- Freeing the **32 KB** sprite mid-fetch yielded a **fragmented hole that didn't reliably merge
  into a usable block**, and a **failed re-create left the screen dark**.
- Net result: the sprite is now **kept resident**, and `freeCanvasForTls()` /
  `restoreCanvasAfterTls()` are **intentional no-ops** retained only as call sites.
- The **one exception** that still frees the sprite is the **LoTW upload's gzip step**
  (`doLotwUpload`), which needs a contiguous ~32 KB block — and it does so **explicitly and
  locally**, freeing right before and restoring right after, in a tightly-scoped path.

**The lesson is not "freeing the sprite doesn't work."** It's that **speculative, per-fetch
freeing is unreliable** (the re-create is the failure point), while **explicit, locally-scoped
freeing works** (LoTW proves it). That distinction is what makes the user's idea viable.

### 3.3 The "web control mode" idea — and why it sidesteps the failure mode

The user's proposal: **while the device is being driven over the web, free the display
sprite** to give the richer web stack ~32 KB more heap. The key realization is that a *mode*
is fundamentally different from the per-fetch freeing that failed:

- In **web-control mode the local display is intentionally off** — the operator is looking at
  their phone, not the Cardputer. So there is **no per-fetch re-create churn**: the sprite is
  freed **once** on entering the mode and recreated **once** on exiting it. The unreliable
  part (frequent free/recreate cycles racing the heap) never happens.
- The re-create still has to succeed **once**, on mode exit — and that's the residual risk
  (§3.5). But a single, deliberate re-create with the device otherwise quiescent is the
  *best* case for it, not the worst, and there are safe fallbacks if it fails (show a
  "return to device" reboot prompt rather than a dark screen).
- Freeing the sprite is the firmware's **already-proven move** (LoTW). Web-control mode is
  essentially "hold the LoTW-style freed state for the duration of the session instead of for
  one gzip."

This is a genuinely good fit: it converts the ~32 KB the display holds into headroom for the
web server's request buffers, the larger served page/forms, and the JSON the richer API
produces — **exactly when the display isn't needed anyway.**

### 3.4 What the freed 32 KB actually buys (and what it doesn't)

Be precise about what the headroom is for, so the idea isn't oversold:

- **It helps the web stack's working set:** bigger/safer request and response buffers, room to
  assemble a settings form's JSON or a multi-field POST, headroom for the slightly larger code
  paths a full UI exercises. It makes the richer surface comfortable rather than
  byte-counting.
- **It does *not* magically enable PSRAM-class features.** 32 KB is meaningful on this part but
  it is not a new megabyte. The discipline that keeps the device alive today (streaming the
  page from PROGMEM, bounded RAM tables, no large single allocations) **still applies** to the
  web paths — the freed sprite widens the margin, it doesn't remove the rules.
- **It interacts with TLS.** If a web-initiated action triggers an **outbound TLS fetch** (a
  GP update or a CloudLog upload requested from the browser), that still needs its contiguous
  block. With the sprite already freed for web-control mode, that block is *easier* to get, not
  harder — the two needs align rather than conflict. (One caveat: LoTW's own sprite-free
  becomes an already-freed no-op in this mode; the paths must be made aware of each other
  so they don't double-free or double-restore — §3.5.)

### 3.5 Residual risks and how to bound them

- **The single re-create on exit.** If recreating the 32 KB sprite fails (heap fragmented by a
  just-finished web session), the device must **not** go dark silently. Safe fallbacks: retry
  after a short settle, fall back to a text-only minimal redraw, or present a **reboot-to-device
  prompt** (the firmware already has reboot-into-X patterns). Never leave a blank panel.
- **Double-free/double-restore with LoTW and TLS suspension.** The existing
  `freeCanvasForTls`/socket-suspend machinery and a new web-control-mode free must share one
  piece of state (a free-reason refcount or an explicit "who owns the freed sprite" flag), or a
  web-mode LoTW upload could restore the sprite mid-session, or fail to. This is a small but
  real correctness item — reuse/extend the existing `s_fetchDepth` discipline rather than
  inventing a parallel one.
- **Entering/exiting the mode cleanly.** The device needs an unambiguous way in and out — a
  menu item or a key, and an automatic exit on web-client disconnect/timeout so a dropped phone
  doesn't strand the device with its display off. Re-assert the display on any local keypress
  too (operator picks the device back up).
- **The mode is opt-in, not the default.** Normal operation keeps the sprite resident (proven,
  safe); the freed-sprite mode is a deliberate state the operator selects when they intend to
  run from the browser. This mirrors how the firmware treats every other at-your-own-margin
  behavior.

---

## 4. The other design considerations (beyond memory)

### 4.1 Text entry and forms — the real new UI work

The current `/api/cmd` injects single hotkeys, which suits Track controls but **not** the
setup screens, which need **strings** (SSID, password, callsign, lat/lon, CI-V address) and
**multi-field forms**. Two approaches:

1. **Semantic config endpoints (recommended).** Add `GET/POST /api/config` (and per-area
   variants) that read and write `cfg` fields directly with validation, rather than
   pantomiming keystrokes. The browser renders real HTML forms; the device validates and calls
   the same `cfg.save()` / apply logic the on-device editors call. This is cleaner, safer
   (validated server-side), and avoids trying to drive a character-cell text editor over a wire
   one keypress at a time. It does mean **enumerating the config surface** as an API — but that
   inventory is small and finite.
2. **Generic key-stream into the editors (not recommended).** Extend `/api/cmd` to allow
   navigation + the text-editor keys, and literally type into the on-device editors remotely.
   Minimal new code, but it forces the browser to mirror the device's modal text-entry UX,
   round-trips every character, and is fragile. Use only for the rare field a semantic endpoint
   doesn't cover.

The pragmatic split: **navigation and live actions via the (widened) key-injection bridge;
configuration via semantic `/api/config` endpoints.**

### 4.2 State sync / liveness

The page today is poll-based (fetch `/api/status` periodically). For "full control" that's
probably still fine — a 1–2 Hz poll of a compact status JSON is cheap and avoids the memory
cost of holding a WebSocket open on a no-PSRAM part. **Recommendation: stay with short-poll
JSON**; a WebSocket would give smoother liveness but costs a persistent connection's buffers
and complexity that this device can ill afford. (This is the opposite call from the *Linux*
client/server scope, precisely because that target has memory to spare and this one doesn't.)

### 4.3 Concurrency with the device's own UI

Today `/api/cmd` carefully **restores `screen`** so the web client doesn't hijack what the
local operator sees. For full control, decide the model: in **web-control mode** (display off)
this tension disappears — the browser *is* the UI. Outside that mode, keep the current
"web nudges live controls without stealing the local view" courtesy, or define a clear
hand-off. The freed-sprite mode actually **simplifies** this: when the device isn't rendering,
there's no local view to fight over.

### 4.4 Security

A browser that can drive **every** feature can change the callsign, key behaviors, trigger
uploads, and (via CAT) tune and PTT the radio. Once that's exposed over Wi-Fi:

- **At minimum a token/password** on the control endpoints (the read endpoints are lower-risk;
  the POSTs are not).
- **Keep it LAN-only by default**; no internet exposure without a deliberate opt-in.
- **Transmit/PTT and destructive actions** (factory reset, certificate changes) deserve the
  most caution — gate them explicitly.
- The device is the authority: **validate every POST server-side** (the `/api/cal` clamp is the
  existing model — extend that discipline to all config writes).
- This is a higher-stakes surface than the current read-mostly page; the security step is
  **not optional** once setup and TX are reachable.

### 4.5 Code size and the page itself

The served page grows as it gains the full feature set (all screens, all forms). Keep it in
**PROGMEM and streamed in chunks** as today (never a RAM copy), and prefer **lean hand-written
HTML/JS over a framework** — the page ships from the device's flash, so every kilobyte is flash
budget and transfer time over Wi-Fi. The richer UI can still be a single self-contained page;
it just needs to be written with the same parsimony as the rest of the firmware.

---

## 5. Pros and cons

**Pros**

- **Builds on a real, shipping foundation** — the web server, page, read API, and key-injection
  bridge already exist; this is an *extension*, not a greenfield build.
- **No extra hardware, no port** — it's the current ADV, over its existing Wi-Fi. Every user who
  has the device gets it with a firmware update.
- **A vastly better setup and operating experience on a phone** than a 240×135 screen and a tiny
  keyboard — entering a WiFi password or a callsign in a browser form is night-and-day easier
  than the on-device editor.
- **Remote/eyes-free operation** — drive the radio and watch a pass from across the room on a
  phone, with the device tucked away with the antenna/rig.
- **The sprite-free "web control mode" is a proven move repurposed** — freeing the sprite is how
  LoTW already gets its block; holding that freed state for a session is a natural, low-novelty
  extension, and it converts unused display memory into web headroom *exactly when the display
  isn't needed.*
- **Accessibility** — a browser UI can be larger, higher-contrast, and screen-reader-friendly in
  ways the embedded display can't.

**Cons**

- **Memory is genuinely tight and the margins are real.** Even with the freed sprite, this is a
  no-PSRAM part; the full feature set must be built with the same parsimony as the firmware, and
  some combinations (a big settings form *plus* an outbound TLS upload *plus* the web buffers)
  need careful sequencing. This is the dominant cost.
- **The sprite re-create on mode exit is a real (if bounded) failure point** — it must have safe
  fallbacks so the device never goes dark, and it must coordinate with the existing LoTW/TLS
  sprite-free machinery (§3.5).
- **Configuration over the web is meaningful new work** — semantic `/api/config` endpoints, with
  validation, for the whole config surface, plus the HTML forms. It's finite but it's the bulk of
  the effort.
- **Security becomes mandatory** the moment setup and TX are reachable over Wi-Fi (§4.4) — auth,
  LAN-only defaults, validated writes. The current read-mostly page mostly dodges this; full
  control cannot.
- **Single-client, cooperative server.** The on-device HTTP listener handles one client at a
  time and runs cooperatively inside the tracking loop; it is not a multi-client web server, and
  shouldn't try to be on this hardware. Fine for one operator's phone; not a multi-user portal.
- **Two control surfaces to keep consistent** — the on-device UI and the web UI must not drift in
  behavior; every feature now has two front doors to test.
- **It doesn't change the ceiling.** This makes the ADV pleasant to operate from a phone, but the
  device is still a no-PSRAM ESP32; the truly expansive "many independent UIs / audio / multi-
  client" vision belongs to the **Linux client/server scope** (`CARDPUTER_ZERO_PORT_SCOPE.md`
  §9), not here. This is the *embedded, self-contained* answer to the same wish.

---

## 6. Suggested phasing

Each phase is independently useful and the order front-loads value while deferring the
memory-sensitive parts until they're needed:

1. **Widen navigation in the key bridge.** Allow `/api/cmd` to move between screens and drive the
   read-only/list views (still no text entry). The browser can now *see* everything the device
   can. Low memory cost; mostly relaxing the existing whitelist with care.
2. **Semantic read of config.** `GET /api/config` returning the current settings as JSON so the
   page can *display* the full configuration. Read-only, low risk.
3. **Semantic write of config (the big one).** `POST /api/config` per area, with server-side
   validation mirroring the on-device editors, so WiFi/radio/rotator/location/station/prefs are
   all editable from a browser form. Add **auth** here — this is where it becomes necessary.
4. **Web-control mode with sprite freeing.** Add the explicit mode (menu/key in, auto-exit on
   disconnect/keypress), free the sprite on entry using/extending the existing
   `freeCanvasForTls` machinery (now made non-no-op and refcounted with the TLS/LoTW path), and
   recreate-with-fallback on exit. This buys the headroom for the richest forms/actions.
5. **Actions and the long tail.** Web-triggered GP update / CloudLog / LoTW upload, log
   add/edit, LoRa message compose — each sequenced against the memory/TLS reality (§3.4), reusing
   the existing fetch-suspension discipline.
6. **Harden:** auth on all control endpoints, LAN-only default with an explicit remote opt-in,
   transmit/destructive-action gating, and the re-create fallbacks.

Phases 1–2 are nearly free and already valuable; phase 3 is the bulk of the new functionality;
phase 4 is where the user's sprite-freeing insight pays off and unlocks comfortable headroom for
phases 3 and 5.

---

## 7. Recommendation

Full web control of the ADV is **feasible and largely an extension of what already ships** — the
server, page, read API, and key-injection bridge are in place; the work is **navigation +
semantic config endpoints + security**, with **memory as the governing constraint.** The user's
**sprite-freeing "web control mode" is the right mechanism and rests on proven precedent** (it's
the LoTW move, held for a session, used exactly when the local display isn't needed) — adopt it,
but as a **deliberate opt-in mode** with a **safe re-create fallback** and **shared state with the
existing TLS/LoTW sprite-free machinery**, never as a default or a per-fetch behavior (which the
codebase already found unreliable).

Sequence it as in §6: relax navigation first (cheap, immediately useful), add read-only then
validated read/write config (the bulk), then introduce the freed-sprite mode to give the richer
surface comfortable headroom, then the action long-tail and hardening. Keep every web path as
parsimonious as the rest of the firmware — stream the page from PROGMEM, validate every write,
bound every buffer — because the freed 32 KB widens the margin without repealing the no-PSRAM
rules. And treat **security as mandatory**, not optional, the moment setup and transmit become
reachable from a browser.

This delivers the phone-friendly, full-feature experience on the **hardware users already own**,
while the expansive multi-UI/audio vision remains the province of the Linux client/server scope.

---

> **No code is changed by this document.** Forward-looking scoping only.
