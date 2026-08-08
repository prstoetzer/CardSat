// =============================================================================
//  CardSatUsbHelper - a second USB host for CardSat, on the end of a Grove cable.
//
//  WHAT PROBLEM THIS SOLVES
//
//  The ESP32-S3 has OTG_NUM_HOST_CHAN = 8 host channels for the entire USB bus,
//  one per open pipe including every device's default control pipe. A hub costs 2,
//  a CDC radio 3, a vendor-serial adapter 4. The IC-705 contains its own internal
//  TI TUSB2046 hub fanning out a CDC device and a Burr-Brown CODEC, so the radio
//  alone costs 5. That makes:
//
//      hub + IC-705              = 7   works
//      hub + TH-D75 + FTDI       = 8   works, no headroom
//      hub + TH-D75 + IC-705     = 10  NOT SOLVABLE IN SOFTWARE
//
//  usb_host_interface_claim() allocates a pipe for EVERY endpoint of an interface,
//  all-or-nothing, so only endpoints that sit on an interface of their own can be
//  skipped. That is true of CDC control interfaces and not of vendor-serial
//  adapters. There is no arrangement of eight channels that fits two USB radios
//  when one of them is an IC-705.
//
//  A second microcontroller brings its own eight. This firmware is that second
//  controller: CardSat keeps one USB device on the Cardputer and hands the other to
//  an M5StickS3 over the Grove UART.
//
//  WHAT THIS IS NOT
//
//  It is a BYTE PIPE. It speaks no CAT dialect, holds no radio model, knows nothing
//  about Doppler or VFOs or rotator grammar, and stores nothing across reboots.
//  CardSat owns every protocol decision exactly as it does for a USB adapter
//  plugged into the Cardputer directly.
//
//  That is a deliberate reversal of its predecessor. CardSatDualRig owned CAT state
//  and carried its own radio catalogue, which meant every radio fix had to be made
//  twice and kept honest across two release cycles -- a standing cost that bought
//  nothing once CAT_DUAL landed natively in CardSat. Retired in 0.9.73.
//
//  Being stateless is also what makes the recovery story simple: there is no
//  configuration to lose, so rebooting is always a safe answer, and CSUH_T_RESCAN
//  uses exactly that. A USB host stack that has wedged (usb_host_install()
//  returning ESP_ERR_INVALID_STATE for the rest of a boot) has no in-place fix;
//  a reboot has no failure mode.
//
//  PROTOCOL: see csuh_proto.h -- COBS framing, CRC-16, credit-based flow control,
//  auto-baud. That header is shared VERBATIM with CardSat's src/csuh_proto.h and
//  the two copies must stay byte-identical; tools/check_csuh_parity.py is the gate.
//
//  Board: M5StickS3 (ESP32-S3-PICO-1-N8R8, 8 MB flash, 8 MB PSRAM, native USB OTG).
//  Libraries: M5Unified, EspUsbHost 2.7.0 **PATCHED** -- build against the copy
//  vendored at third_party/EspUsbHost/ in the CardSat repository, not the stock
//  library. The patch that matters most here is the CDC serial OUT drain: without
//  it, a radio that stops reading its port leaves a transfer enqueued forever,
//  which blocks the interface release, the client deregistration and the host
//  uninstall in turn. The patched copy also defaults ESPUSBHOST_CLAIM_AUDIO to 0,
//  which is what keeps a composite radio's audio interface from spending the
//  channels this firmware exists to conserve.
//
//  POWER. The Stick does not source USB VBUS -- M5Stack frame its USB-C port as a
//  power input, and its 5 V boost feeds the Grove / Hat2 EXT_5V rail instead. So a
//  self-powered hub is required for the radio regardless, exactly as it is on the
//  Cardputer. The Stick itself can be fed 5 V from the Cardputer's Grove port over
//  the same cable that carries the data; see the SAFETY note in setup().
//
//  Build: the exact arduino-cli command is in firmware/README.md. Two things are
//  easy to get wrong and produce a binary that looks fine:
//    * the extra defines MUST go in compiler.cpp.extra_flags (which APPENDS).
//      build.extra_flags REPLACES, wiping the core's CORE_DEBUG_LEVEL, which
//      reintroduces an EspUsbHost 'TAG' compile error.
//    * EspUsbHost must be the PATCHED copy. The stock library compiles and appears
//      to work, then strands the USB stack the first time a radio stops answering.
//  In the IDE: board "ESP32S3 Dev Module", USB Mode = "Hardware CDC and JTAG",
//  Flash 8 MB, PSRAM enabled, Partition "8M with spiffs", Core Debug Level "Error".
// =============================================================================

#ifndef ESP_USB_HOST_MAX_DEVICES
#define ESP_USB_HOST_MAX_DEVICES 4      // hub + device (+ headroom)
#endif

// Serial console debug. OFF by default and it should stay that way in a shipped
// build: the USB host takes the S3's one internal USB PHY, which is the same PHY
// the HWCDC console sits behind. Writing to a console whose peer is gone is at
// best wasted time inside a blocking write and at worst a stall on the loop that
// is supposed to be moving CAT bytes. CardSat learned this the expensive way --
// its 0.9.58 freeze was a `while (Serial.available())` spinning on an
// HWCDC::available() that returns -1 after end(). Set to 1 only for bring-up on a
// board with no USB device attached.
#ifndef CSUH_DEBUG
#define CSUH_DEBUG 0
#endif
#if CSUH_DEBUG
  #define DBG(...)  do { Serial.printf(__VA_ARGS__); } while (0)
#else
  #define DBG(...)  do { } while (0)
#endif

// VENDORED ESP-IDF USB HOST STACK -- must precede <Arduino.h>, which M5Unified
// pulls in. Installed by CardSat's tools/vendor_usb_host.sh.
//
// Without this the sketch links Arduino's PREBUILT libusb.a and silently loses
// every USB host fix CardSat carries: enumeration-stage retry, the reset hold and
// recovery timings, the enumeration filter callback, and the ESP_LOGD narration.
// That was the state this firmware shipped in until it was caught by the 0.9.72
// USB review follow-up -- and it is backwards, because the helper exists to host
// the HARDEST device on the bench (an IC-705, with its own internal hub) and was
// doing it with the weaker of the two stacks available.
//
// Verify it took, in the map: `libraries/UsbHostSrc` in the hundreds and
// `libusb.a(` at zero. arduino-cli silently skips the library if this include is
// missing, and the build still succeeds.
#include <UsbHostSrc.h>
#include <M5Unified.h>
#include <EspUsbHost.h>
#include <atomic>
#include "csuh_proto.h"

// Reported to the host in CSUH_T_HELLO and shown on the status screen. Tracks the
// CardSat release it ships with, because the pairing that matters is helper vs
// host protocol support, not this firmware's own history.
#define CSUH_FW_VERSION "0.9.73"

// ---------------------------------------------------------------- wiring
// M5StickS3 Grove port is GPIO9 / GPIO10.
//   Cardputer G2 (TX) -> Stick GPIO9  (RX)
//   Cardputer G1 (RX) <- Stick GPIO10 (TX)
// Both ends are 3.3 V, so no level shifter. If nothing is ever received, swap
// these two -- a reversed pair is silent, not noisy, so it looks identical to a
// dead cable.
static const int GROVE_RX_PIN = 9;
static const int GROVE_TX_PIN = 10;
static HardwareSerial& gLink = Serial1;
// 4 KB driver RX buffer (default 256). At 230400 the default is ~11 ms of
// headroom -- less than ONE status-sprite push -- so any loop stall while the
// host is transmitting dropped bytes mid-frame. 4 KB rides out ~180 ms.
static const size_t CSUH_LINK_RXBUF = 4096;

