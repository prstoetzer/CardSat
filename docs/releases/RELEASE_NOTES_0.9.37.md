# CardSat v0.9.37 — release notes

This release brings **worldwide LoTW station locations**, an **easier LoTW certificate
setup**, and several **upcoming-activations** improvements. The US LoTW path (state +
county), confirmed working in v0.9.36, is unchanged.

## Logbook of the World

- **New: unified station-location entry.** The LoTW station location is now a single
  consistent flow for every entity, replacing the previous hybrid where US stations used
  text "state/county" rows and everyone else used a separate international picker (which
  was confusing for US users). Now you pick **DXCC → primary → secondary** as a chain of
  gated pickers:
  - **DXCC** is chosen from the full entity list. The entities that actually have a
    subdivision (US, Canada, Japan, the Russias, China, Australia, Finland/Åland/Market
    Reef) are grouped at the **top** and marked with a `>`; the rest follow
    alphabetically. A **typeahead filter** narrows the list as you type — the fast way
    through 340 entities.
  - **Primary** is gated by the DXCC and labeled with the real term for that entity —
    *state* (US), *province* (Canada/China), *oblast* (Russia), *prefecture* (Japan),
    *state* (Australia), *kunta* (Finland). Entities without a subdivision show *(n/a)*.
  - **Secondary** is gated by the primary: US **county** (pick the state first) or
    Japanese **city/gun/ku** (pick the prefecture first). Every other entity shows
    *(n/a)*.
  - **IOTA** stays a free-text field for any entity.

  The US is now simply "United States → state → county," structurally identical to
  Japan's "prefecture → city" — no special cases in the UI.

- **New: full Japanese city/gun/ku data.** Japan now has a complete secondary picker
  (all 1,720 city/gun/ku entries across the 47 prefectures), gated by prefecture, signed
  as `JA_CITY_GUN_KU` at the correct sigspec position. The Finnish kunta list is also now
  complete (all 499 entries).

- The full DXCC list (340 current entities) and all subdivision tables are generated
  directly from the TrustedQSL (tqsl) 2.8.6 config the LoTW distributes. The US
  state/county signing path is unchanged and byte-for-byte identical to before — existing
  configurations keep working with no re-entry.


  primary subdivision for non-US entities, not just US state/county. Under *Settings →
  Station / display*, the new **LoTW subdiv (intl)** row opens a **DXCC-aware picker**:
  CardSat reads your **LoTW DXCC** entity and shows only the subdivisions valid for it —
  Canadian **province**, Russian **oblast**, Japanese **prefecture**, Chinese
  **province**, Australian **state**, or Finnish **kunta** (covering Finland, Åland, and
  Market Reef). Scroll with `;` / `.` and press ENTER; CardSat stores the correct LoTW
  enumeration code and fills the right LoTW field for you, so you never have to look up a
  code. Entities with no subdivision show a short note and are simply left blank.
- **New: IOTA field.** An optional **LoTW IOTA** row accepts your island reference (e.g.
  `NA-005`) and applies to any entity.
- **Signed correctly.** The new subdivision and IOTA values are written into the `.tq8`
  under their exact LoTW field names (`CA_PROVINCE`, `RU_OBLAST`, `JA_PREFECTURE`,
  `CN_PROVINCE`, `AU_STATE`, `FI_KUNTA`, `IOTA`) and inserted into the **cryptographically
  signed data at the exact alphabetical sigspec position** LoTW re-derives — verified
  field by field against the TrustedQSL (tqsl) 2.8.6 source LoTW distributes. The
  subdivision tables themselves are generated from that same `config.xml`. The US
  state/county signing path is byte-for-byte unchanged.

- **New: in-browser certificate converter.** Getting your LoTW certificate onto the card
  no longer needs the command line. Open **`tools/lotw_cert_converter.html`** in any web
  browser, choose the `.p12` you exported from TQSL, enter its password, and click
  **Convert** — it produces the two files CardSat reads, **`lotw_key.pem`** and
  **`lotw_cert.pem`**. It runs **entirely in your browser and works fully offline** (the
  cryptography is bundled into the page); your **private key and password never leave your
  computer**. The previous one-line `openssl pkcs12` method still works and is documented
  for anyone who prefers it (or who wants to keep the key encrypted on the card).

## Upcoming activations (hams.at)

- **New: the activations list is cached to the card.** After a successful download, the
  upcoming-activations roster is saved to `/CardSat/hamsat.dat`. Re-opening the
  **Activations** screen now shows the **last-known list immediately, even with no WiFi**,
  and a live fetch replaces it when you're online. The cache is self-validating (it's
  ignored if it's from a different firmware build), so it never shows stale or malformed
  data.
- **New: the Update screen refreshes activations too.** Pressing **`k`** (full update) on
  the **Update** screen now pulls a fresh activations list right after the orbital-element
  (GP) update, so a single keypress brings both up to date. The GP result message is
  preserved.

## Reliability fixes

- **Fixed: Cloudlog / Wavelog uploads failing with "connection refused."** After the
  device had done other network activity in a session (e.g. a GP update), a Cloudlog
  upload could fail with `code=-1` even though every other TLS operation — including the
  LoTW upload — worked. The cause was the Cloudlog request body being assembled with
  chained `String` operators, which churns the no-PSRAM heap free-list right before the
  TLS handshake; the upload is now built in a single pre-sized buffer (the way the LoTW
  upload already was), which resolves it. The produced JSON is byte-for-byte unchanged.
  A full writeup is in
  [docs/design/CLOUDLOG_UPLOAD_POSTMORTEM.md](../design/CLOUDLOG_UPLOAD_POSTMORTEM.md).
- **Improved: long-session heap stability.** The AMSAT status parse now streams the feed
  one record at a time instead of loading the whole array, so peak heap stays high
  through a GP update (previously it briefly dropped to a few KB). This keeps long
  sessions stable.

## Build

- **LoRa is now built into the standard binaries** (`CARDSAT_HAS_LORA` defaults to `1`).
  Official builds ship with every feature compiled in, so **RadioLib is now a required
  build dependency** (Arduino Library Manager, or the PlatformIO `lib_deps`). The LoRa
  *runtime* path remains untested on hardware — see the manual.

## Notes

- No changes are required to existing configurations. If you already had US LoTW uploads
  working, they continue to work exactly as before; the new subdivision/IOTA rows are
  additive and optional.
- The bundled certificate converter is a static HTML file — there is nothing to install,
  and it does not connect to the network.

See **[MANUAL.md → LoTW upload](../../MANUAL.md)** for the full setup walkthrough and
**[docs/FEATURES.md](../FEATURES.md)** for the complete feature list.
