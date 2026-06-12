// ===========================================================================
//  civ.cpp  -  Icom CI-V backend
// ===========================================================================
#include "civ.h"
#include <HardwareSerial.h>

// Set to 0 to silence the CI-V trace on the serial monitor (115200 baud).
#define CIV_DEBUG 1

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

void CivRig::begin(uint32_t baud, int uartNum, int rxPin, int txPin) {
  static HardwareSerial* hs = nullptr;   // construct once, reuse on re-begin
  if (!hs) hs = new HardwareSerial(uartNum);
  hs->begin(baud, SERIAL_8N1, rxPin, txPin);
  _stream = hs;
}

CivMode CivRig::toCiv(RigMode m) {
  switch (m) {
    case RM_LSB: return CIV_LSB;
    case RM_USB: return CIV_USB;
    case RM_CW:  return CIV_CW;
    case RM_FM:  return CIV_FM;
    case RM_AM:  return CIV_AM;
    case RM_DATA:return CIV_RTTY;
    default:     return CIV_USB;
  }
}

// --- BCD: 1 Hz resolution, 5 bytes, least-significant pair first ----------
void CivRig::freqToBcd(uint32_t hz, uint8_t out[5]) {
  for (int i = 0; i < 5; ++i) {
    uint8_t lo = hz % 10; hz /= 10;
    uint8_t hi = hz % 10; hz /= 10;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
}

bool CivRig::sendFrame(const uint8_t* payload, size_t len) {
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
  if (cmdDelayMs) delay(cmdDelayMs);   // CAT Delay: pause before the next command
  return true;
}

bool CivRig::drainEcho(uint32_t timeoutMs) {
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

void CivRig::selectMain() {
  const RadioProfile& p = RADIOS[_model];
  if (p.selLen) sendFrame(p.selMain, p.selLen);
}
void CivRig::selectSub() {
  const RadioProfile& p = RADIOS[_model];
  if (p.selLen) sendFrame(p.selSub, p.selLen);
}

bool CivRig::setFreqCiv(bool sub, uint32_t hz) {
  sub ? selectSub() : selectMain();
  uint8_t pl[6]; pl[0] = 0x05; freqToBcd(hz, &pl[1]);
  return sendFrame(pl, 6);
}
bool CivRig::setModeCiv(bool sub, CivMode m, uint8_t filter) {
  sub ? selectSub() : selectMain();
  uint8_t pl[3] = { 0x06, (uint8_t)m, filter };
  return sendFrame(pl, 3);
}

bool CivRig::setMainFreq(uint32_t hz) { return setFreqCiv(false, hz); }
bool CivRig::setSubFreq (uint32_t hz) { return setFreqCiv(true,  hz); }
bool CivRig::setMainMode(RigMode m)   { return setModeCiv(false, toCiv(m)); }
bool CivRig::setSubMode (RigMode m)   { return setModeCiv(true,  toCiv(m)); }

bool CivRig::enableSatMode(bool on) {
  if (!RADIOS[_model].hasSatMode) return false;
  // IC-9700/9100: command 0x16, sub 0x5A, data 0x01/0x00.
  uint8_t pl[3] = { 0x16, 0x5A, (uint8_t)(on ? 0x01 : 0x00) };
  return sendFrame(pl, 3);
}

// Transmit CTCSS (PL) tone for an FM uplink. The tone lives on the uplink, so
// we select MAIN first. Repeater-tone frequency: cmd 0x1B sub 0x00 + 2 BCD
// bytes of the tone in tenths of Hz (e.g. 67.0 -> 0670 -> 0x06 0x70). Repeater-
// tone (encoder) on/off: cmd 0x16 sub 0x42 data 0x01/0x00. Verified against the
// Icom CI-V reference (IC-9700/910H/9100 family).
bool CivRig::setCtcss(bool on, float toneHz) {
  if (!RADIOS[_model].hasTone) return false;
  // The caller selects the uplink band first (MAIN or SUB, per VFO Type).
  if (on && toneHz > 0) {
    int t = (int)lroundf(toneHz * 10.0f);         // tenths of Hz, 4 BCD digits
    uint8_t b1 = (uint8_t)((((t / 1000) % 10) << 4) | ((t / 100) % 10));
    uint8_t b2 = (uint8_t)((((t / 10)   % 10) << 4) | (t % 10));
    uint8_t freq[4] = { 0x1B, 0x00, b1, b2 };
    sendFrame(freq, 4);
    uint8_t enc[3]  = { 0x16, 0x42, 0x01 };
    return sendFrame(enc, 3);
  }
  uint8_t off[3] = { 0x16, 0x42, 0x00 };
  return sendFrame(off, 3);
}

bool CivRig::readSubFreq (uint32_t& hzOut) { return readFreqCiv(true,  hzOut); }
bool CivRig::readMainFreq(uint32_t& hzOut) { return readFreqCiv(false, hzOut); }

// Read PTT/transmit state via CI-V "read transceiver status" (0x1C 0x00).
// Reply: FE FE E0 <addr> 1C 00 <00=RX|01=TX> FD. Rigs that don't support it stay
// silent; after a few misses we stop polling so we don't load a single-wire bus.
bool CivRig::readPtt(bool& tx) {
  if (!_stream || _pttRead == 0) return false;
  while (_stream->available()) _stream->read();      // clear stale bytes
  uint8_t f[7] = { 0xFE, 0xFE, _addr, 0xE0, 0x1C, 0x00, 0xFD };
  civLog("TX", f, 7);
  _stream->write(f, 7);
  _stream->flush();
  uint8_t buf[48]; size_t bn = 0; uint32_t t0 = millis();
  while (millis() - t0 < 80) {
    while (_stream->available() && bn < sizeof(buf)) {
      buf[bn++] = (uint8_t)_stream->read(); t0 = millis();
    }
    delay(1);
  }
  for (size_t i = 0; i + 7 < bn; ++i) {
    if (buf[i]==0xFE && buf[i+1]==0xFE && buf[i+2]==0xE0 && buf[i+3]==_addr &&
        buf[i+4]==0x1C && buf[i+5]==0x00 && buf[i+7]==0xFD) {
      tx = (buf[i+6] != 0x00);
      _pttRead = 1; _pttFails = 0;
      return true;
    }
  }
  if (_pttRead != 1 && ++_pttFails >= 3) _pttRead = 0;   // give up on silent rigs
  return false;
}

// Read the operating frequency of the SUB (sub=true) or MAIN (sub=false) band.
bool CivRig::readFreqCiv(bool sub, uint32_t& hzOut) {
  if (!_stream) return false;
  if (!RADIOS[_model].canReadFreq) return false;   // set-only rig
  sub ? selectSub() : selectMain();                 // 07 D1/D0 (drains its echo)
  while (_stream->available()) _stream->read();     // clear anything stale
  // Send read-operating-frequency request (cmd 0x03) WITHOUT draining: we want
  // the radio's reply, which on a single-wire CI-V bus arrives after our echo.
  uint8_t f[6] = { 0xFE, 0xFE, _addr, 0xE0, 0x03, 0xFD };
  civLog("TX", f, 6);
  _stream->write(f, 6);
  _stream->flush();
  // Collect the response bytes (echo + reply) for a short window.
  uint8_t buf[48]; size_t bn = 0; uint32_t t0 = millis();
  while (millis() - t0 < (readBudgetMs ? readBudgetMs : 150)) {
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
      Serial.printf("[CI-V] %s freq read: %lu Hz\n", sub ? "SUB" : "MAIN", (unsigned long)hz);
#endif
      return true;
    }
  }
#if CIV_DEBUG
  Serial.printf("[CI-V] %s freq read: no valid reply\n", sub ? "SUB" : "MAIN");
#endif
  return false;
}