// ---------------------------------------------------------------- tuning
// USB -> link ring. Sized well past the credit window (8 frames x 128 B = 1 KB)
// so a host that stalls for a screen redraw does not immediately cost bytes. The
// Stick has 8 MB of PSRAM, so this is nearly free here -- unlike on CardSat,
// where the same buffer would come out of a no-PSRAM heap.
// P1 (0.9.73 RAM/PSRAM pass): the rings live in PSRAM when it is present -- the
// M5StickS3 has 8 MB of it and the USB transfer layer cannot use it anyway (DMA
// wants internal), so this is memory that would otherwise sit idle. 30x deeper
// rings mean a CI-V radio in transceive mode can flood for MINUTES during a Grove
// link stall without losing a byte (vs ~2 s at 8 KB), and because grantCredit()
// grants from ring free space, helper->host credit starvation stops being a
// reachable state. ~10 KB of internal heap goes back to the USB host stack.
// alloc() falls back to internal RAM at the small sizes if PSRAM is absent.
static const size_t USB_RX_RING = 262144;
// link -> USB ring. Small: CAT commands are tens of bytes and go straight out.
static const size_t USB_TX_RING = 32768;
// Screen-on time after a button press or a CSUH_T_WAKE.
static const uint32_t UI_ON_MS = 12000;
// Return accumulated credit at least this often even if the batch is small, so a
// trickle of single bytes cannot stall behind a batching threshold.
static const uint32_t CREDIT_FLUSH_MS = 20;
static const uint8_t  CREDIT_BATCH = 4;
// A bound device that reports not-ready for this long is presumed gone even
// though no disconnect callback arrived; drop the binding and try to re-find it.
static const uint32_t PORT_STALE_MS = 4000;
// Unsolicited HELLO cadence while no host has said anything yet.
static const uint32_t HELLO_IDLE_MS = 1000;

// =============================================================================
//  SPSC byte ring
// =============================================================================
//  Single producer, single consumer, lock-free. The producer for the USB->link
//  ring is the EspUsbHost client task; the consumer is loop(). Those are
//  different tasks and may be on different cores, so the indices are atomics with
//  acquire/release ordering rather than plain volatile -- on Xtensa a plain
//  volatile store happens to work, but "happens to work on this core" is how a
//  ring corrupts silently after a core-affinity change.
class ByteRing {
public:
  bool alloc(size_t n) {
    // PSRAM first (task-context only ever touches these -- no ISRs), internal as
    // the fallback, and at a survivable size: a Stick without PSRAM configured
    // still works, just with the old shallow buffering.
    _buf = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_buf) { n = n > 8192 ? 8192 : n; _buf = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_8BIT); }
    if (!_buf) return false;
    _cap = n; _head.store(0); _tail.store(0);
    return true;
  }
  // Producer side. Returns bytes accepted; a short return means the ring is full
  // and the caller MUST account for the loss (see gStat.overrun).
  size_t push(const uint8_t* d, size_t n) {
    const size_t head = _head.load(std::memory_order_relaxed);
    const size_t tail = _tail.load(std::memory_order_acquire);
    size_t space = (tail + _cap - head - 1) % _cap;
    if (n > space) n = space;
    for (size_t i = 0; i < n; ++i) _buf[(head + i) % _cap] = d[i];
    _head.store((head + n) % _cap, std::memory_order_release);
    return n;
  }
  // Consumer side.
  size_t pop(uint8_t* d, size_t n) {
    const size_t tail = _tail.load(std::memory_order_relaxed);
    const size_t head = _head.load(std::memory_order_acquire);
    size_t have = (head + _cap - tail) % _cap;
    if (n > have) n = have;
    for (size_t i = 0; i < n; ++i) d[i] = _buf[(tail + i) % _cap];
    _tail.store((tail + n) % _cap, std::memory_order_release);
    return n;
  }
  size_t used() const {
    return (_head.load(std::memory_order_acquire) + _cap
            - _tail.load(std::memory_order_acquire)) % _cap;
  }
  size_t freeSpace() const { return _cap ? (_cap - 1 - used()) : 0; }
  void clear() { _tail.store(_head.load(std::memory_order_acquire)); }
private:
  uint8_t* _buf = nullptr;
  size_t   _cap = 0;
  std::atomic<size_t> _head{0}, _tail{0};
};

static ByteRing gUsbRx;    // device -> link
static ByteRing gUsbTx;    // link -> device

// One chunk in hand between popping it off gUsbTx and the USB host accepting it.
// sendSerial() refuses while an OUT transfer is in flight, so a refusal is routine
// and the bytes must survive it -- see serviceLinkToUsb().
static uint8_t  gTxHold[CSUH_MAX_PAYLOAD];
static size_t   gTxHoldN = 0;

// =============================================================================
//  State
// =============================================================================
static EspUsbHost gUsb;
static bool gUsbUp = false;

struct SeenDev {
  bool     used = false;
  bool     isHub = false;
  uint8_t  address = 0;
  uint16_t vid = 0, pid = 0;
  char     key[CSUH_MAX_KEY]   = {0};
  char     label[CSUH_MAX_LABEL] = {0};
};
static const size_t MAX_SEEN = 6;
static SeenDev gSeen[MAX_SEEN];
static uint32_t gAttachCount = 0;        // raw attaches since boot (never decremented)
static char     gLastAttach[24] = "-";   // last raw attach, vid:pid@addr
static uint32_t gLoopGapMax = 0;         // ninth bench: worst loop() stall since boot
static uint32_t gLoopLastMs = 0;

// gSeen is written by the EspUsbHost client task (connect/disconnect callbacks)
// and read by loop() (findDevice, handleEnum, the status screen). Those are
// different tasks, possibly on different cores, so every touch is inside this
// spinlock. Without it the visible failure would be a torn label or a key read
// half-updated -- which presents as "the helper offered a device that does not
// exist", a symptom nobody would trace back to a missing lock.
static portMUX_TYPE gSeenMux = portMUX_INITIALIZER_UNLOCKED;

// ---- events raised from the USB task ----------------------------------------
// The USB callbacks must NOT write frames themselves: loop() is writing frames to
// the same UART, and two tasks interleaving COBS blocks produces garbage that
// fails CRC at the far end -- an error that would look like a bad cable and be
// chased as one. Callbacks enqueue here; loop() drains and sends.
struct PendEvent { uint8_t code; char detail[CSUH_MAX_KEY]; };
static const size_t EV_Q_N = 8;
static PendEvent gEvQ[EV_Q_N];
static std::atomic<size_t> gEvHead{0}, gEvTail{0};
static uint32_t gEvDropped = 0;

static void queueEvent(uint8_t code, const char* detail) {
  const size_t head = gEvHead.load(std::memory_order_relaxed);
  const size_t next = (head + 1) % EV_Q_N;
  if (next == gEvTail.load(std::memory_order_acquire)) { gEvDropped++; return; }
  gEvQ[head].code = code;
  if (detail) strlcpy(gEvQ[head].detail, detail, sizeof(gEvQ[head].detail));
  else        gEvQ[head].detail[0] = 0;
  gEvHead.store(next, std::memory_order_release);
}

struct PortState {
  bool     open = false;
  uint8_t  addr = ESP_USB_HOST_ANY_ADDRESS;
  char     key[CSUH_MAX_KEY] = {0};      // what the host ASKED for (may be a VID:PID form)
  char     bound[CSUH_MAX_KEY] = {0};    // what we actually bound to
  char     name[CSUH_MAX_LABEL] = {0};
  uint32_t baud = 19200;
  uint8_t  bits = 8, parity = CSUH_PAR_NONE, stop = 1;
  bool     dtr = true, rts = true;
  bool     online = false;
  uint32_t offlineSinceMs = 0;
};
static PortState gPort;

