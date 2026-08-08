// ===========================================================================
//  csuh_link_test.cpp -- src/usbhelper.cpp exercised against a mock helper
// ===========================================================================
//
//  This compiles and runs the ACTUAL shipped client (src/usbhelper.cpp, with a
//  minimal Arduino shim) against a mock CardSatUsbHelper that speaks the real
//  wire protocol. Nothing here is a reimplementation of the client: if the link
//  state machine has a bug, this finds it on a development machine rather than on
//  a bench where it would be indistinguishable from a cable fault, a wrong Grove
//  baud, or a radio that has gone deaf.
//
//  The scenarios are the ones that are expensive to reproduce on hardware:
//
//    1. Link-up handshake and firmware reporting.
//    2. Enumeration, including the explicit empty answer.
//    3. Open success, and every distinct failure the helper can report.
//    4. Byte round-trip through the Stream the CAT backends see.
//    5. THE CREDIT INVARIANT -- a helper flooding CI-V transceive frames faster
//       than the reader drains must never lose a byte, and must never send a
//       frame it was not granted.
//    6. Helper reboot mid-session: a new epoch must silently re-establish the
//       port with no operator action.
//    7. Link death and recovery.
//    8. Protocol-version mismatch is refused, not negotiated around.
//    9. Write back-pressure: a stalled link must not truncate a CAT command.
//
//  Build + run: tools/host_usbhelper/csuh_link_test.sh
// ===========================================================================
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "usbhelper.h"

// ---- shim globals the Arduino stand-in declares ---------------------------
uint32_t g_hostMillis = 1000;
std::deque<uint8_t> hostLinkToHelper;
std::deque<uint8_t> hostLinkToCardSat;

static HardwareSerial g_uart(1);
HardwareSerial& civUartOpen(uint8_t, uint32_t baud, int uartNum, int rxPin, int txPin) {
  (void)uartNum; (void)rxPin; (void)txPin;
  g_uart.end();
  g_uart.begin(baud, SERIAL_8N1, rxPin, txPin);
  return g_uart;
}

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, what) do { \
  ++g_checks; \
  if (!(cond)) { std::printf("  FAIL: %s  (line %d)\n", what, __LINE__); ++g_fails; } \
} while (0)

// ===========================================================================
//  Mock helper
// ===========================================================================
//  Speaks the real protocol. Deliberately NOT a copy of the firmware: it is an
//  independent implementation of the same spec, so a misreading of csuh_proto.h
//  that both copies shared would still be caught by the frame-level assertions
//  below rather than cancelling itself out.
struct MockHelper {
  bool     powered = true;
  uint32_t epoch = 0xA1B2C3D4;
  uint8_t  protoVer = CSUH_PROTO_VER;
  std::string fw = "0.9.73";

  struct Dev { std::string key, label; };
  std::vector<Dev> devs;
  int  openIdx = -1;
  bool refuseOpen = false;
  uint8_t refuseErr = CSUH_ERR_NODEV;

  // credit: how many DATA_IN frames we are allowed to send
  int  txCredit = CSUH_CREDIT_INIT;
  int  peerCredit = CSUH_CREDIT_INIT;   // what the host may send us

  // what the "radio" received, and what it is queued to send back
  std::vector<uint8_t> usbSeen;
  std::deque<uint8_t>  usbPending;

  // instrumentation
  int  creditViolations = 0;      // DATA_OUT arriving with no credit outstanding
  int  framesIn = 0, framesOut = 0;
  int  helloReqs = 0, enumReqs = 0, opens = 0, closes = 0, pings = 0, rescans = 0;

  std::deque<uint8_t> acc;

  void send(uint8_t type, const uint8_t* p, size_t n) {
    if (!powered) return;
    uint8_t wire[CSUH_MAX_ENCODED];
    const size_t w = csuhBuildFrame(type, 0, p, n, wire, sizeof(wire));
    for (size_t i = 0; i < w; ++i) hostLinkToCardSat.push_back(wire[i]);
    framesOut++;
  }
  void sendEmpty(uint8_t t) { send(t, nullptr, 0); }

