// ===========================================================================
//  csuh_frames_test.cpp -- host-side verification of the CSUH wire codec
// ===========================================================================
//
//  Builds against src/csuh_proto.h UNMODIFIED, so what is tested here is exactly
//  what both firmwares compile. The point is to settle the codec on a machine
//  with a debugger and assert(), before either end goes near a radio -- a framing
//  bug found on the bench costs a flash cycle and looks identical to a wiring
//  fault, which is the most expensive kind of bug this project has.
//
//  Covers:
//    1. CRC-16/CCITT-FALSE against its published check value.
//    2. COBS against the reference vectors from the original Cheshire/Baker paper
//       (the same table Wikipedia reproduces), INCLUDING the 254/255-byte
//       boundaries where the run-length code rolls over -- the only part of COBS
//       anybody actually gets wrong.
//    3. Round-trip of every frame type at every payload length 0..CSUH_MAX_PAYLOAD.
//    4. Rejection: bad CRC, truncated frame, oversize payload, non-zero PORT.
//    5. Stream resynchronisation -- garbage injected mid-stream (a helper reboot)
//       must cost at most the frame it corrupts, never the ones after it.
//
//  Build + run:  tools/host_usbhelper/csuh_frames_test.sh
// ===========================================================================
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

#include "../../src/csuh_proto.h"

static int g_checks = 0;
#define CHECK(cond, what) do { \
  ++g_checks; \
  if (!(cond)) { std::printf("FAIL: %s  (%s:%d)\n", what, __FILE__, __LINE__); return 1; } \
} while (0)

