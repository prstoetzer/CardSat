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
  // CAT_USB: transport already open (see Rig::setExternalStream). The adapter's
  // driver owns framing; the baud-dependent stop-bit logic below is a property of
  // the on-board UART path only.
  if (extStream) { _stream = extStream; (void)baud; (void)uartNum;
                   (void)rxPin; (void)txPin; return; }
  static HardwareSerial* hs = nullptr;
  if (!hs) hs = new HardwareSerial(uartNum);
  // Kenwood CAT framing is 8 data bits, no parity. Stop bits are baud-dependent:
  // the IF-232C generation (TS-450/690/790/850/950) requires TWO stop bits at
  // 4800 baud, and one stop bit at every higher rate (e.g. TS-2000 @ 57600).
  // Confirmed from the TS-850 External Control manual (4800 bps, 1 start / 8 data
  // / 2 stop / no parity) and corroborated for the TS-790 family.
  uint32_t cfg = (baud <= 4800) ? SERIAL_8N2 : SERIAL_8N1;
  hs->begin(baud, cfg, rxPin, txPin);
  _stream = hs;
}

// Raw byte write for the serial-terminal diagnostic.
bool KenwoodRig::sendRaw(const uint8_t* b, size_t n) {
  if (!_stream || !b || !n) return false;
  _stream->write(b, n);
  _stream->flush();
  catTrace("TX", b, n);
  return true;
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

// Discard whatever is already buffered, with a HARD bound. A bare
// `while (_stream->available()) _stream->read();` never returns against a stream
// that supplies bytes as fast as they are read -- which a USB serial adapter can.
void KenwoodRig::drainStale() {
  if (!_stream) return;
  const uint32_t t0 = millis();
  unsigned n = 512;
  while (_stream->available() > 0 && n-- && millis() - t0 < 20) {
    if (_stream->read() < 0) break;              // -1: stream gone
  }
}

bool KenwoodRig::sendCmd(const String& cmd) {
  if (!_stream) return false;
  kwLog("TX", cmd);
  catTrace("TX", (const uint8_t*)cmd.c_str(), cmd.length());
  _stream->print(cmd);
  _stream->flush();
  return true;
}

bool KenwoodRig::setVfoFreq(const char* vfo, freq_t hz) {
  char buf[24];
  // Kenwood ASCII frequency is 11 digits. CardSat only sends this rig a sub-GHz IF
  // (transverter LO offset handles higher bands), so 11 digits is always ample.
  snprintf(buf, sizeof(buf), "%s%011llu;", vfo, (unsigned long long)hz);
  return sendCmd(buf);
}

bool KenwoodRig::setModeKw(RigMode m) {
  char buf[6];
  snprintf(buf, sizeof(buf), "MD%c;", modeDigit(m));
  return sendCmd(buf);
}

bool KenwoodRig::readSubFreq(freq_t& hzOut) {
  if (!_stream || !RADIOS[_model].canReadFreq) return false;
  drainStale();                                    // bounded (never spins)
  sendCmd("FA;");                                  // query VFO A (downlink)
  // Collect the reply up to the ';' terminator within a short window.
  // HARD deadline, fixed at entry, and a bounded reply. The old loop reset t0 on
  // every byte (a deadline that cannot expire while bytes arrive) AND appended to an
  // uncapped String -- so a chatty stream both span forever and grew the String until
  // the heap gave out. Over USB CAT that is reachable with a wrong baud or a floating
  // RX line. A Kenwood reply is "FA" + 11 digits + ';' = 14 chars; 64 is generous.
  // Inactivity window (250 ms, as the original) + 800 ms ceiling. Same shape as
  // civ.cpp's loops: a 1200-baud reply ("FA"+11 digits+';' = 117 ms of bus time
  // plus radio processing) is collected byte-by-byte with the window extending,
  // while a stream that never goes quiet hits the ceiling and returns. The 64-char
  // cap already bounds memory; this bounds time without penalizing slow bauds.
  String rx; rx.reserve(64);
  const uint32_t t0 = millis(); uint32_t lastByteMs = t0;
  while (millis() - lastByteMs < 250 && millis() - t0 < 800) {
    unsigned guard = 64;                           // always fall through to the test
    while (_stream->available() > 0 && guard--) {
      const int rc = _stream->read();
      if (rc < 0) break;                          // -1: stream gone, not a byte
      char c = (char)rc;                          // always CONSUME
      if (rx.length() < 64) rx += c;
      lastByteMs = millis();
      if (c == ';') break;
    }
    if (rx.endsWith(";")) break;
    delay(1);
  }
  kwLog("RX", rx);
  if (rx.length()) catTrace("RX", (const uint8_t*)rx.c_str(), rx.length());
  int i = rx.indexOf("FA");
  if (i >= 0 && (int)rx.length() >= i + 13) {       // "FA" + 11 digits
    freq_t hz = 0; bool ok = false;
    for (int k = i + 2; k < i + 13; ++k) {
      char c = rx[k];
      if (c < '0' || c > '9') { ok = false; break; }
      hz = hz * 10 + (c - '0'); ok = true;
    }
    if (ok) {
      hzOut = hz;
#if KW_DEBUG
      Serial.printf("[CAT] VFO-A (downlink) read: %llu Hz\n", (unsigned long long)hz);
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
// CAT list. The TS-790 uses the same FA/FB/MD ASCII protocol (FA; read-back
// confirmed from a live Hamlib TS-790 trace), at 4800 baud 8N2; its CTCSS needs
// the optional TSU-5 decoder unit and is decode-only over the panel, so the
// profile sets hasTone=false. CAT on the TS-790 is via the optional IF-232C
// interface (the operating manual documents the interface but not the command
// set). Still the least bench-verified of the three families -- watch the trace.
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
