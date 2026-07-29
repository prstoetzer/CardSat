# Source-comment audit — 0.9.68 cycle

Companion to DOC_AUDIT_0_9_68.md: an audit of source-code comments for correctness
against the current code and the currently pinned libraries. Method: automated
cross-check sweeps (numeric claims vs constants, version references vs installed
libraries, comment-referenced symbols/paths vs reality, TODO inventory, SCR_ name
validity), then targeted reads of the high-churn areas (feeds, USB host, charge/
sleep, Telnet, AO-7 estimator) with every suspect claim verified against source —
including the installed third-party library source where the claim was about it.

## Fixed

### USB host region (usbserial.cpp / usbserial.h / rotator.cpp)

The largest cluster. The file's forensic comments were written across the
EspUsbHost 2.3.x era and partially updated at 2.4.1; the pinned library is 2.5.2.

1. **Orphaned finisher rationale.** The "Why the library's own uninstall does not
   stick (the 259)" block introduced a hand-rolled drain-and-uninstall pass
   ("finish the job here…") that no longer exists — `usb_host_lib_handle_events`
   appeared only in comments; `finishUninstall()` was removed in fix37. Rewritten
   as an explicit HISTORY block: the fix32 mechanism (IDF v5.4 `usb_host.c` flag
   semantics — still accurate and worth keeping), why the finisher existed, and
   that 2.4.1+ performs the same handshake internally — with the pinned 2.5.2
   source-checked: teardown is split into `releaseClientResources()` /
   `uninstallHostLibrary()` with the uninstall result checked and logged.
2. **"The escape hatch EspUsbHost omits."** 2.5.2's `end()` calls
   `usb_host_lib_unblock()` itself (verified in the installed source, along with
   the 3 s wait, the timed-out-tasks-left-alive path, and the
   begin()-over-live-handles refusal). The claim was 2.3.x-only.
3. **Vestigial includes removed.** `<usb/usb_host.h>` and `<esp_timer.h>` existed
   to serve the removed poke/finisher; every `usb_host_*`/`esp_timer_*` token in
   the file (and in the whole monolith) is comment-only. Removed from **both**
   representations — the `.ino` prologue carried its own copies with the same
   stale comment, which a src-only fix would have missed.
4. **fix37 paragraph** claimed present-tense "EspUsbHost::end() kills the CLIENT
   task first" — true only pre-2.4.1; reframed as the old library's behavior.
5. **Eleven "2.4.1" annotations** describing *ongoing* library behavior updated to
   "2.4.1+" (with the end()-banner noting the 2.5.2 pin); the one genuinely
   historical sentence ("EspUsbHost 2.4.1 fixes the ordering") kept as-is.
6. **rotator.cpp banner** said the hard parts live in usbserial "(resident host,
   …)" — the resident-host design is itself history (see below); now
   "(shared-host lifecycle, …)".
7. **usbserial.h** pointed readers at `usbLastError()`, which does not exist; the
   accessor is `lastError()`.

### Other source fixes

8. **`/api/orbit` banner** said it serves "the same values the nine on-device
   Orbit pages show" — there are eleven pages; made count-free (the parenthetical
   already scopes the content).
9. **`/api/status` comment** referenced `docs/interfaces/API_STATUS.md`, which
   does not exist; the stable contract lives in `docs/interfaces/WEB_API.md`
   ("Stable extension (0.9.62)"). Path corrected.
10. **app.h** "the six mini-games" → seven.
11. **app.h `dxcBandFilterPrev`** commented as the "'b' band-step key" — the key
    is 'n' ('b' is the global screenshot hotkey, as keyDxc itself documents).
12. Grammar: "as a Kenwood's station list" → "as a Kenwood rig's station list".
13. **American-English stragglers** the 488-replacement pass missed: the
    -ise family extensions (quantise/normalise/summarise/stabilise and friends) —
    13 further replacements across src, the monolith, and MANUAL.md.

## Verified correct (no change)

- **Feed size-math comments**: `AprsSta` 33 B → 36 aligned, `DxSpot` 67 B → 72
  aligned (200 × 72 ≈ 14 KB), `Aircraft` 34 B → 36 — all three cap comments
  arithmetically right, including the DXC "lowered from 250 for the largest
  contiguous block" rationale.
- **DXC wire-order comments** (both the sniffer and the parser) carry the
  corrected 0.9.67 field order with the Country-column ground-truth note.
- **keyDxc "24 bands"** = `DXC_BANDS[]` count. **"five named QTH presets"** =
  `qthName[5]`. **STAR_N = 1018** matches the on-device claim. **MAX_SATS = 150**
  — the "150-sat catalog" comments I initially suspected are correct.
- **AO-7 estimator comments**: 15-minute resolution (HH:30 normalization, 900 s
  RMS floor), coarse steps 5 min / 30 min (`PSTEP_C`/`TSTEP_C` = 300/1800), and
  the two-stage coarse-then-refine structure all match the code.
- **PA_ITEMS ↔ keyPrintAbout**: 30 labels, 30 unique `PR_` mappings — the "keep
  in sync" contract holds. **GAMES launcher**: cases 0–6 for the seven games.
- **telAnsi vs telRemoteByte**: both namings are real (state variable vs
  function); neither comment is stale.
- **Charge/sleep and aprs.fi comments** are properly framed rejected-alternative
  rationale (why light sleep was removed; why APRS-IS instead of aprs.fi) — kept.
- **usbserial ODR/build_opt.h banner**: `build_opt.h` exists with
  `-DESP_USB_HOST_MAX_DEVICES=4`, and the platform.txt claim names the actual
  core (3.2.1).
- Sweeps that came back clean: zero real TODO/FIXME markers; zero phantom
  `SCR_` names in comments; all `docs/`/`tools/` paths in comments exist except
  the one fixed above; no lingering unverified count-claims.

## A note for future sessions (memory correction)

Session notes carried "EspUsbHost cannot cleanly release its USB client; the host
stays resident for firmware lifetime; end() only detaches the CDC port." That
describes the **0.9.58 resident-host design against the 2.3.x library** and is
narrated as history in end()'s own comments. Current code performs a **full
teardown** through the library's fixed `end()` (drain → deregister → checked
uninstall), guarded by the M2 `ESP_ERR_TIMEOUT` path (0.9.66): on timeout the
host object is retained, reboot-required is latched, and the console stays down.
The host is torn down only when no port (CAT or rotator) remains.

## Proof of behavioral neutrality

The post-audit rebuild was compared byte-for-byte against the pre-audit
0.9.68-wip binary (same size, 3,003,616 B). Exactly 65 bytes differ, in two
clusters: offsets 177–208 — the 32-byte `app_elf_sha256` field inside the ESP-IDF
`esp_app_desc_t` (comment edits shift ELF line tables, so the ELF's hash changes)
— and the trailing 33 bytes, the appended image SHA-256 (plus its padding byte)
that covers the header. A full `strings` diff of the two binaries is empty. Every
byte of executable code, rodata, and string content is identical, which is the
strongest available evidence this audit changed documentation only. All 14 static
gates pass; the rebuild is EXIT=0 with unchanged flash/RAM figures.
