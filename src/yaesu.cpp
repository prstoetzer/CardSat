// ===========================================================================
//  yaesu.cpp  -  Yaesu 5-byte CAT backend
// ===========================================================================
#include "yaesu.h"
#include <HardwareSerial.h>

// Set to 0 to silence the CAT trace on the serial monitor (115200 baud).
#define YAESU_DEBUG 1

#if YAESU_DEBUG
static void yaLog(const char* tag, const uint8_t* b, size_t n) {
  Serial.printf("[CAT %s]", tag);
  for (size_t i = 0; i < n; ++i) Serial.printf(" %02X", b[i]);
  if (n == 5) {
    uint8_t op = b[4];
    const char* vfo = (op & 0x20) ? "TX" : (op & 0x10) ? "RX" : "MAIN";
    switch (op & 0x0F) {
      case 0x01: { uint32_t f = 0;             // set-freq (10 Hz BCD)
        for (int i = 0; i < 4; ++i) f = f*100 + (b[i]>>4)*10 + (b[i]&0x0F);
        Serial.printf("  set-freq %s %lu Hz", vfo, (unsigned long)f*10); } break;
      case 0x07: Serial.printf("  set-mode %s 0x%02X", vfo, b[0]); break;
      case 0x03: Serial.printf("  read-freq req %s", vfo); break;
      case 0x00: Serial.print(b[4] == 0x80 ? "  CAT OFF" : "  CAT ON"); break;
    }
  }
  Serial.println();
}
#else
static inline void yaLog(const char*, const uint8_t*, size_t) {}
#endif

void YaesuRig::begin(uint32_t baud, int uartNum, int rxPin, int txPin) {
  static HardwareSerial* hs = nullptr;
  if (!hs) hs = new HardwareSerial(uartNum);
  hs->begin(baud, SERIAL_8N2, rxPin, txPin);   // Yaesu CAT = 8N2
  _stream = hs;
  const uint8_t catOn[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };  // enable CAT
  send(catOn);
}

uint8_t YaesuRig::modeCode(RigMode m) {
  switch (m) {                       // FT-847 operating-mode codes
    case RM_LSB: return 0x00;
    case RM_USB: return 0x01;
    case RM_CW:  return 0x02;
    case RM_AM:  return 0x04;
    case RM_FM:  return 0x08;
    case RM_DATA:return 0x0A;
    default:     return 0x01;
  }
}

// Big-endian BCD, 10 Hz resolution, 4 bytes (8 digits of hz/10).
void YaesuRig::freqToBcd(uint32_t hz, uint8_t out[4]) {
  uint32_t f = hz / 10;                          // 10 Hz units
  out[0] = (uint8_t)((((f/10000000)%10)<<4) | ((f/1000000)%10));
  out[1] = (uint8_t)((((f/100000)%10)<<4)   | ((f/10000)%10));
  out[2] = (uint8_t)((((f/1000)%10)<<4)     | ((f/100)%10));
  out[3] = (uint8_t)((((f/10)%10)<<4)       | (f%10));
}

bool YaesuRig::send(const uint8_t cmd[5]) {
  if (!_stream) return false;
  yaLog("TX", cmd, 5);
  _stream->write(cmd, 5);
  _stream->flush();
  delay(_postMs);              // Yaesu CAT dislikes back-to-back fast writes
  // Yaesu set commands are not acknowledged; drain any stray bytes.
  while (_stream->available()) _stream->read();
  return true;
}

bool YaesuRig::setFreq(uint8_t opcode, uint32_t hz) {
  uint8_t cmd[5];
  freqToBcd(hz, cmd);          // P1..P4 = BCD frequency
  cmd[4] = opcode;             // 0x11 = SAT RX, 0x21 = SAT TX
  return send(cmd);
}

bool YaesuRig::setMode(uint8_t opcode, RigMode m) {
  uint8_t cmd[5] = { modeCode(m), 0x00, 0x00, 0x00, opcode };  // 0x17 RX / 0x27 TX
  return send(cmd);
}

// Inverse of freqToBcd: 4 big-endian BCD bytes (8 digits) of 10 Hz units -> Hz.
uint32_t YaesuRig::bcdToFreq(const uint8_t in[4]) {
  uint32_t f = 0;
  for (int i = 0; i < 4; ++i) f = f * 100 + (in[i] >> 4) * 10 + (in[i] & 0x0F);
  return f * 10;
}

