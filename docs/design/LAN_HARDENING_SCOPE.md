# Scope: LAN Equipment-Control Hardening

*Design scoping for protecting CardSat's inbound network services, which can command a radio
and rotator rather than merely display status. Scoping only — problem, current state,
constraints, options, recommendation.*

## 1. The problem

CardSat runs three inbound LAN servers (`app.cpp`):
- **rigctld server** (Hamlib NET rigctl, `cfg.rigdPort`) — a PC/Gpredict can set frequency, mode.
- **rotctld server** (Hamlib NET rotctl, `cfg.rotdPort`) — a PC can command rotator position.
- **Web control** (`cfg.webPort`) — a browser page that polls `/api/status` and POSTs
  `/api/select` and `/api/cmd`; commands reuse the exact keypad handlers, so the web UI can do
  most of what the physical keys can, including engaging radio/rotator control.

None of these authenticate. The consequence is higher than for a status page: an unauthorized
client on the LAN can **move a real antenna or retune a transmitting radio**. On a shared
network (club, Field Day, hotel, RV park) that's a real-world exposure, not a theoretical one.

## 2. Current state — better than it looks, but not enough

Two mitigating facts already in the code:
- **All three servers default to disabled** (`rigdEnable = false`, `rotdEnable = false`,
  `webEnable = false` in `settings.h`). They only listen when the user turns them on. So the
  default posture is safe; the risk is for users who enable them.
- The servers are **cooperative single-client** and the listeners are freed during outbound
  fetches — so the attack surface is small and only present while enabled and connected.

What's missing is everything that protects the service *once enabled*: no authentication, no
client restriction, no on-device indication of remote commands, no read-only option, no rate
limiting, no quick kill.

## 3. Constraints

- **No PSRAM / small heap:** auth must be cheap. No TLS on these servers (the handshake budget is
  already tight for outbound; inbound TLS is out of scope). This means auth is a **shared secret
  over plaintext LAN**, which is acceptable for a same-subnet control channel but should be
  described honestly (it protects against casual/accidental access and other LAN users, not a
  determined attacker sniffing the wire).
- **Hamlib protocol compatibility:** rigctld/rotctld speak a fixed protocol. Bolt-on auth can't
  break Gpredict/`rigctl` compatibility. A token has to be optional or carried in a way Hamlib
  clients tolerate — realistically **subnet restriction + an on-device enable is the primary
  control for the Hamlib ports**, with a token more natural for the web UI.
- **Cooperative loop:** anything added runs in the same non-blocking, one-client-at-a-time style;
  no threads, minimal per-connection state.

## 4. Options (layered — most are cheap and combinable)

1. **Client subnet restriction** *(recommended, cheap).* Only accept connections whose remote IP
   is on the same subnet as CardSat (or an explicit allow-list). Blocks off-subnet and routed
   access outright. Applies uniformly to all three servers, no protocol change. Low effort.
2. **On-device "listening" + "last command" indicator** *(recommended, cheap).* A persistent
   header indication when any server is enabled and listening, and a line showing the last remote
   command + source IP. Makes remote control impossible to overlook — the operator always knows.
   This is the analog of the armed-state visibility the UI review wanted, applied to the network.
3. **Web-UI token/password** *(recommended for web).* The web page is CardSat's own code, so it
   can carry a token (query param or header) that the server checks before honoring `/api/cmd` /
   `/api/select`. `/api/status` can stay open (read-only) or also gate. Cheap, natural fit.
4. **Read-only mode** *(recommended, cheap).* A per-server toggle that allows status/query but
   rejects any state-changing command. Lets someone expose monitoring safely.
5. **Emergency disable** *(cheap).* A single action (settings row, or reuse the global
   emergency-stop family) that turns off all three servers immediately. Pairs with the existing
   Fn+back control-stop.
6. **Rate limiting** *(low priority).* Cap connection/command rate. Minor on a cooperative
   single-client loop; more relevant if it ever goes multi-client.
7. **Hamlib-port token** *(optional, compatibility-risk).* A shared secret on the rigctld/rotctld
   ports. Only if a clean way is found that Gpredict tolerates; otherwise rely on #1 + #2 + #4 for
   those ports.

## 5. Recommended design

A **layered default-safe posture**, most of it cheap:
- **Default disabled** (already true) — keep it.
- **Subnet restriction on by default** for all three servers (#1), with an explicit allow-list
  for users who need cross-subnet.
- **Always-visible listening indicator + last-remote-command/IP** (#2) whenever a server is
  enabled — the single highest-value addition, and it's just UI.
- **Token for the web control command endpoints** (#3), off only if the user explicitly opens it.
- **Per-server read-only toggle** (#4) and a **kill-all-servers action** (#5).
- For the **Hamlib ports**, rely on subnet restriction + read-only + visibility rather than a
  protocol-breaking token, unless a compatible scheme is validated.

Document the honest security model: this protects against other LAN users and accidental access,
not a determined on-wire attacker (no inbound TLS). For a hostile network, the guidance is "don't
enable the control servers."

## 6. Sizing

Low-to-medium effort, and unusually **safe to stage incrementally** because the servers are
off by default — each layer (subnet check, visibility, token, read-only, kill switch) can ship
independently without regressing the safe default. The visibility indicator (#2) and subnet
restriction (#1) are the highest value-to-effort and could land first, even ahead of a full
release, since they don't change protocol behavior.
