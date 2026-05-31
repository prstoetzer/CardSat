// ===========================================================================
//  civ.cpp
// ===========================================================================
#include "civ.h"
#include <HardwareSerial.h>

// Set to 0 to silence the CI-V trace on the serial monitor (115200 baud).
#define CIV_DEBUG 1

static HardwareSerial CivSerial(1);   // default; re-init in begin()

#if CIV_DEBUG
// Decode the command byte of a single CI-V frame for the human-readable tail.
static void civDecode(const uint8_t* b, size_t n) {
  if (n < 5) return;
  uint8_t cmd = b[4];
  switch (cmd) {
    case 0x03:
      if (n >= 11) { uint32_t hz = 0;
        for (int k = 9; k >= 5; --k) hz = hz*100 + (b[k]>>4)*10 + (b[k]&0x0F);
        Serial.printf("  read-freq -> %lu Hz", (unsigned long)hz); }
      else Serial.print("  read-freq req");
      break;
    case 0x05:
      if (n >= 10) { uint32_t hz = 0;
        for (int k = 9; k >= 5; --k) hz = hz*100 + (b[k]>>4)*10 + (b[k]&0x0F);
        Serial.printf("  set-freq %lu Hz", (unsigned long)hz); }
      break;
    case 0x06: { const char* m = "?";
      if (n > 5) switch (b[5]) { case 0:m="LSB";break; case 1:m="USB";break;
        case 2:m="AM";break; case 3:m="CW";break; case 5:m="FM";break;
        case 7:m="CW-R";break; }
      Serial.printf("  set-mode %s", m); } break;
    case 0x07: Serial.print("  sel-band ");
      if (n > 5) { if (b[5]==0xD0) Serial.print("MAIN");
                   else if (b[5]==0xD1) Serial.print("SUB");
                   else if (b[5]==0xB0) Serial.print("swap");
                   else Serial.printf("%02X", b[5]); } break;
    case 0x16: Serial.printf("  sat-mode %s", (n > 6 && b[6]) ? "ON" : "OFF"); break;
    case 0xFB: Serial.print("  ACK"); break;
    case 0xFA: Serial.print("  NAK"); break;
  }
}
static void civLog(const char* dir, const uint8_t* b, size_t n) {
  Serial.printf("[CI-V %s]", dir);
  for (size_t i = 0; i < n; ++i) Serial.printf(" %02X", b[i]);
  civDecode(b, n);
  Serial.println();
}
static void civLogRaw(const char* dir, const uint8_t* b, size_t n) {
  Serial.printf("[CI-V %s]", dir);
  if (!n) Serial.print(" (none)");
  for (size_t i = 0; i < n; ++i) Serial.printf(" %02X", b[i]);
  Serial.println();
}
#else
static inline void civLog(const char*, const uint8_t*, size_t) {}
static inline void civLogRaw(const char*, const uint8_t*, size_t) {}
#endif

void CivRadio::begin(RadioModel model, uint32_t baud,
                     int uartNum, int rxPin, int txPin) {
  setModel(model);
  static HardwareSerial* hs = nullptr;   // construct once, reuse on re-begin
  if (!hs) hs = new HardwareSerial(uartNum);
  hs->begin(baud, SERIAL_8N1, rxPin, txPin);
  _stream = hs;
}

void CivRadio::setModel(RadioModel m) {
  _model = m;
  _addr  = RADIOS[m].civAddr;
}