struct LinkState {
  bool     locked = false;
  uint8_t  baudIdx = 0;
  uint32_t lastTryMs = 0;
  uint32_t lastValidMs = 0;
  uint32_t epoch = 0;
  bool     sawHost = false;      // a host frame has been decoded at least once
  bool     holdIdx = false;      // after a silence-unlock, dwell on the CURRENT baud first
  uint32_t lastHelloMs = 0;
  // Credit. txCredit is what WE may send (DATA_IN); peerCredit is our model of
  // what the HOST may send (DATA_OUT). Modelling the peer's window explicitly is
  // what lets grantCredit() maintain the invariant that every frame the host is
  // allowed to send has somewhere to land:
  //     peerCredit * CSUH_MAX_PAYLOAD <= gUsbTx.freeSpace()
  // Returning credit blindly per frame consumed is the obvious alternative and it
  // is wrong: the moment the radio stops accepting bytes the ring fills, the host
  // keeps sending on credit it still holds, and CAT bytes vanish. A dropped CI-V
  // byte does not present as a link fault -- it presents as a radio fault.
  int16_t  txCredit = CSUH_CREDIT_INIT;
  int16_t  peerCredit = CSUH_CREDIT_INIT;
  uint32_t lastGrantMs = 0;
};
static LinkState gLink_;

struct Stats {
  uint32_t framesRx = 0, framesTx = 0, crcErr = 0, cobsErr = 0;
  uint32_t usbRx = 0, usbTx = 0;
  // Two overrun counters, not one, because they are incremented by different
  // tasks: rxOverrun by the USB client task (device -> ring), txOverrun by loop()
  // (link -> ring). Sharing one counter across two tasks would lose counts to a
  // read-modify-write race, and an undercounted overrun is worse than none -- it
  // reads as "the link is clean" while bytes are going missing.
  uint32_t rxOverrun = 0;    // USB task only
  uint32_t txOverrun = 0;    // loop() only
  uint32_t overrun() const { return rxOverrun + txOverrun; }
};
static Stats gStat;

static uint32_t gUiOnUntil = 0;
static bool     gUiPainted = false;
static uint32_t gLastUiMs = 0;
static uint32_t gRestartAtMs = 0;      // non-zero: reboot when millis() passes it

// =============================================================================
//  Link: framing
// =============================================================================
static uint8_t gRxAcc[CSUH_MAX_ENCODED];
static size_t  gRxAccN = 0;
static bool    gRxOverlong = false;   // current block already too long: discard to the delimiter

static void sendFrame(uint8_t type, const uint8_t* payload, size_t plen) {
  uint8_t wire[CSUH_MAX_ENCODED];
  const size_t n = csuhBuildFrame(type, 0, payload, plen, wire, sizeof(wire));
  if (!n) return;
  gLink.write(wire, n);
  gStat.framesTx++;
}
static void sendEmpty(uint8_t type) { sendFrame(type, nullptr, 0); }

// Audit F3: events queued while unlocked wait for lock (queueEvent is the
// buffered path; this direct sender is called from handlers that only run on a
// decoded host frame, which itself implies lock).
static void sendEvent(uint8_t code, const char* detail) {
  uint8_t p[CSUH_MAX_PAYLOAD];
  size_t n = 0;
  p[n++] = code;
  const size_t dl = detail ? strnlen(detail, CSUH_MAX_PAYLOAD - 2) : 0;
  p[n++] = (uint8_t)dl;
  for (size_t i = 0; i < dl; ++i) p[n++] = (uint8_t)detail[i];
  sendFrame(CSUH_T_EVENT, p, n);
}

static void sendHello() {
  uint8_t p[CSUH_MAX_PAYLOAD];
  size_t n = 0;
  p[n++] = CSUH_PROTO_VER;
  p[n++] = (uint8_t)(gLink_.epoch      ); p[n++] = (uint8_t)(gLink_.epoch >>  8);
  p[n++] = (uint8_t)(gLink_.epoch >> 16); p[n++] = (uint8_t)(gLink_.epoch >> 24);
  p[n++] = (uint8_t)CSUH_MAX_PAYLOAD;
  p[n++] = (uint8_t)CSUH_CREDIT_INIT;
  const char* fw = CSUH_FW_VERSION;
  const size_t fl = sizeof(CSUH_FW_VERSION) - 1;
  p[n++] = (uint8_t)fl;
  for (size_t i = 0; i < fl; ++i) p[n++] = (uint8_t)fw[i];
  sendFrame(CSUH_T_HELLO, p, n);
  // A HELLO resets both credit windows: the host does the same on receipt, so the
  // two ends cannot disagree about how much is in flight after a reconnect.
  gLink_.txCredit = CSUH_CREDIT_INIT;
  gLink_.peerCredit = CSUH_CREDIT_INIT;
}

// Grant the host only as many DATA_OUT frames as the link->USB ring can absorb in
// full. See the note on LinkState::peerCredit for why this is space-based rather
// than consumption-based.
static void grantCredit(bool force) {
  if (!gLink_.locked) return;
  size_t capacity = gUsbTx.freeSpace() / CSUH_MAX_PAYLOAD;
  if (capacity > CSUH_CREDIT_INIT) capacity = CSUH_CREDIT_INIT;
  int grant = (int)capacity - (int)gLink_.peerCredit;
  if (grant <= 0) return;
  if (!force && grant < CREDIT_BATCH &&
      (millis() - gLink_.lastGrantMs) < CREDIT_FLUSH_MS) return;
  const uint8_t k = (uint8_t)(grant > 255 ? 255 : grant);
  sendFrame(CSUH_T_CREDIT_IN, &k, 1);
  gLink_.peerCredit += (int16_t)k;
  gLink_.lastGrantMs = millis();
}

// =============================================================================
//  USB: device registry
// =============================================================================
// Identical key construction to CardSat's usbserial.cpp makeKey(). It has to be
// identical: the key the operator picks on the helper screen is stored in
// CardSat's settings and matched on BOTH sides, so a format that differs by a
// separator would make every saved selection stop resolving with no error to say
// why. Serial-first matters because two adapters of the same model -- the likely
// radio + rotator case -- are indistinguishable by VID:PID alone.
static void makeKey(char* out, size_t n, uint16_t vid, uint16_t pid,
                    const char* serial, uint8_t address) {
  if (serial && *serial) snprintf(out, n, "%04x:%04x/%s", vid, pid, serial);
  else                   snprintf(out, n, "%04x:%04x@%u", vid, pid, (unsigned)address);
}

static void registerSeen(const EspUsbHostDeviceInfo& info) {
  // Build outside the lock: snprintf on the string descriptors is the expensive
  // part and there is no reason to hold up loop() for it.
  SeenDev d;
  d.used = true; d.address = info.address; d.isHub = info.isHub;
  d.vid = info.vid; d.pid = info.pid;
  makeKey(d.key, sizeof(d.key), info.vid, info.pid, info.serial, info.address);
  const char* prod = (info.product && *info.product) ? info.product : "USB serial";
  snprintf(d.label, sizeof(d.label), "%s %04x:%04x", prod, info.vid, info.pid);

  bool full = false;
  portENTER_CRITICAL(&gSeenMux);
  SeenDev* slot = nullptr;
  for (auto& s : gSeen) if (s.used && s.address == info.address) { slot = &s; break; }
  if (!slot) for (auto& s : gSeen) if (!s.used) { slot = &s; break; }
  if (slot) *slot = d; else full = true;
  portEXIT_CRITICAL(&gSeenMux);

  // FULL. Dropping it silently would make the device look like it never
  // enumerated -- a completely different fault with a completely different fix.
  if (full) queueEvent(CSUH_EV_USBERR, "device table full");
}

static void unregisterSeen(uint8_t address) {
  portENTER_CRITICAL(&gSeenMux);
  for (auto& s : gSeen) if (s.used && s.address == address) s.used = false;
  portEXIT_CRITICAL(&gSeenMux);
}

