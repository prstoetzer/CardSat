// ===========================================================================
//  civ.cpp  -  Icom CI-V backend
// ===========================================================================
#include "civ.h"
#include <HardwareSerial.h>
#include <driver/gpio.h>   // gpio_set_pull_mode for the pull-up
#include <driver/uart.h>   // uart_set_line_inverse
#include <soc/gpio_struct.h>  // GPIO.pin[].pad_driver: open-drain without touching the matrix
#include <esp_rom_gpio.h>     // esp_rom_gpio_connect_in_signal: re-assert RX input on the pad
#include <soc/gpio_sig_map.h> // U0/U1/U2RXD_IN_IDX signal indices

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
    case 0x16:                                  // 0x16 group: sub-cmd selects function
      if (n > 5 && b[5] == 0x5A)
        Serial.printf("  sat-mode %s", (n > 6 && b[6]) ? "ON" : "OFF");   // 9100/9700
      else if (n > 5 && b[5] == 0x42)
        Serial.printf("  tone-enc %s", (n > 6 && b[6]) ? "ON" : "OFF");
      else if (n > 5) Serial.printf("  ctl-16 sub %02X", b[5]);
      break;
    case 0x1A:                                  // IC-910 sat-mode lives here (sub 0x07)
      if (n > 5 && b[5] == 0x07)
        Serial.printf("  sat-mode %s", (n > 6 && b[6]) ? "ON" : "OFF");
      else if (n > 5) Serial.printf("  ctl-1A sub %02X", b[5]);
      break;
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
  // CAT_USB: the transport is a USB<->serial adapter, already open. Use it and do
  // NOT touch the on-board UART or its pins -- none of the pin muxing, open-drain or
  // signal-inversion work below applies, and doing it would disturb G1/G2 for no
  // reason. The adapter's own driver handles framing; CI-V is 8N1 either way.
  if (extStream) { _stream = extStream; (void)baud; (void)uartNum;
                   (void)rxPin; (void)txPin; return; }
  static HardwareSerial* hs = nullptr;   // construct once, reuse on re-begin
  if (!hs) hs = new HardwareSerial(uartNum);

  // hs (the UART peripheral) is static and survives rig delete/recreate, so a
  // previous begin() may have left pins attached to the UART matrix. When the
  // wiring mode changes (e.g. TX/RX -> single-pin G1) the OLD pins must be released
  // back to plain GPIO first, or a stale pad keeps its UART routing -- the symptom
  // being the wrong pin still held high and the new pin never driven. Track the
  // pins we last attached (static, like hs) and reset them before reconfiguring.
  static int lastA = -1, lastB = -1;
  if (lastA >= 0) gpio_reset_pin((gpio_num_t)lastA);
  if (lastB >= 0 && lastB != lastA) gpio_reset_pin((gpio_num_t)lastB);
  lastA = lastB = -1;

  if (_pinMode == 0) {
    // Normal, recommended path: separate wires. G2 = TX (push-pull), G1 = RX.
    hs->end();                                   // release any prior pin bindings
    hs->begin(baud, SERIAL_8N1, rxPin, txPin);
    lastA = rxPin; lastB = txPin;
  } else {
    // Single-pin CI-V: one shared GPIO carries both directions, like a real CI-V
    // one-wire bus. The line idles near 3.3 V (UART mark, held by the pull-up) and is
    // pulled low only for data. An external pull-up (the radio's CI-V bus and/or a
    // level-shifter) should still be present for real communication. UNVERIFIED
    // on-air -- see CIV_SINGLE_PIN.md and mind the 5 V / 3.3 V cautions before
    // connecting a radio.
    int pin = (_pinMode == 2) ? rxPin : txPin;   // 1 -> tx pin (G2), 2 -> rx pin (G1)

    // Single-pin CI-V setup (verified on the bench step by step):
    //  1. begin(pin, pin) puts BOTH UART TX and RX on the chosen pad. This is the
    //     same call shape as the known-good two-wire begin(rx,tx), and a scope
    //     confirmed real UART data comes out of `pin` this way.
    //  2. Clear UART signal inversion so the idle/mark state is HIGH, not LOW.
    //  3. Add a pull-up.
    //  4. Enable OPEN-DRAIN at the PAD REGISTER (GPIO.pin[pin].pad_driver = 1).
    //     This is the crucial bit: gpio_set_direction(...OD) re-runs the pad's
    //     direction config and DETACHES the UART output matrix, parking the pad LOW
    //     (the earlier "idle = 0" bug). Setting pad_driver directly flips only the
    //     open-drain bit, leaving the UART TX matrix output attached -- so the pin
    //     idles HIGH via the pull-up and is pulled low only for data, while still
    //     letting the radio pull it low (shared one-wire bus).
    hs->end();
    hs->begin(baud, SERIAL_8N1, pin, pin);       // TX and RX both on `pin`
    uart_set_line_inverse((uart_port_t)uartNum, UART_SIGNAL_INV_DISABLE);  // idle = HIGH
    gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
    GPIO.pin[pin].pad_driver = 1;                // open-drain at the pad; matrix kept
    // Re-assert the UART RX input on the same pad. begin(pin,pin) bound RX, but the
    // TX output binding on a shared pad can leave the input path disabled; this only
    // (re)connects the pad to the RX signal -- it never touches the TX output or the
    // pad direction, so the working TX is undisturbed. Without this CardSat may not
    // hear the bus (not even its own echo). Needed to receive the radio's replies.
    { uint32_t rxSig = (uartNum == 0) ? U0RXD_IN_IDX
                     : (uartNum == 2) ? U2RXD_IN_IDX : U1RXD_IN_IDX;
      esp_rom_gpio_connect_in_signal((gpio_num_t)pin, rxSig, false); }
    lastA = pin; lastB = pin;
    delay(2);
    Serial.printf("[CI-V 1-pin] G%d ready (idle=%d)\n", pin, digitalRead(pin));
  }

  _stream = hs;
}