  void sendHello() {
    std::vector<uint8_t> p;
    p.push_back(protoVer);
    p.push_back(uint8_t(epoch)); p.push_back(uint8_t(epoch >> 8));
    p.push_back(uint8_t(epoch >> 16)); p.push_back(uint8_t(epoch >> 24));
    p.push_back(CSUH_MAX_PAYLOAD);
    p.push_back(CSUH_CREDIT_INIT);
    p.push_back(uint8_t(fw.size()));
    for (char c : fw) p.push_back(uint8_t(c));
    send(CSUH_T_HELLO, p.data(), p.size());
    txCredit = CSUH_CREDIT_INIT;
    peerCredit = CSUH_CREDIT_INIT;
  }

  void sendEnum() {
    if (devs.empty()) { const uint8_t p[3] = {0, 0, 0}; send(CSUH_T_ENUM, p, 3); return; }
    for (size_t i = 0; i < devs.size(); ++i) {
      std::vector<uint8_t> p;
      p.push_back(uint8_t(i)); p.push_back(uint8_t(devs.size()));
      uint8_t fl = CSUH_DEV_LIVE;
      if ((int)i == openIdx) fl |= CSUH_DEV_OPEN;
      p.push_back(fl);
      p.push_back(uint8_t(devs[i].key.size()));
      for (char c : devs[i].key) p.push_back(uint8_t(c));
      p.push_back(uint8_t(devs[i].label.size()));
      for (char c : devs[i].label) p.push_back(uint8_t(c));
      send(CSUH_T_ENUM, p.data(), p.size());
    }
  }

  void sendOpened(uint8_t ok, uint8_t err, const std::string& name) {
    std::vector<uint8_t> p;
    p.push_back(ok); p.push_back(err); p.push_back(uint8_t(name.size()));
    for (char c : name) p.push_back(uint8_t(c));
    send(CSUH_T_OPENED, p.data(), p.size());
  }

  void sendEvent(uint8_t code, const std::string& det) {
    std::vector<uint8_t> p;
    p.push_back(code); p.push_back(uint8_t(det.size()));
    for (char c : det) p.push_back(uint8_t(c));
    send(CSUH_T_EVENT, p.data(), p.size());
  }

  void grant() {
    // Model the firmware: grant back to a full window whenever there is room.
    const int want = CSUH_CREDIT_INIT - peerCredit;
    if (want <= 0) return;
    const uint8_t k = uint8_t(want);
    send(CSUH_T_CREDIT_IN, &k, 1);
    peerCredit += k;
  }

  int findDev(const std::string& want) const {
    if (want.empty()) return devs.size() == 1 ? 0 : (devs.empty() ? -1 : -2);
    for (size_t i = 0; i < devs.size(); ++i) if (devs[i].key == want) return int(i);
    const size_t at = want.find('@');
    if (at == std::string::npos) return -1;
    const std::string vp = want.substr(0, at);
    int hit = -1, n = 0;
    for (size_t i = 0; i < devs.size(); ++i)
      if (devs[i].key.compare(0, vp.size(), vp) == 0 && devs[i].key[vp.size()] == '@') { hit = int(i); n++; }
    return n == 1 ? hit : (n ? -2 : -1);
  }

  void onFrame(uint8_t type, const uint8_t* p, size_t n) {
    framesIn++;
    switch (type) {
      case CSUH_T_HELLO_REQ: helloReqs++; sendHello(); break;
      case CSUH_T_ENUM_REQ:  enumReqs++;  sendEnum();  break;
      case CSUH_T_OPEN: {
        opens++;
        if (refuseOpen) { sendOpened(0, refuseErr, ""); break; }
        size_t o = 9;
        const size_t kl = (n > o) ? p[o] : 0; o++;
        std::string want((const char*)(p + o), kl);
        const int idx = findDev(want);
        if (idx == -2) { sendOpened(0, CSUH_ERR_AMBIG, ""); break; }
        if (idx < 0)   { sendOpened(0, CSUH_ERR_NODEV, ""); break; }
        openIdx = idx;
        sendOpened(1, CSUH_ERR_NONE, devs[idx].label);
        break;
      }
      case CSUH_T_CLOSE: closes++; openIdx = -1; break;
      case CSUH_T_DATA_OUT:
        if (peerCredit <= 0) creditViolations++;
        else peerCredit--;
        for (size_t i = 0; i < n; ++i) usbSeen.push_back(p[i]);
        grant();
        break;
      case CSUH_T_CREDIT_OUT: if (n) txCredit += p[0]; break;
      case CSUH_T_PING:  pings++; send(CSUH_T_PONG, p, n); break;
      case CSUH_T_RESCAN:
        rescans++;
        sendEvent(CSUH_EV_RESTART, "rescan");
        reboot();
        break;
      case CSUH_T_STAT_REQ: {
        uint8_t s[CSUH_STAT_LEN]; memset(s, 0, sizeof(s));
        s[CSUH_STAT_O_FRAMESRX] = 0x2A;      // arbitrary, just to prove it decodes
        s[CSUH_STAT_O_HEAP]     = 0x10;
        send(CSUH_T_STAT, s, CSUH_STAT_LEN);
        break;
      }
      default: break;
    }
  }

