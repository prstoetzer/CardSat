# Headless web control — 0.9.46 scoping

**Goal.** Let the web UI *completely* control CardSat, so the Cardputer can run
headless — mounted at the antenna, in a go-box, or anywhere the screen and keypad
aren't reachable — with a phone or laptop as the entire interface.

**Status.** Scoping only. No code. This documents what "complete control" would
require, three candidate architectures with their costs, the memory reality that
constrains all of them, and a recommended path.

---

## 1. Where we are today

The web UI is a **purpose-built remote for the Track workflow**, not a general
interface. Its backend (`webdHandleRequest`) exposes a fixed set of JSON endpoints:

- **Read**: `/api/status`, `/api/sats`, `/api/passes`, `/api/orbit`, `/api/tx`
- **Act**: `/api/select` (pick sat), `/api/fav` (toggle favourite), `/api/tx` POST
  (pick transponder), `/api/cal` (set calibration Hz), `/api/cmd` (a **whitelisted**
  set of Track/Manual keys)

`/api/cmd` is the closest thing to general control, and it's deliberately narrow: it
only accepts a handful of safe keys (`, / t d r o m x s y`, plus a few Manual ones)
and only runs them in the Track or Manual context. It explicitly refuses anything
that would navigate screens or isn't a known-safe live control. That restraint is
correct for what it is — a remote that shouldn't be able to wander the device into a
destructive state — but it's the opposite of "complete control."

So today the web UI can drive **one workflow well** (select a bird, work the pass,
calibrate) but cannot reach Settings, the Log, Messages, Activations, the Games, the
orbital pages, or any of the ~80 other screens.

## 2. The two facts that make this tractable

Two pieces of existing architecture make full headless control a *reasonable* project
rather than a rewrite:

**(a) One uniform input entry point.** Every screen's keys flow through a single
dispatcher:

```
void App::handleKey(char c, bool enter, bool back)  // switch(screen) -> keyXxx(...)
```

The physical keyboard calls exactly this. There is no per-screen input plumbing to
replicate: a synthetic key injected into `handleKey` is *indistinguishable* from a
real keypress, because the device already funnels all real keypresses through it.
Full remote input is therefore "call `handleKey` with the web-supplied key," not
"reimplement 88 screens' worth of controls."

**(b) The screen is one framebuffer, and we already serialize it.** Everything draws
to a single `M5Canvas` — a **240×135, 16bpp, ~64 KB** sprite. The existing screenshot
feature (`takeScreenshot`, the hidden `b` key) already reads that framebuffer
row-by-row as RGB888 via `readRectRGB()` and writes a BMP. So the mechanism to turn
"whatever is on screen right now" into bytes a browser could display **already exists
and is proven** — a headless view is a matter of shipping those bytes over HTTP
instead of to the SD card.

Put together: **input is a solved dispatch problem, and output is a solved
serialization problem.** What's left is the transport, the encoding, and — the crux —
the memory budget.

## 3. The constraint that shapes everything: memory

This is the ESP32-S3FN8 with **no PSRAM**. The 64 KB display sprite is a single
contiguous allocation that, when resident, **caps the largest free heap block at
~31 KB** (documented at length in the sprite/TLS comments and the TLS migration
postmortem). TLS handshakes need a large contiguous block; this exact tension is what
the entire 0.9.43 BearSSL migration existed to resolve, and the resolution depends on
keeping the baseline DRAM footprint low.

Any headless design has to live inside that budget. Streaming a **live image** of the
screen is the memory-hostile option:

- A raw 240×135 RGB888 frame is **~95 KB** — larger than the largest free block. It
  can't be buffered whole; it must be streamed row-by-row (as the BMP code already
  does), which works but ties up the socket and CPU for the whole transfer.
- Encoding to PNG/JPEG on-device to shrink the transfer costs a codec (flash + a
  working buffer) and CPU time we're tight on.
- Doing this **repeatedly** (a live view refreshing several times a second) competes
  directly with the fetch path for heap and with the Doppler loop for CPU. During an
  actual pass — when the loop is busiest and a fetch might fire — that contention is
  worst.

