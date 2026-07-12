# CardSat — Scoping a Migration to PlatformIO (macOS)

**Status:** Planning document — and its original motivation is now **superseded**. Nothing here
has been built or verified on-device; treat every command as "to be tried."

**History of the motivation (kept for context):** this was written when HTTPS ran on the Arduino
core's precompiled **mbedTLS**, whose ~32 KB contiguous handshake collided with the resident
sprite. Two later releases resolved that without PlatformIO: **v0.9.43** moved all TLS transport
to **BearSSL** (`ESP_SSLClient`, 16 KB RX buffer), and **v0.9.53** fixed the remaining
multi-batch upload stalls by reclaiming heap (4bpp sprite, on-demand audio, buffer right-sizing)
— confirmed on-device with clean back-to-back LoTW uploads.

**What this migration is still good for:** sdkconfig-level tuning the Arduino IDE can't touch —
raising `TCP_SND_BUF`, trimming WiFi buffer counts, reproducible builds. Optional, not a fix for
a current failure. The paths below are kept as written; read their TLS-buffer rationale as
historical.

---

## 1. The honest headline (read this first)

There is a widespread belief that "switch to PlatformIO, set
`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096`, and TLS uses a fraction of the RAM." **For the
Arduino framework this is false**, in PlatformIO exactly as in the Arduino IDE, and for the
same reason: the Arduino framework ships a **prebuilt** `libmbedtls.a`. Setting the macro in
your sketch or in a config header has *no effect* — multiple confirmed reports show TLS
memory consumption unchanged after setting `MBEDTLS_SSL_IN_CONTENT_LEN` /
`MBEDTLS_SSL_OUT_CONTENT_LEN` / `MBEDTLS_SSL_MAX_CONTENT_LEN` under Arduino. The library was
already compiled at the default 16 KB.

So the migration is worth doing **only if** you take one of the paths in §4 that actually
recompiles mbedTLS from source. A plain "import the sketch into PlatformIO, same Arduino
framework" migration buys you a much nicer toolchain (real editor, CLI builds, version-pinned
reproducible environment) but **does not by itself fix the TLS memory problem.** Be clear-eyed
about which goal you're pursuing before investing the time.

Two further facts that shape everything below:

- **The official PlatformIO `espressif32` platform stopped at Arduino core 2.0.** CardSat is
  pinned on **core 3.2.1** (IDF 5.x). The official platform will not build it. The migration
  must use the community **pioarduino** platform fork, which is the maintained path for
  Arduino core 3.x in PlatformIO. This is not optional.
- The Cardputer's board target in PlatformIO is typically `esp32-s3-devkitc-1` (M5Stack's own
  docs use this for the Cardputer), with explicit USB-CDC flags — there isn't a polished
  first-party `m5stack-cardputer` board definition, so expect to specify the board generically
  and pin the partition/flash settings by hand.

---

## 2. What you gain and what it costs

**Gains (independent of the TLS fix):**
- A real editor (VS Code) with working code navigation across the 40 `src/*.{h,cpp}` files —
  a large quality-of-life jump over the Arduino IDE for a codebase this size.
- Reproducible, version-pinned builds: the platform, core, and library versions are declared
  in `platformio.ini` and committed, so "it built last week" stays true.
- CLI builds (`pio run`) that can be scripted, and a clean separation of source vs. build
  output.
- The migration would let CardSat build from `src/*.{h,cpp}` directly — meaning the
  **monolithic `CardSat.ino` concatenation could eventually be retired**, removing the
  dual-apply discipline that currently governs every code change. (See §6 — this is a real
  secondary benefit for this project specifically.)

**Costs / risks:**
- Learning a new tool with its own failure modes (board IDs, partition files, USB-CDC flags,
  upload modes) — the M5/PlatformIO forums are full of first-build snags exactly here.
- The TLS fix specifically (§4) requires the "Arduino as ESP-IDF component" build, which is
  the most advanced PlatformIO mode and the one most likely to throw obscure build errors.
- You'd be on a community platform (pioarduino), not the official one — well-maintained, but a
  third-party dependency.