  void reboot() {
    epoch++;
    openIdx = -1;
    usbSeen.clear(); usbPending.clear();
    acc.clear();
    txCredit = peerCredit = CSUH_CREDIT_INIT;
  }

  // Drain what CardSat wrote, then push any queued "radio" bytes back.
  void pump() {
    if (!powered) { hostLinkToHelper.clear(); return; }
    while (!hostLinkToHelper.empty()) {
      const uint8_t b = hostLinkToHelper.front(); hostLinkToHelper.pop_front();
      if (b != 0x00) { if (acc.size() < CSUH_MAX_ENCODED) acc.push_back(b); continue; }
      if (!acc.empty()) {
        std::vector<uint8_t> blk(acc.begin(), acc.end());
        uint8_t raw[CSUH_MAX_FRAME];
        const size_t rn = csuhCobsDecode(blk.data(), blk.size(), raw, sizeof(raw));
        uint8_t ty, po; const uint8_t* pp; size_t pl;
        if (rn && csuhParseFrame(raw, rn, &ty, &po, &pp, &pl)) onFrame(ty, pp, pl);
      }
      acc.clear();
    }
    // "Radio" -> host, subject to credit.
    while (txCredit > 0 && !usbPending.empty()) {
      uint8_t buf[CSUH_MAX_PAYLOAD];
      size_t k = 0;
      while (k < CSUH_MAX_PAYLOAD && !usbPending.empty()) {
        buf[k++] = usbPending.front(); usbPending.pop_front();
      }
      send(CSUH_T_DATA_IN, buf, k);
      txCredit--;
    }
  }
};

static MockHelper g_mock;

// The shim's delay() lands here, so time passing also lets the mock run -- which
// is what makes the client's own delay(1) inside a blocked write behave the way
// it does against a real helper.
void hostAdvanceMs(uint32_t ms) {
  g_hostMillis += ms;
  g_mock.pump();
}

// One "loop iteration": service the client, let the mock answer, service again so
// the reply is consumed in the same step. Matches how the real loop behaves over
// two ticks and keeps the tests from depending on tick alignment.
static void step(int n = 1) {
  for (int i = 0; i < n; ++i) {
    UsbHelper::service();
    g_mock.pump();
    UsbHelper::service();
    g_hostMillis += 5;
  }
}

// ===========================================================================
//  Scenarios
// ===========================================================================
static void t1_linkup() {
  std::printf("1. link-up handshake\n");
  g_mock = MockHelper();
  g_mock.devs.push_back({ "0c26:0036@2", "IC-705 0c26:0036" });
  hostLinkToHelper.clear(); hostLinkToCardSat.clear();

  CHECK(UsbHelper::begin(230400), "begin() allocates and claims the UART");
  CHECK(!UsbHelper::linked(), "not linked before the helper answers");
  step(3);
  CHECK(UsbHelper::linked(), "linked after HELLO");
  CHECK(std::string(UsbHelper::helperVersion()) == "0.9.73", "helper firmware version reported");
  CHECK(g_mock.helloReqs >= 1, "HELLO_REQ was sent");
  CHECK(UsbHelper::linkBaud() == 230400, "link baud honoured");
  // An unsupported rate must fall back rather than silently never linking.
  UsbHelper::end();
  UsbHelper::begin(9600);
  CHECK(UsbHelper::linkBaud() == CSUH_BAUDS[0], "an out-of-list baud clamps to the default");
  UsbHelper::end();
}

