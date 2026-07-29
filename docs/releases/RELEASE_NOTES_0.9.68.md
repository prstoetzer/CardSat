# CardSat v0.9.68 — release notes

A two-radio release, and a physics release.

The headline is **native dual-radio support**: CardSat can now drive a downlink radio and an
uplink radio itself — any two of 27 half-duplex or receive-only radios, each on its own bus —
without the CardSatDualRig companion in the middle. That includes the **IC-705 over its own
Wi-Fi**, and two USB radios through a hub. The companion is still fully supported and is still
the right answer when the radios live away from the Cardputer.

Underneath that, two pieces of orbital science were rebuilt after they were caught giving wrong
answers on the bench. The **Van Allen belt** zones were flagging satellites that never approach a
belt; they now use McIlwain (L, B/B₀) coordinates traced through the real **IGRF-14** geomagnetic
field. The **orbital-decay** estimate was predicting about **one fifth** of an object's true
remaining life; it is now anchored on each element set's *measured* decay rate and scores within
±30% of reality for 89% of real re-entries.

Both of those were validated against outside data rather than reasoning alone — the field model
against an independent IGRF implementation, the decay model against 244 catalogued objects that
actually re-entered. Where a claim could not be checked against data, this document says so.

Everything here is built clean with zero warnings and gate-checked: 15 static gates (two new this
cycle) plus eight host harnesses (two new this cycle).

# New

### Native dual-radio support (CAT type → Dual (2 radios))

Set **CAT type → Dual (2 radios)** in Settings and open **Dual-Rig setup**, which becomes a
native two-leg editor. Each leg — downlink and uplink — takes any radio from the 27-radio catalog
absorbed from the companion firmware: the Icom CI-V transceivers (IC-705, IC-905, IC-7100,
IC-7000, IC-706MKIIG, IC-275/475), nine IC-R receivers, the old-binary Yaesus (FT-817/818/857/897,
FT-100, VR-5000), the ASCII Yaesus (FT-991/991A, FTX-1), and the Kenwood TH-D74/D75 handhelds.

Each leg picks its own **bus**:

- **Grove** — plain TTL serial on G1/G2. One leg only; it claims the UART exactly as wired CI-V
  does, so a Grove rotator must move elsewhere.
