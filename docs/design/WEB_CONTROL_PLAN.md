# Planning: complete headless web control of CardSat

**Status:** scoping only — no code written.
**Goal:** run the Cardputer as a **headless station server** — permanently cabled to a
radio and/or rotator, screen dark, controlled entirely from a browser on the LAN: from
first-boot setup through daily operating (tracking, tuning, rotor, logging, reporting).

---

## 1. What already exists (build on it, don't duplicate it)

CardSat already ships a small, well-behaved web layer (`webd`, a hand-rolled streaming
HTTP server on `WiFiServer`, enabled by the *Web control* setting):

- **Read endpoints:** `/` (a PROGMEM-streamed page), `/api/status`, `/api/sats`,
  `/api/passes`, `/api/orbit`, `/api/tx`.
- **Write endpoints:** `POST /api/tx?i=` (select transponder), `/api/cal` (RX/TX cal),
  `/api/select` (active sat), `/api/fav`, and `POST /api/cmd?k=` — a **key-injection
  bridge** that runs an allow-listed key in the Track or Manual screen's own handler and
  restores the device's view afterward.
- **Streaming discipline throughout** (chunked PROGMEM page, per-object JSON) — the style
  the no-PSRAM heap demands. This is the house pattern the whole plan follows.
- Also relevant: `rigd`/`rotd` (existing network rig/rotor daemons), and the fact that
  **`Cfg::save()/load()` already round-trips the entire configuration through ArduinoJson**
  — which makes a full settings API nearly free.

What is missing for the headless goal: any way to **provision WiFi with no screen**, any
**authentication**, coverage of the other ~100 screens, **settings editing**, log
management, update/reporting triggers, and a **headless lifecycle** (display off, mDNS
discovery, recovery).

---

## 2. Constraints that shape the design

1. **No PSRAM; heap fragility is proven.** Field logs show largest-free-block ~14 KB
   after downloads; a past bug traced whole-document JSON parsing to TLS instability.
   Every new endpoint must stream; no `String`-assembled multi-KB bodies; one HTTP client
   serviced at a time (as today).
2. **Single-threaded loop.** `webd` is polled from `loop()`. Long operations (GP update,
   transponder cache-all, LoTW upload) block; a browser must not hang a TCP connection
   waiting on them. Needs a **job pattern**: `POST` starts, returns immediately;
   `GET /api/job` reports progress (the on-screen status line already exists as the
   progress source).
3. **The framebuffer is readable.** `canvas` is an M5Canvas sprite: a 240×135×16-bit RAM
   buffer (64,800 bytes) that can be streamed row-by-row without a heap copy. At LAN
   speeds that is ~1–4 fps polling — plenty for a control UI.
4. **Key handling is centralized.** Every screen is a `keyXxx(char, enter, back)` handler
   behind one dispatch. `/api/cmd` proves keys can be injected safely with context save/
   restore. Generalizing this is the cheapest path to *complete* coverage.
5. **8 MB flash** leaves comfortable room for a richer PROGMEM web app (tens of KB of
   HTML/JS/CSS is nothing) and for OTA (dual app partitions — verify partition table).
6. **Plain HTTP on the LAN.** A TLS *server* is not realistic on this heap alongside the
   TLS *client* work. Security posture must be explicit: LAN-only + token auth,
   documented as such.

---

## 3. Architecture: three complementary tracks

### Track A — the "virtual device" (screen mirror + universal key bridge)
**This is the coverage backstop: 100% of CardSat, headless, for very little code.**
- `GET /api/screen` streams the canvas framebuffer as a BMP (16-bit 565, row-streamed —
  zero heap copy; ~65 KB/frame) or optionally RLE for less traffic. Browser polls 1–4 fps.
- `POST /api/key?k=&enter=&back=` becomes a **universal** injection: any key, routed to
  the *current* screen's handler via the normal dispatch (not just Track/Manual).
  The existing allow-list stays for unauthenticated/legacy use; the universal bridge sits
  behind auth (Track C).
- A simple page renders the frame and maps clicks/keyboard to `POST /api/key` — the
  browser literally becomes the Cardputer's screen and keyboard.
- **Why this matters:** every feature ever added — Tools, EQX, games, settings — is
  instantly reachable headless, with zero per-screen web work, forever. Purpose-built
  panels (Track B) then improve UX only where it pays.
- Watch items: injected keys vs. the device's own idle/sleep logic; `draw()` cadence when
  the display is "off" (headless mode should keep rendering to the sprite but skip the
  physical `pushSprite`).

### Track B — purpose-built panels (better-than-mirror UX where it counts)
A richer PROGMEM single-page app (vanilla JS, no framework, hash-routed views), served as
today but larger. Panels in value order:
1. **Dashboard** — status, clock, next passes, active sat/transponder, radio/rotor state
   (compose from existing `/api/status`, `/api/passes`).