static void t2_enumeration() {
  std::printf("2. enumeration\n");
  g_mock = MockHelper();
  g_mock.devs.push_back({ "0c26:0036@2", "IC-705 0c26:0036" });
  g_mock.devs.push_back({ "0403:6001/A50285BI", "FT232R 0403:6001" });
  hostLinkToHelper.clear(); hostLinkToCardSat.clear();
  UsbHelper::begin(230400);
  step(3);
  CHECK(g_mock.enumReqs >= 1, "ENUM_REQ issued automatically on link-up");
  CHECK(UsbHelper::deviceCount() == 2, "both devices listed");
  CHECK(std::string(UsbHelper::deviceKey(0)) == "0c26:0036@2", "address-form key intact");
  CHECK(std::string(UsbHelper::deviceKey(1)) == "0403:6001/A50285BI", "serial-form key intact");
  CHECK(std::string(UsbHelper::deviceLabel(0)) == "IC-705 0c26:0036", "label intact");

  // Empty answer must clear the list, not leave a stale one on screen.
  g_mock.devs.clear();
  UsbHelper::requestScan();
  step(2);
  CHECK(UsbHelper::deviceCount() == 0, "an explicit empty enumeration clears the list");
  UsbHelper::end();
}

static void t3_open() {
  std::printf("3. open, and every failure it can report\n");
  g_mock = MockHelper();
  g_mock.devs.push_back({ "0c26:0036@2", "IC-705 0c26:0036" });
  hostLinkToHelper.clear(); hostLinkToCardSat.clear();
  UsbHelper::begin(230400);
  step(3);

  UsbHelper::configure("0c26:0036@2");
  UsbHelper::open(19200, 8, CSUH_PAR_NONE, 1);
  step(2);
  CHECK(UsbHelper::active(), "port opens");
  CHECK(UsbHelper::stream() != nullptr, "stream() is available once open");
  CHECK(std::string(UsbHelper::deviceName()) == "IC-705 0c26:0036", "bound device name reported");
  CHECK(std::string(UsbHelper::lastError()).empty(), "no error on success");

  // The address moved (a hub re-ordered enumeration): VID:PID fallback finds it.
  UsbHelper::close();
  step(1);
  g_mock.devs[0].key = "0c26:0036@4";
  UsbHelper::requestScan();
  UsbHelper::open(19200, 8, CSUH_PAR_NONE, 1);
  step(2);
  CHECK(UsbHelper::active(), "a moved USB address still resolves by VID:PID");

  // Ambiguity must be refused, never guessed.
  UsbHelper::close(); step(1);
  g_mock.devs.push_back({ "0c26:0036@5", "IC-705 0c26:0036" });
  UsbHelper::open(19200, 8, CSUH_PAR_NONE, 1);
  step(2);
  CHECK(!UsbHelper::active(), "two identical devices are not guessed between");
  CHECK(std::string(UsbHelper::lastError()).find("nominate") != std::string::npos,
        "the ambiguity error tells the operator what to do");

  // The remaining failure codes reach the operator as distinct text.
  struct { uint8_t err; const char* needle; } cases[] = {
    { CSUH_ERR_NODEV,  "not found" },
    { CSUH_ERR_HOST,   "USB host" },
    { CSUH_ERR_BADARG, "port settings" },
    { CSUH_ERR_NOTCDC, "serial port" },
  };
  for (auto& c : cases) {
    UsbHelper::close(); step(1);
    g_mock.refuseOpen = true; g_mock.refuseErr = c.err;
    UsbHelper::open(19200, 8, CSUH_PAR_NONE, 1);
    step(2);
    CHECK(!UsbHelper::active(), "a refused open does not report active");
    CHECK(std::string(UsbHelper::lastError()).find(c.needle) != std::string::npos,
          "each refusal produces its own message");
  }
  g_mock.refuseOpen = false;
  UsbHelper::end();
}