// Find a device for a requested key. Mirrors CardSat's findAdapter(): exact match
// first, then -- only for the address-keyed form, and only when exactly one live
// device carries the same VID:PID -- accept that one.
//
// The fallback exists because the USB address is assigned by enumeration ORDER,
// so it is not a property of the radio. A TH-D75 or IC-705 reports no serial
// number at all, so its key is the address form; plug it into a different hub
// port, or power things up in a different order, and an exact strcmp can never
// match again. The "exactly one" condition is what keeps two identical adapters
// ambiguous rather than guessed between.
//
// Diagnostic counts for the status screen and STAT. seen = every live non-hub
// registry entry regardless of capability; usable = the subset a port could
// open. The GAP between them is the diagnosis: a composite device whose serial
// claim failed is seen-but-not-usable.
static void countDevs(uint8_t& seen, uint8_t& usable);

// Is this registry slot something CAT or a rotator could actually talk to? A hub
// never is, and neither is any device the host did not claim a serial OUT endpoint
// for -- an audio function, a HID device, a composite sibling. registerSeen()
// records everything that enumerates, so the filter belongs at the point of use,
// where enumeration has settled and serialReady() is definitive.
static bool usableDev(const SeenDev& d) {
  return d.used && !d.isHub && gUsb.serialReady(d.address);
}

static void countDevs(uint8_t& seen, uint8_t& usable) {
  seen = 0; usable = 0;
  SeenDev snap[MAX_SEEN];
  snapshotSeen(snap);
  for (size_t i = 0; i < MAX_SEEN; ++i) {
    if (!snap[i].used || snap[i].isHub) continue;
    seen++;
    if (gUsb.serialReady(snap[i].address)) usable++;
  }
}

// Returns the index into `snap`, -1 for none, -2 for ambiguous.
//
// Takes a SNAPSHOT rather than gSeen directly. The registry is mutated by the USB
// task, so a lookup that returned an index into the live array could have that
// slot re-used before the caller read it -- and the caller would then open a
// different device than the one it matched, which is the worst outcome available
// here (two radios on the bench, silently the wrong one).
static int findDevice(const SeenDev* snap, const char* wantKey) {
  if (!wantKey || !*wantKey) {
    // No key nominated: accept the only SERIAL-CAPABLE device, if there is exactly
    // one. The capability test matters most here, on the auto path: a composite
    // radio whose non-serial function enumerates as its own device would make a
    // helper carrying one radio look like a helper carrying two, and the answer
    // would be CSUH_ERR_AMBIG on the default setting.
    int hit = -1, n = 0;
    for (size_t i = 0; i < MAX_SEEN; ++i)
      if (usableDev(snap[i])) { hit = (int)i; n++; }
    if (n == 1) return hit;
    return n ? -2 : -1;
  }
  for (size_t i = 0; i < MAX_SEEN; ++i)
    if (usableDev(snap[i]) && strcmp(snap[i].key, wantKey) == 0) return (int)i;

  const char* at = strchr(wantKey, '@');
  if (!at) return -1;                              // serial-keyed: no fallback, none needed
  const size_t vp = (size_t)(at - wantKey);        // "vvvv:pppp"
  int hit = -1, n = 0;
  for (size_t i = 0; i < MAX_SEEN; ++i) {
    if (!usableDev(snap[i])) continue;
    if (strncmp(snap[i].key, wantKey, vp) == 0 && snap[i].key[vp] == '@') { hit = (int)i; n++; }
  }
  if (n == 1) return hit;
  return n ? -2 : -1;
}

// Copy the registry out from under the USB task. ~600 bytes and a few
// microseconds inside the spinlock; every reader in loop() goes through here.
static void snapshotSeen(SeenDev* out) {
  portENTER_CRITICAL(&gSeenMux);
  memcpy(out, gSeen, sizeof(gSeen));
  portEXIT_CRITICAL(&gSeenMux);
}

// ---- CDC control lines ----------------------------------------------------
// Many CDC-ACM devices ignore host traffic until DTR is asserted -- the line is
// how the host says "a terminal is present". The Kenwood TH-D74/D75 are in that
// group. The address-based API (setSerialBaudRate / setSerialConfig) has no
// control-line call; only EspUsbHostCdcSerial exposes setDtr/setRts, and
// constructing one is inert because it registers as a data sink only inside
// begin(), which is deliberately never called here.
static void setControlLines(uint8_t addr, bool dtr, bool rts) {
  if (addr == ESP_USB_HOST_ANY_ADDRESS) return;
  EspUsbHostCdcSerial line(gUsb);
  line.setAddress(addr);
  line.setDtr(dtr);
  line.setRts(rts);
}

// De-assert DTR/RTS when releasing a device. On CDC-ACM, DTR is what says "the
// host has this port open" and there is no other close notification, so a radio
// that keys its CAT session off DTR otherwise believes the session is still open
// after we let go. Measured on a TH-D75: CAT could not be re-established without
// power-cycling the RADIO. Failures are ignored on purpose -- if the device has
// already been unplugged the control transfer cannot land, and that is exactly
// the case where nothing needs saying.
static void releasePort(const char* why) {
  if (!gPort.open) return;
  setControlLines(gPort.addr, false, false);
  DBG("[port] closed (%s)\n", why ? why : "");
  gPort.open = false;
  gPort.online = false;
  gPort.addr = ESP_USB_HOST_ANY_ADDRESS;
  gPort.bound[0] = 0;
  gPort.name[0] = 0;
  gUsbTx.clear();
  gUsbRx.clear();
  gTxHoldN = 0;                 // the chunk in hand belongs to the port being closed
}

static EspUsbHostSerialParity parityOf(uint8_t p) {
  switch (p) {
    case CSUH_PAR_ODD:  return ESP_USB_HOST_SERIAL_PARITY_ODD;
    case CSUH_PAR_EVEN: return ESP_USB_HOST_SERIAL_PARITY_EVEN;
    default:            return ESP_USB_HOST_SERIAL_PARITY_NONE;
  }
}

static bool applyLineCoding() {
  EspUsbHostSerialConfig sc;
  sc.baud     = gPort.baud;
  sc.dataBits = gPort.bits;
  sc.parity   = parityOf(gPort.parity);
  sc.stopBits = (gPort.stop == 2) ? ESP_USB_HOST_SERIAL_STOP_BITS_2
                                  : ESP_USB_HOST_SERIAL_STOP_BITS_1;
  return gUsb.setSerialConfig(sc, gPort.addr);
}

static void sendOpened(uint8_t ok, uint8_t err, const char* name) {
  uint8_t p[CSUH_MAX_PAYLOAD];
  size_t n = 0;
  p[n++] = ok; p[n++] = err;
  const size_t nl = name ? strnlen(name, CSUH_MAX_LABEL - 1) : 0;
  p[n++] = (uint8_t)nl;
  for (size_t i = 0; i < nl; ++i) p[n++] = (uint8_t)name[i];
  sendFrame(CSUH_T_OPENED, p, n);
}

// =============================================================================
//  USB callbacks (run on the EspUsbHost client task -- byte stores only)
// =============================================================================
static void onUsbConnected(const EspUsbHostDeviceInfo& info) {
  // Raw attach diagnostics (0.9.73 bench): count and remember every attach the
  // host stack ever reports, BEFORE any filtering, and never clear them on
  // detach. "attach 0" on the status screen after plugging a device means the
  // interrupt level never fired -- a power/wiring fault (VBUS, CC, cable), not a
  // firmware one -- while "attach N, usable 0" means enumeration works and the
  // serial claim is what failed. Those two need completely different benches.
  gAttachCount++;
  snprintf(gLastAttach, sizeof(gLastAttach), "%04x:%04x@%u",
           (unsigned)info.vid, (unsigned)info.pid, (unsigned)info.address);
  registerSeen(info);
  if (info.isHub) return;
  char k[CSUH_MAX_KEY];
  makeKey(k, sizeof(k), info.vid, info.pid, info.serial, info.address);
  queueEvent(CSUH_EV_ATTACH, k);
}