2. **Operate** — the Track screen as real controls: big RX/TX readout, tune/passband
   buttons, mode, TX-enable, rotor az/el with a polar widget (existing `/api/cmd`
   verbs already cover most of this).
3. **Settings** — a generated form over **the cfg JSON**: `GET /api/settings` streams the
   same document `Cfg::save()` writes; `POST /api/settings` feeds partial JSON through the
   same loader then saves. One endpoint pair, all ~85 settings, always in sync with
   firmware — no per-setting web code. Redact secrets (WiFi/QRZ/LoTW/Cloudlog passwords)
   on GET; accept on POST.
4. **Passes & schedule** — next passes for favorites, tap-to-select, pass detail.
5. **Log** — QSO list, new-entry form, **ADIF download** (`GET /api/log.adi`, streamed
   from the CSV), trigger LoTW/Cloudlog upload as jobs.
6. **Update & data** — GP update, transponder cache-all, AMSAT catalog/status refresh as
   jobs with progress; data ages shown.
7. **AMSAT reporting** — the report picker as a form (satellite → mode (pretty names) →
   status → send).
Deliberately **not** web-ported: games, the illumination/EQX/analysis graphics, Tools —
the mirror covers them.

### Track C — headless lifecycle (setup, discovery, security, recovery)
1. **First-boot / lost-WiFi provisioning:** if no WiFi is configured **or** join fails
   for N seconds, start **SoftAP** `CardSat-XXXX` with a minimal captive portal: scan
   networks (scan code exists), enter credentials, set the web-auth token, reboot to STA.
   This is the single piece that makes true headlessness possible.
2. **Discovery:** mDNS (`cardsat.local`) so no IP hunting; show the URL on-screen when a
   display is attached.
3. **Auth:** a settings-stored token; browser sends it once, stored client-side; all
   POST/universal-key/settings endpoints require it (GET status endpoints may stay open,
   configurable). Plain HTTP + token, LAN-only — stated plainly in docs.
4. **Headless display mode:** setting to blank/skip the physical panel (battery + OLED
   wear) while the sprite keeps rendering for the mirror; wake on local keypress.
5. **OTA firmware upload** (`POST /api/ota`) — for a device in a cabinet this is the
   difference between convenient and miserable. Requires partition-table verification;
   gate behind auth + confirmation; keep the SD "launcher" path as fallback.
6. **Recovery:** watchdog note — if WiFi drops, webd already rebinds; provisioning AP as
   last resort after prolonged failure is a policy decision (flag to Paul).

---

## 4. Suggested phasing

| Phase | Contents | Outcome |
|---|---|---|
| **1** | Auth token · mDNS · `GET/POST /api/settings` (cfg-JSON reuse) · job pattern + `/api/job` · update/cache triggers | Configure + maintain a *provisioned* device fully from the browser |
| **2** | Screen mirror + universal key bridge + mirror page · headless display mode | **Complete** control of everything, headless |
| **3** | SoftAP provisioning portal | True zero-screen first boot |
| **4** | Purpose-built panels: dashboard, operate, passes, log (+ADIF), reporting | Daily-driver UX |
| **5** | OTA · optional SSE push instead of polling · SD file manager | Cabinet-grade convenience |

Phases 1–2 are modest (the hard primitives exist); Phase 3 is self-contained; Phase 4 is
the bulk of the HTML work and can land panel-by-panel across releases.

## 5. Risks / open decisions (for Paul)

- **Security posture sign-off:** plain-HTTP + token on a trusted LAN — acceptable? (TLS
  server judged infeasible on this heap; document loudly, default web control OFF as now.)
- **Universal key injection scope:** behind auth, should *everything* be injectable
  (including settings edits and deletes), or keep a small deny-list (e.g. format-internal)?
- **SoftAP fallback policy:** auto-start AP after N minutes of WiFi failure on a deployed
  station could be a nuisance vs. a lifesaver — pick N / make it a setting.
- **OTA appetite + partition check** before promising Phase 5.
- **Mirror frame format:** BMP-565 (simplest, ~65 KB/frame) vs RLE (less traffic, more
  code) — measure first on real LAN.
- **Blocking ops inventory:** confirm every >1 s operation is jobified before the panels
  expose it, or the browser experience will be "mystery hangs."

## 6. Verification approach

Host-side: endpoint handlers exercised with a mock client where practical; JSON shapes
diffed against `Cfg` round-trip. Bench (authoritative): provisioning flow with a factory-
blank unit; mirror latency/fps on real WiFi; a full headless operating session (select →
track → tune → rotor → log → report) with the physical display dark; heap watermarks
logged before/after each phase's endpoints under polling load; job-pattern behavior
during a GP update while the mirror is polling.
