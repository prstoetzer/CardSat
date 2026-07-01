# LoRa RX / hex monitor (lorarx) — implementation notes

**Status: implemented, host-verified; on-device unverified.** A general-purpose
tool to receive and inspect **any** LoRa signal on the Cap LoRa (SX1262) — not
just satellites. (An earlier, since-removed experiment explored a satellite-specific
receiver; that work is archived separately in
`docs/design/postmortems/LORA_SAT_RX_EXPERIMENT.md` for future reference.) This
feature has **no satellite infrastructure** (no GP/TLE, no Doppler, no external
service, no network).

## What it does

Reachable with **`g`** from the Messages screen. Two sub-screens:

**Config screen** — set the full SX1262 LoRa receive parameter set:
- **Frequency** — **ENTER** on this row opens a free numeric entry to type the
  carrier directly in **MHz** (e.g. `433.775`). `,`//` still nudge it by the current
  step (`s` cycles 1k/10k/100k/1M Hz) for fine tuning. ENTER on any other row starts
  receiving.
- **SF** 7..12
- **Bandwidth** — the full SX1262 LoRa ladder: 7.8, 10.4, 15.6, 20.8, 31.25,
  41.7, 62.5, 125, 250, 500 kHz
- **Coding rate** 4/5..4/8
- **Sync word** (0x00..0xFF; 0x12 private / 0x34 public)
- **Preamble** length (4..64 symbols)
- **CRC** on/off

`;`/`.` move between rows, `,`//` adjust the selected row, **ENTER** starts
receiving and switches to the monitor.

**Monitor screen** — a classic hexdump of received frames. It uses CardSat's
standard title bar (clock + battery) and footer, and **updates automatically as
frames arrive** (the main loop repaints it on a brisk cadence while it's active —
it is not keypress-driven).
- Under the title bar, a status line shows the live freq / SF / BW / CR, a
  **PAUSE / RX / --** state indicator, and a packet counter.
- Each frame is shown as **16 bytes per row: a hex line, with the ASCII line
  directly beneath it** (non-printable bytes shown as `.`), preceded by the byte
  offset. Long frames are stored up to 64 bytes (`trunc` is shown, with the true
  length, if the on-air frame was longer).
- The newest frame is shown; `;`/`.` scroll back/forward through the last 12
  frames (a `-N` marker shows how far back you are).
- **`p` pauses** the display so you can read a frame on a busy channel: the radio
  keeps receiving into the ring while paused, the viewed frame stays frozen, and a
  `+N new` indicator counts frames that arrived while paused. `p` again resumes and
  jumps back to the newest.
- **Live tuning** without leaving the screen: `,`//` nudge the frequency by the
  current step, `f` cycles the step, `s`/`b`/`c` cycle SF/BW/CR — each change
  re-applies to the radio immediately so you can peak a signal. `x` clears the
  buffer.
- **`` ` `` (backtick) or ESC/DEL** returns to the config screen (the radio keeps
  running); pressing it again on the config screen leaves the mode and restores
  CardSat messaging.

## Persistence

All RX parameters are stored in Settings (`lrxFreqKHz`, `lrxSf`, `lrxBwHz`,
`lrxCr`, `lrxSync`, `lrxPreamble`, `lrxCrc`) and restored on the next entry — so
the monitor comes back exactly as you left it. These are **separate** from the
CardSat messaging LoRa parameters, so tuning the monitor never disturbs
messaging.

## Separation & reversibility

- Self-contained in `src/lorarx.{h,cpp}`; the app sees only the `LoraRx` facade.
- **Minimal resident RAM:** the frame ring is a fixed buffer inside the facade
  object (12 frames × 64 bytes); there is **no per-frame heap allocation** and no
  large transient state. Unlike the satellite experiment, there is nothing to
  fetch and no propagator, so this holds essentially constant memory.
- Entering **takes over the shared SX1262** (CardSat messaging RX pauses while
  active; `loraPoll()` is skipped); `exit()` calls `loraStart()` to restore
  messaging. All radio reconfiguration uses the wrapper's shared-SPI/SD discipline
  (`LoraRadio::setRadioRx()`).
- The whole feature is behind **`#if CARDSAT_HAS_LORARX`** (default 1). Setting it
  to 0 compiles it out; every app-side call site is guarded. Helpers are static
  members / methods of `LoraRx`, and the only App-private accesses (`cfg`, `lora`,
  `loraStart`) go through `friend class LoraRx` — so there are no free functions
  reaching into private state (the access-control pitfall that must be avoided in
  the single-.ino build).

## Integration points (for audit / removal)

All guarded by `#if CARDSAT_HAS_LORARX`:
1. `Screen` enum: `SCR_LORARX`.
2. Key/draw dispatch: `case SCR_LORARX`.
3. Service loop: `if (lorarx.active()) lorarx.service(); else loraPoll();`.
4. Launcher `g` in `keyMessages()`; footer hints in `drawMessages()`.
5. `App`: `friend class LoraRx;` + `LoraRx lorarx;` member; `#include "lorarx.h"`.
6. Settings: the `lrx*` fields + their load/save keys.
7. Shared editor: edit code **240** — `App::keyEdit` commits a typed MHz value via
   `LoraRx::setFreqMHz()`, and `editHome(240)` returns to the RX screen.

Reused from the removed experiment (kept because it is generally useful):
`LoraRadio::setRadioRx(freq, sf, bw, cr, sync, preamble, crc)` — full LoRa RX
config with the shared-bus discipline.

## Honest limitations

- **LoRa only** (the SX1262 wrapper is LoRa-mode; FSK/GFSK are not received).
- **Frame store is 64 bytes/frame, 12 frames** — enough to identify and inspect
  most beacons; longer frames are truncated for display (flagged) though the true
  length is shown.
- **Antenna/physics** — reception depends on a suitable antenna for the chosen
  band (e.g. 70cm for 433 MHz) and signal strength. The Cap LoRa's SX1262 has no
  band-pass filter, so any supported frequency works with the right antenna.

## Testing status

- **Host-side:** balance/parity/drift pass; module byte-identical `src`↔`.ino`;
  no duplicate definitions; all referenced symbols present and ordered; all
  App-private access is friend-covered.
- **On-device:** unverified. On a real signal: set the parameters on the config
  screen, ENTER, and frames should appear as hexdumps; use the live `s`/`b`/`c`
  and frequency nudge to peak reception if the parameters aren't exactly known.
