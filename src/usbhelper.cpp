// ===========================================================================
//  usbhelper.cpp  --  CardSat's client for the CardSatUsbHelper companion
// ===========================================================================
//  See usbhelper.h for what this is and why it exists. This file is the link
//  state machine: framing, credit, enumeration, and the Stream adaptor the CAT
//  and rotator backends actually talk to.
// ===========================================================================
#include "usbhelper.h"

#if CARDSAT_HAS_USBHELPER

#include "rig.h"        // civUartOpen(): the one place the Grove UART is opened
#include <string.h>

// A NAMED namespace, not an anonymous one, and that is deliberate.
//
// CardSat ships in two representations: separate translation units under src/,
// and one monolithic CardSat.ino. In the src/ build an anonymous namespace would
// be right and every name here would be private to this file. In the MONOLITH
// every inlined .cpp lands in the same translation unit, so file-scope statics
// collide -- s_uart, s_devTab and s_errMsg are all already taken by other components,
// and the collision is a compile error thousands of lines from either definition.
//
// A file-scope `using namespace csuh;` would fix the references below and
// reintroduce the same ambiguity for later code that legitimately has its own
// s_devTab and s_errMsg. So: named namespace, qualified at the two places outside it
// that need these names. Internal linkage is lost; nothing here is a symbol
// anyone else could sensibly reach for.
namespace csuh {

// ---- tuning ---------------------------------------------------------------
// RX ring (helper -> us). MUST be at least CSUH_CREDIT_INIT * CSUH_MAX_PAYLOAD,
// because credit is granted from free space in whole-frame units and the whole
// point of that arithmetic is that an in-flight frame can always land. 2 KB gives
// the grant loop room to run ahead of the reader instead of returning credit one
// frame at a time.
const size_t RX_RING = 2048;
// TX ring (us -> helper). CAT commands are tens of bytes; this is generous.
const size_t TX_RING = 1024;

const uint32_t HELLO_RETRY_MS  = 500;    // while unlinked
const uint32_t PING_IDLE_MS    = 1500;   // max gap between frames WE send (see ping below)
const uint32_t LINK_DEAD_MS    = 6500;   // no valid frame -> not linked. Deliberately
                                         // LONGER than the helper's 5000 ms relock: when a
                                         // gap does occur, the helper gives up first and
                                         // rescans while this end is still transmitting
                                         // steadily at the fixed rate -- so it re-locks in
                                         // one scan step and the stream never detaches.
                                         // Equal timers meant both ends abandoned the link
                                         // in the same instant, which is half of how the
                                         // first bench flap sustained itself.
const uint32_t OPEN_RETRY_MS   = 1200;   // re-issue a wanted-but-unopened port
const uint32_t CREDIT_FLUSH_MS = 20;     // grant even a small batch this often
const uint8_t  CREDIT_BATCH    = 4;
// Longest HelperStream::write() will wait for ring space before giving up. A CAT
// backend does not check the return of write(), so silently dropping bytes here
// would corrupt a command in a way that presents as a radio fault. Waiting is the
// honest behaviour -- it is what a UART does when its FIFO is full -- and the
// deadline exists only so a dead link cannot hang the Doppler loop.
const uint32_t WRITE_WAIT_MS   = 60;

const uint8_t MAX_DEV = 6;   // matches the helper's own registry size

// ---- ring -----------------------------------------------------------------
// Single-threaded: everything here runs on the loop task. (The helper's copy of
// this needs atomics because its producer is the USB host task; ours does not,
// and pretending otherwise would just be cargo-culted ceremony.)
struct Ring {
  uint8_t* buf = nullptr;
  size_t   cap = 0, head = 0, tail = 0;
  bool alloc(size_t n) { buf = (uint8_t*)malloc(n); cap = buf ? n : 0; head = tail = 0; return buf != nullptr; }
  void free_() { if (buf) { ::free(buf); buf = nullptr; } cap = head = tail = 0; }
  size_t used() const { return cap ? (head + cap - tail) % cap : 0; }
  size_t freeSpace() const { return cap ? (cap - 1 - used()) : 0; }
  size_t push(const uint8_t* d, size_t n) {
    size_t sp = freeSpace(); if (n > sp) n = sp;
    for (size_t i = 0; i < n; ++i) buf[(head + i) % cap] = d[i];
    head = (head + n) % cap; return n;
  }
  size_t pop(uint8_t* d, size_t n) {
    size_t hv = used(); if (n > hv) n = hv;
    for (size_t i = 0; i < n; ++i) d[i] = buf[(tail + i) % cap];
    tail = (tail + n) % cap; return n;
  }
  int peek1() const { return used() ? (int)buf[tail] : -1; }
  void clear() { tail = head; }
};

// ---- state ----------------------------------------------------------------
HardwareSerial* s_uart = nullptr;
bool     s_started = false;
uint32_t s_baud = CSUH_BAUDS[0];

Ring s_rx, s_tx;

uint8_t  s_acc[CSUH_MAX_ENCODED];
size_t   s_accN = 0;
bool     s_overlong = false;

bool     s_linked = false;
bool     s_haveEpoch = false;
uint32_t s_epoch = 0;
uint32_t s_lastValid = 0, s_lastHelloReq = 0, s_lastPing = 0;
char     s_fw[20] = {0};

// Credit. s_txCredit is what WE may send; s_peerCredit is our model of what the
// helper may send. Keeping an explicit model of the peer's window is what lets
// grantCredit() maintain the invariant that every frame the helper is allowed to
// send has somewhere to land: s_peerCredit * CSUH_MAX_PAYLOAD <= s_rx.freeSpace().
int16_t  s_txCredit   = CSUH_CREDIT_INIT;
int16_t  s_peerCredit = CSUH_CREDIT_INIT;
uint32_t s_lastGrantMs = 0;

struct Dev {
  char key[CSUH_MAX_KEY]     = {0};
  char label[CSUH_MAX_LABEL] = {0};
  bool live = false, open = false;
};
Dev     s_devTab[MAX_DEV];
uint8_t s_devN = 0;

char     s_wantKey[CSUH_MAX_KEY] = {0};
bool     s_wantOpen = false;
bool     s_open = false;
uint32_t s_pBaud = 19200;
uint8_t  s_pBits = 8, s_pPar = CSUH_PAR_NONE, s_pStop = 1;
uint32_t s_lastOpenTry = 0;
char     s_devName[CSUH_MAX_LABEL] = {0};
char     s_errMsg[72]   = {0};
char     s_event[72] = {0};

CsuhStats s_stats;
uint32_t  s_rxFrames = 0, s_crcErr = 0, s_cobsErr = 0, s_txFrames = 0;
uint32_t  s_lastTxMs = 0;      // last frame WE sent -- the peer's liveness clock sees only these
uint32_t  s_helloReqTx = 0;    // tenth bench: HELLO_REQs sent while unlinked
uint32_t  s_helloRx    = 0;    // tenth bench: HELLOs received (before acceptance checks)
// Eleventh bench: per-class RX counters. The impossible triangle -- handshake
// completing, timer firing, traffic "flowing" -- resolves only if specific
// frame classes vanish between HELLOs. These name the classes.
uint32_t  s_rxPong = 0, s_rxStat = 0, s_rxData = 0, s_rxCredit = 0, s_rxEvent = 0;
uint32_t  s_writeTimeouts = 0;   // audit F4: atomic CAT writes refused whole
uint16_t  s_pingTok = 0;         // audit F11: token of the outstanding ping
uint32_t  s_pingSentMs = 0, s_lastRttMs = 0, s_pongBad = 0;
uint8_t   s_rxLastTy = 0;
uint32_t  s_linkDrops = 0;     // twelfth bench: times the dead timer fired
uint32_t  s_restarts = 0;      // helper epoch changes after the first (i.e., real reboots)

HelperStream s_stream;

// ---- frame TX -------------------------------------------------------------
void sendFrame(uint8_t type, const uint8_t* payload, size_t plen) {
  s_lastTxMs = millis();
  if (!s_uart) return;
  uint8_t wire[CSUH_MAX_ENCODED];
  const size_t n = csuhBuildFrame(type, 0, payload, plen, wire, sizeof(wire));
  if (!n) return;
  s_uart->write(wire, n);
  s_txFrames++;
}
void sendEmpty(uint8_t type) { sendFrame(type, nullptr, 0); }

void setErr(const char* e) { strlcpy(s_errMsg, e ? e : "", sizeof(s_errMsg)); }

// ---- credit ---------------------------------------------------------------
// Grant only as many frames as the RX ring can absorb in full. This is the whole
// safety argument for the receive path: because the helper never sends more than
// s_peerCredit frames and each is at most CSUH_MAX_PAYLOAD bytes, a granted frame
// can always be stored. The alternative -- returning credit for every frame
// consumed, regardless of space -- overflows the moment the reader stalls, and a
// dropped CI-V byte does not look like a link fault. It looks like a radio fault.
void grantCredit(bool force) {
  if (!s_linked) return;
  size_t capacity = s_rx.freeSpace() / CSUH_MAX_PAYLOAD;
  if (capacity > CSUH_CREDIT_INIT) capacity = CSUH_CREDIT_INIT;
  int grant = (int)capacity - (int)s_peerCredit;
  if (grant <= 0) return;
  if (!force && grant < CREDIT_BATCH && (millis() - s_lastGrantMs) < CREDIT_FLUSH_MS) return;
  const uint8_t k = (uint8_t)(grant > 255 ? 255 : grant);
  sendFrame(CSUH_T_CREDIT_OUT, &k, 1);
  s_peerCredit += (int16_t)k;
  s_lastGrantMs = millis();
}

// ---- port -----------------------------------------------------------------
void sendOpen() {
  uint8_t p[CSUH_MAX_PAYLOAD];
  size_t n = 0;
  p[n++] = (uint8_t)(s_pBaud);       p[n++] = (uint8_t)(s_pBaud >> 8);
  p[n++] = (uint8_t)(s_pBaud >> 16); p[n++] = (uint8_t)(s_pBaud >> 24);
  p[n++] = s_pBits; p[n++] = s_pPar; p[n++] = s_pStop;
  p[n++] = 1;   // DTR: many CDC devices ignore host traffic until it is asserted
  p[n++] = 1;   // RTS
  const size_t kl = strnlen(s_wantKey, CSUH_MAX_KEY - 1);
  p[n++] = (uint8_t)kl;
  for (size_t i = 0; i < kl; ++i) p[n++] = (uint8_t)s_wantKey[i];
  sendFrame(CSUH_T_OPEN, p, n);
  s_lastOpenTry = millis();
}

// Everything that must be forgotten when the far end restarts. Called on a fresh
// epoch and on begin()/end(). The device list goes because addresses are assigned
// by enumeration order and are not a property of the radio -- carrying a stale
// list across a reboot would offer the operator keys that no longer resolve.
void resetPeerState() {
  s_devN = 0;
  s_open = false;
  s_devName[0] = 0;
  s_rx.clear(); s_tx.clear();
  s_txCredit   = CSUH_CREDIT_INIT;
  s_peerCredit = CSUH_CREDIT_INIT;
  s_lastOpenTry = 0;
}

// ---- frame handlers -------------------------------------------------------
void onHello(const uint8_t* p, size_t n) {
  if (n < 7) return;
  const uint8_t ver = p[0];
  const uint32_t epoch = (uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                         ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
  // p[5] = the helper's max payload, p[6] = its initial credit. We do not adapt to
  // them: a helper built from a different csuh_proto.h is a parity failure, not a
  // negotiation, and quietly running with mismatched sizing would hide it.
  // Audit F8 (0.9.73): and now they are ENFORCED, not just documented -- the
  // repository parity gates protect the tree, but nothing in the tree can stop
  // an operator running today's CardSat against last week's flashed helper.
  // Runtime is the only place a flashed-pair mismatch can be caught.
  if (ver == CSUH_PROTO_VER &&
      (p[5] != CSUH_MAX_PAYLOAD || p[6] != CSUH_CREDIT_INIT)) {
    char b[72];
    snprintf(b, sizeof(b), "helper proto params differ - flash BOTH boards");
    setErr(b);
    s_linked = false;
    return;
  }
  if (ver != CSUH_PROTO_VER) {
    char b[72];
    snprintf(b, sizeof(b), "helper protocol v%u, expected v%u", ver, CSUH_PROTO_VER);
    setErr(b);
    s_linked = false;
    return;
  }
  const size_t fl = (n > 7) ? p[7] : 0;
  if (fl && n >= 8 + fl) {
    const size_t c = fl < sizeof(s_fw) - 1 ? fl : sizeof(s_fw) - 1;
    memcpy(s_fw, p + 8, c); s_fw[c] = 0;
  }
  const bool fresh = !s_haveEpoch || epoch != s_epoch;
  if (fresh && s_haveEpoch) s_restarts++;   // a CHANGED epoch is a real helper reboot
  s_epoch = epoch; s_haveEpoch = true;
  s_linked = true;
  if (fresh) {
    // The far end restarted (or this is the first HELLO). Drop everything we
    // believed about it and re-establish. Re-issuing the OPEN here is what makes a
    // helper reboot mid-pass invisible to the operator.
    resetPeerState();
    setErr("");
    snprintf(s_event, sizeof(s_event), "helper ready (fw %s)", s_fw[0] ? s_fw : "?");
    sendEmpty(CSUH_T_ENUM_REQ);
    if (s_wantOpen) sendOpen();
  }
}

void onEnum(const uint8_t* p, size_t n) {
  if (n < 3) return;
  const uint8_t idx = p[0], cnt = p[1], flags = p[2];
  if (cnt == 0) { s_devN = 0; return; }        // explicit "nothing plugged in"
  if (idx == 0) s_devN = 0;                    // first of a burst: start clean
  if (idx >= MAX_DEV || n < 4) return;
  size_t o = 3;
  const size_t kl = p[o++];
  if (o + kl > n || kl >= CSUH_MAX_KEY) return;
  Dev d;
  memcpy(d.key, p + o, kl); d.key[kl] = 0; o += kl;
  if (o < n) {
    const size_t ll = p[o++];
    if (o + ll <= n && ll < CSUH_MAX_LABEL) { memcpy(d.label, p + o, ll); d.label[ll] = 0; }
  }
  d.live = (flags & CSUH_DEV_LIVE) != 0;
  d.open = (flags & CSUH_DEV_OPEN) != 0;
  s_devTab[idx] = d;
  if (idx + 1 > s_devN) s_devN = (uint8_t)(idx + 1);
}

void onOpened(const uint8_t* p, size_t n) {
  if (n < 2) return;
  const uint8_t ok = p[0], err = p[1];
  char name[CSUH_MAX_LABEL] = {0};
  if (n > 2) {
    const size_t nl = p[2];
    if (3 + nl <= n && nl < sizeof(name)) { memcpy(name, p + 3, nl); name[nl] = 0; }
  }
  if (ok) {
    s_open = true;
    strlcpy(s_devName, name, sizeof(s_devName));
    setErr("");
    return;
  }
  s_open = false;
  switch (err) {
    case CSUH_ERR_NODEV:
      setErr(s_wantKey[0] ? "Helper: nominated device not found"
                          : "Helper: no USB device attached");
      break;
    case CSUH_ERR_AMBIG:
      // Never guessed between. Two identical adapters are indistinguishable by
      // VID:PID, and picking one would work perfectly right up until it did not.
      setErr("Helper: more than one device - nominate one");
      break;
    case CSUH_ERR_HOST:   setErr("Helper: USB host not running"); break;
    case CSUH_ERR_BADARG: setErr("Helper: rejected the port settings"); break;
    case CSUH_ERR_NOTCDC: {
      char b[72];
      // %.40s: name can be a full CSUH_MAX_LABEL and the rest of the sentence is
      // 27 characters, which would overrun this buffer.
      snprintf(b, sizeof(b), "Helper: %.40s has no serial port", name[0] ? name : "device");
      setErr(b);
      break;
    }
    default: setErr("Helper: open failed"); break;
  }
}

void onEvent(const uint8_t* p, size_t n) {
  if (n < 1) return;
  const uint8_t code = p[0];
  char det[48] = {0};
  if (n > 1) {
    const size_t dl = p[1];
    if (2 + dl <= n && dl < sizeof(det)) { memcpy(det, p + 2, dl); det[dl] = 0; }
  }
  switch (code) {
    case CSUH_EV_ATTACH:
      snprintf(s_event, sizeof(s_event), "attached %s", det);
      sendEmpty(CSUH_T_ENUM_REQ);
      // A device arriving is exactly when a wanted-but-unopened port should be
      // retried, rather than waiting out the retry timer: plugging the radio in
      // is the operator's way of saying "now".
      if (s_wantOpen && !s_open) sendOpen();
      break;
    case CSUH_EV_DETACH:
      snprintf(s_event, sizeof(s_event), "detached %s", det);
      sendEmpty(CSUH_T_ENUM_REQ);
      break;
    case CSUH_EV_PORTLOST:
      s_open = false;
      s_devName[0] = 0;
      setErr("Helper: the radio went away");
      snprintf(s_event, sizeof(s_event), "port lost %s", det);
      break;
    case CSUH_EV_REBIND:
      s_open = true;
      snprintf(s_event, sizeof(s_event), "re-bound %s", det);
      setErr("");
      break;
    case CSUH_EV_OVERRUN:
      snprintf(s_event, sizeof(s_event), "helper overrun %s", det);
      setErr("Helper: bytes lost (overrun)");
      break;
    case CSUH_EV_HOSTDOWN:
      setErr("Helper: USB host failed to start");
      snprintf(s_event, sizeof(s_event), "usb host down");
      break;
    case CSUH_EV_RESTART:
      // Expected: a rescan was asked for. Drop the DEVICE model now rather than
      // waiting for the silence timer, so the UI does not show a stale list --
      // but do NOT clear s_linked: this event ARRIVED over the Grove link, which
      // is proof the link is alive, and clearing the flag here made the screen
      // lie ("no link" while frames flowed). Sixth bench found the class of bug:
      // link liveness belongs to the timers and the HELLO handshake exclusively;
      // events may only invalidate what they actually know about (devices).
      // s_haveEpoch stays too -- the restart, when it lands, announces itself
      // with a NEW epoch in its HELLO, and that path already re-establishes and
      // re-OPENs cleanly.
      resetPeerState();
      snprintf(s_event, sizeof(s_event), "helper restarting");
      break;
    case CSUH_EV_USBERR:
    default:
      snprintf(s_event, sizeof(s_event), "helper: %s", det);
      break;
  }
}

void onStat(const uint8_t* p, size_t n) {
  // Floor at the ORIGINAL 36-byte block, not CSUH_STAT_LEN: an older helper's
  // STAT must keep parsing after the 0.9.73 trailing extension, or a firmware
  // mismatch would silently blank the stats screen. The extension is read only
  // when it is actually present.
  if (n < 36) return;
  auto g32 = [&](size_t o) -> uint32_t {
    return (uint32_t)p[o] | ((uint32_t)p[o+1] << 8) |
           ((uint32_t)p[o+2] << 16) | ((uint32_t)p[o+3] << 24);
  };
  s_stats.framesRx = g32(CSUH_STAT_O_FRAMESRX);
  s_stats.framesTx = g32(CSUH_STAT_O_FRAMESTX);
  s_stats.crcErr   = g32(CSUH_STAT_O_CRCERR);
  s_stats.cobsErr  = g32(CSUH_STAT_O_COBSERR);
  s_stats.usbRx    = g32(CSUH_STAT_O_USBRX);
  s_stats.usbTx    = g32(CSUH_STAT_O_USBTX);
  s_stats.overrun  = g32(CSUH_STAT_O_OVERRUN);
  s_stats.heap     = g32(CSUH_STAT_O_HEAP);
  s_stats.uptime   = g32(CSUH_STAT_O_UPTIME);
  s_stats.ext      = (n >= CSUH_STAT_LEN);
  if (s_stats.ext) {
    s_stats.seen   = p[CSUH_STAT_O_SEEN];
    s_stats.usable = p[CSUH_STAT_O_USABLE];
    s_stats.hostUp = p[CSUH_STAT_O_HOSTUP];
  }
  s_stats.valid    = true;
}

void handleFrame(uint8_t type, const uint8_t* p, size_t n) {
  switch (type) {
    case CSUH_T_HELLO:      s_helloRx++; onHello(p, n); break;
    case CSUH_T_ENUM:       onEnum(p, n);  break;
    case CSUH_T_OPENED:     onOpened(p, n); break;
    case CSUH_T_DATA_IN:
      s_rxData++;
      if (s_peerCredit > 0) s_peerCredit--;
      // Guaranteed to fit: grantCredit() never lets the helper hold more credit
      // than the ring has whole-frame room for. push() returning short would mean
      // that invariant broke, so count it rather than losing it silently.
      if (n) {
        const size_t took = s_rx.push(p, n);
        if (took < n) setErr("Helper: RX ring overflow (credit invariant)");
      }
      break;
    case CSUH_T_PONG: {
      // Audit F11: the token is validated, and a matching pong yields an RTT.
      // A mismatched token means the reply belongs to an older ping -- counted,
      // because a steady stream of stale pongs is a servicing-delay signature.
      s_rxPong++;
      const uint16_t tok = (n >= 2) ? (uint16_t)(p[0] | (p[1] << 8)) : 0;
      if (n >= 2 && tok == s_pingTok && s_pingSentMs) {
        s_lastRttMs = millis() - s_pingSentMs;
        s_pingSentMs = 0;
      } else if (n >= 2) s_pongBad++;
      break;
    }
    case CSUH_T_CREDIT_IN:
      s_rxCredit++;
      if (n >= 1) {
        s_txCredit += (int16_t)p[0];
        if (s_txCredit > CSUH_CREDIT_INIT) s_txCredit = CSUH_CREDIT_INIT;
      }
      break;
    case CSUH_T_EVENT:      s_rxEvent++; onEvent(p, n); break;
    case CSUH_T_STAT:       s_rxStat++; onStat(p, n);  break;
    default: break;         // ignore unknown types so a newer helper stays usable
  }
}

// ---- link RX --------------------------------------------------------------
void pumpRx() {
  if (!s_uart) return;
  int avail = s_uart->available();
  // available() returns -1 on a UART that has been torn down (the HWCDC lesson
  // from 0.9.58, generalised): a `while (available())` on -1 never exits.
  if (avail <= 0) return;
  while (avail-- > 0) {
    const int c = s_uart->read();
    if (c < 0) break;
    const uint8_t b = (uint8_t)c;
    if (b != 0x00) {
      if (s_accN < sizeof(s_acc)) s_acc[s_accN++] = b;
      else s_overlong = true;
      continue;
    }
    if (s_overlong || s_accN == 0) {
      if (s_overlong) s_cobsErr++;
      s_accN = 0; s_overlong = false;
      continue;
    }
    uint8_t raw[CSUH_MAX_FRAME];
    const size_t rn = csuhCobsDecode(s_acc, s_accN, raw, sizeof(raw));
    s_accN = 0;
    if (!rn) { s_cobsErr++; continue; }
    uint8_t ty, po; const uint8_t* pp; size_t pl;
    if (!csuhParseFrame(raw, rn, &ty, &po, &pp, &pl)) { s_crcErr++; continue; }
    // Only helper->host types are legal inbound. Anything else means the Grove
    // pair is looped back on itself, which is worth naming rather than
    // half-processing into nonsense.
    if (!(ty & 0x80)) { s_cobsErr++; continue; }
    s_rxFrames++;
    s_rxLastTy = ty;
    s_lastValid = millis();
    handleFrame(ty, pp, pl);
  }
}

// ---- link TX --------------------------------------------------------------
void pumpTx() {
  while (s_txCredit > 0 && s_tx.used()) {
    uint8_t buf[CSUH_MAX_PAYLOAD];
    const size_t n = s_tx.pop(buf, sizeof(buf));
    if (!n) break;
    sendFrame(CSUH_T_DATA_OUT, buf, n);
    s_txCredit--;
  }
}

}  // namespace csuh

// ===========================================================================
//  HelperStream
// ===========================================================================
//  Qualified with csuh:: throughout -- see the note on the namespace above.
int HelperStream::available() {
  UsbHelper::service();
  return (int)csuh::s_rx.used();
}
int HelperStream::read() {
  UsbHelper::service();
  uint8_t b;
  return csuh::s_rx.pop(&b, 1) ? (int)b : -1;
}
int HelperStream::peek() {
  UsbHelper::service();
  return csuh::s_rx.peek1();
}
size_t HelperStream::write(uint8_t b) { return write(&b, 1); }
size_t HelperStream::write(const uint8_t* d, size_t n) {
  if (!csuh::s_started || !d || !n) return 0;
  // Audit F4 (0.9.73): ALL-OR-NOTHING for anything that fits the ring. The old
  // incremental push could time out mid-command and hand the radio the first
  // half of a CAT frame -- and a malformed command a radio acts on is strictly
  // worse than a dropped one the backend retries. Oversized writes (bigger than
  // the whole ring; nothing CAT-shaped is) keep the legacy chunked behavior.
  const uint32_t t0 = millis();
  if (n <= csuh::TX_RING - 1) {
    for (;;) {
      if ((csuh::TX_RING - 1) - csuh::s_tx.used() >= n) {
        csuh::s_tx.push(d, n);
        csuh::pumpTx();
        return n;
      }
      UsbHelper::service();
      if ((millis() - t0) >= csuh::WRITE_WAIT_MS) {
        csuh::s_writeTimeouts++;
        return 0;                    // nothing queued: the command stays whole
      }
      delay(1);
    }
  }
  size_t done = 0;
  while (done < n) {
    done += csuh::s_tx.push(d + done, n - done);
    if (done >= n) break;
    UsbHelper::service();
    if ((millis() - t0) >= csuh::WRITE_WAIT_MS) break;
    delay(1);
  }
  csuh::pumpTx();
  return done;
}
void HelperStream::flush() {
  const uint32_t t0 = millis();
  while (csuh::s_tx.used() && (millis() - t0) < csuh::WRITE_WAIT_MS) {
    UsbHelper::service();
    delay(1);
  }
}

// ===========================================================================
//  UsbHelper
// ===========================================================================
namespace UsbHelper {

// Every name below without a qualifier comes from csuh (above). The directive is
// scoped to THIS namespace, so it cannot leak into the rest of the monolith.
using namespace csuh;

bool begin(uint32_t linkBaud) {
  if (s_started) end();
  // Clamp to a rate the helper actually scans. Anything else could never link,
  // and a link that silently never comes up is the hardest kind of fault to read.
  bool ok = false;
  for (int i = 0; i < CSUH_BAUD_N; ++i) if (CSUH_BAUDS[i] == linkBaud) { ok = true; break; }
  s_baud = ok ? linkBaud : CSUH_BAUDS[0];

  if (!s_rx.alloc(RX_RING) || !s_tx.alloc(TX_RING)) {
    s_rx.free_(); s_tx.free_();
    setErr("Helper: out of memory");
    return false;
  }
  // Two-wire mode (pinMode 0) through the shared opener, so the "release the
  // previously bound pins" bookkeeping stays global. There is one Grove UART and
  // exactly one place that configures it.
  s_uart = &civUartOpen(0, s_baud, CIV_UART_NUM, CIV_RX_PIN, CIV_TX_PIN);
  s_started = true;
  s_linked = false; s_haveEpoch = false; s_epoch = 0;
  s_accN = 0; s_overlong = false;
  s_fw[0] = 0; s_errMsg[0] = 0; s_event[0] = 0;
  s_rxFrames = s_crcErr = s_cobsErr = s_txFrames = 0;
  s_stats = CsuhStats();
  s_lastValid = 0; s_lastHelloReq = 0; s_lastPing = 0; s_lastGrantMs = 0;
  resetPeerState();
  sendEmpty(CSUH_T_HELLO_REQ);
  return true;
}

void end() {
  if (!s_started) return;
  // Tell the helper to drop DTR before we go. On CDC-ACM, DTR is the only "the
  // host has this port open" signal there is, and a radio that keys its CAT
  // session off it otherwise needs a POWER CYCLE to talk again (measured on a
  // TH-D75). Cheap insurance: one frame.
  if (s_linked && s_open) { sendEmpty(CSUH_T_CLOSE); if (s_uart) s_uart->flush(); }
  if (s_uart) s_uart->end();
  s_uart = nullptr;
  s_rx.free_(); s_tx.free_();
  s_started = false; s_linked = false; s_open = false;
  s_wantOpen = false;
  s_devN = 0; s_devName[0] = 0;
}

bool started() { return s_started; }

void service() {
  if (!s_started) return;
  // Re-entrancy guard: HelperStream::write() calls service() while service() may
  // itself be inside pumpTx(). Without this, a full ring plus a slow link would
  // recurse until the stack gave out -- and a stack overflow inside the Doppler
  // tick reports as a watchdog panic with a backtrace that names neither.
  static bool inService = false;
  if (inService) return;
  inService = true;

  const uint32_t now = millis();
  pumpRx();

  // Liveness. A link that has gone quiet is not linked, whatever it last said.
  //
  // ROOT CAUSE OF THE 0.9.73 LINK "BLINK" (fourteen bench cycles): `now` above
  // is captured BEFORE pumpRx(), and pumpRx() stamps s_lastValid with a LATER
  // millis() whenever a frame decodes during the pump. The old comparison
  // `now - s_lastValid` was then OLDER minus NEWER: unsigned underflow,
  // ~4.29e9, always > LINK_DEAD_MS -- and the link flag died on the spot, at
  // random-looking moments, MORE often the MORE inbound traffic there was.
  // Proven by instrumentation, not narrative: dr reached 110 while the age
  // display (computed with a fresh millis()) never left 0.1 s. The fix is a
  // fresh capture after the pump, plus a signed-delta guard so any future
  // ordering drift degrades to "slightly late timeout" instead of this.
  const uint32_t nowLive = millis();
  if (s_linked && s_lastValid &&
      (int32_t)(nowLive - s_lastValid) > (int32_t)LINK_DEAD_MS) {
    s_linked = false;
    s_haveEpoch = false;
    s_open = false;
    s_linkDrops++;
    setErr("Helper: no response over Grove");
  }
  if (!s_linked) {
    if ((now - s_lastHelloReq) > HELLO_RETRY_MS) {
      s_lastHelloReq = now;
      s_helloReqTx++;
      sendEmpty(CSUH_T_HELLO_REQ);
    }
  } else {
    // KEEPALIVE INVARIANT (first bench flap, root cause): gate the ping on the
    // last frame this end SENT, never on traffic received. The helper's liveness
    // clock sees only frames FROM us -- so during an IC-705 transceive flood the
    // old `s_lastValid` gate kept this end silent while data poured IN, the
    // helper hit its 5 s relock, walked off the correct baud, and its own TX
    // turned to noise here... which unlinked this end 5-6 s later, and the two
    // ends chased each other in a visible up/down flap. RX traffic says the FAR
    // end is alive; only TX traffic proves that WE are.
    if ((now - s_lastTxMs) > PING_IDLE_MS) {
      s_pingTok = (uint16_t)(now & 0xFFFF);
      s_pingSentMs = now;
      const uint8_t tok[2] = { (uint8_t)(s_pingTok & 0xFF), (uint8_t)(s_pingTok >> 8) };
      sendFrame(CSUH_T_PING, tok, 2);
    }
    // A port that was asked for but is not open gets retried. This covers the
    // radio being plugged in after CardSat engaged, which is the ordinary order
    // of operations on a bench.
    if (s_wantOpen && !s_open && (now - s_lastOpenTry) > OPEN_RETRY_MS) sendOpen();
  }

  pumpTx();
  grantCredit(false);
  inService = false;
}

bool        linked()        { return s_linked; }
const char* helperVersion() { return s_fw; }
uint32_t    linkBaud()      { return s_baud; }
uint32_t    lastSeenMs()    { return s_lastValid; }

void requestScan() { if (s_started) sendEmpty(CSUH_T_ENUM_REQ); }
uint8_t     deviceCount() { return s_devN; }
const char* deviceLabel(uint8_t i) { return i < s_devN ? s_devTab[i].label : ""; }
const char* deviceKey(uint8_t i)   { return i < s_devN ? s_devTab[i].key   : ""; }
bool        deviceIsOpen(uint8_t i){ return i < s_devN && s_devTab[i].open; }

void rescan() {
  if (!s_started) return;
  sendEmpty(CSUH_T_RESCAN);
  // Do not wait for the EVENT: if the helper is wedged badly enough to need a
  // rescan it may not answer at all. Drop our model now; the next HELLO (new
  // epoch) rebuilds it.
  s_linked = false; s_haveEpoch = false;
  resetPeerState();
  snprintf(s_event, sizeof(s_event), "rescan requested");
}

void configure(const char* key) {
  strlcpy(s_wantKey, key ? key : "", sizeof(s_wantKey));
}

bool open(uint32_t baud, uint8_t dataBits, uint8_t parity, uint8_t stopBits) {
  if (!s_started) { setErr("Helper: not started"); return false; }
  s_pBaud = baud ? baud : 19200;
  s_pBits = dataBits ? dataBits : 8;
  s_pPar  = parity;
  s_pStop = stopBits ? stopBits : 1;
  s_wantOpen = true;
  s_open = false;
  if (s_linked) sendOpen();
  // True means "the request is in flight", not "the port is open" -- the OPEN is
  // a round trip and the helper may not even be powered yet. active() is the
  // question worth asking, and the retry in service() keeps trying meanwhile.
  return true;
}

void close() {
  s_wantOpen = false;
  if (s_started && s_linked) sendEmpty(CSUH_T_CLOSE);
  s_open = false;
  s_devName[0] = 0;
  s_rx.clear(); s_tx.clear();
}

bool        active()     { return s_started && s_linked && s_open; }
Stream*     stream()     { return active() ? (Stream*)&s_stream : nullptr; }
const char* deviceName() { return s_devName; }
const char* lastError()  { return s_errMsg; }
const char* lastEvent()  { return s_event; }

void setLine(uint32_t baud, uint8_t dataBits, uint8_t parity, uint8_t stopBits) {
  s_pBaud = baud ? baud : s_pBaud;
  s_pBits = dataBits ? dataBits : s_pBits;
  s_pPar  = parity;
  s_pStop = stopBits ? stopBits : s_pStop;
  if (!active()) return;
  uint8_t p[7];
  p[0] = (uint8_t)(s_pBaud);       p[1] = (uint8_t)(s_pBaud >> 8);
  p[2] = (uint8_t)(s_pBaud >> 16); p[3] = (uint8_t)(s_pBaud >> 24);
  p[4] = s_pBits; p[5] = s_pPar; p[6] = s_pStop;
  sendFrame(CSUH_T_LINE, p, 7);
}

void setModem(bool dtr, bool rts) {
  if (!active()) return;
  const uint8_t p[2] = { (uint8_t)(dtr ? 1 : 0), (uint8_t)(rts ? 1 : 0) };
  sendFrame(CSUH_T_MODEM, p, 2);
}

void requestStats() { if (s_started && s_linked) sendEmpty(CSUH_T_STAT_REQ); }
uint32_t  restarts()     { return s_restarts; }
uint32_t  helloReqTx()   { return s_helloReqTx; }
uint32_t  helloRx()      { return s_helloRx; }
uint32_t  lastRttMs()    { return s_lastRttMs; }
uint32_t  linkDrops()    { return s_linkDrops; }
uint32_t  lastValidAge() { return s_lastValid ? (millis() - s_lastValid) : 0; }
void rxClassCounts(uint32_t* pong, uint32_t* stat, uint32_t* data,
                   uint32_t* credit, uint32_t* event, uint8_t* lastTy) {
  *pong = s_rxPong; *stat = s_rxStat; *data = s_rxData;
  *credit = s_rxCredit; *event = s_rxEvent; *lastTy = s_rxLastTy;
}
const char* lastErr()    { return s_errMsg; }
uint8_t   txCredit()     { return (uint8_t)s_txCredit; }
uint32_t  localCrcErr()  { return s_crcErr; }
uint32_t  localCobsErr() { return s_cobsErr; }

const CsuhStats& stats() { return s_stats; }

uint32_t rxFrames()      { return s_rxFrames; }
uint32_t rxCrcErrors()   { return s_crcErr; }
uint32_t rxCobsErrors()  { return s_cobsErr; }
uint32_t txFrames()      { return s_txFrames; }

}  // namespace UsbHelper

#endif  // CARDSAT_HAS_USBHELPER
