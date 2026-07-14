# Scope: TLS Certificate Validation (Critical)

*Design scoping for replacing the blanket `setInsecure()` on CardSat's HTTPS paths with real
certificate validation, without breaking public-data fetches or blowing the no-PSRAM heap
budget. This is a scoping document, not an implementation — it frames the problem, the
constraints, the options, and a recommended path, so the work can be sized and sequenced.*

## 1. The problem, precisely

Every TLS connection in `net.cpp` calls `client.setInsecure()` (five sites: lines ~307, 453,
777, 1011, 1169). That disables certificate and hostname validation entirely — CardSat will
complete a TLS handshake with *any* server presenting *any* certificate. A man-in-the-middle on
the same network (or upstream) can therefore impersonate a host, and CardSat cannot tell.

The severity is not uniform across the hosts:

| Host | Carries | Risk if MITM'd |
|---|---|---|
| lotw.arrl.org | LoTW upload (signed .tq8; no login, but QSO data) | Forged confirmations / data disclosure |
| QRZ XML (login) | **QRZ username + password** | **Credential theft** |
| Cloudlog / self-hosted | **Cloudlog API key**, QSO data | **Credential theft**, log tampering |
| amsat.org status/reports | AMSAT report POST (callsign) | Spoofed status data |
| newark192.amsat.org, celestrak | GP orbital elements | **Substituted orbits** → wrong pointing/Doppler |
| db.satnogs.org | transmitter lists | Substituted frequencies |
| hams.at, NOAA, open-meteo | public read-only data | Spoofed data (low) |

The "public data" comment next to the `setInsecure()` calls is only a valid rationale for the
bottom rows. It is **not** valid for the credential-bearing services (QRZ, Cloudlog) or for the
orbital data that actually drives antenna pointing and radio tuning.

## 2. The binding constraint: memory

This is why it wasn't done already. Certificate validation on BearSSL (ESP_SSLClient) requires
a **trust anchor** — either a full CA bundle or pinned roots — held in RAM during the handshake,
on top of the TLS buffers. CardSat runs on a **no-PSRAM ESP32-S3** where the *largest contiguous
free block* is the scarce resource (currently ~31.7 KB with the display sprite resident; the
handshake is already gated behind `TLS_MIN_BLOCK = 28000`, and 0.9.43 moved off the core's
mbedTLS specifically because its ~32 KB fixed buffers wouldn't fit).

A full Mozilla CA bundle is ~200 KB of certificates — a non-starter to hold in RAM, and even as
a flash-resident blob the per-connection parsing/validation working set adds to the handshake
peak. So the design cannot be "load a CA bundle like a desktop." It must be **minimal-trust-anchor**:
carry only the specific roots CardSat's own hosts chain to.

## 3. Options

### Option A — Pin the specific root CAs for CardSat's fixed hosts *(recommended)*
CardSat talks to a **small, known set of hosts** (§1). Each chains to a small number of public
roots (ISRG Root X1 for Let's Encrypt, DigiCert/Amazon roots for the AWS-fronted ones, etc.).
Embed only those handful of root certs in flash (PROGMEM), and set them as the trust anchors.

- **Pros:** small (a few KB of certs, not 200 KB), validates the hosts that matter, no per-host
  user configuration. Handshake peak rises only modestly over the current buffers.
- **Cons:** if a host rotates to a root CardSat doesn't carry, that host fails until the firmware
  ships a new root. Mitigated by (a) carrying the *root*, not the intermediate/leaf — roots have
  10–20 year lifetimes — and (b) a clear error that tells the user which host failed validation.
- **Effort:** Medium. Identify each host's root, embed, wire `setTrustAnchors`/equivalent in
  ESP_SSLClient, add a fallback path (§4), test each host end to end.

### Option B — Full CA bundle in flash
Embed a trimmed Mozilla bundle (~130 roots) in flash; validate against all.

- **Pros:** any public HTTPS host validates, including a user's self-hosted Cloudlog behind a
  public CA. Future-proof against root changes among the fixed hosts.
- **Cons:** larger flash cost; higher handshake working-set (parsing a large anchor set); still
  doesn't cover self-signed LAN Cloudlog. More RAM pressure on the exact constraint that's tight.
- **Effort:** Medium-High, mostly around fitting the working set under the block gate.

### Option C — Fingerprint / SPKI pinning per host
Pin the SHA-256 of each host's certificate (or public key) rather than validating a chain.

- **Pros:** tiny (32 bytes/host), no chain-building cost, strongest against a rogue CA.
- **Cons:** **brittle** — breaks every time a host rotates its leaf cert (LoTW, AWS-fronted hosts
  rotate regularly). Would strand users on stale firmware. Good for a *self-signed LAN Cloudlog*
  (user pins their own cert once), wrong for public hosts.
- **Effort:** Low per host, high maintenance.

### Option D — Opt-in "insecure compatibility" mode
Keep validation on by default, but expose a per-service (or global) "allow insecure TLS" toggle
for users whose setup needs it (self-signed LAN service, a host CardSat's roots don't cover).

- This is not an alternative to A/B/C — it's the **safety valve** that must accompany whichever is
  chosen, so a root-rotation or an odd LAN setup doesn't brick a user's workflow.

## 4. Recommended design

**Option A (pinned roots for the fixed hosts) + Option D (opt-in insecure fallback), with C
available for self-signed LAN Cloudlog.**

Concretely:
1. **Embed the handful of root CAs** CardSat's fixed hosts chain to, in flash/PROGMEM. Audit each
   host's current chain; carry the roots, not leaves.
2. **Validate by default** on all outbound HTTPS. Hostname check on.
3. **Credential-bearing services never fall back silently.** If QRZ/Cloudlog validation fails,
   error clearly ("QRZ certificate not trusted — check date/clock or update firmware"); do not
   send credentials over an unvalidated connection.
4. **Per-service opt-in insecure** for the LAN Cloudlog case, clearly labeled, off by default,
   with an on-screen indication that it's active. A self-signed LAN host can also pin its own
   cert fingerprint (Option C) as the safer alternative to blanket-insecure.
5. **Clock dependency:** cert validity checks need correct time. CardSat already sets time via
   NTP/GPS, but validation must produce a *specific* "clock not set / wrong" error rather than a
   generic handshake failure, because a wrong RTC is the most likely false-positive cause.
6. **Memory:** measure the handshake peak with anchors loaded against `TLS_MIN_BLOCK`. If the
   peak rises, either raise the gate (and accept fewer concurrent features during a fetch) or
   free the display sprite during validated handshakes as the fetch paths already do for buffers.

## 5. Risks & test matrix

- **Root rotation** stranding a host — mitigated by pinning roots (long-lived) + the opt-in valve.
- **Clock skew** false-failures — needs the specific error + a "set clock" nudge.
- **Handshake memory regression** — must be measured per host; this is the gating unknown.
- Test each host in §1 end to end: success with a valid cert, correct failure with a bad/expired
  cert (use `badssl.com` style endpoints), and the opt-in path for a self-signed LAN service.

## 6. Sizing

Medium effort, **its own release**. The work is not large in lines but is high-care: it touches
security-critical paths, needs per-host validation on real hardware, and has a memory-measurement
gate that could force design iteration. It should not be bundled with unrelated changes. Ship it
behind a short beta so the pinned-root set is confirmed against every host before it's the default.
