// ===========================================================================
//  kenwood.cpp  -  Kenwood ASCII CAT backend
// ===========================================================================
#include "kenwood.h"
#include <HardwareSerial.h>

// Set to 0 to silence the CAT trace on the serial monitor (115200 baud).
#define KW_DEBUG 1

#if KW_DEBUG
static void kwLog(const char* tag, const String& s) {
  String t = s; t.replace("\r", "");   // show the command without the CR
  Serial.printf("[CAT %s] %s\n", tag, t.c_str());
}
#else
static inline void kwLog(const char*, const String&) {}
#endif

void KenwoodRig::begin(uint32_t baud, int uartNum, int rxPin, int txPin) {
  static HardwareSerial* hs = nullptr;
  if (!hs) hs = new HardwareSerial(uartNum);
  hs->begin(baud, SERIAL_8N1, rxPin, txPin);   // Kenwood CAT = 8N1
  _stream = hs;
}

char KenwoodRig::modeDigit(RigMode m) {
  switch (m) {                 // Kenwood mode codes
    case RM_LSB: return '1';
    case RM_USB: return '2';
    case RM_CW:  return '3';
    case RM_FM:  return '4';
    case RM_AM:  return '5';
    case RM_DATA:return '6';   // FSK
    default:     return '2';
  }
}

bool KenwoodRig::sendCmd(const String& cmd) {
  if (!_stream) return false;
  kwLog("TX", cmd);
  _stream->print(cmd);
  _stream->flush();
  return true;
}

bool KenwoodRig::setVfoFreq(const char* vfo, uint32_t hz) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%s%011lu;", vfo, (unsigned long)hz);
  return sendCmd(buf);
}

bool KenwoodRig::setModeKw(RigMode m) {
  char buf[6];
  snprintf(buf, sizeof(buf), "MD%c;", modeDigit(m));
  return sendCmd(buf);
}

bool KenwoodRig::readSubFreq(uint32_t& hzOut) {
  if (!_stream || !RADIOS[_model].canReadFreq) return false;
  while (_stream->available()) _stream->read();    // clear stale bytes
  sendCmd("FA;");                                  // query VFO A (downlink)
  // Collect the reply up to the ';' terminator within a short window.
  String rx; uint32_t t0 = millis();
  while (millis() - t0 < 250) {
    while (_stream->available()) {
      char c = (char)_stream->read();
      rx += c; t0 = millis();
      if (c == ';') break;
    }
    if (rx.endsWith(";")) break;
    delay(1);
  }
  kwLog("RX", rx);
  int i = rx.indexOf("FA");
  if (i >= 0 && (int)rx.length() >= i + 13) {       // "FA" + 11 digits
    uint32_t hz = 0; bool ok = false;
    for (int k = i + 2; k < i + 13; ++k) {
      char c = rx[k];
      if (c < '0' || c > '9') { ok = false; break; }
      hz = hz * 10 + (c - '0'); ok = true;
    }
    if (ok) {
      hzOut = hz;
#if KW_DEBUG
      Serial.printf("[CAT] VFO-A (downlink) read: %lu Hz\n", (unsigned long)hz);
#endif
      return true;
    }
  }
#if KW_DEBUG
  Serial.println("[CAT] VFO-A read: no valid reply");
#endif
  return false;
}

// Transmit CTCSS (PL) tone for an FM uplink. TS-2000: TNnn sets the tone
// (encode) number -- 1-based into the same 39-tone list as ctcssToneIndex --
// and TO1/TO0 turns the TONE (encode) function on/off. The rig applies it to
// the current TX (uplink) band. Per Hamlib kenwood TN variant + the TS-2000
// CAT list; least bench-verified of the three families, so watch the trace.
bool KenwoodRig::setCtcss(bool on, float toneHz) {
  if (!RADIOS[_model].hasTone) return false;
  if (!on || toneHz <= 0) return sendCmd("TO0;");      // TONE function off
  int i = ctcssToneIndex(toneHz);
  if (i < 0) return false;
  char buf[8];
  snprintf(buf, sizeof(buf), "TN%02d;", i + 1);        // tone number (1-based)
  sendCmd(buf);
  return sendCmd("TO1;");                              // TONE (encode) on
}