// Raw byte write for the serial-terminal diagnostic: push arbitrary bytes onto
// the CAT port exactly as typed, and trace them as TX so the monitor shows them.
bool CivRig::sendRaw(const uint8_t* b, size_t n) {
  if (!_stream || !b || !n) return false;
  _stream->write(b, n);
  catTrace("TX", b, n);
  return true;
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
  catTrace("TX", buf, n); // and to the on-device serial-terminal monitor
  _stream->write(buf, n);
  _stream->flush();
  drainEcho();            // swallow our own echo + radio's OK/NG (0xFB/0xFA)
  if (cmdDelayMs) delay(cmdDelayMs);   // CAT Delay: pause before the next command
  return true;
}

// Discard whatever is already sitting in the RX buffer, with a HARD bound.
// A bare `while (_stream->available()) _stream->read();` is an infinite loop against
// any stream that produces bytes as fast as they are consumed -- which a USB serial
// adapter can, and a UART generally cannot. Both call sites want "clear what is
// there now", not "read until the end of time", so a cap is a faithful fix and not
// a behaviour change: 512 bytes is far more than any CI-V frame or echo (longest is
// ~11 bytes), and the time bound catches a stream that is merely fast.
void CivRig::drainStale() {
  if (!_stream) return;
  const uint32_t t0 = millis();
  unsigned n = 512;
  while (_stream->available() > 0 && n-- && millis() - t0 < 20) {
    if (_stream->read() < 0) break;              // -1: stream gone
  }
}

