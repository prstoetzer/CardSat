#pragma once
// ===========================================================================
//  Arduino.h  --  HOST-TEST SHIM. Not shipped, not compiled into firmware.
// ===========================================================================
//  Just enough Arduino to build src/usbhelper.cpp on a development machine so
//  the link state machine can be exercised against a mock helper with assert()
//  and a debugger, instead of only on a bench with a radio attached.
//
//  DELIBERATELY MINIMAL. It provides only what usbhelper.cpp actually uses. If a
//  future change to that file needs more Arduino surface, the build breaks here
//  and someone has to add it consciously -- which is the point. A shim that
//  quietly grew to cover everything would stop being a statement about what the
//  transport depends on.
//
//  millis() is TEST-CONTROLLED (see hostAdvanceMs). Real time would make the
//  timeout tests either slow or flaky, and flaky is worse: a test that passes
//  four times out of five teaches people to re-run it.
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <deque>

// ---- test-controlled clock ------------------------------------------------
extern uint32_t g_hostMillis;
inline uint32_t millis() { return g_hostMillis; }
void hostAdvanceMs(uint32_t ms);          // defined in the test; may pump the mock
inline void delay(uint32_t ms) { hostAdvanceMs(ms); }

// ---- strlcpy (BSD; always on ESP-IDF, on glibc only since 2.38) -----------
// Defining it unconditionally collides with the fortified glibc declaration on a
// modern Linux and compiles fine on an older one -- the sort of difference that
// makes a test "work on my machine".
#if !defined(__GLIBC__) || (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38)
inline size_t strlcpy(char* dst, const char* src, size_t n) {
  const size_t sl = strlen(src);
  if (n) { const size_t c = (sl >= n) ? n - 1 : sl; memcpy(dst, src, c); dst[c] = 0; }
  return sl;
}
#endif

// ---- Print / Stream -------------------------------------------------------
class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t b) = 0;
  virtual size_t write(const uint8_t* d, size_t n) {
    size_t k = 0; while (k < n) { if (!write(d[k])) break; ++k; } return k;
  }
};

class Stream : public Print {
public:
  virtual int  available() = 0;
  virtual int  read() = 0;
  virtual int  peek() = 0;
  virtual void flush() {}
};

// ---- HardwareSerial -------------------------------------------------------
// Backed by two byte queues wired to the mock helper. hostLinkToHelper carries
// what CardSat writes; hostLinkToCardSat carries what the mock sends back. The
// test owns both, so it can also corrupt or drop bytes to model a bad cable.
extern std::deque<uint8_t> hostLinkToHelper;
extern std::deque<uint8_t> hostLinkToCardSat;

#define SERIAL_8N1 0x800001c

class HardwareSerial : public Stream {
public:
  explicit HardwareSerial(int uartNum) : _uart(uartNum) {}
  void begin(uint32_t baud, uint32_t cfg = SERIAL_8N1, int rx = -1, int tx = -1) {
    (void)cfg; (void)rx; (void)tx; _baud = baud; _open = true;
  }
  void end() { _open = false; }
  int  available() override { return _open ? (int)hostLinkToCardSat.size() : -1; }
  int  read() override {
    if (!_open || hostLinkToCardSat.empty()) return -1;
    const uint8_t b = hostLinkToCardSat.front(); hostLinkToCardSat.pop_front(); return b;
  }
  int  peek() override {
    if (!_open || hostLinkToCardSat.empty()) return -1;
    return hostLinkToCardSat.front();
  }
  size_t write(uint8_t b) override {
    if (!_open) return 0;
    hostLinkToHelper.push_back(b); return 1;
  }
  size_t write(const uint8_t* d, size_t n) override {
    if (!_open) return 0;
    for (size_t i = 0; i < n; ++i) hostLinkToHelper.push_back(d[i]);
    return n;
  }
  void flush() override {}
  uint32_t baud() const { return _baud; }
  bool     isOpen() const { return _open; }
private:
  int      _uart;
  uint32_t _baud = 0;
  bool     _open = false;
};
