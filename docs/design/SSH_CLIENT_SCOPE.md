# SSH client in the Tools menu — scope

*Scoping note. No implementation. Assesses feasibility, the honest blockers, and what a minimal
version would take.*

## Summary / verdict

**Technically possible, but it does not fit cleanly, and the memory reality makes it the
riskiest feature yet proposed for CardSat.** The crypto primitives SSH needs are already linked
(mbedTLS, for LoTW signing), the keyboard exposes control characters, and raw TCP is available —
so the pieces exist. But an SSH handshake's *peak contiguous* memory demand collides directly
with the constraint this project has spent its entire recent history fighting, and SSH is a
category mismatch with a Tools menu that is otherwise entirely offline calculators and
references. Recommendation: **treat as an experimental/optional build, scoped small (a real but
minimal client), gated on an on-device memory proof before any promise is made** — and be
willing to conclude it doesn't belong if the handshake won't fit alongside everything resident.

This is a "prove the hard part first" feature, not a "design it and build it" feature.

## What's easy

- **Adding the menu item.** The Tools hub (`SCR_TOOLS`, app.cpp:17067) is a flat list of labels
  with a dispatched key handler. Adding "SSH client" is a label + a `case` + a new screen enum.
  Trivial. This is *not* where the work is.
- **Raw TCP transport.** `WiFiClient` is already used directly under the TLS layer
  (net.cpp:302/448). SSH runs over plain TCP (port 22), so the transport is a bare `WiFiClient` —
  no TLS/BearSSL involved. Available today.
- **Crypto primitives.** mbedTLS is linked and used for LoTW signing (lotw.cpp includes
  `mbedtls/pk.h`, `sha1.h`, `ctr_drbg.h`, `entropy.h`, etc.). SSH needs AES, SHA-256, HMAC, a
  DH/ECDH key exchange, and RSA/Ed25519 host-key verification — mbedTLS provides all of these.
  So an SSH library that can use mbedTLS as its crypto backend (e.g. libssh2, or wolfSSH over
  wolfCrypt) doesn't have to bring its own crypto. This is the single biggest enabler.
- **Keyboard for a terminal.** `keysState()` exposes `word`, `ctrl`, `fn`, `enter`, `del`
  (app.cpp:4485+). Control characters (Ctrl-C, Ctrl-D, Ctrl-Z, arrow/escape sequences) are
  reachable, which a usable terminal requires.

## The real blockers

### 1. Peak contiguous heap during the SSH handshake (the decisive issue)

This is the whole ballgame. The project's central constraint is **contiguous free heap** on a
no-PSRAM part. The measured reality (0.9.53 on-device logs): the largest single free block sits
at ~31.7 KB, and the 4bpp sprite change freed ~16 KB of *total* heap (boot free rose from ~40 KB
to ~70 KB) — the largest-block ceiling itself did not grow, but the freed region is what lets the
16 KB BearSSL RX buffer and the network stack's send buffers coexist. That coexistence margin is
exactly what the 0.9.53 release existed to win back, and an SSH session would spend it again.

An SSH handshake is in the same weight class or heavier:
- Key exchange (Diffie-Hellman group exchange, or ECDH) plus host-key and session-key
  derivation involves several large temporaries — big integers, exchange hashes, cipher/MAC
  state for both directions.
