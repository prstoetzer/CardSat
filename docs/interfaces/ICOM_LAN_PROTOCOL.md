# IC-9700 LAN (Icom "RS-BA1" UDP) protocol — CardSat implementation spec

Byte-exact reference for adding a **native network CAT backend** to CardSat that talks
straight to an IC-9700's built-in Ethernet port (no PC/rigctld bridge). Reverse-engineered;
Icom has not published it. Packet layouts below were extracted verbatim from
`nonoo/kappanhang` (Go), cross-checked against the microenh/NetworkIcom write-up and the
wfview forum. kappanhang explicitly lists the **IC-9700** as supported, so the same wire
format applies.

CardSat needs **CAT only**: the **Control** (50001) and **Serial/CI-V** (50002) streams.
The **Audio** stream (50003) is not opened.

Everything is **UDP**. Multi-byte session IDs are **big-endian**; the small header `seq`
fields are **little-endian**; the CI-V inner seq is **big-endian**. Handshake/control
packets are each sent **twice** for loss resilience.

---

## 1. Session IDs and the 16-byte header

Two 32-bit session IDs identify the link: **localSID** (ours) and **remoteSID** (radio's).
kappanhang derives localSID from our IP+port: `(uint32(IP[last4]) << 16) | (port & 0xffff)`.
A random uint32 also works (microenh used random) since the radio just echoes it; CardSat
can use either. remoteSID is learned from the radio's "I am here" reply.

Common 16-byte header (offsets):

| Off | Sz | Field | Notes |
|-----|----|-------|-------|
| 0   | 4  | len   | total packet length, LE (and doubles as a packet-type tag — see below) |
| 4   | 2  | type  | control subtype: `0x0000` data/idle, `0x0001` retransmit-req, `0x0003` are-you-there, `0x0004` i-am-here, `0x0005` disconnect, `0x0006` are-you-ready/ready |
| 6   | 2  | seq   | LE. For tracked packets this is filled by a **shared** counter (starts at 1). Pings use a **separate** counter. |
| 8   | 4  | localSID | big-endian |
| 12  | 4  | remoteSID | big-endian |

Longer packets are identified by their **total length** (= first byte, since all are <256):
`0x10`=16 (control/idle), `0x15`=21 (ping), `0x16`=22 (serial open/close),
`0x40`=64 (auth/token), `0x50`=80 (status), `0x60`=96 (login reply), `0x80`=128 (login),
`0x90`=144 (conninfo), `0xa8`=168 (capabilities). `0x18`=range-retransmit request.

---

## 2. Connection bootstrap (both streams use this)

Sent on whichever port the stream uses (control 50001, serial 50002), each with its own
local/remote SID:

1. **Are-you-there (pkt3)** ×2 — 16 bytes: `10 00 00 00 03 00 00 00 | localSID | remoteSID(=0)`.
2. **I-am-here (pkt4)** — radio reply, 16 bytes prefix `10 00 00 00 04 00 00 00`.
   Set **remoteSID = big-endian(r[8:12])**.
3. **Are-you-ready (pkt6)** ×2 — 16 bytes: `10 00 00 00 06 00 01 00 | localSID | remoteSID`.
4. **Ready** — radio reply, prefix `10 00 00 00 06 00 01 00`.

Disconnect (teardown): **pkt5** ×2: `10 00 00 00 05 00 00 00 | localSID | remoteSID`.

---

## 3. Keepalive, idle, retransmit

**Ping (pkt7, 21 bytes)** — own sequence counter, sent periodically (≈100 ms–1 s; tunable)
on both streams and must also be answered:
```
15 00 00 00 07 00 | seqLE(2) | localSID(4) | remoteSID(4) | dir(1) | counter(4)
```
`dir` = `0x00` request / `0x01` reply. To **reply** to a radio ping: swap local/remote SID,
set `dir=0x01`, echo the radio's `counter` (its bytes [17:21]). Identify a ping by
`len==21 && bytes[1:6]=={00 00 00 07 00}` (first byte may be 0x15 or 0x00).

**Idle / tracked send (pkt0, 16 bytes)** — `10 00 00 00 00 00 | seqLE | localSID | remoteSID`.
Idle pkt0s are sent every **100 ms**, backing off to **1 s** after 1 s of no tracked
traffic. *Every* non-handshake control/serial packet (login, auth, conninfo, open/close,
CI-V data) is sent through the **tracked-send** path, which: stamps header `seq` (offset 6,
shared counter starting at 1), stores the packet in a retransmit buffer, then sends.

**Retransmit:** radio asks with `10 00 00 00 01 00 | seqLE | …` (single) or
`18 00 00 00 01 00 …` + 4-byte `(startLE,endLE)` range pairs. Respond by resending the
buffered packet(s) **twice**; if not buffered, send an idle carrying that seq. (Low risk on
a quiet wired/Wi-Fi LAN, but the parser must not choke on these.)

---

## 4. Control stream (50001) auth sequence

After the §2 bootstrap on 50001:

1. **Login (0x80, 128 B)** via tracked send. Layout:
   - `[0]=0x80`; `[8:12]`=localSID; `[12:16]`=remoteSID
   - `[19]=0x70 [20]=0x01`; inner seq LE at `[23],[24]`; 2 random bytes at `[26],[27]`
   - `[64:80]` = `passcode(username)` (16 B), `[80:96]` = `passcode(password)` (16 B)
   - `[96:112]` = app name, plaintext, null-padded (kappanhang: `"icom-pc"`; CardSat: `"CardSat"`)
   - all other bytes `0x00`
2. **Login reply (0x60, 96 B)**, prefix `60 00 00 00 00 00 01 00`. If `r[48:52]=={ff ff ff fe}`
   → **invalid username/password**. Capture **authID = r[26:32]** (6 bytes).
3. Start pings + idles.
4. **Auth #1 (0x40, magic=0x02)** then **Auth #2 (0x40, magic=0x05)** via tracked send.
   Token/auth packet layout:
   - `[0]=0x40`; `[8:12]`=localSID; `[12:16]`=remoteSID
   - `[19]=0x30 [20]=0x01 [21]=magic`; inner seq LE at `[23],[24]`; `[26:32]`=authID
   - magic: `0x02` first auth, `0x05` second auth **and** periodic re-auth, `0x01` de-auth.
5. Radio sends, in some order:
   - **Capabilities (0xa8, 168 B)**, prefix `a8 00 00 00 00 00`: capture **a8replyID = r[66:82]** (16 B).
   - **Auth reply (0x40, 64 B)**, prefix `40 00 00 00 00 00`: if `r[21]==0x05` → **authOK**.
   - **Status (0x50, 80 B)**: `r[48:51]=={ff ff ff}` → auth failed; `r[48:51]=={00 00 00} && r[64]==0x01` → radio disconnected.
6. Once authOK **and** a8replyID received → **ConnInfo (0x90, 144 B)** via tracked send
   (this is the host-originated packet that makes CI-V actually work):
   - `[0]=0x90`; `[8:12]`=localSID; `[12:16]`=remoteSID
   - `[19]=0x80 [20]=0x01 [21]=0x03`; inner seq LE `[23],[24]`; `[26:32]`=authID
   - `[32:48]` = a8replyID (16 B)
   - `[64:72]` = radio model name, plaintext (e.g. `"IC-9700"` — use the name the radio
     reports; kappanhang hardcodes `"IC-705"`)
   - `[96:112]` = `passcode(username)`
   - `[112:114]=01 01`, `[114:116]=04 04`, sample-rate words and the **serial port (50002)**
     and **audio port (50003)** as big-endian `uint16` in the trailing block, plus a
     tx-buffer-length word; remaining bytes `0x00`. *(CAT-only: this still advertises the
     audio port, but we never open the audio socket — see §7.)*
7. **ConnInfo reply (0x90, 144 B)** with `r[96]==1` → success. Re-sync IDs from the reply:
   `remoteSID=BE(r[8:12])`, `localSID=BE(r[12:16])`, `authID=r[26:32]`; device name is the
   null-terminated string at `r[64:]`. Now open the serial stream (§5).
8. **Re-auth:** send Auth (magic `0x05`) **every 60 s**, expect a reply within ~3 s.
9. **Teardown:** Auth (magic `0x01`) de-auth, wait ~500 ms (let the radio request any
   retransmits), then disconnect (§2) on each stream.

---

## 5. Serial (CI-V) stream (50002)

§2 bootstrap on 50002 (own SID) → start pings/idles → **Open**:

**Open/Close (0x16, 22 B)** via tracked send:
```
16 00 00 00 00 00 | seqLE(2) | localSID(4) | remoteSID(4) | c0 01 00 | civSeqBE(2) | magic(1)
```
`[16]=0xc0` (serial-control marker), `[19:21]`=civ inner seq **big-endian**, `[21]=magic`:
open=`0x05`, close=`0x00`.

**CI-V data send (len = 21 + N):**
```
(0x15+N) 00 00 00 00 00 | seqLE(2) | localSID(4) | remoteSID(4) | c1 N 00 | civSeqBE(2) | CIVframe(N)
```
`[16]=0xc1` (CI-V data marker), `[17]=N` (CI-V byte count), `[19:21]`=civ inner seq BE,
`[21:]` = the raw CI-V frame `FE FE <to> <from> <cmd> [data] FD`. Max N = **80** (Hamlib limit).
The header `seq` at `[6:8]` is the shared tracked counter; `civSeq` is a separate serial-side
counter.

**CI-V data receive:** accept when `len>=22 && r[16]==0xc1 && r[0]-0x15==r[17]`; the CI-V frame
is `r[21:]`. Reorder by header seq before decoding.

**CI-V semantics (ask–answer, serialised):** one outstanding query at a time. The radio first
**echoes** the sent frame (to/from IDs as-sent), then sends the **response** (IDs swapped),
ending in `0xFD`. Plain ACK = `0xFB`, NAK = `0xFA`. A second query before the first answers
usually NAKs — so the backend queues CI-V and waits for echo+response (or a timeout).

IC-9700 CI-V address = **0xA2**; host/controller default = **0xE0**. These go in the `<to>`/
`<from>` bytes of the CI-V frame — identical to CardSat's existing wired Icom frames
(including the group-07 main/sub work). Only the transport changes.

**Close:** Open/Close with magic `0x00`, then disconnect.

---

## 6. passcode() — username/password obfuscation (functional constant)

Required verbatim for interop. `sequence` maps ASCII 32..126 → byte; output is 16 bytes
(zero-padded). For i in 0..15 while i<len(s): `p = s[i] + i; if p > 126: p = 32 + (p % 127);
out[i] = sequence[p]`.

```
sequence (index = ASCII code 32..126):
32:0x47 33:0x5d 34:0x4c 35:0x42 36:0x66 37:0x20 38:0x23 39:0x46 40:0x4e 41:0x57
42:0x45 43:0x3d 44:0x67 45:0x76 46:0x60 47:0x41 48:0x62 49:0x39 50:0x59 51:0x2d
52:0x68 53:0x7e 54:0x7c 55:0x65 56:0x7d 57:0x49 58:0x29 59:0x72 60:0x73 61:0x78
62:0x21 63:0x6e 64:0x5a 65:0x5e 66:0x4a 67:0x3e 68:0x71 69:0x2c 70:0x2a 71:0x54
72:0x3c 73:0x3a 74:0x63 75:0x4f 76:0x43 77:0x75 78:0x27 79:0x79 80:0x5b 81:0x35
82:0x70 83:0x48 84:0x6b 85:0x56 86:0x6f 87:0x34 88:0x32 89:0x6c 90:0x30 91:0x61
92:0x6d 93:0x7b 94:0x2f 95:0x4b 96:0x64 97:0x38 98:0x2b 99:0x2e 100:0x50 101:0x40
102:0x3f 103:0x55 104:0x33 105:0x37 106:0x25 107:0x77 108:0x24 109:0x26 110:0x74
111:0x6a 112:0x28 113:0x53 114:0x4d 115:0x69 116:0x22 117:0x5c 118:0x44 119:0x31
120:0x36 121:0x58 122:0x3b 123:0x7a 124:0x51 125:0x5f 126:0x52
```

---

## 7. CAT-only notes & the one hardware-verify item

- **Audio:** the ConnInfo (0x90) packet advertises both serial and audio ports because
  kappanhang/wfview always open all three streams. CardSat will send ConnInfo but only run
  the bootstrap+open on the **serial** socket, never connecting on 50003. microenh got CI-V
  working reliably with **control+serial only** (his fix was exactly "send a ConnInfo from
  the host"), so this should hold — but it is **the** thing to confirm against a real IC-9700:
  whether the radio tolerates the audio stream never being opened. Fallback if it doesn't:
  open the audio socket, complete its bootstrap, and simply discard all audio packets.
- **Non-blocking discipline:** pings (own counter) and idle pkt0s must keep flowing and radio
  pings must be answered promptly, or the radio drops the link — stricter than rotctld. The
  backend state machine has to be serviced every loop tick between Doppler/rotator/UI work.
- **Device name** in ConnInfo: prefer the model string the radio reports (parse the
  null-terminated name from the 0xa8 / 0x90 packets) rather than hardcoding.
- **Coexists with rotctld/PstRotator:** independent sockets (control + serial here, +1 for the
  rotator), all within the ESP32 lwIP budget; network CAT frees the UART so there's no
  hardware contention with any rotator backend.

Reference clients: `github.com/nonoo/kappanhang` (Go — source of the layouts above),
`gitlab.com/eliggett/wfview` (C++), `github.com/microenh/NetworkIcom` (Swift + analysis).