static void t4_roundtrip() {
  std::printf("4. byte round-trip through the Stream\n");
  g_mock = MockHelper();
  g_mock.devs.push_back({ "2166:9023@2", "TH-D75 2166:9023" });
  hostLinkToHelper.clear(); hostLinkToCardSat.clear();
  UsbHelper::begin(230400);
  step(3);
  UsbHelper::configure("");
  UsbHelper::open(9600, 8, CSUH_PAR_NONE, 1);
  step(2);
  CHECK(UsbHelper::active(), "port open with no key when only one device is present");

  Stream* s = UsbHelper::stream();
  const char* cmd = "FQ 0,0435800000\r";
  s->write((const uint8_t*)cmd, strlen(cmd));
  step(2);
  CHECK(g_mock.usbSeen.size() == strlen(cmd), "every byte of the command reached the device");
  CHECK(std::memcmp(g_mock.usbSeen.data(), cmd, strlen(cmd)) == 0, "and unaltered");

  // Reply, including a 0x00 byte -- the delimiter must survive COBS, which is the
  // entire reason for using it. A CI-V frame is full of zeros.
  const uint8_t reply[] = { 0xFE, 0xFE, 0xE0, 0xA4, 0x00, 0x03, 0x00, 0x58, 0x45, 0x43, 0x14, 0xFD };
  for (uint8_t b : reply) g_mock.usbPending.push_back(b);
  step(3);
  std::vector<uint8_t> got;
  while (s->available() > 0) got.push_back((uint8_t)s->read());
  CHECK(got.size() == sizeof(reply), "the whole reply came back");
  CHECK(got.size() == sizeof(reply) && std::memcmp(got.data(), reply, sizeof(reply)) == 0,
        "a reply containing 0x00 survives the framing");
  UsbHelper::end();
}

static void t5_credit() {
  std::printf("5. credit invariant under a transceive flood\n");
  g_mock = MockHelper();
  g_mock.devs.push_back({ "0c26:0036@2", "IC-705 0c26:0036" });
  hostLinkToHelper.clear(); hostLinkToCardSat.clear();
  UsbHelper::begin(230400);
  step(3);
  UsbHelper::configure("");
  UsbHelper::open(19200, 8, CSUH_PAR_NONE, 1);
  step(2);
  CHECK(UsbHelper::active(), "port open");

  // 8 KB of unsolicited traffic -- far more than the 2 KB ring -- with the reader
  // doing nothing. This is an IC-705 in CI-V transceive mode while CardSat is busy
  // repainting a screen.
  const size_t FLOOD = 8192;
  for (size_t i = 0; i < FLOOD; ++i) g_mock.usbPending.push_back(uint8_t(i & 0xFF));

  Stream* s = UsbHelper::stream();
  std::vector<uint8_t> got;
  // Interleave servicing and reading, but read far more slowly than the flood
  // arrives, so the ring is under real pressure throughout.
  for (int round = 0; round < 400 && got.size() < FLOOD; ++round) {
    step(1);
    for (int k = 0; k < 32 && s->available() > 0; ++k) got.push_back((uint8_t)s->read());
  }
  // Drain whatever is left.
  for (int round = 0; round < 400 && got.size() < FLOOD; ++round) {
    step(1);
    while (s->available() > 0) got.push_back((uint8_t)s->read());
  }

  CHECK(got.size() == FLOOD, "not one byte was lost under sustained back-pressure");
  bool ordered = got.size() == FLOOD;
  for (size_t i = 0; ordered && i < got.size(); ++i)
    if (got[i] != uint8_t(i & 0xFF)) ordered = false;
  CHECK(ordered, "and the byte order is exactly as sent");
  CHECK(std::string(UsbHelper::lastError()).find("overflow") == std::string::npos,
        "the ring never overflowed, so the credit invariant held");
  UsbHelper::end();
}

static void t6_reboot() {
  std::printf("6. helper reboot mid-session\n");
  g_mock = MockHelper();
  g_mock.devs.push_back({ "0c26:0036@2", "IC-705 0c26:0036" });
  hostLinkToHelper.clear(); hostLinkToCardSat.clear();
  UsbHelper::begin(230400);
  step(3);
  UsbHelper::configure("0c26:0036@2");
  UsbHelper::open(19200, 8, CSUH_PAR_NONE, 1);
  step(2);
  CHECK(UsbHelper::active(), "port open before the reboot");
  const int opensBefore = g_mock.opens;

  // The Stick browns out and comes back with a new epoch.
  g_mock.reboot();
  g_mock.sendHello();
  step(4);
  CHECK(UsbHelper::linked(), "still linked after the reboot");
  CHECK(g_mock.opens > opensBefore, "the port was re-opened without the operator asking");
  CHECK(UsbHelper::active(), "and it came back up");
  CHECK(g_mock.enumReqs >= 2, "the device list was re-fetched rather than trusted");
  UsbHelper::end();
}