- M5Unified / M5Cardputer, RadioLib, and any other libraries must resolve and compile under
  the new toolchain. They almost certainly do (they're standard), but it's work to confirm.
- Verification is still on *your* hardware; this scope cannot prove the build works.

---

## 3. Install PlatformIO on macOS (the toolchain itself — no project yet)

This part is the same regardless of which §4 path you pick later.

1. **Install Visual Studio Code** (the editor PlatformIO lives inside). Download the macOS
   build from the official site, drag to Applications, open it once.
2. **Install Git** if you don't have it: `git --version` in Terminal; if it prompts to install
   the Command Line Tools, accept. pioarduino requires git to function.
3. **Install the PlatformIO extension.** Two choices:
   - The standard **PlatformIO IDE** extension (search "PlatformIO IDE" in the VS Code
     Extensions panel), **or**
   - The **pioarduino IDE** extension — a community fork of the PlatformIO extension that is
     pre-tuned for the pioarduino platform and Arduino core 3.x. For this project the
     pioarduino extension is the smoother path since you'll use the pioarduino platform anyway.

   Pick one; don't install both. Installing the extension also installs the PlatformIO core
   (the `pio` CLI) into `~/.platformio`.
4. **Confirm the CLI works.** Open VS Code's integrated terminal and run `pio --version`. (If
   you prefer a pure-CLI install without VS Code, pioarduino provides a `get-platformio.py`
   bootstrap script, but VS Code is the recommended route for a first-time user.)

At this point you have the toolchain but no CardSat project yet.

---

## 4. The three real paths (choose one)

### Path A — Lift-and-shift onto pioarduino, keep the Arduino framework
**Fixes the TLS problem?** ❌ No. **Effort:** Low. **Risk:** Low.

This is "same code, same Arduino framework, nicer toolchain, reproducible build." It does
**not** shrink the TLS buffers (prebuilt mbedTLS), so uploads still depend on the
contiguous-block headroom — i.e. you still rely on the `LOG_VIEW_MAX` reclaim already shipped.
Worth doing purely for the editor/build improvements, or as **step one** before attempting
Path B or C.

Sketch of `platformio.ini`:

```ini
[env:cardsat]
; Community platform — REQUIRED for Arduino core 3.x (official one stops at 2.0).
; Pin to a specific pioarduino release rather than "stable" for reproducibility;
; check the pioarduino releases page for the version whose Arduino core == 3.2.1.
platform = https://github.com/pioarduino/platform-espressif32/releases/download/<PINNED>/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino

; Cardputer specifics (from M5Stack's own Arduino guidance):
upload_speed = 1500000
monitor_speed = 115200
build_flags =
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DARDUINO_USB_MODE=1
  -DCORE_DEBUG_LEVEL=3

; No PSRAM on the S3FN8 — do NOT add -DBOARD_HAS_PSRAM.
board_build.flash_mode = dio
board_build.flash_size = 8MB
; Match the partition layout the project already uses (default_8MB.csv equivalent).
board_build.partitions = default_8MB.csv

; Libraries (confirm exact names/versions against what the project uses today):
lib_deps =
  m5stack/M5Cardputer
  ; M5Unified is pulled in by M5Cardputer; RadioLib 7.7.1, etc.
```

Then drop `CardSat.ino` (or the `src/` tree — see §6) into the project's `src/` folder, fix
include paths, and `pio run`. The verification target is simply: does it build and run with
behavior identical to the Arduino IDE build?

### Path B — Arduino as an ESP-IDF component, with a custom sdkconfig
**Fixes the TLS problem?** ✅ Yes (this is the one that recompiles mbedTLS). **Effort:** High.
**Risk:** Medium-High.

This is the path that actually shrinks the handshake's contiguous requirement. Instead of the
prebuilt Arduino libraries, the whole stack — including mbedTLS — is **compiled from source**
as part of your build, so an `sdkconfig` change to the TLS record length takes effect. The
pioarduino platform supports this "hybrid" / "Arduino as component" mode and even publishes a
`pioarduino/sdkconfig` repo of settings for exactly this purpose.

The decisive settings (these are what the Arduino framework bakes in at 16 KB and you'd
override):

```
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y      # optional: free TLS buffers between records
```

Dropping the IN/OUT content length from 16384 to 4096 turns the handshake's ~32 KB
contiguous demand into well under ~10 KB — which makes the 55–60 KB of fragmented free heap
genuinely usable for TLS, and the whole sprite-vs-handshake contiguous-block problem
disappears. (4096 is safe for these endpoints: Cloudlog and LoTW responses are small and the
servers don't push 16 KB TLS records. If any server ever needs a bigger record, mbedTLS
returns a clear protocol error rather than corrupting anything, and the value can be raised.)

