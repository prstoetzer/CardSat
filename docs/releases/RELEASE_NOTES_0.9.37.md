# CardSat v0.9.37 — release notes

This release brings **worldwide LoTW station locations**, an **easier LoTW certificate
setup**, and several **upcoming-activations** improvements. The US LoTW path (state +
county), confirmed working in v0.9.36, is unchanged.

## Logbook of the World

- **New: international primary subdivisions.** LoTW's station location can now carry the
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
- **New: a "Downloading activations…" banner** appears while the feed refreshes, so it's
  clear the screen is working rather than empty.
- **New: the Update screen refreshes activations too.** Pressing **`k`** (full update) on
  the **Update** screen now pulls a fresh activations list right after the orbital-element
  (GP) update, so a single keypress brings both up to date. The GP result message is
  preserved.

## Notes

- No changes are required to existing configurations. If you already had US LoTW uploads
  working, they continue to work exactly as before; the new subdivision/IOTA rows are
  additive and optional.
- The bundled certificate converter is a static HTML file — there is nothing to install,
  and it does not connect to the network.

See **[MANUAL.md → LoTW upload](../../MANUAL.md)** for the full setup walkthrough and
**[docs/FEATURES.md](../FEATURES.md)** for the complete feature list.