The escape from this is the central design decision below: **don't stream pixels.
Stream state, and render the UI in the browser.**

## 4. Three candidate architectures

### Option A — Screen mirroring ("VNC for CardSat")

Ship the framebuffer as an image; send synthetic keys back. The browser shows a
picture of the Cardputer screen and has on-screen buttons for the keys.

- **Pros.** Conceptually simple. Perfectly faithful — every screen, including games
  and custom graphics, appears exactly as on the device with zero per-screen work.
  Input is trivially `handleKey(c, enter, back)`.
- **Cons.** The memory/CPU cost above, and it scales badly: every screen refresh
  moves ~tens of KB. It's laggy over WiFi, it fights TLS for heap, and a live view
  refreshing continuously is exactly the workload the hardware is worst at. Also it
  hijacks the *physical* screen — the browser sees whatever the device shows, so two
  people can't look at different things, and the device screen can't be doing
  something else.
- **Verdict.** Viable as an *occasional* "show me the current screen" snapshot (cheap,
  on-demand, one frame). **Not** viable as the primary always-on headless interface.

### Option B — Semantic remote (state model + browser-rendered UI) — RECOMMENDED

Generalize what the web UI already does. Instead of a picture, the device sends a
compact **description of the current screen** (which screen, its title, its list rows
or field values, the footer hints), and the browser renders that natively as HTML.
Input still goes back through `handleKey`.

- **Pros.** Cheap on the wire and on heap (a few hundred bytes of JSON, not tens of
  KB of pixels) — it fits the memory budget and doesn't fight TLS. It's the natural
  extension of the existing `/api/status` + `/api/cmd` model. The browser UI can be
  *better* than the 240×135 screen (bigger text, real buttons, mouse/touch), and it
  decouples the remote view from the physical screen. Fast and responsive.
- **Cons.** It needs a **per-screen "describe yourself" contract**. Not every screen,
  but every screen you want remotely reachable, has to emit its state in a structured
  form and accept the relevant keys. That's real, bounded work — call it a small
  adapter per screen — and it's the bulk of the project. Screens that are inherently
  graphical (the polar plot, the world map, the games) don't describe cleanly as
  text and would either get a bespoke representation (we already do this for the
  polar plot and passes in the web UI) or fall back to Option A's snapshot for that
  one screen.
- **Verdict.** The right primary architecture. It matches the hardware's strengths,
  builds on what's already there, and the cost is proportional to how many screens
  you choose to expose.

### Option C — Hybrid (B primary, A as a fallback tile)

Do Option B for the screens that matter, and offer an on-demand **"view device
screen"** snapshot (a single Option-A frame, fetched only when the user asks) for the
graphical screens and for parity/debugging.

- **Pros.** Best of both: cheap semantic control everywhere it's worth building, plus
  a faithful picture on demand for the handful of screens that are pictures. The
  snapshot is one frame on a button press, so it never runs continuously and never
  competes with a pass.
- **Cons.** Two mechanisms to build and maintain instead of one.
- **Verdict.** The pragmatic target if we want *genuinely complete* coverage. Option B
  handles the interactive/text screens; the on-demand snapshot covers the graphical
  ones without paying the streaming cost.

## 5. Recommended path for 0.9.46

**Adopt Option B as the architecture, and phase it. Do not attempt "all 88 screens"
in one release.** The value is front-loaded: a handful of screens deliver most of the
headless usefulness.

**Phase 1 (0.9.46 target): the operating core.** Make the device fully operable
headless for the *common session*, without touching the physical unit:

1. A **navigation contract**: an endpoint to report the current screen + an endpoint
   to inject a key through `handleKey`, so the browser can drive Home → menus → any
   screen. This is the enabling primitive; everything else builds on it.
2. **Home / menu** described as a list the browser renders and navigates.
3. **Settings** as structured rows (label + current value + how to change), reusing
   the section/row model that `drawSettings` already has internally — this is the
   single highest-value screen to expose, since "I can't change a setting without the
   device in hand" is the sharpest headless limitation.
