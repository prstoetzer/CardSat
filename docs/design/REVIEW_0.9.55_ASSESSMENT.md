# Assessment of the v0.9.55 Codebase & Documentation Review

*Response to the external static review (July 14, 2026) covering architecture, memory
lifetime, networking, downloads, radio-control, build reproducibility, and doc/source
consistency. Every technical finding below was re-verified against current source before
being accepted. Bottom line: the review is accurate and fair — most findings check out
against the code, and the two top-priority items (insecure TLS on credential paths, and
non-transactional/oversized downloads) are real correctness/security bugs that should be
fixed before 1.0.*

## Verification summary

| # | Finding (severity) | Verified? | Notes |
|---|---|---|---|
| 2 | Insecure TLS on all HTTPS (Critical) | **Confirmed** | `setInsecure()` at net.cpp:307/453/726/960/1118; QRZ/Cloudlog/LoTW credentials transit these paths. |
| 3 | 400 KB download cap vs "multi-MB" claim (High) | **Confirmed** | `httpsGetToFileRetry(url, path, 400000, …)` (net.cpp:640) vs README:42 "multi-megabyte group." |
| 4 | Downloads not transactional (High) | **Confirmed (partial)** | Some paths guard against caching a truncated catalog, but the destination can be opened for write before validation; not a clean tmp→rename→promote. |
| 1 | ~41–49 KB always-resident screen state (High) | **Plausible, not measured** | The arrays named all exist in the global `App`; exact bytes need a linker map. Direction is correct. |
| 5 | Retry error-string mismatch (Medium) | **Confirmed** | net.cpp:529 emits `"file too big for storage (…)"`; net.cpp:616 checks `lastErr == "low flash"` — never matches, so the immediate-abort never fires. |
| 6 | LAN control assumes trusted network (Medium) | **Confirmed** | Web control / network CAT / rotator listeners have no auth; they actuate hardware. |
| 7 | Build reproducibility weak (Medium) | **Confirmed** | `platformio.ini` has minimal version pinning; no committed lockfile / linker-map archiving. |
| 8 | 32-bit Hz caps at 4.29 GHz (Medium) | **Confirmed** | `Transponder` freqs are `uint32_t` Hz (satdb.h:21–23); 24 GHz/47 GHz microwave can't be represented. |
| 9 | MAX_SATS 150 vs 220 docs (Medium) | **Confirmed** | config.h:181 = 150; README:290 and CODE_REFERENCE:67 say 220. |

Nothing in the review was found to be wrong. A few items are estimates (the exact idle-RAM
figure) that need on-device measurement to pin down, but the *direction* of every finding is
correct.

## What belongs in 0.9.56 vs later

This review is broad — it spans security, data integrity, a large memory refactor, and build
process. Trying to do all of it in one release would be a mistake (the memory refactor alone
is a multi-week architectural change with real regression risk). Recommended split:

### 0.9.56 — correctness & honesty (bounded, low-regression-risk)

These are small, self-contained, and high-value. None require hardware to validate host-side.

1. **Fix the retry error-string mismatch (#5).** Introduce a typed `DownloadError` enum and
   branch on it instead of matching display text. Immediate win, removes a real logic bug.
2. **Make GP downloads transactional (#4).** Download to `<path>.tmp`, verify
   length/completion, parse-check, then atomically rename into place; on any failure keep the
   old catalog and delete only the temp. This protects the one piece of state a field user
   can't easily re-fetch without signal.
3. **Fix the 400 KB cap (#3).** Replace the fixed cap with a storage-budget preflight
   (declared Content-Length vs free space) and an explicit "download exceeds storage" error
   for unknown-length responses. Removes the silent conflict with the advertised catalog size.
4. **Reconcile the doc/config contradictions (#9).** MAX_SATS to a single source of truth
   (150) across README, CODE_REFERENCE, and the manual; refresh the stale CODE_REFERENCE
   constants; name the current TLS wrapper; update the CI-V comment to cite the tested IC-821
   (model/wiring/voltage/baud/firmware). Same class of work as the printing-doc reconciliation
   already done for 0.9.56.
5. **Document the frequency ceiling (#8).** State the 4.29 GHz `uint32_t` Hz limit explicitly
   in the docs. (Migrating to 32-bit kHz storage is a larger, data-format change — defer the
   *migration* but ship the *disclosure* now.)

### Before 1.0 — security (needs design, moderate effort)

6. **TLS certificate validation (#2, Critical).** This is the most important finding, but it
   is not a one-liner: it needs a CA bundle (or pinned roots) plus hostname validation, a
   fingerprint-pinning path for self-signed LAN services, and an explicit opt-in "insecure
   compatibility" mode so public-data fetches still work on constrained setups. Credential-
   bearing services (QRZ, Cloudlog, LoTW) must not silently use insecure TLS. Worth its own
   focused release so it gets the testing it deserves.
7. **LAN control hardening (#6).** Disable command interfaces by default; add a token/password,
   subnet restriction, an on-device "listening" indicator, a read-only mode, and an emergency
   server-disable. These actuate real antennas and radios, so the bar is higher than for an
   info page.

### A dedicated RAM release — architectural (highest effort, highest regression risk)

8. **Lifecycle-based screen state (#1).** The review's central architectural point is sound: a
   `ScreenScratch` union / arena for mutually-exclusive foreground screens (overhead, pass
   plots, planner, target, rove, sky, Wi-Fi scan, equator, illumination) would let the largest
   *active* screen occupy RAM instead of the *sum* of all of them, plausibly reclaiming 25–35
   KB at idle. But this touches many screens and their state assumptions; it deserves its own
   release with heap-telemetry before/after and careful regression testing, not a rushed
   inclusion. Sequence the safe, isolated ones first (share the two 128-entry pass arrays;
   allocate the memo directory only while browsing; allocate transponders only when loaded).

### Build process — ongoing

9. **Pin the build (#7).** Pin the PlatformIO platform, every library version, and Git deps by
   commit; archive the resolved graph, linker map, and size report per release. This also makes
   the RAM-release measurements (#1) trustworthy and comparable. Low risk, do it alongside any
   release.

## Why this sequencing

- The **0.9.56 bucket** is all bounded, host-verifiable, low-regression work that removes real
  bugs and doc contradictions — the same kind of correctness pass 0.9.56 already did for
  printing.
- **Security (TLS, LAN)** is high-value but needs real design and testing; rushing it risks
  either breaking public-data fetches (over-strict TLS) or shipping a half-measure. It earns a
  focused release.
- The **RAM refactor** is the biggest prize but the biggest risk; it should be measured, staged,
  and regression-tested on its own, not bundled with correctness fixes.

## Note on the RAM figures

The ~41–49 KB estimate is credible but unmeasured. Before committing to the refactor, capture a
**linker map** and add `sizeof` instrumentation / heap telemetry so the before/after is real
numbers, not estimates — the review itself recommends this, and it's the right gate for #1 and
#7 both.