**Why "Medium-High" risk:** this is the most advanced PlatformIO build mode. Getting Arduino +
IDF-component + a custom `sdkconfig.defaults` to compile cleanly for an S3 no-PSRAM target is
exactly where obscure errors live (missing partition CSV references, sdkconfig key drift
between IDF versions, USB-CDC interactions). Budget real time for first-build debugging, and
expect to iterate against the pioarduino wiki and forums. The **payoff is permanent**: it
fixes the root cause rather than managing headroom.

### Path C — Full ESP-IDF (drop the Arduino framework entirely)
**Fixes the TLS problem?** ✅ Yes. **Effort:** Very High (likely a rewrite). **Risk:** High.

Mentioned only for completeness. CardSat is built entirely on Arduino/M5Unified APIs
(`M5Cardputer`, `WiFiClientSecure`, `HTTPClient`, `String`, the M5Canvas sprite, RadioLib's
Arduino bindings). Going pure-IDF means replacing or re-bridging all of that. This is a
months-scale rewrite with no benefit over Path B for the TLS goal. **Not recommended.**

---

## 5. Recommended sequence

1. **Do Path A first.** Stand up the pioarduino toolchain, get an identical-behavior build of
   the *current* firmware in VS Code, and confirm flashing works on macOS. This de-risks
   everything: you learn the tool on known-good code, with the `LOG_VIEW_MAX` fix already
   covering uploads. If you stop here, you've gained the editor and reproducible builds.
2. **Then attempt Path B on a branch.** Switch to the Arduino-as-component build, add the
   `sdkconfig` overrides for the 4 KB TLS buffers, and rebuild. Verify on the bench that an
   upload now succeeds **with the drawing sprite resident and no `LOG_VIEW_MAX` reduction**
   (you could even restore `LOG_VIEW_MAX` to 60 as the proof). Log the largest-contiguous-block
   before the handshake — it should no longer matter, because the handshake now needs <10 KB.
3. **If Path B lands, the headroom problem is permanently solved** and you can stop hand-tuning
   static RAM for TLS. If it proves too fiddly, you fall back to Path A + the existing
   `LOG_VIEW_MAX` reclaim, which already works.

---

## 6. Project-specific note: the `CardSat.ino` concatenation

CardSat currently maintains **two** representations of every module: `src/*.{h,cpp}` and a
monolithic `CardSat.ino` that concatenates all of them, kept byte-identical by a strict
dual-apply + parity-check discipline (the Arduino IDE compiles the `.ino`). PlatformIO compiles
`src/*.{cpp}` directly — it has no need for the concatenated `.ino`. So a successful migration
(any path) opens the door to **retiring `CardSat.ino` entirely** and building from `src/` alone,
which would eliminate the dual-apply rule and the `parity.py` check from every future change.

Treat this as a *follow-on* cleanup, not part of the migration itself: keep both
representations through the migration so you can A/B the PlatformIO build against the Arduino
IDE build, and only delete `CardSat.ino` once the PlatformIO build is trusted as the source of
truth. (`balance.py` stays useful regardless; it's the `.ino`-vs-`src` parity machinery that
becomes unnecessary.)

---

## 7. What still cannot be promised

- **None of this is verified.** Versions, board IDs, partition names, and sdkconfig keys drift
  between platform releases; every command here is a starting point to be confirmed against the
  current pioarduino release notes and wiki at the time you do the work.
- The exact pinned pioarduino release whose bundled Arduino core equals **3.2.1** must be looked
  up on the pioarduino releases page — don't assume "stable" matches your pinned core.
- Library resolution (M5Cardputer/M5Unified, RadioLib 7.7.1, and anything else) needs to be
  confirmed to compile under the new toolchain; almost certainly fine, but it's real work.
- On-device behavior — uploads, CAT, LoRa, the display — remains the authoritative test, exactly
  as it is today. The bench (IC-821H + scope) and a real Cloudlog/LoTW upload are the proof.

---

## 8. One-paragraph recommendation

If the goal is **"stop fighting the TLS contiguous-block problem forever,"** Path B is the only
thing that actually does it, and it's worth the effort — but get there in two steps: migrate to
the pioarduino Arduino framework first (Path A) to learn the tool on known-good code, then take
the Arduino-as-component + 4 KB-TLS-buffer step (Path B) on a branch and prove it on the bench.
If the goal is only **"a nicer development environment,"** Path A alone is a clear win and low
risk, and the already-shipped `LOG_VIEW_MAX` reclaim keeps uploads working without any of the
deeper changes.