static void onUsbDisconnected(const EspUsbHostDeviceInfo& info) {
  char k[CSUH_MAX_KEY];
  makeKey(k, sizeof(k), info.vid, info.pid, info.serial, info.address);
  unregisterSeen(info.address);
  if (gPort.open && gPort.addr == info.address) {
    // Do NOT call releasePort() here: it issues control transfers, and this runs
    // on the host's own task for a device that has already gone. Mark it and let
    // loop() do the tidying.
    gPort.open = false;
    gPort.online = false;
    gPort.addr = ESP_USB_HOST_ANY_ADDRESS;
    queueEvent(CSUH_EV_PORTLOST, k);
  } else {
    queueEvent(CSUH_EV_DETACH, k);
  }
}

static void onUsbSerialData(const EspUsbHostSerialData& d) {
  if (!gPort.open || d.address != gPort.addr) return;
  const size_t took = gUsbRx.push(d.data, d.length);
  gStat.usbRx += (uint32_t)took;
  if (took < d.length) gStat.rxOverrun += (uint32_t)(d.length - took);
}

// =============================================================================
//  Link: frame handlers
// =============================================================================
static void handleOpen(const uint8_t* p, size_t n) {
  if (n < 8) { sendOpened(0, CSUH_ERR_BADARG, ""); return; }
  const uint32_t baud = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  const uint8_t bits = p[4], par = p[5], stop = p[6];
  const uint8_t dtr = p[7], rts = (n > 8) ? p[8] : 1;
  const size_t kOff = 9;
  if (n < kOff + 1) { sendOpened(0, CSUH_ERR_BADARG, ""); return; }
  const size_t klen = p[kOff];
  if (n < kOff + 1 + klen || klen >= CSUH_MAX_KEY) { sendOpened(0, CSUH_ERR_BADARG, ""); return; }

  if (baud < 300 || baud > 1000000UL || bits < 5 || bits > 8 ||
      par > CSUH_PAR_EVEN || stop < 1 || stop > 2) {
    sendOpened(0, CSUH_ERR_BADARG, ""); return;
  }
  if (!gUsbUp) { sendOpened(0, CSUH_ERR_HOST, ""); return; }

  char want[CSUH_MAX_KEY];
  memcpy(want, p + kOff + 1, klen); want[klen] = 0;

  // Audit F5 (0.9.73): an OPEN identical to the active port is answered with
  // OPENED(success) WITHOUT touching the USB device. The host retries OPEN
  // whenever its OPENED reply was lost -- and the old unconditional
  // release+reopen turned every such retry into a DTR/RTS bounce on the radio,
  // which the TH-D75's session sensitivity (patch 9 history) tolerates badly.
  // Identical means: same requested key, baud, bits, parity, stop, DTR and RTS,
  // with the bound device still serial-ready.
  if (gPort.open &&
      strncmp(gPort.key, want, sizeof(gPort.key)) == 0 &&
      gPort.baud == baud && gPort.bits == bits && gPort.parity == par &&
      gPort.stop == stop && gPort.dtr == (dtr != 0) && gPort.rts == (rts != 0) &&
      gUsb.serialReady(gPort.addr)) {
    sendOpened(1, 0, gPort.name);
    return;
  }

  // A v1 OPEN otherwise REPLACES whatever is open. Re-opening after a settings
  // change is what the host means, and requiring a CLOSE first would add a
  // state the two ends could disagree about.
  releasePort("re-open");

  SeenDev snap[MAX_SEEN];
  snapshotSeen(snap);
  const int idx = findDevice(snap, want);
  if (idx == -2) { sendOpened(0, CSUH_ERR_AMBIG, ""); return; }
  if (idx < 0)   { sendOpened(0, CSUH_ERR_NODEV, ""); return; }

  gPort.addr = snap[idx].address;
  strlcpy(gPort.key,   want,           sizeof(gPort.key));
  strlcpy(gPort.bound, snap[idx].key,  sizeof(gPort.bound));
  strlcpy(gPort.name,  snap[idx].label, sizeof(gPort.name));
  gPort.baud = baud; gPort.bits = bits; gPort.parity = par; gPort.stop = stop;
  gPort.dtr = dtr != 0; gPort.rts = rts != 0;

  if (!gUsb.serialReady(gPort.addr)) {
    // Enumerated, but no usable serial interface was claimed -- a HID device, or a
    // composite whose CDC function the library could not bind. Say which, rather
    // than letting it look like the device is absent.
    gPort.addr = ESP_USB_HOST_ANY_ADDRESS;
    sendOpened(0, CSUH_ERR_NOTCDC, snap[idx].label);
    return;
  }

  if (!applyLineCoding()) {
    // Reporting OPENED-ok after a failed SET_LINE_CODING would leave CardSat
    // believing a line rate the radio never received -- which presents as a radio
    // that answers nothing, and sends the operator looking at the radio.
    gPort.addr = ESP_USB_HOST_ANY_ADDRESS;
    sendOpened(0, CSUH_ERR_BADARG, snap[idx].label);
    return;
  }
  setControlLines(gPort.addr, gPort.dtr, gPort.rts);
  gUsbRx.clear(); gUsbTx.clear();
  gTxHoldN = 0;
  gPort.open = true;
  gPort.online = true;
  gPort.offlineSinceMs = 0;
  DBG("[port] open %s addr %u @%lu\n", gPort.bound, gPort.addr, (unsigned long)baud);
  sendOpened(1, CSUH_ERR_NONE, gPort.name);
}

static void handleEnum() {
  SeenDev snap[MAX_SEEN];
  snapshotSeen(snap);
  uint8_t live[MAX_SEEN]; uint8_t cnt = 0;
  for (uint8_t i = 0; i < MAX_SEEN; ++i)
    if (usableDev(snap[i])) live[cnt++] = i;
  if (!cnt) {
    // An explicit empty answer. Silence would be indistinguishable from a link
    // that dropped the request, and the operator would have no way to tell
    // "nothing is plugged in" from "the helper is not listening".
    uint8_t p[3] = { 0, 0, 0 };
    sendFrame(CSUH_T_ENUM, p, 3);
    return;
  }
  for (uint8_t i = 0; i < cnt; ++i) {
    const SeenDev& d = snap[live[i]];
    uint8_t p[CSUH_MAX_PAYLOAD];
    size_t n = 0;
    p[n++] = i; p[n++] = cnt;
    uint8_t flags = CSUH_DEV_LIVE;
    if (gPort.open && gPort.addr == d.address) flags |= CSUH_DEV_OPEN;
    p[n++] = flags;
    const size_t kl = strnlen(d.key, CSUH_MAX_KEY - 1);
    p[n++] = (uint8_t)kl;
    for (size_t j = 0; j < kl; ++j) p[n++] = (uint8_t)d.key[j];
    const size_t ll = strnlen(d.label, CSUH_MAX_LABEL - 1);
    p[n++] = (uint8_t)ll;
    for (size_t j = 0; j < ll; ++j) p[n++] = (uint8_t)d.label[j];
    sendFrame(CSUH_T_ENUM, p, n);
  }
}

static void handleStat() {
  uint8_t p[CSUH_STAT_LEN];
  auto put32 = [&](size_t off, uint32_t v) {
    p[off] = (uint8_t)(v); p[off+1] = (uint8_t)(v >> 8);
    p[off+2] = (uint8_t)(v >> 16); p[off+3] = (uint8_t)(v >> 24);
  };
  put32(CSUH_STAT_O_FRAMESRX, gStat.framesRx);
  put32(CSUH_STAT_O_FRAMESTX, gStat.framesTx);
  put32(CSUH_STAT_O_CRCERR,   gStat.crcErr);
  put32(CSUH_STAT_O_COBSERR,  gStat.cobsErr);
  put32(CSUH_STAT_O_USBRX,    gStat.usbRx);
  put32(CSUH_STAT_O_USBTX,    gStat.usbTx);
  put32(CSUH_STAT_O_OVERRUN,  gStat.overrun());
  put32(CSUH_STAT_O_HEAP,     (uint32_t)ESP.getFreeHeap());
  put32(CSUH_STAT_O_UPTIME,   millis() / 1000UL);
  uint8_t seen = 0, usable = 0;
  countDevs(seen, usable);
  p[CSUH_STAT_O_SEEN]   = seen;
  p[CSUH_STAT_O_USABLE] = usable;
  p[CSUH_STAT_O_HOSTUP] = gUsbUp ? 1 : 0;
  sendFrame(CSUH_T_STAT, p, CSUH_STAT_LEN);
}

