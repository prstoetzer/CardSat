# Scope: Continuous LoRa Message Monitoring & Notifications

**Status: design scope only — not implemented.** This scopes making CardSat
**continuously listen** for LoRa text messages in the background and **notify** the
operator when one arrives, regardless of which screen they're on.

---

## 1. Current state (what already exists)

CardSat already has most of the receive plumbing:

- **`loraPoll()` runs every main-loop iteration** (called unconditionally in the
  service loop). When the radio is initialized it drains any received frame,
  validates the 2-byte magic/version header, parses the fixed-width callsign and
  text, and stores it via `msgPush()` into the message history.
- The SX1262 is left in **continuous receive** (`startReceive()`) after each poll,
  so the hardware is already always-listening when LoRa is enabled.
- A **`SCR_MESSAGES`** screen displays the history.

**The gap is purely notification/awareness, not reception:** today, a message that
arrives while the operator is on *any other screen* is silently stored —
`loraPoll()` only forces a redraw `if (screen == SCR_MESSAGES)`. There is no alert,
no unread indicator, and nothing to draw the operator's attention to a new message
during a pass or while browsing other screens.

So this feature is **small and low-risk**: it adds awareness on top of an existing,
working receive path.

---

## 2. Proposed design

### 2.1 Unread tracking
Add a small amount of state:
```
uint16_t msgUnread = 0;        // count of messages received but not yet viewed
uint32_t msgLastRxMs = 0;      // when the last message arrived (for the banner)
char     msgLastFrom[…];       // sender of the most recent unread (for the banner)
```
In `loraPoll()`, after `msgPush(...)` for an inbound (not self-echo) message,
increment `msgUnread`, stamp `msgLastRxMs`, and capture the sender. Clear
`msgUnread = 0` whenever the operator opens `SCR_MESSAGES`.

### 2.2 The notification itself
Three complementary, individually-toggleable cues (mirroring how the AOS alarm is
already handled, so the patterns exist):

1. **Header indicator (always on, cheap):** a small envelope glyph + unread count
   in the header (next to the battery), drawn by the shared `header()` so it shows
   on every screen. Zero intrusion; always visible.
2. **Transient banner (opt-in):** when a message arrives, show a brief bottom-of-
   screen banner ("✉ msg from N8HM") for ~3 s on whatever screen is active, using
   the existing `setStatus()` mechanism (already cross-screen). This is the
   minimal, safe default.
3. **Audible chirp (opt-in):** a short `M5Cardputer.Speaker.tone()` beep on arrival,
   reusing the existing tone helper. Off by default to avoid surprising a operator
   mid-pass; gated behind a setting like the AOS alarm.

### 2.3 Settings
Add to the LoRa settings group:
- **Msg notify:** Off / Banner / Banner+Beep (default Banner).
- Respect existing global mute/quiet behaviors if any.

### 2.4 "Open messages" affordance
Optionally, a global key (e.g. on the home/track screens) jumps straight to
`SCR_MESSAGES` when unread > 0, so the operator can act on the banner quickly.

### 2.5 Keeping the receiver alive during tracking
Confirm `loraPoll()` continues to run while the Doppler loop is active (it's in the
same service path today, so it should). The only caution: a very busy Doppler tick
shouldn't starve the LoRa poll. Since both are cooperative in the main loop and
`loraPoll()` is cheap (a non-blocking drain), this is fine — but it's worth an
explicit note that LoRa RX and CAT tracking coexist on the single core.

---

## 3. Risks & limitations

- **Very low risk overall.** Reception already works and runs every loop; this adds
  a counter, a header glyph, an opt-in banner/beep, and a setting. No new protocol,
  no new radio state machine, no blocking calls.
- **Shared-radio contention (LoRa hardware path is UNTESTED).** Per the project's
  standing note, the SX1262 LoRa path itself is marked untested on hardware. This
  feature inherits that caveat — the notification logic is host-reasonable, but
  end-to-end RX depends on the still-unvalidated LoRa hardware path.
- **Single-core cooperative timing.** LoRa RX, CAT Doppler, and UI all share the
  ESP32-S3's loop. A flood of LoRa traffic could, in theory, add latency to the
  Doppler tick. Mitigation: `loraPoll()` drains at most one frame per loop and is
  non-blocking; document the coexistence.
- **Notification fatigue / mid-pass distraction.** An audible beep during a critical
  SSB pass could be unwelcome. Mitigation: beep is **opt-in, off by default**; the
  banner is brief; the header glyph is silent.
- **No wake-from-sleep guarantee.** If the new Charge/Sleep screen has blanked the
  display, a LoRa message should arguably wake it. Decide explicitly: either let
  LoRa RX wake the screen (like the AOS alarm does) or leave the device truly idle
  in charge mode. Recommend: **header glyph + count update silently; do not force a
  wake** in charge mode (keep that mode minimal), but **do** beep/banner on normal
  screens. This interaction with `SAME`/charge mode must be specified.
- **Dual-apply discipline.** Touches `loraPoll()`, `header()`, a settings row, and
  a little state — all must stay byte-identical across `src/*` and `CardSat.ino`
  and pass the brace/parity suite. Modest.
- **Buffer/history bounds.** Ensure the unread counter and history ring can't
  overflow on a burst; clamp `msgUnread` and rely on the existing `msgPush()` ring
  bounds.

---

## 4. Recommendation

Build it — it's a high-value, low-risk awareness layer over an already-working
receiver. Ship as: **a silent header envelope+count (always)**, a **brief
cross-screen banner (default)**, and an **opt-in beep**, with a **Msg notify**
setting (Off / Banner / Banner+Beep). Explicitly specify the **charge-mode
interaction** (silent count update, no forced wake) and note that the underlying
**LoRa hardware path remains untested** until validated on real SX1262 hardware.

> **No code is changed by this document.** Scoping/design only; the LoRa hardware
> path remains host-verified/untested, nothing flashed.