// --- BCD: 1 Hz resolution, 5 bytes, least-significant pair first ----------
void CivRadio::freqToBcd(uint32_t hz, uint8_t out[5]) {
  for (int i = 0; i < 5; ++i) {
    uint8_t lo = hz % 10; hz /= 10;
    uint8_t hi = hz % 10; hz /= 10;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
}

bool CivRadio::sendFrame(const uint8_t* payload, size_t len) {
  if (!_stream) return false;
  uint8_t buf[20];
  size_t n = 0;
  buf[n++] = 0xFE; buf[n++] = 0xFE;
  buf[n++] = _addr;       // to radio
  buf[n++] = 0xE0;        // from controller
  for (size_t i = 0; i < len && n < sizeof(buf) - 1; ++i) buf[n++] = payload[i];
  buf[n++] = 0xFD;        // end of message
  civLog("TX", buf, n);   // trace the command to the serial monitor
  _stream->write(buf, n);
  _stream->flush();
  drainEcho();            // swallow our own echo + radio's OK/NG (0xFB/0xFA)
  return true;
}

bool CivRadio::drainEcho(uint32_t timeoutMs) {
  if (!_stream) return false;
  uint32_t t0 = millis();
  int fd = 0;                          // 1 = our echo seen, 2 = radio reply seen
#if CIV_DEBUG
  uint8_t rx[40]; size_t rn = 0;
#endif
  while (millis() - t0 < timeoutMs) {
    while (_stream->available()) {
      uint8_t b = (uint8_t)_stream->read();
#if CIV_DEBUG
      if (rn < sizeof(rx)) rx[rn++] = b;
#endif
      if (b == 0xFD) fd++;
      t0 = millis();
    }
    if (fd >= 2) break;                       // echo + ACK/NAK both arrived
    if (fd >= 1 && millis() - t0 > 25) break;  // echo seen, radio not replying
    delay(1);
  }
#if CIV_DEBUG
  // Report only the radio's reply (ACK/NAK), not the echo of our own frame.
  for (size_t i = 0; i + 5 < rn; ++i) {
    if (rx[i]==0xFE && rx[i+1]==0xFE && rx[i+2]==0xE0 && rx[i+3]==_addr) {
      if (rx[i+4]==0xFB) { Serial.println("[CI-V RX] radio ACK (FB)"); break; }
      if (rx[i+4]==0xFA) { Serial.println("[CI-V RX] radio NAK (FA)"); break; }
    }
  }
#endif
  return fd >= 1;
}

void CivRadio::selectMain() {
  const RadioProfile& p = RADIOS[_model];
  if (p.selLen) sendFrame(p.selMain, p.selLen);
}
void CivRadio::selectSub() {
  const RadioProfile& p = RADIOS[_model];
  if (p.selLen) sendFrame(p.selSub, p.selLen);
}
void CivRadio::selectSubBand() { selectSub(); }

bool CivRadio::setMainFreq(uint32_t hz) {
  selectMain();
  uint8_t pl[6]; pl[0] = 0x05; freqToBcd(hz, &pl[1]);
  return sendFrame(pl, 6);
}
bool CivRadio::setSubFreq(uint32_t hz) {
  selectSub();
  uint8_t pl[6]; pl[0] = 0x05; freqToBcd(hz, &pl[1]);
  return sendFrame(pl, 6);
}
bool CivRadio::setMainMode(CivMode m, uint8_t filter) {
  selectMain();
  uint8_t pl[3] = { 0x06, (uint8_t)m, filter };
  return sendFrame(pl, 3);
}
bool CivRadio::setSubMode(CivMode m, uint8_t filter) {
  selectSub();
  uint8_t pl[3] = { 0x06, (uint8_t)m, filter };
  return sendFrame(pl, 3);
}

bool CivRadio::updateDoppler(uint32_t rxHz, uint32_t txHz) {
  bool ok = true;
  ok &= setSubFreq(rxHz);    // downlink (RX) on SUB
  delay(8);
  ok &= setMainFreq(txHz);   // uplink (TX) on MAIN
  return ok;
}

bool CivRadio::enableSatMode(bool on) {
  if (!RADIOS[_model].hasSatMode) return false;
  // IC-9700/9100: command 0x16, sub 0x5A, data 0x01/0x00.
  uint8_t pl[3] = { 0x16, 0x5A, (uint8_t)(on ? 0x01 : 0x00) };
  return sendFrame(pl, 3);
}

bool CivRadio::readSubFreq(uint32_t& hzOut) {
  if (!_stream) return false;
  if (!RADIOS[_model].canReadFreq) return false;   // set-only rig (820/821/970)
  selectSub();                                      // 07 D1 (drains its echo)
  while (_stream->available()) _stream->read();     // clear anything stale
  // Send read-operating-frequency request (cmd 0x03) WITHOUT draining: we want
  // the radio's reply, which on a single-wire CI-V bus arrives after our echo.
  uint8_t f[6] = { 0xFE, 0xFE, _addr, 0xE0, 0x03, 0xFD };
  civLog("TX", f, 6);
  _stream->write(f, 6);
  _stream->flush();
  // Collect the response bytes (echo + reply) for a short window.
  uint8_t buf[48]; size_t bn = 0; uint32_t t0 = millis();
  while (millis() - t0 < 150) {
    while (_stream->available() && bn < sizeof(buf)) {
      buf[bn++] = (uint8_t)_stream->read(); t0 = millis();
    }
    delay(1);
  }
  civLogRaw("RX", buf, bn);
  // Find the reply frame addressed to the controller:
  //   FE FE E0 <addr> 03 b0 b1 b2 b3 b4 FD   (b0 = least-significant BCD pair)
  for (size_t i = 0; i + 10 < bn; ++i) {
    if (buf[i] == 0xFE && buf[i+1] == 0xFE && buf[i+2] == 0xE0 &&
        buf[i+3] == _addr && buf[i+4] == 0x03 && buf[i+10] == 0xFD) {
      uint32_t hz = 0;
      for (int k = 9; k >= 5; --k)
        hz = hz * 100 + (buf[i+k] >> 4) * 10 + (buf[i+k] & 0x0F);
      hzOut = hz;
#if CIV_DEBUG
      Serial.printf("[CI-V] SUB freq read: %lu Hz\n", (unsigned long)hz);
#endif
      return true;
    }
  }
#if CIV_DEBUG
  Serial.println("[CI-V] SUB freq read: no valid reply");
#endif
  return false;
}

CivMode CivRadio::modeFromString(const String& s) {
  String u = s; u.toUpperCase();
  if (u.indexOf("FM")  >= 0) return CIV_FM;
  if (u.indexOf("USB") >= 0) return CIV_USB;
  if (u.indexOf("LSB") >= 0) return CIV_LSB;
  if (u.indexOf("CW")  >= 0) return CIV_CW;
  if (u.indexOf("AM")  >= 0) return CIV_AM;
  // Linear transponders are most often operated USB up / USB down.
  return CIV_USB;
}