- libssh2 with an mbedTLS backend is known to run on ESP32, but its handshake working set and
  peak allocations are substantial, and it is *additional* to everything CardSat keeps resident
  (the sprite, WiFi buffers, the satellite array, the app's own state).
- SSH then keeps a **persistent** per-session working set for the duration of the connection
  (cipher state, receive buffer, channel buffers) — unlike a fetch, which is transient. So it's
  not just a handshake spike; it's resident memory held for as long as the terminal is open,
  competing with everything else the whole time.

The honest position: **we do not know it fits, and it might not.** Everything else in this scope
is moot until an on-device spike test proves an SSH handshake completes and holds a session with
acceptable margin, *with the sprite and normal state resident*. That test should come before any
UI work. If it doesn't fit, options narrow to (a) freeing more resident memory specifically for
an SSH session (hard — the sprite is the big lever and can't be dropped/recreated safely on this
M5GFX/IDF combo), or (b) concluding SSH is out of budget on this hardware. This is a real
possible outcome and should be stated to users, not discovered late.

### 2. A terminal emulator is a genuine sub-project

A usable SSH client is mostly a *terminal*, and CardSat has never had one:
- **Screen size.** 240×135 at `setTextSize(1)`. With the current font that's roughly ~40 columns
  × ~16 rows — enough for `ls`, `top`, editing a config, reading a log, but cramped, and many
  TUI programs assume 80×24 and will render oddly.
- **VT100/ANSI escape handling.** Even basic interactive use (a shell prompt, `vi`, `top`,
  colored `ls`) streams escape sequences: cursor moves, clear-line/screen, color SGR, scroll
  regions. A terminal that ignores them shows garbage. A *minimal* usable subset (cursor
  positioning, erase, basic SGR, newline/CR) is real work; full xterm compliance is a large
  project on its own. Scope must pick a deliberate subset and accept that some programs won't
  render.
- **A scrollback/screen buffer.** A character grid (say 40×16 = 640 cells, plus attributes) is
  cheap; meaningful scrollback is not free and competes for the same memory as the session. Keep
  it small (one screen + a few lines) to protect the heap.
- **Line discipline / input modes.** Raw vs cooked mode, local echo vs server echo, key-to-escape
  mapping for arrows/Home/End/function keys from the Cardputer's layout. Fiddly but bounded.

None of this is exotic, but together it's comparable in effort to a mid-size existing feature
(think the rove planner), *on top of* the SSH protocol integration.

### 3. Host-key verification and secrets (security correctness)

An SSH client that skips host-key checking is a footgun (trivially MITM'd). Doing it right means:
- Storing and checking `known_hosts`-style host-key fingerprints on the SD card, with a
  trust-on-first-use prompt and a clear "host key changed" warning.
- Handling **credentials**: password entry (screen-visible on a 240×135 display, in a room —
  a real privacy consideration), or public-key auth, which means storing a **private key on the
  SD card**. CardSat already stores the LoTW private key on SD, so there's precedent and a
  documented trust model, but an SSH key is a broader-access secret and deserves its own
  handling and warnings.
- This is the same class of "unauthenticated/plaintext trust" caveat the web-control and
  LoRa features carry, and it needs the same honest banner treatment.

### 4. Library integration and the single-file build

- **Library choice.** libssh2 (mbedTLS backend) is the most likely candidate; it has ESP32
  precedent. wolfSSH is another. Either is a substantial third-party dependency with its own
  build flags, and it must be reconciled with the project's **dual representation** — the
  monolithic `CardSat.ino` mirrors the `src/` tree, and a large external library doesn't inline
  the way app code does. SSH would almost certainly have to be a *library dependency* (managed by
  the build), not mirrored source, which is a different integration pattern than the rest of the
  codebase and worth deciding deliberately.
- **Flash budget.** libssh2 + the terminal code adds meaningful flash. The device is 8 MB with a
  2,624 KB app partition (from the boot log); there's room, but it's not free, and it's worth
  measuring.
- **Watchdog / blocking.** The SSH handshake and blocking reads must not starve the main loop's
  cooperative heartbeat (the same discipline that shaped jobbed pass-sweeps and on-demand audio).
  A terminal session is long-lived and interactive, so it needs its own modal loop that still
  services the watchdog and lets the user exit cleanly.

## Effort and risk

- **Effort: large** — among the biggest features proposed. Roughly three sub-projects: SSH
  protocol/library integration, a terminal emulator (new to CardSat), and secrets/host-key
  handling — plus the build-integration question.
- **Risk: high, and front-loaded in memory.** The dominant risk is that it simply won't fit in
  contiguous heap alongside the resident sprite and state, and that risk is *knowable early* with
  a spike test. Secondary risks (terminal rendering fidelity, watchdog behavior, key handling)
  are the normal kind.
- **Heap: the opposite of everything in 0.9.53.** Recent work *removed* resident memory to make
  uploads reliable. SSH *adds* a persistent per-session working set for the duration of a
  connection. These pull against each other. If SSH ships, it must be a modal state that owns the
  screen (so nothing else competes) and it should probably decline to run if the largest block is
  below a measured threshold — the same defensive posture as the TLS reclaim logic.

## Does it fit CardSat's purpose?

Worth asking plainly. Every current Tools item is an **offline** calculator or reference that
supports operating: calculators, DXCC/zone lookups, antenna math, orbit tools. SSH is a
different kind of thing — a general network client, not a satellite/operating aid. That's not
disqualifying (CardSat already has non-satellite conveniences), and there's a real niche —
poking at a home server, a rotator controller, an APRS box, or a remote station from the same
handheld — but it's a genuine scope expansion of what CardSat *is*, and it carries the largest
resource cost of anything on the roadmap. It should be adopted as a deliberate choice about the
device's identity, not slipped in as "another Tools item," because it is not another Tools item.

## Recommendation

1. **Prove the hard part first.** Before any UI or terminal work, do an on-device spike: bring up
   an SSH library (libssh2/mbedTLS), attempt a real handshake and a held session over plain
   `WiFiClient`, and measure peak + resident heap **with the sprite and normal state resident**.
   This single test decides whether the feature is viable at all. Do not build the UI first.
2. **If it fits:** scope a *minimal* client deliberately — a small fixed terminal (≈40×16, no
   scrollback beyond a few lines), a chosen VT100 subset (cursor/erase/basic SGR), TOFU host-key
   storage with a change warning, password auth first (public-key later), and a modal
   session loop that services the watchdog and exits cleanly. Mark it experimental with a
   plaintext-secret/host-trust banner like the other network features.
3. **Build-integration decision:** accept SSH as a managed *library dependency* rather than
   mirrored source, and document that exception to the single-file mirroring model.
4. **Guard it:** refuse to open a session if the largest contiguous block is below a measured
   floor, mirroring the TLS-reclaim defensive posture, so an SSH attempt can't wedge the device.
5. **If it doesn't fit:** say so honestly and shelve it. "SSH doesn't fit in contiguous RAM on
   this no-PSRAM board alongside the display and app state" is a legitimate, defensible outcome,
   and far better than a flaky client that crashes mid-session.

**Bottom line:** the enablers are genuinely present (mbedTLS crypto linked, raw TCP available,
control-key input, trivial menu hook), which makes this more feasible than it first sounds. But
the memory question is decisive and unresolved, the terminal emulator is a real sub-project, and
SSH is a deliberate expansion of what CardSat is. Scope it as an experiment that must earn its
place by passing an on-device memory proof first — everything else follows from that result.

## Grounding facts (checked in code)

- Tools hub: `SCR_TOOLS` dispatch (app.cpp:4956/10395), item list + handler (app.cpp:17067+);
  adding an item is a label + case + screen enum.
- Raw TCP: `WiFiClient transport` used directly under TLS (net.cpp:302/448); SSH would use it
  bare on port 22.
- Crypto linked: mbedTLS via LoTW signing (lotw.cpp:7–13 — `pk.h`, `sha1.h`, `ctr_drbg.h`,
  `entropy.h`, `x509_crt.h`).
- Keyboard modifiers: `keysState()` exposes `word`, `ctrl`, `fn`, `enter`, `del` (app.cpp:4485+).
- Text: monospace at `setTextSize(1)` on a 240×135 canvas → ≈40×16 char grid.
- Contiguous-heap constraint and history: the 4bpp sprite change (app.cpp:222–232) and the whole
  0.9.53 upload-reliability work; SSH pulls against exactly this.
- Private-key-on-SD precedent and trust model: LoTW key handling (docs + lotw.cpp).