static void t7_linkdeath() {
  std::printf("7. link death and recovery\n");
  g_mock = MockHelper();
  g_mock.devs.push_back({ "0c26:0036@2", "IC-705 0c26:0036" });
  hostLinkToHelper.clear(); hostLinkToCardSat.clear();
  UsbHelper::begin(230400);
  step(3);
  UsbHelper::configure("");
  UsbHelper::open(19200, 8, CSUH_PAR_NONE, 1);
  step(2);
  CHECK(UsbHelper::active(), "up before the cable is pulled");

  // Cable out: the helper hears nothing and says nothing.
  g_mock.powered = false;
  for (int i = 0; i < 30; ++i) { UsbHelper::service(); g_hostMillis += 300; }
  CHECK(!UsbHelper::linked(), "silence past the deadline is reported as not linked");
  CHECK(!UsbHelper::active(), "and the port is no longer claimed to be open");
  CHECK(std::string(UsbHelper::lastError()).find("no response") != std::string::npos,
        "the error names the Grove link, not the radio");

  // Cable back in.
  g_mock.powered = true;
  g_mock.reboot();
  step(8);
  CHECK(UsbHelper::linked(), "re-links by itself");
  CHECK(UsbHelper::active(), "and re-opens the port by itself");
  UsbHelper::end();
}

static void t8_versionmismatch() {
  std::printf("8. protocol version mismatch\n");
  g_mock = MockHelper();
  g_mock.protoVer = CSUH_PROTO_VER + 1;
  hostLinkToHelper.clear(); hostLinkToCardSat.clear();
  UsbHelper::begin(230400);
  step(4);
  CHECK(!UsbHelper::linked(), "a mismatched helper is refused, not negotiated with");
  CHECK(std::string(UsbHelper::lastError()).find("protocol") != std::string::npos,
        "and the reason says so plainly");
  UsbHelper::end();
}

static void t9_writebackpressure() {
  std::printf("9. write back-pressure\n");
  g_mock = MockHelper();
  g_mock.devs.push_back({ "0c26:0036@2", "IC-705 0c26:0036" });
  hostLinkToHelper.clear(); hostLinkToCardSat.clear();
  UsbHelper::begin(230400);
  step(3);
  UsbHelper::configure("");
  UsbHelper::open(19200, 8, CSUH_PAR_NONE, 1);
  step(2);

  // A long burst in one call -- more than the TX ring holds -- with the mock
  // draining normally. A CAT backend never checks write()'s return, so a short
  // write here would corrupt a command and present as a radio fault.
  std::vector<uint8_t> big(3000);
  for (size_t i = 0; i < big.size(); ++i) big[i] = uint8_t(i * 13);
  Stream* s = UsbHelper::stream();
  const size_t wrote = s->write(big.data(), big.size());
  step(30);
  CHECK(wrote == big.size(), "a burst larger than the TX ring is not truncated");
  CHECK(g_mock.usbSeen.size() == big.size(), "and all of it reaches the device");
  CHECK(g_mock.usbSeen.size() == big.size() &&
        std::memcmp(g_mock.usbSeen.data(), big.data(), big.size()) == 0,
        "in order and unaltered");
  CHECK(g_mock.creditViolations == 0, "the client never sent a frame it had no credit for");
  UsbHelper::end();
}

int main() {
  std::printf("CSUH link-layer integration test (src/usbhelper.cpp vs mock helper)\n\n");
  t1_linkup();
  t2_enumeration();
  t3_open();
  t4_roundtrip();
  t5_credit();
  t6_reboot();
  t7_linkdeath();
  t8_versionmismatch();
  t9_writebackpressure();
  std::printf("\n%s (%d checks, %d failures)\n",
              g_fails ? "FAILURES" : "ALL PASS", g_checks, g_fails);
  return g_fails ? 1 : 0;
}