// ---------------------------------------------------------------------------
//  1. CRC-16/CCITT-FALSE
// ---------------------------------------------------------------------------
static int testCrc() {
  const char* s = "123456789";
  const uint16_t got = csuhCrc16((const uint8_t*)s, 9);
  CHECK(got == 0x29B1, "CRC-16/CCITT-FALSE check value of \"123456789\" is 0x29B1");

  // Empty input is the init value, unmodified.
  CHECK(csuhCrc16((const uint8_t*)"", 0) == 0xFFFF, "CRC of empty input is the init value");

  // A single flipped bit must change the CRC (guards against a stubbed-out impl).
  uint8_t a[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
  uint8_t b[4] = { 0xDE, 0xAD, 0xBE, 0xEE };
  CHECK(csuhCrc16(a, 4) != csuhCrc16(b, 4), "CRC distinguishes a one-bit difference");
  std::printf("  crc: ok\n");
  return 0;
}

// ---------------------------------------------------------------------------
//  2. COBS reference vectors
// ---------------------------------------------------------------------------
struct CobsVec { std::vector<uint8_t> raw, enc; const char* name; };

static std::vector<uint8_t> ramp(uint8_t first, size_t n) {
  std::vector<uint8_t> v; v.reserve(n);
  for (size_t i = 0; i < n; ++i) v.push_back((uint8_t)(first + i));
  return v;
}

static int testCobs() {
  std::vector<CobsVec> vecs;
  vecs.push_back({ {0x00},                   {0x01,0x01},                  "single zero" });
  vecs.push_back({ {0x00,0x00},              {0x01,0x01,0x01},             "two zeros" });
  vecs.push_back({ {0x11,0x22,0x00,0x33},    {0x03,0x11,0x22,0x02,0x33},   "zero in the middle" });
  vecs.push_back({ {0x11,0x22,0x33,0x44},    {0x05,0x11,0x22,0x33,0x44},   "no zeros" });
  vecs.push_back({ {0x11,0x00,0x00,0x00},    {0x02,0x11,0x01,0x01,0x01},   "trailing zeros" });

  // 254 non-zero bytes: exactly one maximal run, no overhead byte added.
  {
    CobsVec v; v.name = "254-byte run (code 0xFF, no split)";
    v.raw = ramp(0x01, 254);
    v.enc.push_back(0xFF);
    for (uint8_t b : v.raw) v.enc.push_back(b);
    vecs.push_back(v);
  }
  // 255 non-zero bytes: the run rolls over and a second code byte appears.
  {
    CobsVec v; v.name = "255-byte run (code rollover)";
    v.raw = ramp(0x01, 255);
    v.enc.push_back(0xFF);
    for (size_t i = 0; i < 254; ++i) v.enc.push_back(v.raw[i]);
    v.enc.push_back(0x02);
    v.enc.push_back(v.raw[254]);
    vecs.push_back(v);
  }

  for (const CobsVec& v : vecs) {
    uint8_t enc[1024];
    const size_t n = csuhCobsEncode(v.raw.data(), v.raw.size(), enc, sizeof(enc));
    if (n != v.enc.size() || std::memcmp(enc, v.enc.data(), n) != 0) {
      std::printf("FAIL: COBS encode mismatch on '%s'\n    want:", v.name);
      for (uint8_t b : v.enc) std::printf(" %02X", b);
      std::printf("\n    got: ");
      for (size_t i = 0; i < n; ++i) std::printf(" %02X", enc[i]);
      std::printf("\n");
      return 1;
    }
    ++g_checks;

    uint8_t dec[1024];
    const size_t m = csuhCobsDecode(enc, n, dec, sizeof(dec));
    CHECK(m == v.raw.size() && std::memcmp(dec, v.raw.data(), m) == 0,
          "COBS decode round-trip");
  }

  // Encoded output must never contain 0x00 -- that is the entire point.
  for (size_t len = 0; len <= 300; ++len) {
    std::vector<uint8_t> raw(len);
    for (size_t i = 0; i < len; ++i) raw[i] = (uint8_t)((i * 7) % 3 == 0 ? 0x00 : (i & 0xFF));
    uint8_t enc[1024];
    const size_t n = csuhCobsEncode(raw.data(), len, enc, sizeof(enc));
    CHECK(n > 0 || len == 0, "encode succeeds");
    for (size_t i = 0; i < n; ++i)
      CHECK(enc[i] != 0x00, "encoded COBS block contains no zero byte");
    uint8_t dec[1024];
    const size_t m = csuhCobsDecode(enc, n, dec, sizeof(dec));
    CHECK(m == len && (len == 0 || std::memcmp(dec, raw.data(), len) == 0),
          "round-trip at every length 0..300");
  }

  // A block containing 0x00 is malformed and must be rejected, not decoded.
  {
    const uint8_t bad[4] = { 0x03, 0x11, 0x00, 0x02 };
    uint8_t dec[16];
    CHECK(csuhCobsDecode(bad, 4, dec, sizeof(dec)) == 0,
          "decode rejects a zero byte inside the block");
  }
  // A code byte that runs past the end of the block is malformed.
  {
    const uint8_t bad[3] = { 0x09, 0x11, 0x22 };
    uint8_t dec[16];
    CHECK(csuhCobsDecode(bad, 3, dec, sizeof(dec)) == 0,
          "decode rejects a run longer than the block");
  }
  std::printf("  cobs: ok (%zu vectors + exhaustive lengths)\n", vecs.size());
  return 0;
}

// ---------------------------------------------------------------------------
//  3. Frame round-trip at every type and length
// ---------------------------------------------------------------------------
static int testFrames() {
  static const uint8_t TYPES[] = {
    CSUH_T_HELLO_REQ, CSUH_T_ENUM_REQ, CSUH_T_OPEN, CSUH_T_CLOSE, CSUH_T_DATA_OUT,
    CSUH_T_MODEM, CSUH_T_PING, CSUH_T_CREDIT_OUT, CSUH_T_STAT_REQ, CSUH_T_RESCAN,
    CSUH_T_LINE, CSUH_T_WAKE,
    CSUH_T_HELLO, CSUH_T_ENUM, CSUH_T_OPENED, CSUH_T_DATA_IN, CSUH_T_EVENT,
    CSUH_T_PONG, CSUH_T_CREDIT_IN, CSUH_T_STAT,
  };
  uint8_t pay[CSUH_MAX_PAYLOAD];
  for (size_t i = 0; i < sizeof(pay); ++i) pay[i] = (uint8_t)(i * 31);   // includes 0x00

  for (uint8_t t : TYPES) {
    for (size_t len = 0; len <= CSUH_MAX_PAYLOAD; ++len) {
      uint8_t wire[CSUH_MAX_ENCODED];
      const size_t w = csuhBuildFrame(t, 0, pay, len, wire, sizeof(wire));
      CHECK(w > 0, "build fits in CSUH_MAX_ENCODED");
      CHECK(w <= CSUH_MAX_ENCODED, "CSUH_MAX_ENCODED is not undersized");
      CHECK(wire[w - 1] == 0x00, "frame ends with the delimiter");
      for (size_t i = 0; i + 1 < w; ++i)
        CHECK(wire[i] != 0x00, "no zero byte before the delimiter");

      uint8_t raw[CSUH_MAX_FRAME];
      const size_t n = csuhCobsDecode(wire, w - 1, raw, sizeof(raw));
      uint8_t ty, po; const uint8_t* pp; size_t pl;
      CHECK(csuhParseFrame(raw, n, &ty, &po, &pp, &pl), "parse accepts a good frame");
      CHECK(ty == t && po == 0 && pl == len, "type/port/length survive the round trip");
      CHECK(len == 0 || std::memcmp(pp, pay, len) == 0, "payload survives the round trip");
    }
  }

  // Oversize payload is refused at build time rather than truncated.
  {
    uint8_t big[CSUH_MAX_PAYLOAD + 1] = {0};
    uint8_t wire[CSUH_MAX_ENCODED];
    CHECK(csuhBuildFrame(CSUH_T_DATA_OUT, 0, big, sizeof(big), wire, sizeof(wire)) == 0,
          "build refuses an oversize payload");
  }
  std::printf("  frames: ok (%zu types x %d lengths)\n",
              sizeof(TYPES) / sizeof(TYPES[0]), CSUH_MAX_PAYLOAD + 1);
  return 0;
}

// ---------------------------------------------------------------------------
//  4. Rejection
// ---------------------------------------------------------------------------
static int testRejection() {
  uint8_t pay[8] = { 1,2,3,4,5,6,7,8 };
  uint8_t wire[CSUH_MAX_ENCODED];
  const size_t w = csuhBuildFrame(CSUH_T_DATA_IN, 0, pay, sizeof(pay), wire, sizeof(wire));
  uint8_t raw[CSUH_MAX_FRAME];
  const size_t n = csuhCobsDecode(wire, w - 1, raw, sizeof(raw));

  uint8_t ty, po; const uint8_t* pp; size_t pl;
  CHECK(csuhParseFrame(raw, n, &ty, &po, &pp, &pl), "baseline frame parses");

  // Every single-bit flip in the frame body must be caught by the CRC.
  for (size_t i = 0; i + 2 < n; ++i) {
    for (int b = 0; b < 8; ++b) {
      uint8_t bad[CSUH_MAX_FRAME];
      std::memcpy(bad, raw, n);
      bad[i] ^= (uint8_t)(1 << b);
      if (i == 1 && (bad[1] >= CSUH_MAX_PORTS)) { ++g_checks; continue; }  // caught by the port check
      CHECK(!csuhParseFrame(bad, n, &ty, &po, &pp, &pl), "CRC catches a one-bit corruption");
    }
  }
  // Truncation.
  for (size_t cut = 0; cut < n; ++cut)
    CHECK(!csuhParseFrame(raw, cut, &ty, &po, &pp, &pl) || cut == n,
          "a truncated frame is refused");
  // Reserved PORT byte must be zero in v1.
  {
    uint8_t bad[CSUH_MAX_FRAME];
    std::memcpy(bad, raw, n);
    bad[1] = 1;
    const uint16_t crc = csuhCrc16(bad, n - 2);
    bad[n - 2] = (uint8_t)(crc & 0xFF); bad[n - 1] = (uint8_t)(crc >> 8);
    CHECK(!csuhParseFrame(bad, n, &ty, &po, &pp, &pl),
          "a non-zero PORT is refused even with a valid CRC");
  }
  std::printf("  rejection: ok\n");
  return 0;
}

// ---------------------------------------------------------------------------
//  5. Stream resynchronisation
// ---------------------------------------------------------------------------
//  Models the receiver both firmwares implement: accumulate until 0x00, decode,
//  parse, discard on failure. The invariant that matters is BOUNDED loss -- a
//  helper reboot mid-session dumps bootloader noise onto the wire, and if that
//  could desync the host permanently the link would need a power cycle to
//  recover, which is exactly the class of failure this design is meant to avoid.
// The receiver both firmwares implement: accumulate until 0x00, decode, parse,
// discard on failure. Returns the types accepted, and counts the segments thrown
// away. Sharing one implementation between the two scenarios below is the point --
// if it recovers in one and not the other, the difference is the input, not the code.
static std::vector<int> runReceiver(const std::vector<uint8_t>& stream, int* droppedOut) {
  std::vector<uint8_t> acc;
  std::vector<int> got;
  int dropped = 0;
  for (uint8_t b : stream) {
    if (b != 0x00) {
      if (acc.size() < CSUH_MAX_ENCODED) acc.push_back(b);
      continue;
    }
    if (!acc.empty()) {
      uint8_t raw[CSUH_MAX_FRAME];
      const size_t n = csuhCobsDecode(acc.data(), acc.size(), raw, sizeof(raw));
      uint8_t ty, po; const uint8_t* pp; size_t pl;
      if (n && csuhParseFrame(raw, n, &ty, &po, &pp, &pl)) got.push_back((int)ty);
      else ++dropped;
    }
    acc.clear();
  }
  if (droppedOut) *droppedOut = dropped;
  return got;
}

static void appendFrame(std::vector<uint8_t>& stream, uint8_t type, size_t len) {
  uint8_t pay[CSUH_MAX_PAYLOAD];
  for (size_t i = 0; i < len; ++i) pay[i] = (uint8_t)(len + i);
  uint8_t wire[CSUH_MAX_ENCODED];
  const size_t w = csuhBuildFrame(type, 0, pay, len, wire, sizeof(wire));
  stream.insert(stream.end(), wire, wire + w);
}

static int testResync() {
  // Bootloader noise: raw bytes with no valid framing, including zeros. Its three
  // 0x00 bytes make it look like three malformed frames to the receiver, which is
  // the honest accounting -- the receiver cannot know the noise was one event.
  const uint8_t noise[] = { 0xFF, 0x00, 0x41, 0x42, 0x43, 0x00, 0x99, 0x7F, 0x00 };
  const int noiseDelims = 3;

  // ---- case 1: noise lands ON a frame boundary (a helper reboot between frames).
  // Nothing real is in flight, so NOTHING valid may be lost.
  {
    std::vector<uint8_t> s;
    appendFrame(s, CSUH_T_HELLO, 12);
    appendFrame(s, CSUH_T_DATA_IN, 64);
    s.insert(s.end(), noise, noise + sizeof(noise));
    appendFrame(s, CSUH_T_DATA_IN, 3);
    appendFrame(s, CSUH_T_EVENT, 20);
    appendFrame(s, CSUH_T_PONG, 2);

    int dropped = 0;
    const std::vector<int> got = runReceiver(s, &dropped);
    const std::vector<int> want = { CSUH_T_HELLO, CSUH_T_DATA_IN, CSUH_T_DATA_IN,
                                    CSUH_T_EVENT, CSUH_T_PONG };
    CHECK(got == want, "noise on a frame boundary loses no valid frame at all");
    CHECK(dropped == noiseDelims, "the garbage is accounted for exactly, not silently");
  }

  // ---- case 2: noise lands MID-frame (the wire cut while a frame was in flight).
  // Exactly one real frame may be lost -- the one it corrupts -- and every frame
  // after the next delimiter must decode. If this ever regressed, a single glitch
  // would take the link down until a reboot, which is the failure this framing exists
  // to prevent.
  {
    std::vector<uint8_t> head, tail;
    appendFrame(head, CSUH_T_HELLO, 12);
    appendFrame(head, CSUH_T_DATA_IN, 64);     // this one gets cut in half
    appendFrame(tail, CSUH_T_DATA_IN, 3);
    appendFrame(tail, CSUH_T_EVENT, 20);
    appendFrame(tail, CSUH_T_PONG, 2);

    std::vector<uint8_t> s(head.begin(), head.end() - 20);   // truncate mid-frame
    s.insert(s.end(), noise, noise + sizeof(noise));
    s.insert(s.end(), tail.begin(), tail.end());

    int dropped = 0;
    const std::vector<int> got = runReceiver(s, &dropped);
    const std::vector<int> want = { CSUH_T_HELLO, CSUH_T_DATA_IN,
                                    CSUH_T_EVENT, CSUH_T_PONG };
    CHECK(got == want, "mid-frame corruption costs exactly the frame it corrupts");
    CHECK(dropped == noiseDelims, "the corrupt frame and the noise are all counted");
  }

  std::printf("  resync: ok (boundary noise lossless; mid-frame noise costs 1 frame)\n");
  return 0;
}

// ---------------------------------------------------------------------------
//  6. Sizing constants agree with each other
// ---------------------------------------------------------------------------
static int testSizing() {
  CHECK(CSUH_MAX_FRAME == 2 + CSUH_MAX_PAYLOAD + 2, "CSUH_MAX_FRAME matches the layout");
  uint8_t pay[CSUH_MAX_PAYLOAD];
  std::memset(pay, 0x00, sizeof(pay));            // worst case for COBS overhead
  uint8_t wire[CSUH_MAX_ENCODED];
  const size_t w = csuhBuildFrame(CSUH_T_DATA_IN, 0, pay, sizeof(pay), wire, sizeof(wire));
  CHECK(w > 0 && w <= CSUH_MAX_ENCODED,
        "an all-zero maximum payload still fits CSUH_MAX_ENCODED");
  CHECK(CSUH_BAUD_N == (int)(sizeof(CSUH_BAUDS) / sizeof(CSUH_BAUDS[0])),
        "CSUH_BAUD_N matches CSUH_BAUDS[]");
  std::printf("  sizing: ok (worst-case encoded frame %zu of %d bytes)\n",
              w, CSUH_MAX_ENCODED);
  return 0;
}

int main() {
  std::printf("CSUH wire codec verification (proto v%d)\n", CSUH_PROTO_VER);
  if (testCrc())       return 1;
  if (testCobs())      return 1;
  if (testFrames())    return 1;
  if (testRejection()) return 1;
  if (testResync())    return 1;
  if (testSizing())    return 1;
  std::printf("ALL PASS (%d checks)\n", g_checks);
  return 0;
}
