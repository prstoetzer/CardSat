# CardSat — Cardputer ADV satellite tracker + multi-radio CAT Doppler control

A self-contained, offline-first amateur-radio satellite tracker for the
**M5Stack Cardputer ADV** (ESP32-S3). It downloads GP (orbital element) data and
transponder data over WiFi, predicts passes with SGP4, and drives an Icom, Yaesu, or
Kenwood radio over CAT with real-time Doppler correction — using the AMSAT
**"One True Rule"** (constant frequency *at the satellite*), per-satellite
calibration, an all-favorites pass schedule, an AOS alarm, visual-pass and Sun/Moon
transit prediction, sun/eclipse status, and more.

> **Status: running on hardware; most CAT control still being verified on the air.**
> CardSat runs on the Cardputer ADV, and every feature has been exercised on the
> device. Pass prediction, the polar and pass-detail plots, mutual-window search, GPS,
> the AOS alarm, deep sleep, and the offline GP/transponder caches are all confirmed.
> **Single-pin CI-V is confirmed working on an IC-821** (full bidirectional exchange
> over one wire). The other per-protocol CAT encoders (separate-pin CI-V, Yaesu,
> Kenwood), the **Icom LAN (RS-BA1)** backend, and the rotator backends are host-tested
> but not yet confirmed against that specific hardware — verify those on the air. See
> **[docs/THINGS_TO_VERIFY.md](docs/THINGS_TO_VERIFY.md)**.