static void uiWake() { gUiOnUntil = millis() + UI_ON_MS; gUiPainted = false; }

static void handleFrame(uint8_t type, const uint8_t* p, size_t n) {
  gLink_.sawHost = true;
  switch (type) {
    case CSUH_T_HELLO_REQ:
      sendHello();
      break;
    case CSUH_T_ENUM_REQ:
      handleEnum();
      break;
    case CSUH_T_OPEN:
      handleOpen(p, n);
      break;
    case CSUH_T_CLOSE:
      releasePort("host");
      break;
    case CSUH_T_DATA_OUT: {
      if (gLink_.peerCredit > 0) gLink_.peerCredit--;
      // Guaranteed to fit: grantCredit() never lets the host hold more credit than
      // the ring has whole-frame room for. A short push means that invariant broke,
      // so it is counted rather than lost quietly.
      if (n) {
        const size_t took = gUsbTx.push(p, n);
        if (took < n) gStat.txOverrun += (uint32_t)(n - took);
      }
      break;
    }
    case CSUH_T_MODEM:
      if (n >= 2 && gPort.open) {
        gPort.dtr = p[0] != 0; gPort.rts = p[1] != 0;
        setControlLines(gPort.addr, gPort.dtr, gPort.rts);
      }
      break;
    case CSUH_T_LINE:
      if (n >= 7 && gPort.open) {
        gPort.baud = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        gPort.bits = p[4]; gPort.parity = p[5]; gPort.stop = p[6];
        applyLineCoding();
      }
      break;
    case CSUH_T_PING:
      if (n >= 2) sendFrame(CSUH_T_PONG, p, 2);
      else        sendEmpty(CSUH_T_PONG);
      break;
    case CSUH_T_CREDIT_OUT:
      if (n >= 1) {
        gLink_.txCredit += (int16_t)p[0];
        if (gLink_.txCredit > CSUH_CREDIT_INIT) gLink_.txCredit = CSUH_CREDIT_INIT;
      }
      break;
    case CSUH_T_STAT_REQ:
      handleStat();
      break;
    case CSUH_T_RESCAN:
      // Stateless, so a reboot loses nothing and is the one recovery that cannot
      // itself get stuck. Announce first, then give the UART time to drain.
      sendEvent(CSUH_EV_RESTART, "rescan");
      gLink.flush();
      gRestartAtMs = millis() + 120;
      break;
    case CSUH_T_WAKE:
      if (n >= 1 && p[0]) uiWake(); else gUiOnUntil = 0;
      break;
    default:
      break;   // unknown types are ignored, so a newer host can add some safely
  }
}

// Pull bytes off the UART, reassemble frames, dispatch.
static void serviceLinkRx() {
  int avail = gLink.available();
  while (avail-- > 0) {
    const int c = gLink.read();
    if (c < 0) break;
    const uint8_t b = (uint8_t)c;
    if (b != 0x00) {
      if (gRxAccN < sizeof(gRxAcc)) gRxAcc[gRxAccN++] = b;
      else gRxOverlong = true;      // keep discarding until the next delimiter
      continue;
    }
    // delimiter
    if (gRxOverlong || gRxAccN == 0) {
      if (gRxOverlong) gStat.cobsErr++;
      gRxAccN = 0; gRxOverlong = false;
      continue;
    }
    uint8_t raw[CSUH_MAX_FRAME];
    const size_t rn = csuhCobsDecode(gRxAcc, gRxAccN, raw, sizeof(raw));
    gRxAccN = 0;
    if (!rn) { gStat.cobsErr++; continue; }
    uint8_t ty, po; const uint8_t* pp; size_t pl;
    if (!csuhParseFrame(raw, rn, &ty, &po, &pp, &pl)) { gStat.crcErr++; continue; }
    // Only host->helper types are legal inbound. A helper->host code arriving here
    // means the two Grove pairs are looped, which is worth naming rather than
    // half-processing.
    if (ty & 0x80) { gStat.cobsErr++; continue; }
    gStat.framesRx++;
    gLink_.lastValidMs = millis();
    if (!gLink_.locked) {
      gLink_.locked = true;
      DBG("[link] locked at %lu\n", (unsigned long)CSUH_BAUDS[gLink_.baudIdx]);
    }
    handleFrame(ty, pp, pl);
  }
}

// Auto-baud. The helper keeps no configuration, so it cannot remember the link
// rate; it walks CSUH_BAUDS[] until a frame passes CRC. This is only safe because
// the framing is CRC-checked -- at the wrong rate the decoder sees noise, and
// noise does not pass a CRC. Once locked, silence past CSUH_BAUD_RELOCK_MS drops
// back to scanning so a host that changes rate is picked up without a reboot.
static void serviceLinkBaud() {
  const uint32_t now = millis();
  if (gLink_.locked) {
    if (gLink_.lastValidMs && (now - gLink_.lastValidMs) > CSUH_BAUD_RELOCK_MS) {
      gLink_.locked = false;
      gLink_.sawHost = false;
      gLink_.lastTryMs = now;
      // Dwell on the rate we JUST held before scanning away from it. The first
      // bench flap sustained itself partly here: on a silence-unlock the scan
      // advanced immediately, so a host that was merely quiet -- not re-rated --
      // came back to a helper listening (and transmitting!) at the wrong baud,
      // and the resulting mutual garbage took a full scan cycle to resolve.
      // Silence overwhelmingly means "quiet host at the same rate", so the same
      // rate gets the first try.
      gLink_.holdIdx = true;
      DBG("[link] unlocked (silence)\n");
    }
    return;
  }
  if ((now - gLink_.lastTryMs) < CSUH_BAUD_TRY_MS) return;
  gLink_.lastTryMs = now;
  if (gLink_.holdIdx) gLink_.holdIdx = false;                       // first try: same rate
  else gLink_.baudIdx = (uint8_t)((gLink_.baudIdx + 1) % CSUH_BAUD_N);
  gLink.end();
  gLink.setRxBufferSize(CSUH_LINK_RXBUF);
  gLink.begin(CSUH_BAUDS[gLink_.baudIdx], SERIAL_8N1, GROVE_RX_PIN, GROVE_TX_PIN);
  gRxAccN = 0; gRxOverlong = false;
}

// Move device -> host, subject to credit.
static void serviceUsbToLink() {
  // Audit F3 (0.9.73): DATA_IN must not be transmitted while the link is
  // scanning -- an unlocked UART is at an arbitrary candidate rate, so anything
  // sent is garbage at the host: extra CRC errors there, and worse, consumed
  // credits the host never sees, feeding the F1 desync. Radio bytes stay in the
  // (256 KB) ring and drain the moment lock returns; handshake traffic is what
  // locking needs and is sent from the frame handlers, not here.
  if (!gLink_.locked) return;
  while (gLink_.txCredit > 0) {
    const size_t have = gUsbRx.used();
    if (!have) break;
    uint8_t buf[CSUH_MAX_PAYLOAD];
    const size_t n = gUsbRx.pop(buf, have < CSUH_MAX_PAYLOAD ? have : CSUH_MAX_PAYLOAD);
    if (!n) break;
    sendFrame(CSUH_T_DATA_IN, buf, n);
    gLink_.txCredit--;
  }
}

