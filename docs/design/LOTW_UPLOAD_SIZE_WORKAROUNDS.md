# LoTW Upload Size — Root Cause, PlatformIO Migration, and On-Platform Workaround

## The problem, stated precisely

LoTW `.tq8` uploads stall mid-body and the connection half-closes. On-device
instrumentation (the `write()`-return diagnostic added in 0.9.42) showed:

```
POST TLS connected in 338ms; streaming request
write() returned 0 at off 0/1024 (connected=0, avail=0)
writeAll TIMEOUT after 30s at off 0/1024
writeAll failed at streamed 6144/9080
```

Two separate uploads stalled at **5120** and **6144** bytes — straddling the
ESP32 lwip default **`TCP_SND_BUF` ≈ 5744 bytes**.

### What was ruled out (with evidence)

- **Server-side rejection / size limit** — DISPROVEN. From a host, LoTW accepts a
  9 KB multipart body over HTTP/1.1 with `Connection: close` and our exact
  User-Agent, returns `HTTP 200`, even throttled to 2 KB/s. The server is patient
  and accepts our exact request shape at any speed.
- **TLS 1.3 large-body send bug** — DISPROVEN. arduino-esp32's `WiFiClientSecure`
  runs TLS **1.2**, not 1.3 (confirmed in the core; the examples report "running on
  TLS 1.2"). Pinning to 1.2 would be a no-op.
- **SD short-read** — DISPROVEN. `f.read()` never reported 0; the diagnostic would
  have printed `f.read() returned 0 ... (SD short read)`.
- **Buffering the body in RAM instead of streaming** — would NOT help. The stall is
  in the SEND path (`client.write`), which is identical whether the source is a file
  or a RAM buffer.

### Root cause

The ESP32 lwip TCP send buffer (`TCP_SND_BUF`, default ~5744 bytes) fills when the
application writes the body faster than the peer (LoTW behind an AWS ELB,
`54.85.x`) drains it. Once full, `client.write()` returns 0. If the queued data
isn't pushed, the peer never ACKs, the window never reopens, and after ~30 s the
socket half-closes (`connected=0`). Cloudlog's 3851-byte body fits **under**
`TCP_SND_BUF`, which is exactly why Cloudlog uploads always succeeded while LoTW
(≈9080 bytes for 14 QSOs) failed. The two are otherwise byte-for-byte the same
streaming code path.

### The 0.9.42 first fix (in this build)

On a full-buffer `write()==0`, call `client.flush()` to force the queued segments
out so the peer can ACK and reopen the window, then retry. Compile-safe; relies on
`WiFiClientSecure::flush()` actually pushing a TCP segment. **If `flush()` is a
partial no-op on this core, the stall persists and we fall to the options below.**

---

## Option A — On-platform workaround: reboot-per-batch small uploads

### Why "just loop and send smaller chunks in one session" does NOT work

The obvious idea — split the QSOs into sub-5744-byte `.tq8` files and POST them in
a loop — fails on a constraint this project already hit and solved elsewhere:

> **Multiple cold-start TLS handshakes cannot run in one session on this
> no-PSRAM part.** The first handshake needs ~32 KB contiguous; post-handshake
> heap fragmentation collapses the largest block to ~18–23 KB, so a *second*
> handshake in the same session fails with `SSL - Memory allocation failed`.

This is the same "-1 wall" that the **"Cache all transponders"** feature works
around: it does **one batch per boot, then `ESP.restart()`**, and
`resumeCacheIfPending()` in `setup()` runs the next batch in a pristine boot
(`src/app.cpp`, `resumeCacheIfPending` / `startBatchedCache`). Each batch gets a
fresh ~32 KB block because a cold boot has no prior TLS fragmentation.

> NOTE: within a single session, back-to-back streamed *fetches* (GP, weather,
> QRZ, Cloudlog) DO succeed repeatedly — because they reuse the settled
> post-first-handshake heap layout (~21 KB block), which is enough for the
> *download* handshakes. The LoTW *upload* is different only in body size vs.
> `TCP_SND_BUF`, not in handshake feasibility. So the multi-handshake limit is the
> constraint for *many sequential uploads*, not for one upload after a fetch cycle.

### Design: reboot-per-batch LoTW upload (mirrors the transponder cache)

The existing LoTW flow already:
- caps a run at 50 QSOs and leaves the remainder un-flagged for "next upload"
  (`doLotwUpload`, `const int CAP = 50`), and
- already reboots for the *passphrase* case (`resumeLotwIfPending`,
  `lotwRebootPrompt`).

Extend it to a **size-bounded, reboot-driven multi-batch**:

1. **Sizing.** Choose a per-batch QSO count whose *compressed* `.tq8` body stays
   safely under `TCP_SND_BUF`. Empirically ~9080 bytes for 14 QSOs ⇒ ~650 B/QSO
   compressed. Target ≤ ~4500 bytes of body ⇒ **~6 QSOs per batch** (leave margin
   below 5744 for the multipart headers/tail, ~250 bytes). Make this a constant
   `LOTW_BATCH_QSOS = 6` so it's tunable if the flush fix shifts the ceiling.
2. **Batch marker in NVS.** Persist a resume cursor (e.g. `lotwBatchStart`) the way
   the transponder cache persists its resume index. On "upload all", set the cursor
   to the first un-uploaded QSO and reboot.
3. **`resumeLotwIfPending()` in `setup()`** (already called at line 257): if a batch
   is pending, in the pristine boot sign + upload exactly `LOTW_BATCH_QSOS`
   un-uploaded QSOs, mark them uploaded on `.UPL.` acceptance, advance the cursor,
   and `ESP.restart()` to do the next batch — until no un-uploaded QSOs remain.
4. **Progress + safety.** Show "LoTW batch k/N" on the boot screen. Guard against an
   infinite reboot loop exactly as `resumeCacheIfPending` does: if a batch marks
   zero QSOs uploaded (server error, bad passphrase), stop and show the error
   instead of rebooting again.
5. **Passphrase.** The `.tq8` signer needs the key passphrase each batch. Either
   (a) re-prompt after each reboot (secure, tedious), or (b) hold the passphrase in
   RTC memory across the deliberate `ESP.restart()` (survives soft reset; cleared on
   power loss) so a multi-batch run is one prompt. (b) is the better UX; document
   the RTC-retention security tradeoff in `MANUAL.md`.

### Cost / risk of Option A

- **Effort:** moderate. It reuses three existing mechanisms (the 50-cap gather, the
  reboot-resume pattern from the transponder cache, the passphrase-reboot path).
  Main new work: the NVS/RTC cursor, the per-batch size bound, and the boot-time
  resume loop.
- **UX:** each batch = one reboot (~2–3 s). 14 QSOs ⇒ ~3 batches ⇒ ~10 s of reboots.
  Acceptable for a log-upload action; not great for hundreds of QSOs.
- **Risk:** low and well-trodden — this is literally how transponder caching already
  works on this exact hardware. The one genuinely new risk is passphrase retention
  across reboot (RTC) if we choose UX (b).
- **Reversibility:** if the flush fix (or PlatformIO) later removes the size ceiling,
  set `LOTW_BATCH_QSOS` very high (or bypass the batch path) and the reboot loop
  collapses to a single upload.

---

## Option B — PlatformIO migration (the permanent cure)

Documented in `docs/guides/PLATFORMIO_MIGRATION.md`. Summary of what it buys and
what it costs, specific to the two no-PSRAM problems this session surfaced (the TLS
handshake ~32 KB contiguous requirement, and now the `TCP_SND_BUF` send ceiling).

### What the migration changes

Move the build from Arduino IDE (fixed, pre-compiled core + fixed `sdkconfig`) to
**PlatformIO with the "Arduino-as-an-ESP-IDF-component" layout**, which recompiles
the core against a **project-owned `sdkconfig`**. That unlocks the settings that are
baked-in and unreachable today:

1. **`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` / `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN`**
   (default 16384 each). Dropping these to **4096** cuts the TLS record buffers from
   ~32 KB to <10 KB, so the handshake no longer needs a ~32 KB contiguous block.
   This removes the entire "second handshake in a session fails" class of bug — and
   with it, the *reason* Option A must reboot per batch. (LoTW's server supports
   these smaller record sizes; standard servers do.)
2. **`CONFIG_LWIP_TCP_SND_BUF_DEFAULT`** (the `TCP_SND_BUF` ceiling, ~5744 today).
   Raising it (e.g. to 11–16 KB) lets a full LoTW body sit in the send buffer at
   once, eliminating the ~5744 stall directly — no flush gymnastics, no batching.
3. **`CONFIG_LWIP_TCP_WND_DEFAULT` / `CONFIG_LWIP_TCP_RECV_MSS`** — matching receive
   window / MSS tuning if downloads ever need it (they don't today; downloads
   stream to file fine).
4. Optional: **`CONFIG_MBEDTLS_DYNAMIC_BUFFER`** — allocate TLS buffers only for the
   negotiated sizes, further easing the no-PSRAM squeeze.

### What the migration costs

- **Build system rework.** New `platformio.ini`, the Arduino-as-component wrapper,
  and a project `sdkconfig.defaults`. The monolithic `CardSat.ino` + `src/*` dual
  representation still builds under PlatformIO (it compiles `src/`), but the
  **dual-apply discipline and the `.ino`-for-Arduino-IDE deliverable would need a
  decision**: keep shipping the single `.ino` for Arduino-IDE users AND a PlatformIO
  project, or move the canonical build to PlatformIO and treat the `.ino` as a
  generated/secondary artifact. This is the biggest non-code decision.
- **Toolchain pinning.** Must pin the ESP-IDF version and the Arduino-as-component
  version to a known-good pair (the ES8311 mic needs IDF 5.4.x per the voice-memo
  notes; 5.5.x regressed it — so the migration must NOT drag the mic onto 5.5.x).
  This is a real constraint: the sdkconfig freedom is gained only on an IDF version
  that still supports the mic.
- **Flashing/distribution.** M5Burner and the bmorcelli Launcher `.bin` flow need
  re-validation against the PlatformIO output (partition layout, merged-bin offset).
- **Testing surface.** Every network path (all the fetches, Cloudlog, LoTW), plus
  audio, LoRa, and rotator I/O, re-tested on the new core build. Non-trivial but
  bounded.
- **Effort:** substantial — days, not hours — and it touches the release/distribution
  pipeline, not just code. But it is the *only* path that removes the root cause for
  the whole no-PSRAM TLS-contiguous-block AND send-buffer class of problems, rather
  than working around each one.

### Risk

- **Medium-high**, mostly in the build/distribution rework and the IDF-version /
  mic-compatibility pin, not in the sdkconfig values themselves (those are
  well-understood). Mitigate by doing it on a branch, keeping the Arduino-IDE `.ino`
  build working in parallel until the PlatformIO build is validated end-to-end on the
  bench (IC-821H) and via M5Burner.

---

## Recommendation

1. **Ship the `flush()` fix first** (this build). If LoTW now uploads the full body,
   the whole problem is closed for typical batch sizes and neither option below is
   needed near-term.
2. **If flush is insufficient:** implement **Option A (reboot-per-batch)** as the
   pragmatic on-platform fix. It's low-risk, reuses the proven transponder-cache
   reboot pattern, and ships without touching the build system. Set
   `LOTW_BATCH_QSOS` conservatively (~6) and refine against on-device logs.
3. **Plan Option B (PlatformIO) as the strategic cure** on its own branch, scheduled
   deliberately — it removes the root cause for both the handshake-contiguity and
   the send-buffer ceilings, at the cost of a build/distribution migration and an
   IDF-version pin that must preserve ES8311 mic support (IDF 5.4.x).

The two are complementary: Option A makes LoTW work *now*; Option B makes the
workaround unnecessary *later* and simplifies the code (the reboot loop can be
retired once the sdkconfig ceilings are raised).