4. **Satellites list** (select/favourite — largely already there) and **Passes /
   Next Passes**.
5. A **on-demand screen snapshot** (one Option-A frame) as the universal fallback so
   *nothing* is truly unreachable, even before it has a native representation.

That combination — navigate anywhere, operate the common screens natively, and fall
back to a snapshot for the rest — is a defensible "you can run it headless" claim for
0.9.46.

**Later phases:** native representations for Log, Messages/roster, Activations,
Orbital analysis, one at a time, by value. The games and pure-graphics screens can
stay snapshot-only indefinitely — nobody needs to play Zap the Sats from a phone.

## 6. Hard problems and honest risks

- **Memory is the gating factor, not features.** Every added capability has to be
  weighed against the ~31 KB-largest-block reality and the TLS budget. The semantic
  approach is chosen precisely because it's frugal; a snapshot must stay strictly
  on-demand (one frame, never a stream) or it reintroduces the 0.9.43 problem.
- **Security becomes real.** Today the web UI can't do much damage, so it ships with
  no auth. "Complete control" means the web UI can change settings, wipe the log,
  transmit — actions that need protecting. Headless control on an open LAN without at
  least a shared-secret/PIN is a footgun. **Auth is in-scope for headless, not
  optional**, and it's new surface (a token check on the mutating endpoints, a login
  on the page). This is arguably the biggest *new* design area, separate from the
  rendering work.
- **State coherence.** With the physical screen and a browser both able to drive
  `handleKey`, they can race (someone turns the device knob while you tap a web
  button). The current UI mostly reads state and only nudges; full control has two
  active controllers. We'll want the device to remain the single source of truth
  (browser reflects device state each poll, as it does now) and to accept that the
  last input wins — but edge cases (mid-edit on both, a screen change under the
  browser's feet) need thought.
- **Text entry.** Some screens (WiFi password, notes, sked entry, callsign) need
  *typed* input, not single keys. `handleKey` takes a char at a time, so a web text
  field can feed characters — but the editor screens (`SCR_EDIT`, `SCR_NOTEEDIT`)
  have their own key semantics (cursor, backspace, commit) that a web form has to map
  onto. Doable, but it's a distinct sub-contract from "press a menu key."
- **The snapshot fallback still costs a frame.** Even on-demand, one 240×135 frame is
  ~95 KB streamed. It's fine as a deliberate button press, but it must never be on an
  auto-refresh timer, and it should probably be blocked outright while a fetch is in
  flight.
- **Scope discipline.** The temptation is "expose everything." The project stays sane
  only if screens are added by value with the snapshot as the safety net, rather than
  treating 1:1 coverage as the bar.

## 7. What this is NOT

- Not a rewrite — it extends the existing web server and reuses `handleKey` and
  `readRectRGB`.
- Not screen-scraping pixels as the primary path (that's the rejected Option A).
- Not a second UI framework on the device — the rendering moves to the browser; the
  device emits state.
- Not "all 88 screens in 0.9.46" — Phase 1 is the operating core plus a universal
  snapshot fallback.

## 8. One-paragraph recommendation

Headless control is **feasible and a good fit for the architecture**, because the
device already has a single input funnel (`handleKey`) and a proven framebuffer
serializer (`readRectRGB`). Build it as a **semantic remote** (Option B): the device
describes the current screen as compact JSON and accepts keys through `handleKey`; the
browser renders and navigates. Phase 0.9.46 to the **operating core** — a
navigate-anywhere key/screen contract, native Settings/Home/Sats/Passes, and a
strictly on-demand single-frame screen snapshot as the universal fallback — rather
than chasing 1:1 coverage. Treat **authentication as in-scope**, since complete
control turns the currently-harmless web UI into something that can change settings
and transmit. The gating constraint throughout is the no-PSRAM heap budget, which is
exactly why the pixel-streaming approach is the fallback and the frugal semantic
model is the spine.
