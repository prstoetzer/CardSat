# Heap optimization: proactive WiFi-cycle defrag (0.9.41)

This note documents the last-resort WiFi disconnect/reconnect that
`Net::reclaimHeapForTls()` can perform before an mbedTLS handshake, why it exists,
how it is gated, and **how to disable it** if it ever causes trouble.

## The problem it addresses

On the no-PSRAM ESP32-S3 the mbedTLS handshake's first allocation is a single
~16.7 KB contiguous block (the 16 KB RX TLS record buffer). Over a long session of
repeated HTTPS fetches the heap fragments until the largest *contiguous* free block
falls below `TLS_MIN_BLOCK` (28 KB) even when plenty of total heap is free, and the
next `connect()` then fails. The existing `reclaimHeapForTls()` waits passively
(~600 ms) for in-flight frees to land and coalesce — but a passive wait only helps if
the stack happens to free things on its own in that window.

## What the cycle does

When the passive wait still leaves the largest block below `TLS_MIN_BLOCK`,
`reclaimHeapForTls()` calls the existing `hardResetWifi()` exactly once. That does a
`WiFi.disconnect(true)` (radio off) + `WiFi.reconnect()`, which forcibly returns the
whole LWIP socket pool and the mbedTLS async buffers to the heap, so adjacent free
blocks merge into one large enough for the handshake. It then re-measures and returns
the largest block to the caller, which still declines gracefully if it is *somehow*
still short.

This is the same mechanism the on-demand `heapDefragViaReconnect()` already used; the
0.9.41 change is making it fire **automatically** inside the pre-handshake path.

## How it is gated (why it is safe)

The cycle only runs when **all** of these hold:

- `Net::TLS_WIFI_CYCLE` is `true` (the master toggle — see "How to disable").
- The largest block is *still* below `TLS_MIN_BLOCK` after the passive wait. On a
  healthy fetch the block is fine and the cycle never runs, so the common path pays
  nothing.
- `WiFi.status() == WL_CONNECTED` — there's a live association to recycle. (No point
  disconnecting if we're already down; that's the `connFails`/`hardReset` recovery's
  job.)
- At least `WIFI_CYCLE_MIN_GAP_MS` (30 s) has elapsed since the last proactive cycle
  (`lastWifiCycleMs` guard). This stops it from firing repeatedly across a burst of
  chained fetches and, crucially, stops it from **fighting** the existing
  `connFails` → `hardResetWifi()` socket-pool recovery (the two share the WiFi reset
  so they must not both trigger in quick succession).

## Tradeoffs

- **Cost:** a full disconnect/reconnect is ~2–5 s (association + auth + DHCP). That's
  why it's gated to fire only when really needed, never on a healthy fetch.
- **Risk:** the reconnect can itself fail (bad moment in the AP's DHCP lease, transient
  RF). `hardResetWifi()` bounds the reconnect to 12 s and returns failure; the caller
  then declines the handshake just as it would have anyway, so we are no worse off than
  without the cycle — and the existing `recoverExhausted` → reboot-prompt path remains
  the backstop if resets keep failing.
- **Interaction:** a mid-session WiFi bounce also drops the rigctld/rotctld/webd
  listeners and any CAT-over-WiFi session for its duration. The TLS-busy trampoline
  already drops listeners around fetches, but the cycle's outage is longer than a normal
  fetch, so it could briefly hiccup an active networked rotator/CAT session. The 30 s
  gap and "only when actually short" gating keep this rare.

## How to disable (revert)

Set the master toggle off:

```cpp
Net::TLS_WIFI_CYCLE = false;   // in net.cpp, or at runtime before fetches
```

With it `false`, `reclaimHeapForTls()` reverts to exactly the prior behavior:
passive coalesce-wait only, then decline if still short. The gating members
(`WIFI_CYCLE_MIN_GAP_MS`, `lastWifiCycleMs`) become inert. No other code depends on
the cycle, so this is a clean revert. The `BEGIN/END 0.9.41 proactive WiFi-cycle`
comment markers in `net.cpp` bracket the code to remove entirely if a hard revert is
preferred.

## Verifying it on hardware

The host cannot measure the real `largest_free_block`, so confirm on-device by watching
the serial log around a fetch that previously failed: look for
`[net] proactive WiFi-cycle defrag (largest N < 28000)` followed by
`[net] post-cycle largest block M` with `M` risen above the floor. If the cycle fires
but `M` doesn't clear the floor on your AP/IDF, the cycle isn't helping on that setup
and can be disabled.