bool CivRig::drainEcho(uint32_t timeoutMs) {
  if (!_stream) return false;
  uint32_t t0 = millis();
  int fd = 0;                          // 1 = our echo seen, 2 = radio reply seen
#if CIV_DEBUG
  uint8_t rx[40]; size_t rn = 0;
#endif
  // Capture received bytes for the on-device serial monitor (separate small
  // buffer so it works even when CIV_DEBUG is off).
  uint8_t mon[48]; size_t mn = 0;
  // HARD deadline, never extended. The inner loop below used to do `t0 = millis()`
  // on every byte received, which is a timeout that cannot expire while bytes keep
  // arriving: the inner `while (available())` never exits, `delay(1)` is never
  // reached, nothing yields, and the task spins forever. On a UART that was
  // survivable -- a silent radio stops sending and the loop drains. Over USB it is
  // not: an adapter that echoes, a wrong baud producing framing garbage, or plain
  // noise on a floating RX line keeps available() true indefinitely. That froze the
  // firmware at engage with no watchdog and no crash (0.9.58-wip bench: the freeze
  // landed immediately after begin() returned, on the first CAT read).
  //
  // `deadline` is now fixed at entry. The inner loop additionally caps the bytes it
  // will consume per pass, so it always returns to the outer test.
  // INACTIVITY timeout plus an ABSOLUTE ceiling -- both, deliberately.
  //
  // The original loop reset its deadline on every byte: adaptive to slow bauds
  // (at 1200 bps a set-freq echo alone is ~92 ms) but unbounded against a stream
  // that never goes quiet -- the USB CAT freeze. The first fix (a hard deadline
  // from entry) was bounded but NOT adaptive: 60 ms expires mid-echo at 1200 bps,
  // stranding half a frame in the RX buffer to poison the next read. The IC-820H's
  // documented default CI-V rate is exactly 1200 bps, so that was a real wired-path
  // regression, caught in review before it shipped.
  //
  // This shape has both properties: a byte extends the deadline by timeoutMs (the
  // old, baud-tolerant behavior), and nothing can extend it past hardCap from entry
  // (the termination guarantee). A healthy 1200-baud bus finishes its frame with
  // 8 ms inter-byte gaps and exits on the early tests; a chatty adapter hits the
  // ceiling at 400 ms and returns.
  const uint32_t hardCap = 400;
  uint32_t lastByteMs = t0;
  while (millis() - lastByteMs < timeoutMs && millis() - t0 < hardCap) {
    unsigned guard = 64;                       // bytes per pass: always fall through
    while (_stream->available() > 0 && guard--) {
      const int rb = _stream->read();
      if (rb < 0) break;                         // -1: stream gone, not a byte
      uint8_t b = (uint8_t)rb;
#if CIV_DEBUG
      if (rn < sizeof(rx)) rx[rn++] = b;
#endif
      if (mn < sizeof(mon)) mon[mn++] = b;
      if (b == 0xFD) fd++;
      lastByteMs = millis();                   // extends the inactivity window
    }
    if (fd >= 2) break;                        // echo + ACK/NAK both arrived
    if (fd >= 1 && millis() - lastByteMs > 25) break;  // echo seen, radio not replying
    delay(1);
  }
  if (mn) catTrace("RX", mon, mn);     // report raw received bytes to the monitor
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

// Map a frequency to the Icom CI-V band code used by the "07 D2" band-selection
// command: 0x01 = 144 MHz, 0x02 = 430/440 MHz, 0x03 = 1.2 GHz. Returns 0 for a
// frequency outside those amateur VHF/UHF bands (caller skips the assignment).
static uint8_t civBandCode(uint32_t hz) {
  if (hz >= 144000000UL && hz <= 148000000UL) return 0x01;   // 2 m
  if (hz >= 430000000UL && hz <= 450000000UL) return 0x02;   // 70 cm
  if (hz >= 1240000000UL && hz <= 1300000000UL) return 0x03; // 23 cm
  return 0x00;
}

// Assign which band sits on MAIN vs SUB via CI-V "07 D2 00/01 <bandcode>".
// MAIN gets the band of mainHz, SUB gets the band of subHz. Only rigs whose
// profile sets canAssignBand (IC-9100/IC-9700) act; others return false.
//
// *** UNTESTED ON HARDWARE. *** The author operates an IC-821, which has no
// band-assignment command, so this path has never been exercised on a real
// radio. The frame format (07 D2 00 = main, 07 D2 01 = sub; band codes
// 01/02/03) is taken from the IC-9700 CI-V Reference Guide. It is sent once at
// CAT-engage, never per-tick, so a wrong frame cannot spam the bus.
bool CivRig::assignBands(uint32_t mainHz, uint32_t subHz) {
  if (!RADIOS[_model].canAssignBand) return false;
  uint8_t mb = civBandCode(mainHz);
  uint8_t sb = civBandCode(subHz);
  if (!mb || !sb) return false;                 // unknown band -> leave to operator

  // --- IC-910: read MAIN's band, swap MAIN/SUB if it's the wrong one. ---
  // The 910 has no "07 D2" band-assignment. Its 07 group instead exposes
  //   07 D1 = Select MAIN VFO,  07 D0 = Switch VFO A and VFO B (swap MAIN/SUB).
  // Because the 910's MAIN and SUB can never be on the same band, checking MAIN
  // alone is sufficient: if MAIN isn't the band we want there, one swap fixes
  // both legs. This mirrors how Hamlib drives the 910 (confirmed from its serial
  // trace: 07 D1 then 03 to read MAIN, 07 D0 to swap). readMainFreq() issues the
  // (now-corrected) selMain = 07 D1 then 03. Fired once at engage.
  // *** UNTESTED ON HARDWARE *** (author has no IC-910).
  if (_model == RIG_IC910) {
    uint32_t mainNow = 0;
    if (!readMainFreq(mainNow) || !mainNow) {
      if (CIV_DEBUG) Serial.println("[CAT] 910 assignBands: MAIN read failed, no swap");
      return false;                               // can't tell -> don't guess
    }
    bool ok = true;
    if (civBandCode(mainNow) != mb) {             // wrong band on MAIN -> swap
      uint8_t swapAB[2] = { 0x07, 0xD0 };         // Switch VFO A and VFO B
      ok = sendFrame(swapAB, 2);
      if (CIV_DEBUG)
        Serial.printf("[CAT] 910 assignBands: MAIN had band%02X, want band%02X -> SWAP %s\n",
                      civBandCode(mainNow), mb, ok ? "sent" : "FAILED");
    } else if (CIV_DEBUG) {
      Serial.printf("[CAT] 910 assignBands: MAIN already band%02X, no swap\n", mb);
    }
    return ok;
  }

  // --- IC-9100 / IC-9700: direct band-selection set via 07 D2. ---
  bool ok = true;
  uint8_t main[4] = { 0x07, 0xD2, 0x00, mb };   // assign MAIN band
  uint8_t sub [4] = { 0x07, 0xD2, 0x01, sb };   // assign SUB band
  ok &= sendFrame(main, 4);
  ok &= sendFrame(sub,  4);
  if (CIV_DEBUG) {
    Serial.printf("[CAT] assignBands: MAIN<-band%02X (%lu Hz) SUB<-band%02X (%lu Hz) %s\n",
                  mb, (unsigned long)mainHz, sb, (unsigned long)subHz,
                  ok ? "sent" : "FAILED");
  }
  return ok;
}

bool CivRig::setFreqCiv(bool sub, uint32_t hz) {
  sub ? selectSub() : selectMain();
  uint8_t pl[6]; pl[0] = 0x05; freqToBcd(hz, &pl[1]);
  bool ok = sendFrame(pl, 6);
  // Remember what we last commanded on each band, so a flaky SUB read (the
  // IC-821 in particular often won't answer 0x03 for the SUB band) can fall back
  // to this value instead of returning nothing.
  if (ok) { if (sub) _lastSubHz = hz; else _lastMainHz = hz; }
  return ok;
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
  // Satmode command differs by rig: IC-9100/9700 use 0x16/0x5A, but the IC-910
  // uses 0x1A/0x07 (per its CONTROL COMMAND table). Both the command and the
  // sub-command come from the profile.
  uint8_t pl[3] = { RADIOS[_model].satModeCmd,
                    RADIOS[_model].satModeSub,
                    (uint8_t)(on ? 0x01 : 0x00) };
  return sendFrame(pl, 3);
}

// Transmit CTCSS (PL) tone for an FM uplink. The tone lives on the uplink, so
// we select MAIN first. Repeater-tone frequency: cmd 0x1B sub 0x00 + 2 BCD
// bytes of the tone in tenths of Hz (e.g. 67.0 -> 0670 -> 0x06 0x70), confirmed
// from the IC-9700 CI-V Reference Guide. Encoder on/off: cmd 0x16, sub is per-rig
// (profile toneEncSub): IC-9100/9700 = 0x42 (Repeater tone), IC-910 = 0x43
// (Subaudible tone -- on the 910, 0x42 is the auto-notch filter). The IC-910's
// 0x1B tone-frequency command is unconfirmed; if the rig NAKs it, the encoder
// on/off still applies whatever tone is set on the radio.
bool CivRig::setCtcss(bool on, float toneHz) {
  if (!RADIOS[_model].hasTone) return false;
  // The caller selects the uplink band first (MAIN or SUB, per VFO Type).
  if (on && toneHz > 0) {
    int t = (int)lroundf(toneHz * 10.0f);         // tenths of Hz, 4 BCD digits
    uint8_t b1 = (uint8_t)((((t / 1000) % 10) << 4) | ((t / 100) % 10));
    uint8_t b2 = (uint8_t)((((t / 10)   % 10) << 4) | (t % 10));
    uint8_t freq[4] = { 0x1B, 0x00, b1, b2 };
    sendFrame(freq, 4);
    uint8_t enc[3]  = { 0x16, RADIOS[_model].toneEncSub, 0x01 };
    return sendFrame(enc, 3);
  }
  uint8_t off[3] = { 0x16, RADIOS[_model].toneEncSub, 0x00 };
  return sendFrame(off, 3);
}

bool CivRig::readSubFreq (uint32_t& hzOut) { return readFreqCiv(true,  hzOut); }
bool CivRig::readMainFreq(uint32_t& hzOut) { return readFreqCiv(false, hzOut); }

// Read PTT/transmit state via CI-V "read transceiver status" (0x1C 0x00).
// Reply: FE FE E0 <addr> 1C 00 <00=RX|01=TX> FD. Rigs that don't support it stay
// silent; after a few misses we stop polling so we don't load a single-wire bus.
bool CivRig::readPtt(bool& tx) {
  if (!_stream || _pttRead == 0) return false;
  drainStale();                                      // bounded (see drainStale)
  uint8_t f[7] = { 0xFE, 0xFE, _addr, 0xE0, 0x1C, 0x00, 0xFD };
  civLog("TX", f, 7);
  _stream->write(f, 7);
  _stream->flush();
  // HARD deadline, fixed at entry. `t0 = millis()` used to be reset on every byte
  // received, which cannot expire while bytes keep arriving -- the same defect as
  // drainEcho had. Worse here: once bn hits sizeof(buf) the inner loop stops
  // CONSUMING but available() stays true, so the bytes are never drained and the
  // deadline never advances. Over USB (echo, baud mismatch, noise on a floating RX)
  // that is an unbreakable spin with no yield: no watchdog, no crash, frozen screen.
  // Inactivity window (80 ms, as the original) + absolute ceiling (400 ms). See
  // drainEcho for the full rationale: at 1200 bps the echo+reply is ~125 ms of
  // bus time, so a hard 80 ms truncates it -- the inactivity form collects the
  // whole exchange at any baud, and the ceiling still guarantees termination.
  uint8_t buf[48]; size_t bn = 0;
  const uint32_t t0 = millis(); uint32_t lastByteMs = t0;
  while (millis() - lastByteMs < 80 && millis() - t0 < 400) {
    unsigned guard = 64;                          // always fall through to the test
    while (_stream->available() > 0 && guard--) {
      const int rb = _stream->read();
      if (rb < 0) break;                         // -1: stream gone, not a byte
      uint8_t b = (uint8_t)rb;       // always CONSUME, even if full
      if (bn < sizeof(buf)) buf[bn++] = b;
      lastByteMs = millis();
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
//
// The SUB-band read is the unreliable one, especially on the IC-821: the radio
// frequently won't answer cmd 0x03 for the SUB band, or answers with the MAIN
// frequency, unless the SUB band is explicitly re-selected immediately before the
// read and given a moment to settle. We therefore (1) re-select the band, (2)
// wait the inter-command settle, (3) flush stale bytes, (4) send 0x03, and (5) if
// no valid reply arrives within the budget, fall back to the last value we
// COMMANDED on that band rather than failing outright. The boolean return tells
// the caller whether the value is fresh (true) or a fallback/none (false); hzOut
// is always left at the best estimate we have.
bool CivRig::readFreqCiv(bool sub, uint32_t& hzOut) {
  uint32_t lastSet = sub ? _lastSubHz : _lastMainHz;
  if (lastSet) hzOut = lastSet;                     // default to last-commanded
  if (!_stream) return false;
  if (!RADIOS[_model].canReadFreq) return false;    // set-only rig

  // Re-select the target band immediately before the read. sendFrame() applies
  // the configured CAT inter-command delay after the select, which doubles as the
  // settle time the IC-821's SUB band needs before it will report correctly.
  sub ? selectSub() : selectMain();
  drainStale();                                      // bounded (see drainStale)

  // Send read-operating-frequency request (cmd 0x03) WITHOUT draining: we want
  // the radio's reply, which on a single-wire CI-V bus arrives after our echo.
  uint8_t f[6] = { 0xFE, 0xFE, _addr, 0xE0, 0x03, 0xFD };
  civLog("TX", f, 6);
  _stream->write(f, 6);
  _stream->flush();
  // Collect the response bytes (echo + reply) for a short window.
  // HARD deadline, fixed at entry -- see readPtt for why. This is the read the
  // Doppler loop runs every CAT tick, so a spin here freezes tracking outright.
  // Inactivity window (the CAT read budget, as the original) + 500 ms ceiling.
  // See drainEcho: adaptive to slow bauds, bounded against a chatty stream.
  uint8_t buf[48]; size_t bn = 0;
  const uint32_t inact = (readBudgetMs ? readBudgetMs : 150);
  const uint32_t t0 = millis(); uint32_t lastByteMs = t0;
  while (millis() - lastByteMs < inact && millis() - t0 < 500) {
    unsigned guard = 64;                          // always fall through to the test
    while (_stream->available() > 0 && guard--) {
      const int rb = _stream->read();
      if (rb < 0) break;                         // -1: stream gone, not a byte
      uint8_t b = (uint8_t)rb;       // always CONSUME, even if full
      if (bn < sizeof(buf)) buf[bn++] = b;
      lastByteMs = millis();
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
      if (sub) _lastSubHz = hz; else _lastMainHz = hz;   // keep the cache current
#if CIV_DEBUG
      Serial.printf("[CI-V] %s freq read: %lu Hz\n", sub ? "SUB" : "MAIN", (unsigned long)hz);
#endif
      return true;
    }
  }
#if CIV_DEBUG
  Serial.printf("[CI-V] %s freq read: no valid reply%s\n", sub ? "SUB" : "MAIN",
                lastSet ? " (using last-set)" : "");
#endif
  return false;   // hzOut already holds the last-set fallback (or 0 if none yet)
}