// Move host -> device.
//
// sendSerial() now returns false while an OUT transfer is still in flight (patched
// library, one reusable transfer per device). That makes REFUSALS ROUTINE rather
// than exceptional, and the old shape lost data on every one of them: it popped a
// chunk out of the ring and then discarded it if the send was refused. Bytes taken
// out of the ring must therefore be held until they are actually accepted.
//
// Holding them also completes the back-pressure chain. With the chunk still
// outstanding the ring does not drain, grantCredit() does not reopen the window,
// and the host stops sending -- which is what the credit scheme was always
// supposed to achieve. Previously credit bounded the LINK while the USB side was
// unbounded, so a stalled radio was absorbed by the heap instead of by flow
// control.
static void serviceLinkToUsb() {
  if (!gPort.open) { gUsbTx.clear(); gTxHoldN = 0; return; }
  for (;;) {
    if (!gTxHoldN) {
      gTxHoldN = gUsbTx.pop(gTxHold, sizeof(gTxHold));
      if (!gTxHoldN) return;                       // nothing waiting
    }
    if (!gUsb.sendSerial(gTxHold, gTxHoldN, gPort.addr)) return;   // busy: keep it
    gStat.usbTx += (uint32_t)gTxHoldN;
    gTxHoldN = 0;
  }
}

// Drain events raised by the USB task. This is the ONLY place they reach the wire,
// which is what keeps frame writes single-threaded.
static void serviceEvents() {
  if (!gLink_.locked) return;   // audit F3: events wait in the queue for lock
  for (;;) {
    const size_t tail = gEvTail.load(std::memory_order_relaxed);
    if (tail == gEvHead.load(std::memory_order_acquire)) break;
    sendEvent(gEvQ[tail].code, gEvQ[tail].detail);
    gEvTail.store((tail + 1) % EV_Q_N, std::memory_order_release);
  }
  if (gEvDropped) {
    // Say so rather than letting a lost attach look like a device that never
    // appeared. Reported once per burst, not once per loss.
    char b[32]; snprintf(b, sizeof(b), "events dropped %lu", (unsigned long)gEvDropped);
    gEvDropped = 0;
    sendEvent(CSUH_EV_USBERR, b);
  }
}

// A bound device that reports not-ready for a while is presumed gone even though
// no disconnect callback arrived -- the device yanked mid-transfer, or the host
// confused. Without this the port stays bound to an address that no longer exists
// and nothing ever recovers it.
static void servicePortHealth() {
  if (!gPort.open) return;
  const bool ready = gUsb.serialReady(gPort.addr);
  const uint32_t now = millis();
  if (ready) { gPort.online = true; gPort.offlineSinceMs = 0; return; }
  gPort.online = false;
  if (!gPort.offlineSinceMs) { gPort.offlineSinceMs = now; return; }
  if ((now - gPort.offlineSinceMs) < PORT_STALE_MS) return;

  // Try to re-find the same device (its address may have moved) before giving up.
  char want[CSUH_MAX_KEY];
  strlcpy(want, gPort.key, sizeof(want));
  const uint32_t baud = gPort.baud;
  const uint8_t bits = gPort.bits, par = gPort.parity, stp = gPort.stop;
  const bool dtr = gPort.dtr, rts = gPort.rts;
  releasePort("stale");

  SeenDev snap[MAX_SEEN];
  snapshotSeen(snap);
  const int idx = findDevice(snap, want);
  if (idx < 0) { sendEvent(CSUH_EV_PORTLOST, want); return; }
  gPort.addr = snap[idx].address;
  strlcpy(gPort.key, want, sizeof(gPort.key));
  strlcpy(gPort.bound, snap[idx].key, sizeof(gPort.bound));
  strlcpy(gPort.name, snap[idx].label, sizeof(gPort.name));
  gPort.baud = baud; gPort.bits = bits; gPort.parity = par; gPort.stop = stp;
  gPort.dtr = dtr; gPort.rts = rts;
  if (!gUsb.serialReady(gPort.addr)) { gPort.addr = ESP_USB_HOST_ANY_ADDRESS; return; }
  applyLineCoding();
  setControlLines(gPort.addr, dtr, rts);
  gPort.open = true; gPort.online = true; gPort.offlineSinceMs = 0;
  sendEvent(CSUH_EV_REBIND, gPort.bound);
}

// =============================================================================
//  Display
// =============================================================================
//  Off by default. The Stick runs from a 250 mAh cell and this firmware may sit
//  on a desk for a whole pass doing nothing that anybody needs to watch; a lit
//  screen is the largest avoidable draw on the board. Button A wakes it, and it
//  puts itself back to sleep.
static void screenOff() {
  M5.Display.setBrightness(0);
  M5.Display.sleep();
}
static void screenOn() {
  M5.Display.wakeup();
  M5.Display.setBrightness(96);
}

static const char* portStateText() {
  if (!gUsbUp)       return "USB host down";
  if (!gPort.open)   return "no port open";
  return gPort.online ? "open" : "open (offline)";
}

// The status paint goes through an offscreen sprite and lands on the panel in
// one pushSprite. The first bench session found the direct version FLASHING:
// fillScreen(TFT_BLACK) straight on the physical display, repainted every
// 400 ms, is a full-panel black wipe with the text redrawn after it -- a 2.5 Hz
// strobe. The sprite costs one 240x135 16-bit buffer (~63 KB, PSRAM-preferred by
// M5GFX) and turns the same cadence into a tear-free update. If the sprite
// cannot allocate, painting falls back to the old direct path -- ugly beats dark.
static M5Canvas gStatCanvas(&M5.Display);
static bool     gStatCanvasTried = false;