- **USB** — a USB↔serial adapter. **Both legs may be USB** through a hub, each nominated to its
  own adapter with `a`; CardSat binds a second CDC on the shared USB host. (A USB rotator is
  excluded while both legs are on USB — three ports behind a hub presses the S3's channel budget.)
- **LAN** — Icom network CAT. The **IC-705 over its own Wi-Fi** is the flagship case: it speaks
  the same RS-BA1-family protocol as the IC-9700 path CardSat already had, so it needs no wiring
  at all. Set the radio's WLAN on, create a Network User1, and enter the radio's IP and control
  port on the leg's rows. Two LAN legs can coexist, each with its own host and login.

CardSat composes the pair into one full-duplex rig, so engage, the Doppler loop, calibration and
the UI all treat it exactly like a single radio. Per-leg CI-V address and baud, with the catalog
default marked `*`. **PTT is never commanded** — you key the uplink radio by hand — and FM uplink
tones are set on the radio itself, the same contract the companion honors. The four CAT dialect
encoders are byte-verified against the companion's bench-validated frames by a host harness.

The IC-905 is selectable on the same protocol family but is **untested on hardware**, and the
VR-5000's opcodes carry the companion's own verify-on-hardware caveat. Both are labeled as such.

# Fixes and polish

### Van Allen belt zones: the real geomagnetic field

The orbital-zone tool was reporting belt transits for satellites that never go near a belt. The
cause was structural: belts are **flux tubes**, and the model gated on L-shell from a centered
dipole plus an altitude floor. Because L diverges toward the poles, a 1200 km satellite reaches
L = 3 at only 51° magnetic latitude — so every high-inclination bird above the floor scored an
"outer belt" transit each time it crossed the auroral zone, while the outer belt's equatorial
altitude for those shells is 12,700–38,200 km.

Belt membership is now tested in McIlwain **(L, B/B₀)** coordinates computed from **IGRF-14**
(degree 13, with secular variation through 2030). For each sample CardSat traces the field line to
its minimum-field point — the shell's magnetic equator — which gives both the shell L and B/B₀,
the displacement along that shell. A satellite is in a belt only when the shell is a belt shell
**and** B/B₀ ≤ 3, roughly within 30° magnetic latitude of the shell's equator. The altitude floor
is gone: it could not express the distinction, since a belt's field lines pass through every
altitude at their high-latitude horns. Both numbers are shown on the status line, so a surprising
verdict explains itself.

A consequence worth knowing: with a real field model, the **South Atlantic Anomaly now satisfies
the inner-belt test on its own** — the anomaly *is* the inner belt reaching down into LEO where
the offset field is weak, which a centered dipole cannot reproduce and which is why the SAA needed
a hand-drawn ellipse. The separate SAA zone is kept deliberately, since that is the form operators
expect.

The field model is verified against an independent IGRF implementation to about one part in 10⁵
from the surface to GEO.

### Orbital decay: anchored on measurement, not a fudge factor

The decay estimate predicted roughly **one fifth** of an object's true remaining life and
essentially never landed within ±30% of a real re-entry. Four defects compounded: a factor-of-two
error in the decay rate, a calibration constant tuned on the ISS that masked it, no eccentricity
correction (a GTO read **43 days** when reality is years to decades), and perigee falling far too
fast during circularization.

The estimate now works from **two anchors**, and a new **"Decay from"** row names which one
produced the number:

- **Observed n-dot**, preferred. An element set's mean-motion derivative is a *measurement* of the
  current decay rate. Anchoring to it makes the present rate correct by construction and removes
  every quantity the modeled path has to guess — the B\* conversion, absolute air density, solar
  activity, attitude, true cross-section — because all of them are already folded into the
  measured number. This works for about 95% of catalogued objects.
- **B\*** for the rest, using the textbook `Cd·A/m = 12.741621·B*` with a density calibration
  fitted against real re-entries.

Both integrate the same King-Hele decay, now with the eccentricity factor that accounts for an
eccentric satellite spending almost none of its orbit near perigee, perigee-preserving
circularization, and an eccentricity-aware re-entry threshold.

Scored against **244 catalogued objects that actually re-entered**, from element sets 30, 14, 7
and 3 days before the event: median prediction **1.05×** the true remaining life, **89%** within
±30%, consistent at every lead time. The solar-activity bracket now appears only on the B\* path,
because the solar scale cancels exactly out of an anchored estimate and showing a "range" of two
identical numbers would imply a precision the estimate does not have.

### Dual-Rig screen fixes, and a class of bug caught for good

Bench reports on the new dual-rig screen turned up three issues, all fixed: pressing ESC out of a
leg's edit field landed in the new-activation editor; the LAN row read `LAN:<host>:<port>`, which
made it unclear where the radio's IP belonged; and the Settings row still said "(Stick)" in native
mode, while entering the screen still queried a companion that was not there.

The first of those was the second occurrence of one bug shape — a new edit field's number falling
into a broad catch-all in the cancel router — so it is now covered by a gate that verifies every
edit field returns to the screen that opened it. That gate immediately found **five more
pre-existing instances**: canceling the target-search grid or a QTH preset name dropped you into
the new-activation editor, the LoRa frequency and TX-power fields dropped you into the QSO log
editor, and the grid-calculator and EME grid fields dropped you onto the Passes screen. All fixed.

# Under the hood

- **Dual-USB CAT.** A second CAT port (CAT-B) binds another CDC on the shared USB host, shaped on
  the existing rotator port: nominated adapter, own line settings, and the host released only when
  the last port lets go. All three adapter pickers now exclude each other symmetrically.
- **Two new gates** (15 total): `audit_edit_home` (edit-field cancel routing) and the decay
  harness's extraction check. **Two new host harnesses** (8 total): `host_geomag` (IGRF vs an
  independent implementation) and `host_decay` (twelve real re-entries, eccentric-orbit sanity,
  anchor selection). Both extract the code under test from `src/app.cpp` at build time, so they
  cannot drift from the firmware.
- **`tools/fetch_decay_calibration.py`** rebuilds the decay calibration set from Space-Track,
  batching by decay month to respect the published rate limits.
- **Documentation and comment audits.** A full pass over the manual and on-device help, and a pass
  over source comments checking every claim against the code and the pinned library versions —
  which removed a block of USB-host forensics describing machinery deleted two releases ago, and
  two now-unused includes.
- **American English** spelling normalized across source comments and documentation.

# Known limits, stated plainly

- Every object that re-enters is low-eccentricity, so the decay model's eccentricity correction is
  **theory-pinned, not data-validated**; the harness fixes only its qualitative behavior.
- The decay model's density constants were fitted at one point in the solar cycle and will drift.
  The n-dot anchored path does not depend on them, which is why it is primary.
- The B/B₀ ≤ 3 belt cutoff is a judgment about where a belt stops, not a measured boundary.
- Objects under active propulsion are not doing drag decay, and their decay figures should be
  read accordingly.