> **New in v0.9.42:** **large LoTW and Cloudlog uploads now work.** A full log is split
> into small batches (6 QSOs for LoTW, 15 for Cloudlog), each uploaded in its own quick
> reboot, so you enter your LoTW key passphrase **once** and the device finishes the whole
> run on its own — for both "un-uploaded only" and "upload ALL" modes. This release also
> fixes a subtle bug where **HTTPS could fail after playing a sound or voice memo** (audio
> and TLS were competing for the same internal RAM), adds a persistent **speaker-volume
> setting** with live feedback, adds a **QRZ grid-backfill** utility for the log, and
> includes a small **games** menu. The deep debugging behind the upload and audio fixes is
> written up in **[docs/design/UPLOAD_AND_AUDIO_TLS_POSTMORTEM.md](docs/design/UPLOAD_AND_AUDIO_TLS_POSTMORTEM.md)**.
> See the **[release notes](docs/releases/RELEASE_NOTES_0.9.42.md)**.
>
> **In v0.9.40:** an **out-of-passband warning** — tuning a linear transponder's knob
> past either edge of the passband now flashes a warning while CardSat pulls you back —
> plus **received LoRa messages wrap** to a second line instead of being cut off. Logging
> gains a fix: **editing a QSO re-arms its upload** (the corrected record is re-sent to LoTW
> and Cloudlog), and the Edit QSO screen now has **LoTW/Cloudlog flag rows** you can toggle to
> override that. The on-device **Help** screen also gains five built-in references — a
> **Glossary & math**, a **User guide**, a **Ham satellite history**, a **Tech help** guide
> (antennas, feedline, pointing, working a pass, and the interfaces), and a **Learn** screen
> (radio + orbital theory) — and **About** gains a **License & credits** screen. New
> operating aids: a **point-here arrow** for hand-aiming (`a` on Track), a **"what's overhead
> now"** screen, **sked reminders** set from the activations feed, an **aurora-likelihood**
> line on Space Wx, and **rise directions** in the visible-pass list. There's also a guide to
> **curating your own GP data** for offline/SD-card use instead of the online update. See the
> **[release notes](docs/releases/RELEASE_NOTES_0.9.40.md)**.
>
> **In v0.9.39:** **LoRa messaging is hardware-verified.** Two-way LoRa text messaging
> between CardSat and a LilyGo T-LoRa unit running the companion CardSat Pager firmware is
> confirmed working — this release fixes a bug where a sent message could echo back to you
> (a transmit-complete interrupt was being mistaken for an incoming packet). See the
> **[release notes](docs/releases/RELEASE_NOTES_0.9.39.md)**.
>
> **In v0.9.38:** **logging polish + an upload failsafe for LoTW.** When you log a
> QSO from a live tracking screen, the **Call** field is now pre-selected so you can type
> the callsign right away, and the **QSO log lists newest-first**. The automatic
> **reboot-to-upload** failsafe (confirm with ENTER, cancel with `` ` ``) now covers
> **LoTW** as well as Cloudlog — for the rare post-long-session "connection refused" — and
> the upload screen now updates the moment an upload finishes. The **Space Wx**,
> **Weather**, and **Activations** screens now refresh the same way — show cached data
> immediately, fetch in the background with an "Updating…" bar, then a brief result. See
> the **[release notes](docs/releases/RELEASE_NOTES_0.9.38.md)**.
>
> **In v0.9.37:** **worldwide LoTW locations and an easier certificate setup.**
> The LoTW station fields now cover **non-US entities** — Settings has a DXCC-aware
> **subdivision picker** (Canadian province, Russian oblast, Japanese prefecture,
> Chinese province, Australian state, Finnish kunta) plus an **IOTA** field, all signed
> into the upload at the exact field/order LoTW expects. (US state + county are
> unchanged.) Getting your certificate onto the card is simpler too: a **browser-based
> converter** (`tools/lotw_cert_converter.html`) turns the `.p12` you export from TQSL
> into the two `.pem` files CardSat needs — entirely in your browser, offline, with your
> private key never leaving your computer (no more `openssl` command line). Plus: the
> **upcoming-activations** list (hams.at) is now **cached to the card** so it shows the
> last-known roster with no WiFi, and the **Update** screen's `k` now **pulls activations
> alongside the GP update**. See the
> **[release notes](docs/releases/RELEASE_NOTES_0.9.37.md)**.
>
> **In v0.9.36:** **upload to Cloudlog / Wavelog.** CardSat can now send your
> satellite QSOs straight to a self-hosted **Cloudlog** (or **Wavelog**) online logbook
> over WiFi — set your instance URL, a read-write API key, and your station profile ID in
> Settings, then **Log → Upload to Cloudlog**. Because Cloudlog can forward to LoTW
> itself, it's an alternative to the on-device LoTW upload; the two are tracked separately
> so nothing is double-counted. This release also makes the **LoTW upload work end to
> end** — a CardSat-built satellite QSO now signs, uploads, and **posts to a real LoTW
> account** (the `.tq8` station/contact field names, US county value, and date/time format
> are now exactly what LoTW's processor expects, and an accepted upload is reported
> correctly). Plus: **API keys and passwords are kept out of the USB serial log**, the
> orbital-analysis **altitude no longer reads above apogee**, and **uploads are more
> robust when memory is tight**. See the
> **[release notes](docs/releases/RELEASE_NOTES_0.9.36.md)**.
>
> **In v0.9.35:** a built-in **Notes** editor on the **Log** menu — a free-form,
> multi-page text editor with a file browser, for sked details, grids you still need,
> antenna settings, or any operating reminder. Notes are plain `.txt` files under
> `/CardSat/notes/` (SD card or internal flash), listed newest-first with a saved
> date/time. The editor's commands use the **Fn** modifier so the `;` `.` `,` `/`
> keys stay typeable. This release also fixes a memory bug that could make **LoTW
> uploads fail** on the no-PSRAM Cardputer. See the
> **[release notes](docs/releases/RELEASE_NOTES_0.9.35.md)** and **§8 → Notes** in
> the manual.
>
> **In v0.9.34:** **direct Logbook of the World (LoTW) upload.** CardSat can
> sign your satellite QSOs and send them straight to ARRL's LoTW over WiFi — no PC,
> no TQSL, no separate upload step. It builds the same cryptographically-signed
> `.tq8` TQSL would and posts it to LoTW's self-authenticating service. It needs a
> **microSD card** and your existing LoTW certificate exported to the card (a
> one-time `openssl pkcs12` step); **your private key lives on the SD card**, so use
> a card you control. New **Sign & upload to LoTW** action on the Log menu, plus
> **LoTW DXCC / CQ zone / ITU zone** fields in Settings. Sent QSOs are flagged so
> they're never uploaded twice. Also: an **Activations** screen on the main menu
> that downloads the **hams.at** upcoming-activations feed (roves, grid activations,
> special ops) and lists them with times, mode, frequency and comments.

---

## What it does

- **Constant-frequency-at-the-satellite Doppler** (KB5MU's *One True Rule*) on both
  legs, so your signal never walks through the passband. Tune with the device keys
  **or the radio's own knob** — let go and nothing drifts.
- **Three CAT families, ten radios** behind one rig interface: Icom **CI-V**
  (IC-820/821/910/970/9100/9700), Yaesu (**FT-847**, **FT-736R**), Kenwood
  (**TS-790**, **TS-2000**) — plus native **Icom LAN (RS-BA1)** control of the
  **IC-9700** over WiFi with no wiring.
- **Linear-transponder passband tracking** with correct inversion and automatic
  sideband, and **automatic PL/CTCSS** on FM uplinks.
- **Prediction & planning** — an all-favorites **Next Passes** schedule, **pass-detail**
  and **polar** plots, the **OSCARLOCATOR** live azimuthal board, a **world map** with
  footprints and terminator, **mutual-window** (sat-to-sat) search, and **orbital
  analysis**.
- **Observe the sky** — **visual-pass flags** (sunlit bird + dark sky), **Sun/Moon
  transit** prediction, **illumination/eclipse**, and **decay/reentry** watch flags.
- **Operating aids** — **jump-to-beacon**, per-satellite **calibration** and
  **operating notes**, an **AOS alarm**, **deep sleep until the next pass**, a built-in
  **logbook** (ADIF/LoTW) with DXCC/grid/state tracking, a free-form **Notes** editor,
  **LoRa messaging**, voice memos, and an optional IR-LED pass beacon.
- **Offline-first** — GP elements, transponders, and DXCC data are cached to microSD
  (or internal flash) for full operation with no network.

The complete, detailed feature list is in **[docs/FEATURES.md](docs/FEATURES.md)**.

## Screenshots

A few of CardSat's screens (240×135 native captures). The full set is in the
[manual](MANUAL.md#20-screen-by-screen-reference).

<table>
<tr>
<td align="center"><img src="docs/img/track.jpg" width="240"><br><b>Track</b> — live Doppler &amp; CAT read-back</td>
<td align="center"><img src="docs/img/satellites.jpg" width="240"><br><b>Satellites</b> — the catalog with activity marks</td>
<td align="center"><img src="docs/img/next-passes.jpg" width="240"><br><b>Next Passes</b> — unified favorites schedule</td>
</tr>
<tr>
<td align="center"><img src="docs/img/pass-polar.jpg" width="240"><br><b>Pass polar</b> — sky track for the next pass</td>
<td align="center"><img src="docs/img/world-map.jpg" width="240"><br><b>World map</b> — footprints, sun terminator</td>
<td align="center"><img src="docs/img/analysis-info.jpg" width="240"><br><b>Orbital analysis</b> — nine pages of detail</td>
</tr>
<tr>
<td align="center"><img src="docs/img/space-wx.jpg" width="240"><br><b>Space Wx</b> — solar flux, Kp, operating outlook</td>
<td align="center"><img src="docs/img/illumination.jpg" width="240"><br><b>Illumination</b> — sunlit/eclipse over the orbit</td>
<td align="center"><img src="docs/img/home.jpg" width="240"><br><b>Home</b> — every screen is one hop away</td>
</tr>
</table>

## Hardware

- **M5Stack Cardputer ADV** (StampS3A = ESP32-S3FN8, 8 MB flash, **no PSRAM**,
  240×135 IPS LCD, 56-key keyboard, microSD, speaker, Grove port, 2×7 header).
- A **CAT interface appropriate to your radio**, between its control jack and the
  3.3 V GPIO signals. The Grove **power** pin is 5 V and the ESP32-S3 GPIOs are
  **not** 5 V tolerant — never wire CAT direct. Icom = a 3.3 V-safe single-wire CI-V
  interface; Kenwood = a MAX3232 RS-232 level shifter; Yaesu = a serial CAT interface
  (verify TTL vs RS-232).
- *(Optional)* a GPS source (Grove port or an M5Stack Cap LoRa GNSS), and an
  *(optional)* **antenna rotator** (GS-232A/B via an SC16IS750 I²C→UART bridge +
  MAX3232, or a direct-Yaesu I²C interface).

Full pin-by-pin wiring is in **[docs/WIRING.md](docs/WIRING.md)**.

## Install & upgrade

Two prebuilt binaries ship with each release — no toolchain required:

| File | Use with | Notes |
|---|---|---|
| `CardSat.bin` | **[Launcher](https://github.com/bmorcelli/Launcher)** | App-only image; Launcher writes the partition table/bootloader. **Preserves saved data** on upgrade. |
| `CardSat_Merged.bin` | **M5Burner** / direct flash (esptool / web flasher) at `0x0` | Complete standalone image; carries an empty filesystem. |

**Your settings live on the microSD card when one is inserted** (CardSat prefers SD,
under `/CardSat`, and falls back to internal flash otherwise). Flashing never touches
the SD card — so with a card in, your configuration **survives any flash**, including a
full merged flash. Without a card, use **Launcher** (`CardSat.bin`) to keep your
internal data across an upgrade; a full `CardSat_Merged.bin` flash erases it.

Building from source (Arduino IDE single-file `CardSat.ino`, or PlatformIO) and the
complete flashing/upgrade detail are in **[docs/BUILD_AND_FLASH.md](docs/BUILD_AND_FLASH.md)**
(and the step-by-step **[docs/guides/ARDUINO_SETUP.md](docs/guides/ARDUINO_SETUP.md)**).

## Quick start

Navigation uses the legends printed on the Cardputer keys:
`;` up · `.` down · `,` left · `/` right · **ENTER** select · `` ` `` or **DEL** back.

1. **Settings** — WiFi (press `s` on the SSID row to scan), radio model, **CAT baud**
   (and CI-V address for Icom), minimum pass elevation, AOS alarm. Once a network is
   saved, CardSat auto-connects and NTP-syncs the clock at every boot.
2. **Location** — set your grid or lat/lon, or enable GPS; set the UTC clock if you
   have no network/GPS.
3. **Update** — download GP data (and NTP time-sync); optionally cache *all*
   transponders for full offline use.
4. **Satellites** — pick a bird (`f` to favorite); transponders load from cache or
   SatNOGS.
5. **Next Passes** — what's coming up across all favorites.
6. **Passes → Track** — live az/el and Doppler; `m` switches TUNE/CAL, `d` toggles
   radio-knob tuning.

See **[MANUAL.md](MANUAL.md)** for the complete guide.

## Documentation

| Document | What's in it |
|---|---|
| **[MANUAL.md](MANUAL.md)** | The complete user guide — every screen, setting, and workflow. |
| **[docs/FEATURES.md](docs/FEATURES.md)** | Full, detailed feature list. |
| **[docs/BUILD_AND_FLASH.md](docs/BUILD_AND_FLASH.md)** | Prebuilt binaries, upgrading, building from source. |
| **[docs/WIRING.md](docs/WIRING.md)** | CAT, GPS, and rotator wiring. |
| **[docs/RADIOS.md](docs/RADIOS.md)** | Per-radio behavior: bands, sat mode, read-back. |
| **[docs/THINGS_TO_VERIFY.md](docs/THINGS_TO_VERIFY.md)** | What's confirmed on hardware vs still to test on the air. |
| **[docs/interfaces/](docs/interfaces/)** | Electrical/protocol interface specs: CI-V, single-pin CI-V, Icom LAN, rotator, RS-232, radio settings chart. |
| **[docs/guides/ARDUINO_SETUP.md](docs/guides/ARDUINO_SETUP.md)** | From-scratch Arduino IDE setup. |
| **[docs/guides/PORTING.md](docs/guides/PORTING.md)** | Porting CardSat (or a subset) to other ESP32 boards or non-ESP32 platforms. |
| **[docs/guides/CODE_REFERENCE.md](docs/guides/CODE_REFERENCE.md)** | File-by-file annotated code reference (interfaces, key functions, data flows). |
| **[docs/design/](docs/design/)** | Design/scope notes for current and proposed features. |
| **[docs/releases/](docs/releases/)** | Per-version release notes. |
| **[CardSat_CheatCard_4x6.pdf](CardSat_CheatCard_4x6.pdf)** | Printable 4×6 key-reference card (front: operating, back: setup). |
| **[CardSat_Manual.pdf](CardSat_Manual.pdf)** | PDF build of the manual. |

## Data sources

AMSAT publishes **GP (OMM) element sets as JSON**; CardSat reads
`https://newark192.amsat.org/gpdata/current/daily-bulletin.json` (configurable in
**Settings → GP URL**), streams it straight to flash, and parses one element set at a
time so the full catalog loads on the no-PSRAM S3. Transponders come from the
**SatNOGS DB** (`db.satnogs.org`). Up to **220 satellites** in RAM and **64
transponders** per active satellite. The **Workable DXCC** entity list is derived from
**cty.dat** (AD1C, country-files.com), bundled in flash. Optional screens pull live,
cached data: **space weather** from **NOAA SWPC**, **terrestrial weather** from
**Open-Meteo** (CC BY 4.0), and **callsign lookup** from **QRZ.com** (your own XML
subscription).

## Supporting AMSAT

CardSat runs on data and infrastructure that **[AMSAT](https://www.amsat.org/)**
provides, and on the satellites AMSAT volunteers help keep flying. **If you find
CardSat useful, please consider joining and/or donating to AMSAT at
[www.amsat.org](https://www.amsat.org/).**

## Credits & license

- SGP4 propagation: [Hopperpop/Sgp4-Library](https://github.com/Hopperpop/Sgp4-Library).
- GP data: [AMSAT](https://www.amsat.org/). Transponders: [SatNOGS DB](https://db.satnogs.org/).
- "One True Rule" Doppler tuning: Paul Williamson **KB5MU**,
  [AMSAT](https://www.amsat.org/the-one-true-rule-for-doppler-tuning/).

Released under the **MIT License** (see [MANUAL.md](MANUAL.md) §23 for the full text).
Built for amateur-radio use; respect your local licensing and band plans.