static void drawStatus() {
  if (!gStatCanvasTried) {
    gStatCanvasTried = true;
    gStatCanvas.setColorDepth(16);
    if (!gStatCanvas.createSprite(M5.Display.width(), M5.Display.height()))
      DBG("[UI] status sprite alloc failed; direct draws\n");
  }
  const bool spr = (gStatCanvas.width() > 0);
  LovyanGFX& d = spr ? (LovyanGFX&)gStatCanvas : (LovyanGFX&)M5.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextSize(1);
  const int W = d.width();
  int y = 2;
  const int lh = 10;

  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.setCursor(2, y); d.print("CardSat USB Helper"); y += lh + 2;

  // Link
  d.setTextColor(gLink_.locked ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  d.setCursor(2, y);
  if (gLink_.locked) d.printf("link %lu 8N1", (unsigned long)CSUH_BAUDS[gLink_.baudIdx]);
  else               d.printf("link: scanning %lu", (unsigned long)CSUH_BAUDS[gLink_.baudIdx]);
  y += lh;

  // Port
  d.setTextColor(gPort.open && gPort.online ? TFT_GREEN
                 : (gPort.open ? TFT_ORANGE : TFT_DARKGREY), TFT_BLACK);
  d.setCursor(2, y); d.print(portStateText()); y += lh;
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(2, y);
  d.print(gPort.open ? gPort.name : "-");
  y += lh + 2;

  // Traffic
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.setCursor(2, y); d.printf("usb rx %lu tx %lu",
                              (unsigned long)gStat.usbRx, (unsigned long)gStat.usbTx); y += lh;
  d.setCursor(2, y); d.printf("frm rx %lu tx %lu",
                              (unsigned long)gStat.framesRx, (unsigned long)gStat.framesTx); y += lh;

  // Errors -- red only when non-zero, so a clean link is visibly clean.
  const bool bad = gStat.crcErr || gStat.cobsErr || gStat.overrun();
  d.setTextColor(bad ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
  d.setCursor(2, y); d.printf("crc %lu cobs %lu ovr %lu",
                              (unsigned long)gStat.crcErr, (unsigned long)gStat.cobsErr,
                              (unsigned long)gStat.overrun());
  y += lh;

  // USB host + enumeration diagnostics (0.9.73 bench). The three lines answer,
  // in order: did the host stack even come up; has an attach interrupt EVER
  // fired (raw, pre-filter, never cleared); and of what attached, what could a
  // port actually open. attach 0 with a device plugged in = power/wiring, not
  // firmware -- see the README's "nothing ever enumerates" checklist.
  {
    uint8_t seen = 0, usable = 0;
    countDevs(seen, usable);
    d.setTextColor(gUsbUp ? TFT_DARKGREY : TFT_RED, TFT_BLACK);
    d.setCursor(2, y); d.print(gUsbUp ? "usb host up" : "USB HOST FAILED"); y += lh;
    d.setTextColor(gAttachCount ? TFT_LIGHTGREY : TFT_ORANGE, TFT_BLACK);
    d.setCursor(2, y); d.printf("attach %lu  seen %u usable %u",
                                (unsigned long)gAttachCount, seen, usable); y += lh;
    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    d.setCursor(2, y); d.printf("last %s", gLastAttach); y += lh;
    // Radio-TX hop truth (bench 4): txOvr counts DATA_OUT bytes the ring could
    // not take (should be 0); hold!=0 painted red means a chunk is stuck
    // between the ring and sendSerial -- the radio is refusing OUT transfers.
    d.setTextColor((gStat.txOverrun || gTxHoldN) ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
    d.setCursor(2, y); d.printf("txOvr %lu hold %u",
                                (unsigned long)gStat.txOverrun, (unsigned)gTxHoldN); y += lh;
    d.setTextColor(gLoopGapMax > 1000 ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
    d.setCursor(2, y); d.printf("loop gap max %lu ms", (unsigned long)gLoopGapMax);
  }
  y += lh;

  // Devices seen
  SeenDev snap[MAX_SEEN];
  snapshotSeen(snap);
  int nd = 0; for (auto& sd : snap) if (usableDev(sd)) nd++;
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.setCursor(2, y); d.printf("devices %d   heap %luk", nd,
                              (unsigned long)(ESP.getFreeHeap() / 1024)); y += lh;

  if (y + lh < d.height()) {
    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    d.setCursor(2, d.height() - lh - 1);
    d.print("BtnA: refresh");
  }
  (void)W;

  if (spr) gStatCanvas.pushSprite(0, 0);
}

static void serviceUi() {
  const uint32_t now = millis();
  if (M5.BtnA.wasPressed()) {
    if (now < gUiOnUntil) gUiOnUntil = 0;     // press again to dismiss early
    else                  uiWake();
    if (gUiOnUntil) screenOn(); else screenOff();
  }
  if (!gUiOnUntil) return;
  if (now >= gUiOnUntil) { gUiOnUntil = 0; screenOff(); return; }
  if (!gUiPainted || (now - gLastUiMs) > 400) {
    screenOn();
    drawStatus();
    gUiPainted = true;
    gLastUiMs = now;
  }
}

// =============================================================================
//  setup / loop
// =============================================================================
void setup() {
  auto mcfg = M5.config();
  // SAFETY -- read before changing. M5Unified's cfg.output_power defaults to TRUE,
  // which makes M5.begin() drive EXT_5V as an OUTPUT, and on this board EXT_5V is
  // the Grove 5 V pin. Clear it BEFORE begin() so the Stick never even momentarily
  // sources 5 V on the Grove line during init.
  mcfg.output_power = false;
  M5.begin(mcfg);

  // SAFETY -- force the Grove / EXT_5V rail to INPUT (never source 5 V).
  // Intended power topology when tethered to a Cardputer over Grove:
  //   Cardputer Grove = 5 V OUTPUT  --->  feeds  --->  Stick Grove = 5 V INPUT
  // If BOTH ends drove 5 V, two supplies would fight on one wire: the
  // short-circuit case M5Stack explicitly warns about. Forcing INPUT here means
  // the Stick can only ever RECEIVE 5 V on Grove, so it is safe no matter how the
  // Cardputer's rail is configured. setExtOutput(false) = INPUT, true = OUTPUT.
  // NEVER call setExtOutput(true) while anything feeds Grove 5 V.
  M5.Power.setExtOutput(false);
#ifdef CSUH_FORCE_EXT_OUTPUT
  // =============================== ANSWERED — DO NOT USE FOR USB ===============
  // This experiment asked whether the PM1 EXT boost also feeds USB-C VBUS. The
  // K150 schematic (V0.6) and the official docs answer NO, definitively:
  // EXT_5V_EN powers the Grove port, the Hat EXT_5V pin and the IR pair ONLY.
  // The USB-C VBUS pin runs input-only through an AW32901 OVP switch into the
  // LGS4056 charger -- there is no boost, no register, no firmware path that can
  // ever source it, and the CC pins carry fixed Rd pull-downs (permanent
  // device-role at the connector). VBUS for an attached radio must be injected
  // externally; see the README's "Field power" section for the sanctioned looms.
  //
  // The define is retained only for the non-USB case (powering a Grove
  // peripheral or the IR pair from the Stick) and the original hazard stands in
  // full: NEVER compile this in with the Cardputer feeding Grove 5 V -- two
  // supplies on one wire is the short-circuit case M5Stack warn about.
  M5.Power.setExtOutput(true);
  DBG("[PWR] CSUH_FORCE_EXT_OUTPUT: PM1 EXT boost ENABLED (Grove/Hat/IR only)\n");
#endif

  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  screenOff();                       // dark until asked; see the display note above

#if CSUH_DEBUG
  Serial.begin(115200);
  Serial.println("\nCardSatUsbHelper " CSUH_FW_VERSION);
#endif

  // A fresh identity each boot. The host compares this against the epoch it last
  // saw and re-OPENs its port when it changes, which is how a helper reboot in the
  // middle of a pass recovers without the operator doing anything.
  gLink_.epoch = esp_random();

  if (!gUsbRx.alloc(USB_RX_RING) || !gUsbTx.alloc(USB_TX_RING)) {
    // Nothing useful can happen without the rings. Say so on screen (this is the
    // one condition worth lighting the display unasked) and stop.
    screenOn();
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.setCursor(2, 2);
    M5.Display.print("ring alloc FAILED");
    for (;;) delay(1000);
  }

  gLink.setRxBufferSize(CSUH_LINK_RXBUF);
  gLink.begin(CSUH_BAUDS[0], SERIAL_8N1, GROVE_RX_PIN, GROVE_TX_PIN);
  gLink_.baudIdx = 0;
  gLink_.lastTryMs = millis();

  gUsb.onDeviceConnected(onUsbConnected);
  gUsb.onDeviceDisconnected(onUsbDisconnected);
  gUsb.onSerialData(onUsbSerialData);
  EspUsbHostConfig ucfg;
  gUsbUp = gUsb.begin(ucfg);
  if (!gUsbUp) {
    DBG("[USB] host begin FAILED\n");
    sendEvent(CSUH_EV_HOSTDOWN, "usb host begin failed");
  }
}

// Ninth bench instrument: measure loop stalls DIRECTLY instead of inferring
// them. gLoopGapMax is the worst gap between consecutive loop() passes since
// boot. The link starves when this exceeds the far end's 6.5 s timer; anything
// over ~1 s is abnormal and painted red. If the link blinks while this stays in
// the tens of milliseconds, the helper is exonerated and the fault is physical
// -- the G1 conductor between the boards.
void loop() {
  {
    const uint32_t nowMs = millis();
    if (gLoopLastMs && (nowMs - gLoopLastMs) > gLoopGapMax) gLoopGapMax = nowMs - gLoopLastMs;
    gLoopLastMs = nowMs;
  }
  M5.update();

  if (gRestartAtMs && (int32_t)(millis() - gRestartAtMs) >= 0) {
    gLink.flush();
    ESP.restart();
  }

  serviceLinkBaud();
  serviceLinkRx();
  serviceEvents();
  servicePortHealth();
  serviceLinkToUsb();
  serviceUsbToLink();
  grantCredit(false);

  // Unsolicited HELLO while nobody has talked to us. Costs one short frame a
  // second and means a host that boots second does not have to wait for its own
  // poll cycle -- and, more usefully, a host that was mid-session when the helper
  // rebooted learns about the new epoch immediately instead of after a timeout.
  const uint32_t now = millis();
  if (gLink_.locked && !gLink_.sawHost && (now - gLink_.lastHelloMs) > HELLO_IDLE_MS) {
    gLink_.lastHelloMs = now;
    sendHello();
  }

  serviceUi();
}
