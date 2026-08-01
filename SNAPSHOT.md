# CardSat 0.9.70-wip — work-in-progress snapshot

**This is not a release.** `FW_VERSION` is `0.9.70-wip`; the bump to a release number
is a deliberate decision that has not been made. Much of what is here has never run on
hardware — `docs/THINGS_TO_VERIFY.md` is the list, and it is long.

## State at packaging

* Build: EXIT=0, **0 warnings**, flash 3,042,602 (96.7%), static RAM 162,176 (49%).
* All **20 static gates** pass; all **10 host harnesses** pass.
* `CardSat.ino` in this zip is byte-identical to the source that produced
  `firmware/CardSat-app.bin` (MD5 `4a2fb1e3b0d8aefc0d5ee3ea56981d10`).
* Companion `CardSatDualRig-app.bin` MD5 `a3e00f3432997971765c47afcd63976a`, rebuilt
  from the source in `companion/`.

## Read this before building

`third_party/EspUsbHost/` is a **patched** EspUsbHost 2.5.2 (MIT, upstream credit
intact). It is *not* used automatically — arduino-cli resolves libraries from
`~/Arduino/libraries`, so it must be copied there and the build redone in full. A
build against the stock library compiles cleanly and looks normal, but behaves
differently on radios exposing two CDC-ACM functions. See
`third_party/EspUsbHost/PATCHES.md`, and `UPSTREAM_ISSUE.md` for the report that is
written but **not yet filed**.

## Largest open items

* **Native dual-radio has never driven a real radio.** The hub enumeration bugs fixed
  in this snapshot are a strong candidate for why — the Cardputer has one USB port, so
  two adapters require a hub, and hubs were being mistaken for the whole bus.
* **TH-D75 CAT does not work.** Root-caused to the vendored library patch above;
  whether that patch is sufficient is unproven — if its CAT port is the second CDC
  function, an app-selectable function index is needed.
* **USB host wedging** (reboot required) is reported but not diagnosed. The SD log
  already captures what is needed; two theories were tested and rejected rather than
  shipping a speculative fix.
* **Doppler calibration defect** is documented, pinned by `tools/host_doppler`, and
  deliberately unfixed pending a decision — it changes on-air behaviour for anyone
  with saved calibrations.

Full history, reasoning and method lessons: `docs/design/AUDIT_FINDINGS_TRACKING.md`.
Session handoff: `docs/design/HANDOFF_MEMO.md`.
