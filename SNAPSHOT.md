# CardSat 0.9.72 — release snapshot

**This is a release.** `FW_VERSION` is `0.9.72`. A USB release: the ESP-IDF USB host
stack is compiled from source (see `docs/design/USB_HOST_VENDORING.md`), which made the
IC-705 work over USB CAT and fixed three other USB defects.

## State at packaging

* Build: EXIT=0, **0 warnings**, flash 3,067,552 (73.1% of the 4 MB app partition),
  static RAM 162,376 (49%).
* All **static gates** pass.
* `CardSat.ino` in this zip is byte-identical to the source that produced
  `firmware/CardSat-app.bin` (MD5 `1db3bc9a69d93827a00293ef63594fe1`).
* `CardSat_Manual.pdf` regenerated from `MANUAL.md` at v0.9.72 (167 pages).

## Building this package

Two steps beyond a normal Arduino build, and **both are silent if skipped**:

1. Copy `third_party/EspUsbHost/` over the installed EspUsbHost library.
2. Run `./tools/vendor_usb_host.sh` to install the ESP-IDF USB host component.

Then **delete the sketch build cache** — `build_opt.h` is not a dependency of any object
file, so a changed flag otherwise yields a byte-identical binary. Verify the vendoring
took by checking the map: `libraries/UsbHostSrc` in the hundreds, `libusb.a(` at zero.

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