// Read the SAT-RX (downlink) frequency. FT-847 "read freq & mode" is opcode
// 0x03, patched to 0x13 for the SAT-RX VFO; the reply is 5 bytes: 4 big-endian
// BCD frequency bytes (10 Hz units) + 1 mode byte. Only firmware-updated FT-847s
// can read at all (early units stay silent -> we time out and return false). The
// FT-736R has no read path (canReadFreq is false for it).
bool YaesuRig::readSubFreq(uint32_t& hzOut) {
  if (!_stream || !RADIOS[_model].canReadFreq) return false;
  while (_stream->available()) _stream->read();                 // flush stale bytes
  const uint8_t cmd[5] = { 0x00, 0x00, 0x00, 0x00, 0x13 };      // read SAT-RX
  yaLog("TX", cmd, 5);
  _stream->write(cmd, 5);
  _stream->flush();
  uint8_t r[5]; size_t n = 0; uint32_t t0 = millis();
  while (millis() - t0 < 200 && n < 5) {
    if (_stream->available()) { r[n++] = (uint8_t)_stream->read(); t0 = millis(); }
    else delay(1);
  }
#if YAESU_DEBUG
  Serial.print("[CAT RX]");
  for (size_t i = 0; i < n; ++i) Serial.printf(" %02X", r[i]);
  if (n < 5) Serial.print("  (no/short reply -- early FT-847 firmware can't read)");
  Serial.println();
#endif
  if (n < 5) return false;                                       // timeout / no read
  uint32_t hz = bcdToFreq(r);                                    // r[4] = mode (unused)
  // Plausibility + wrong-VFO guard. In satellite mode the FT-847 sometimes
  // returns the SAT-TX (uplink) VFO instead of SAT-RX (Hamlib #1286, "freqs
  // alternate"). A real knob move within a transponder passband is well under
  // 1 MHz, while the uplink is a whole band away -- so reject reads that jump
  // > 1 MHz from the downlink we last commanded and hold the passband steady.
  if (hz < 1000000UL || hz > 1300000000UL) return false;
  if (_lastSubHz) {
    uint32_t d = hz > _lastSubHz ? hz - _lastSubHz : _lastSubHz - hz;
    if (d > 1000000UL) {
#if YAESU_DEBUG
      Serial.printf("[CAT] read %lu Hz rejected (>1 MHz from downlink %lu -- wrong VFO?)\n",
                    (unsigned long)hz, (unsigned long)_lastSubHz);
#endif
      return false;
    }
  }
  hzOut = hz;
#if YAESU_DEBUG
  Serial.printf("[CAT] SAT-RX (downlink) read: %lu Hz\n", (unsigned long)hz);
#endif
  return true;
}

// FT-847 CTCSS CAT codes, in the same order as the shared 39-tone list
// (see ctcssToneIndex). The satellite uplink is the SAT-TX VFO, so the tone
// frequency uses opcode 0x2B and the encoder is enabled with {0x4A..0x2A}
// (off = {0x8A..0x2A}). Codes + sequences verified against Hamlib ft847.c.
static const uint8_t FT847_CTCSS_CODE[39] = {
  0x3F,0x39,0x1F,0x3E,0x0F,0x3D,0x1E,0x3C,0x0E,0x3B,
  0x1D,0x3A,0x0D,0x1C,0x0C,0x1B,0x0B,0x1A,0x0A,0x19,
  0x09,0x18,0x08,0x17,0x07,0x16,0x06,0x15,0x05,0x14,
  0x04,0x13,0x03,0x12,0x02,0x11,0x01,0x10,0x00
};

bool YaesuRig::setCtcss(bool on, float toneHz) {
  if (!RADIOS[_model].hasTone) return false;
  if (!on || toneHz <= 0) {
    const uint8_t off[5] = { 0x8A, 0x00, 0x00, 0x00, 0x2A };  // CTCSS/DCS off, sat tx
    return send(off);
  }
  int i = ctcssToneIndex(toneHz);
  if (i < 0) return false;
  const uint8_t freq[5] = { FT847_CTCSS_CODE[i], 0x00, 0x00, 0x00, 0x2B }; // freq, sat tx
  send(freq);
  const uint8_t enc[5]  = { 0x4A, 0x00, 0x00, 0x00, 0x2A };   // CTCSS enc on, sat tx
  return send(enc);
}
