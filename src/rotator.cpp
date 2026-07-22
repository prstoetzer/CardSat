// ===========================================================================
//  rotator.cpp  -  GS-232 rotator over an SC16IS750/752 I2C->UART bridge
// ===========================================================================
#include "rotator.h"
#include <Wire.h>
#include "usbserial.h"   // UsbSerial::rot* -- the USB rotator transport

// Set to 0 to silence the rotator trace on the serial monitor (115200 baud).
#define ROT_DEBUG 1

// --- SC16IS750/760 register map (16C550-compatible) ------------------------
//  The I2C sub-address byte is (reg << 3) | (channel << 1); channel 0 here.
static constexpr uint8_t SC_RHR   = 0x00;   // read  (FIFO out)
static constexpr uint8_t SC_THR   = 0x00;   // write (FIFO in)
static constexpr uint8_t SC_FCR   = 0x02;   // FIFO control (write)
static constexpr uint8_t SC_LCR   = 0x03;   // line control
static constexpr uint8_t SC_LSR   = 0x05;   // line status
static constexpr uint8_t SC_TXLVL = 0x08;   // TX FIFO free spaces
static constexpr uint8_t SC_RXLVL = 0x09;   // RX FIFO bytes available
static constexpr uint8_t SC_SPR   = 0x07;   // scratchpad (presence test)
static constexpr uint8_t SC_IOCTL = 0x0E;   // I/O control (bit3 = soft reset)
static constexpr uint8_t SC_DLL   = 0x00;   // divisor low  (when LCR[7]=1)
static constexpr uint8_t SC_DLH   = 0x01;   // divisor high (when LCR[7]=1)

// ---------------------------------------------------------------------------
//  BridgeStream -- SC16IS750/752 I2C->UART bridge as an Arduino Stream
// ---------------------------------------------------------------------------
//  One copy of the register plumbing that Gs232Rotator, EasycommRotator and
//  SpidRotator each used to carry privately. Behaviour is byte-for-byte what
//  they did: same soft reset, same divisor maths, same scratchpad presence test,
//  same 50 ms THR-empty guard so a stalled bridge can never hang the loop.
void BridgeStream::wreg(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(_addr);
  Wire1.write((uint8_t)(reg << 3));        // channel 0
  Wire1.write(val);
  Wire1.endTransmission();
}

uint8_t BridgeStream::rreg(uint8_t reg) {
  Wire1.beginTransmission(_addr);
  Wire1.write((uint8_t)(reg << 3));
  Wire1.endTransmission();
  Wire1.requestFrom((int)_addr, 1);
  return Wire1.available() ? (uint8_t)Wire1.read() : 0x00;
}

bool BridgeStream::bridgeInit() {
  wreg(SC_IOCTL, 0x08);                    // software reset (self-clearing)
  delay(5);
  uint32_t div = ROT_XTAL_HZ / (16UL * _baud);
  wreg(SC_LCR, 0x80);                      // enable divisor latch
  wreg(SC_DLL, (uint8_t)(div & 0xFF));
  wreg(SC_DLH, (uint8_t)((div >> 8) & 0xFF));
  wreg(SC_LCR, 0x03);                      // 8 data bits, no parity, 1 stop; latch off
  wreg(SC_FCR, 0x07);                      // enable FIFO + reset RX/TX FIFO
  // Presence test: the scratchpad register should hold whatever we write.
  wreg(SC_SPR, 0x5A);
  bool ok = (rreg(SC_SPR) == 0x5A);
#if ROT_DEBUG
  Serial.printf("[ROT] SC16IS750 @0x%02X %s (baud %lu, div %lu)\n",
                _addr, ok ? "found" : "NOT FOUND",
                (unsigned long)_baud, (unsigned long)div);
#endif
  return ok;
}

bool BridgeStream::begin() {
  Wire1.begin(ROT_I2C_SDA, ROT_I2C_SCL, ROT_I2C_HZ);
  _ok = bridgeInit();
  return _ok;
}

size_t BridgeStream::write(uint8_t c) {
  if (!_ok) return 0;
  uint32_t t0 = millis();
  while (!(rreg(SC_LSR) & 0x20)) {         // wait for THR empty
    if (millis() - t0 > 50) return 0;      // don't hang if the bridge stalls
  }
  wreg(SC_THR, c);
  return 1;
}

int BridgeStream::available() {
  if (_peek >= 0) return 1;
  return _ok ? (int)rreg(SC_RXLVL) : 0;
}

int BridgeStream::read() {
  if (_peek >= 0) { int c = _peek; _peek = -1; return c; }
  if (!_ok || rreg(SC_RXLVL) == 0) return -1;
  return (int)rreg(SC_RHR);
}

int BridgeStream::peek() {
  if (_peek < 0) _peek = read();
  return _peek;
}

// ---------------------------------------------------------------------------
//  UsbRotStream -- the rotator's USB<->serial adapter as a Stream
// ---------------------------------------------------------------------------
//  All the hard parts (resident host, device binding, slot rules) live in
//  usbserial.cpp; this is a forwarding shim so the rotator backends see a Stream
//  like any other.
UsbRotStream::~UsbRotStream() {
#if CARDSAT_HAS_USBCAT
  UsbSerial::rotEnd();     // release the CDC port with the Stream that owns it
#endif
}

bool UsbRotStream::begin() {
#if CARDSAT_HAS_USBCAT
  return UsbSerial::rotBegin();
#else
  return false;
#endif
}
bool UsbRotStream::ok() const {
#if CARDSAT_HAS_USBCAT
  return UsbSerial::rotActive();
#else
  return false;
#endif
}
int UsbRotStream::available() {
#if CARDSAT_HAS_USBCAT
  if (_peek >= 0) return 1;
  Stream* s = UsbSerial::rotStream();
  return s ? s->available() : 0;
#else
  return 0;
#endif
}
int UsbRotStream::read() {
#if CARDSAT_HAS_USBCAT
  if (_peek >= 0) { int c = _peek; _peek = -1; return c; }
  Stream* s = UsbSerial::rotStream();
  return s ? s->read() : -1;
#else
  return -1;
#endif
}
int UsbRotStream::peek() {
  if (_peek < 0) _peek = read();
  return _peek;
}
void UsbRotStream::flush() {
#if CARDSAT_HAS_USBCAT
  Stream* s = UsbSerial::rotStream();
  if (s) s->flush();
#endif
}
size_t UsbRotStream::write(uint8_t c) {
#if CARDSAT_HAS_USBCAT
  Stream* s = UsbSerial::rotStream();
  return s ? s->write(c) : 0;
#else
  (void)c; return 0;
#endif
}

void Gs232Rotator::begin() {
  // The transport is already open (makeRotator built and began it); GS-232 needs
  // no link-level handshake, so "ready" is simply "we have a working wire".
  _ok = (_s != nullptr);
  if (_ok) flushIn();
}

bool Gs232Rotator::point(float az, float el) {
  if (!_ok) return false;
  if (az < 0)   az += 360.0f;
  if (az > 450) az = 450;
  if (el < 0)   el = 0;
  if (el > 180) el = 180;
  char cmd[16];
  snprintf(cmd, sizeof(cmd), "W%03d %03d\r", (int)lroundf(az), (int)lroundf(el));
#if ROT_DEBUG
  Serial.printf("[ROT TX] W%03d %03d\n", (int)lroundf(az), (int)lroundf(el));
#endif
  puts_(cmd);
  return true;
}

void Gs232Rotator::stop() {
  if (!_ok) return;
#if ROT_DEBUG
  Serial.println("[ROT TX] S (stop)");
#endif
  puts_("S\r");
}

// Read position. Accepts both GS-232A ("+0aaa+0eee") and GS-232B
// ("AZ=aaaEL=eee", with or without a separating space) reply formats.
bool Gs232Rotator::readPos(float& az, float& el) {
  if (!_ok) return false;
  flushIn();
  puts_("C2\r");
  String r;
  uint32_t t0 = millis();
  while (millis() - t0 < 500) {
    int c = getc_();
    if (c < 0) { delay(2); continue; }
    if (c == '\r' || c == '\n') { if (r.length()) break; else continue; }
    r += (char)c; t0 = millis();
    if (r.length() > 24) break;
  }
#if ROT_DEBUG
  Serial.printf("[ROT RX] %s\n", r.length() ? r.c_str() : "(no reply)");
#endif
  int ia = r.indexOf("AZ=");
  if (ia >= 0) {                           // GS-232B form
    int ie = r.indexOf("EL=");
    if (ie < 0) return false;
    az = (float)atoi(r.c_str() + ia + 3);
    el = (float)atoi(r.c_str() + ie + 3);
    return true;
  }
  int p1 = r.indexOf('+');                 // GS-232A form "+0aaa+0eee"
  if (p1 >= 0) {
    int p2 = r.indexOf('+', p1 + 1);
    if (p2 < 0) return false;
    az = (float)atoi(r.c_str() + p1 + 1);
    el = (float)atoi(r.c_str() + p2 + 1);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
//  Easycomm I/II/III backend (ASCII) over the SC16IS750/752 I2C->UART bridge
// ---------------------------------------------------------------------------
// Bridge plumbing mirrors Gs232Rotator (same register map / sequence); kept as
// its own copy rather than refactored into a shared base so the working GS-232
// path is untouched.
void EasycommRotator::begin() {
  _ok = (_s != nullptr);
  if (_ok) flushIn();
}

bool EasycommRotator::point(float az, float el) {
  if (!_ok) return false;
  if (az < 0)   az += 360.0f;
  if (az > 360) az -= 360.0f;
  if (el < 0)   el = 0;
  if (el > 180) el = 180;
  char cmd[32];
  if (_ver == 1)   // Easycomm I: integer degrees
    snprintf(cmd, sizeof(cmd), "AZ%d EL%d\r", (int)lroundf(az), (int)lroundf(el));
  else             // Easycomm II/III: one-decimal degrees
    snprintf(cmd, sizeof(cmd), "AZ%.1f EL%.1f\r", az, el);
#if ROT_DEBUG
  Serial.printf("[ROT TX] %s", cmd);
#endif
  puts_(cmd);
  return true;
}

void EasycommRotator::stop() {
  if (!_ok) return;
#if ROT_DEBUG
  Serial.println("[ROT TX] SA SE (stop)");
#endif
  puts_("SA SE\r");
}

// Query "AZ EL\r" -> reply contains "AZ<val>" and "EL<val>" tokens (decimal for
// II/III, integer for I; atof handles both). Some III controllers append more
// fields (VE.., etc.) which we ignore.
bool EasycommRotator::readPos(float& az, float& el) {
  if (!_ok) return false;
  flushIn();
  puts_("AZ EL\r");
  String r;
  uint32_t t0 = millis();
  while (millis() - t0 < 500) {
    int c = getc_();
    if (c < 0) { delay(2); continue; }
    if (c == '\r' || c == '\n') { if (r.length()) break; else continue; }
    r += (char)c; t0 = millis();
    if (r.length() > 48) break;
  }
#if ROT_DEBUG
  Serial.printf("[ROT RX] %s\n", r.length() ? r.c_str() : "(no reply)");
#endif
  int ia = r.indexOf("AZ");
  int ie = r.indexOf("EL");
  if (ia < 0 || ie < 0) return false;
  az = (float)atof(r.c_str() + ia + 2);
  el = (float)atof(r.c_str() + ie + 2);
  return true;
}

// ---------------------------------------------------------------------------
//  SPID Rot2Prog (MD-01/02) backend (binary) over the I2C->UART bridge
// ---------------------------------------------------------------------------
void SpidRotator::begin() {
  _ok = (_s != nullptr);
  if (_ok) flushIn();
}

// Blocking-with-timeout single byte. SPID replies are fixed 12-byte frames, so
// the caller reads a known count; this bounds each byte so a silent controller
// can never spin the loop.
int SpidRotator::getb_(uint32_t toMs) {
  if (!_s) return -1;
  uint32_t t0 = millis();
  while (millis() - t0 < toMs) {
    if (_s->available()) return _s->read();
    delay(1);
  }
  return -1;
}

// Rot2Prog SET frame (13 bytes): 0x57 H1 H2 H3 H4 PH V1 V2 V3 V4 PV CMD 0x20.
// Each angle is encoded as (deg + 360) * RES, then the 4 most-significant decimal
// digits are sent one-per-byte as raw values (0..9); PH/PV carry the resolution.
bool SpidRotator::point(float az, float el) {
  if (!_ok) return false;
  if (az < 0)   az += 360.0f;
  if (az > 360) az -= 360.0f;
  if (el < 0)   el = 0;
  if (el > 180) el = 180;
  int ha = (int)lroundf((az + 360.0f) * RES);   // azimuth encoded value
  int ve = (int)lroundf((el + 360.0f) * RES);   // elevation encoded value
  uint8_t f[13];
  f[0] = 0x57;
  f[1] = (ha / 1000) % 10; f[2] = (ha / 100) % 10; f[3] = (ha / 10) % 10; f[4] = ha % 10;
  f[5] = (uint8_t)RES;
  f[6] = (ve / 1000) % 10; f[7] = (ve / 100) % 10; f[8] = (ve / 10) % 10; f[9] = ve % 10;
  f[10] = (uint8_t)RES;
  f[11] = 0x2F;            // SET
  f[12] = 0x20;
#if ROT_DEBUG
  Serial.printf("[ROT TX] SPID set az=%d el=%d (ha=%d ve=%d)\n",
                (int)lroundf(az), (int)lroundf(el), ha, ve);
#endif
  flushIn();
  for (int i = 0; i < 13; i++) putb_(f[i]);
  return true;
}

void SpidRotator::stop() {
  if (!_ok) return;
  uint8_t f[13] = {0x57,0,0,0,0,0,0,0,0,0,0,0x0F,0x20};   // CMD 0x0F = stop
#if ROT_DEBUG
  Serial.println("[ROT TX] SPID stop");
#endif
  flushIn();
  for (int i = 0; i < 13; i++) putb_(f[i]);
}

// STATUS query: send a status frame (CMD 0x1F) and read the 12-byte reply
//   0x57 H1 H2 H3 H4 PH V1 V2 V3 V4 PV 0x20
// az = (H1*1000 + H2*100 + H3*10 + H4)/RES - 360 ; el decoded the same way.
bool SpidRotator::readPos(float& az, float& el) {
  if (!_ok) return false;
  flushIn();
  uint8_t q[13] = {0x57,0,0,0,0,0,0,0,0,0,0,0x1F,0x20};   // CMD 0x1F = status
  for (int i = 0; i < 13; i++) putb_(q[i]);
  uint8_t r[12];
  int got = 0;
  while (got < 12) {
    int b = getb_(500);
    if (b < 0) break;
    if (got == 0 && b != 0x57) continue;   // resync to frame start
    r[got++] = (uint8_t)b;
  }
  if (got < 12 || r[0] != 0x57 || r[11] != 0x20) {
#if ROT_DEBUG
    Serial.printf("[ROT RX] SPID status incomplete (%d bytes)\n", got);
#endif
    return false;
  }
  // Decode: the four digits form (deg + 360) * RES, MSD first.
  float a = (r[1]*1000 + r[2]*100 + r[3]*10 + r[4]) / (float)RES - 360.0f;
  float e = (r[6]*1000 + r[7]*100 + r[8]*10 + r[9]) / (float)RES - 360.0f;
  az = a; el = e;
#if ROT_DEBUG
  Serial.printf("[ROT RX] SPID az=%.1f el=%.1f\n", az, el);
#endif
  return true;
}

// ---------------------------------------------------------------------------
//  rotctld (Hamlib NET rotctl) TCP client backend
// ---------------------------------------------------------------------------
static constexpr uint32_t ROTCTLD_RETRY_MS = 5000;   // don't hammer a down server
static constexpr int32_t  ROTCTLD_CONN_MS  = 1200;   // bounded connect attempt (ms)

void RotctlRotator::begin() {
  _ok = false;
  _lastTry = 0;                 // allow an immediate attempt
  ensure();
}

bool RotctlRotator::ensure() {
  if (_client.connected()) { _ok = true; return true; }
  uint32_t now = millis();
  if (_lastTry && (now - _lastTry) < ROTCTLD_RETRY_MS) { _ok = false; return false; }
  _lastTry = now;
  _client.stop();
  if (WiFi.status() != WL_CONNECTED || _host.length() == 0) { _ok = false; return false; }
  bool c = _client.connect(_host.c_str(), _port, ROTCTLD_CONN_MS);
#if ROT_DEBUG
  Serial.printf("[ROT] rotctld %s:%u %s\n", _host.c_str(), (unsigned)_port,
                c ? "connected" : "connect failed");
#endif
  _ok = c;
  return c;
}

void RotctlRotator::drainInput() {
  uint32_t t0 = millis();
  while (_client.available() && (millis() - t0) < 30) _client.read();
}

bool RotctlRotator::point(float az, float el) {
  if (!ensure()) return false;
  if (el < 0) el = 0;          // az is passed through as-is (caller picks 0-360 vs +/-180)
  drainInput();                          // clear any stale ack from a prior command
  char cmd[24];
  int n = snprintf(cmd, sizeof(cmd), "P %.1f %.1f\n", az, el);
#if ROT_DEBUG
  Serial.printf("[ROT TX] P %.1f %.1f\n", az, el);
#endif
  if (_client.write((const uint8_t*)cmd, (size_t)n) != (size_t)n) {
    _ok = false; _client.stop(); return false;   // socket gone; reconnect next time
  }
  return true;
}

void RotctlRotator::stop() {
  if (!ensure()) return;
  drainInput();
#if ROT_DEBUG
  Serial.println("[ROT TX] S (stop)");
#endif
  _client.print("S\n");
}

// Default-protocol get_pos: azimuth then elevation, each on its own line.
// (An error reply is "RPRT -n".) Not currently called by the UI, but provided
// for interface completeness and future read-back display.
bool RotctlRotator::readPos(float& az, float& el) {
  if (!ensure()) return false;
  drainInput();
  _client.print("p\n");
  String ln[2]; int got = 0; String cur;
  uint32_t t0 = millis();
  while (got < 2 && (millis() - t0) < 400) {
    int ch = _client.read();
    if (ch < 0) { delay(2); continue; }
    if (ch == '\n' || ch == '\r') {
      if (cur.length()) { ln[got++] = cur; cur = ""; t0 = millis(); }
      continue;
    }
    cur += (char)ch;
  }
#if ROT_DEBUG
  Serial.printf("[ROT RX] %s | %s\n", got > 0 ? ln[0].c_str() : "(none)",
                got > 1 ? ln[1].c_str() : "");
#endif
  if (got < 2 || ln[0].startsWith("RPRT")) return false;
  az = ln[0].toFloat();
  el = ln[1].toFloat();
  return true;
}

// ---------------------------------------------------------------------------
//  PstRotator UDP control backend
// ---------------------------------------------------------------------------
void PstRotator::begin() {
  _bound = false; _ok = false;
  ensure();
}

bool PstRotator::ensure() {
  if (WiFi.status() != WL_CONNECTED || _host.length() == 0) { _ok = false; return false; }
  if (!_bound) _bound = _udp.begin(_port + 1);   // RX replies on port+1; opens the socket
  _ok = _bound;
  return _ok;
}

bool PstRotator::send(const char* msg) {
  if (!ensure()) return false;
  if (!_udp.beginPacket(_host.c_str(), _port)) { _ok = false; return false; }
  _udp.write((const uint8_t*)msg, strlen(msg));
  bool ok = _udp.endPacket();
#if ROT_DEBUG
  Serial.printf("[ROT TX] %s -> %s:%u %s\n", msg, _host.c_str(), (unsigned)_port,
                ok ? "" : "(fail)");
#endif
  if (!ok) _ok = false;
  return ok;
}

bool PstRotator::point(float az, float el) {
  if (az < 0) az += 360.0f;          // PstRotator owns its own range; keep az >= 0
  if (el < 0) el = 0;
  char msg[80];
  snprintf(msg, sizeof(msg),
           "<PST><AZIMUTH>%.1f</AZIMUTH><ELEVATION>%.1f</ELEVATION></PST>", az, el);
  return send(msg);
}

void PstRotator::stop() {
  send("<PST><STOP>1</STOP></PST>");
}

// PstRotator answers AZ?/EL? on UDP port+1 as "AZ:xxx.x" / "EL:yy.y". Provided
// for interface completeness; the tracking loop does not call it.
bool PstRotator::readPos(float& az, float& el) {
  if (!ensure()) return false;
  while (_udp.parsePacket() > 0) { uint8_t d[64]; _udp.read(d, sizeof(d)); }  // drain stale
  send("<PST>AZ?</PST>");
  send("<PST>EL?</PST>");
  bool gotAz = false, gotEl = false;
  char buf[80];
  uint32_t t0 = millis();
  while ((!gotAz || !gotEl) && millis() - t0 < 400) {
    int n = _udp.parsePacket();
    if (n <= 0) { delay(2); continue; }
    int r = _udp.read((uint8_t*)buf, sizeof(buf) - 1);
    if (r < 0) r = 0;
    buf[r] = 0;
    char* p;
    if (!gotAz && (p = strstr(buf, "AZ:"))) { az = atof(p + 3); gotAz = true; }
    if (!gotEl && (p = strstr(buf, "EL:"))) { el = atof(p + 3); gotEl = true; }
  }
  return gotAz && gotEl;
}

// ===========================================================================
//  YaesuRotator -- direct (no GS-232 box) az/el control via an I2C ADS1115 ADC
//  (position) + PCF8574 outputs (four direction lines) on Wire1. CardSat closes
//  the loop itself. *** UNTESTED hardware; use at your own risk. ***
//  See ROTOR_INTERFACE.md.
// ===========================================================================
void YaesuRotator::begin() {
  Wire1.begin(ROT_I2C_SDA, ROT_I2C_SCL, ROT_I2C_HZ);
  allStop();                                       // motors off before anything else
  _have = false;
  Wire1.beginTransmission(YAESU_OUT_ADDR); bool oOut = (Wire1.endTransmission() == 0);
  Wire1.beginTransmission(YAESU_ADC_ADDR); bool oAdc = (Wire1.endTransmission() == 0);
  _ok = oOut && oAdc;
  _stallMs = millis();
  Serial.printf("[rot] Yaesu direct: ADC@0x%02X %s, OUT@0x%02X %s\n",
                YAESU_ADC_ADDR, oAdc ? "ok" : "MISSING",
                YAESU_OUT_ADDR, oOut ? "ok" : "MISSING");
}

int32_t YaesuRotator::adcRead(uint8_t ch) {
  // ADS1115 single-shot, single-ended AINch vs GND. Config reg (0x01):
  // OS=1 | MUX=100+ch | PGA=001 (+/-4.096 V) | MODE=1 | DR | COMP_QUE=11 (off).
  uint16_t cfg = 0x8000
               | (uint16_t)((4 + (ch & 3)) << 12)
               | (uint16_t)(0x1 << 9)              // PGA +/-4.096 V
               | (uint16_t)(0x1 << 8)              // single-shot
               | (uint16_t)(YAESU_ADC_DR << 5)
               | 0x0003;                           // comparator disabled
  Wire1.beginTransmission(YAESU_ADC_ADDR);
  Wire1.write(0x01); Wire1.write((uint8_t)(cfg >> 8)); Wire1.write((uint8_t)(cfg & 0xFF));
  if (Wire1.endTransmission() != 0) return -1;
  delay(YAESU_ADC_MS);
  Wire1.beginTransmission(YAESU_ADC_ADDR);
  Wire1.write(0x00);                               // conversion register
  if (Wire1.endTransmission() != 0) return -1;
  if (Wire1.requestFrom((int)YAESU_ADC_ADDR, 2) != 2) return -1;
  int16_t raw = (int16_t)(((uint16_t)Wire1.read() << 8) | (uint16_t)Wire1.read());
  return raw < 0 ? 0 : raw;                        // single-ended: clamp tiny negatives
}

void YaesuRotator::outWrite(uint8_t bits) {
  // 'bits' = active direction lines (1 = drive). Relay/opto modules are
  // active-low, so a driven line is a 0 on the port; idle lines float high.
  uint8_t port = YAESU_OUT_ACTIVE_LOW ? (uint8_t)~bits : bits;
  Wire1.beginTransmission(YAESU_OUT_ADDR);
  Wire1.write(port);
  // M25: honour the I2C result. If the expander didn't ACK, the motor/stop command
  // did NOT reach the hardware -- mark the backend not-ready so the controller stops
  // believing a command (including allStop) succeeded. Still record _out so a later
  // successful write can converge, but don't paper over a dead bus.
  if (Wire1.endTransmission() != 0) {
    _ok = false;
  }
  _out = bits;
}

bool YaesuRotator::cnt2deg(int32_t c, int c0, int cF, float dmax, float& outDeg) {
  if (cF == c0) return false;                      // not calibrated yet
  outDeg = (float)(c - c0) * dmax / (float)(cF - c0);
  return true;
}

bool YaesuRotator::point(float az, float el) {
  // M26: don't accept a target while the backend isn't ready or isn't calibrated --
  // returning true here would let tracking believe the antenna is being commanded when
  // the I2C expander/ADC is absent or the endpoints were never learned.
  if (!_ok) return false;
  if (az < 0) az = 0; if (az > (float)_azFull) az = (float)_azFull;
  if (el < 0) el = 0; if (el > 180.0f) el = 180.0f;
  _tAz = az; _tEl = el; _have = true; _stallMs = millis();
  return true;
}

bool YaesuRotator::readPos(float& az, float& el) {
  if (!_ok) return false;
  int32_t a = adcRead(0), e = adcRead(1);
  if (a < 0 || e < 0) return false;
  return cnt2deg(a, _azC0, _azCF, (float)_azFull, az)
      && cnt2deg(e, _elC0, _elCF, 180.0f, el);
}

bool YaesuRotator::rawPos(int32_t& azCnt, int32_t& elCnt) {
  if (!_ok) return false;
  azCnt = adcRead(0); elCnt = adcRead(1);
  return azCnt >= 0 && elCnt >= 0;
}

void YaesuRotator::stop() { _have = false; allStop(); }

void YaesuRotator::service() {
  if (!_ok) return;
  if (!_have) { if (_out) allStop(); return; }     // idle: ensure motors off
  uint32_t now = millis();
  if (now - _lastSvc < YAESU_SVC_MS) return;        // rate-limit the control loop
  _lastSvc = now;

  int32_t azc = adcRead(0), elc = adcRead(1);
  if (azc < 0 || elc < 0) { allStop(); return; }    // ADC fault -> fail safe
  float az, el;
  if (!cnt2deg(azc, _azC0, _azCF, (float)_azFull, az) ||
      !cnt2deg(elc, _elC0, _elCF, 180.0f, el)) { allStop(); return; }  // uncalibrated

  uint8_t bits = 0;
  float daz = az - _tAz; if (daz < 0) daz = -daz;
  float del = el - _tEl; if (del < 0) del = -del;
  if (daz > (float)_db) bits |= (uint8_t)(1 << (_tAz > az ? YAESU_BIT_CW : YAESU_BIT_CCW));
  if (del > (float)_db) bits |= (uint8_t)(1 << (_tEl > el ? YAESU_BIT_UP : YAESU_BIT_DOWN));

  // Soft limits: never drive past the calibrated travel.
  if (az <= 0.0f)           bits &= (uint8_t)~(1 << YAESU_BIT_CCW);
  if (az >= (float)_azFull) bits &= (uint8_t)~(1 << YAESU_BIT_CW);
  if (el <= 0.0f)           bits &= (uint8_t)~(1 << YAESU_BIT_DOWN);
  if (el >= 180.0f)         bits &= (uint8_t)~(1 << YAESU_BIT_UP);

  // Stall watchdog: commanding motion but counts not changing -> cut output.
  if (bits) {
    int32_t dA = azc - _lastAzCnt; if (dA < 0) dA = -dA;
    int32_t dE = elc - _lastElCnt; if (dE < 0) dE = -dE;
    if (dA > YAESU_STALL_CNT || dE > YAESU_STALL_CNT) _stallMs = now;
    else if (now - _stallMs > YAESU_STALL_MS) { allStop(); return; }
  } else {
    _stallMs = now;
  }
  _lastAzCnt = azc; _lastElCnt = elc;
  outWrite(bits);
}

// ---------------------------------------------------------------------------
//  Rotator + transport construction
// ---------------------------------------------------------------------------
// The serial backends hold a Stream* they do not own. Keeping the pair together
// is the whole job here: build the transport, open it, hand it to the protocol,
// and remember it so freeRotator() can delete both. A bare `delete rot` would
// leak the Stream -- which is why the app calls freeRotator() instead.
//
// One transport is live at a time (a single rotator), so a file-scope pointer is
// enough and avoids giving every backend an owning pointer it would have to
// delete correctly.
static Stream* s_rotXport = nullptr;
// The transport we ALLOCATED, if any. Separate from s_rotXport because the Grove
// UART is a borrowed static HardwareSerial we must never delete -- and because
// deleting must go through RotWire*, whose destructor is virtual. Deleting
// through Stream* silently skips ~UsbRotStream() (Arduino's Stream has no virtual
// dtor), which is exactly how 0.9.58 shipped with the rotator's USB port never
// being released. GCC warns; the build uses -w.
static RotWire* s_rotOwned = nullptr;

// Grove UART1 on G1/G2. SHARED with wired CI-V and the Grove GPS -- the app must
// have cleared the conflict before we get here (App::rotTransportConflict).
// Constructed once and reused: HardwareSerial owns a UART peripheral, and
// churning it on every rebuild is how you strand pin mux state (see the same
// static-instance reasoning in civ.cpp begin()).
static HardwareSerial* groveSerial() {
  static HardwareSerial* hs = nullptr;
  if (!hs) hs = new HardwareSerial(ROT_GROVE_UART_NUM);
  return hs;
}

// Build the serial transport for `transport`, open it, and return it (or nullptr
// if it could not be opened -- caller reports "rotator not ready" as usual).
// Returns the Stream the protocol talks through, and sets s_rotOwned to the
// object we allocated (nullptr for Grove, which we borrow).
static Stream* makeRotTransport(uint8_t transport, uint32_t baud) {
  s_rotOwned = nullptr;
  switch (transport) {
    case ROT_XPORT_GROVE: {
      // Borrowed: a static HardwareSerial reused across rebuilds. Not ours to free.
      HardwareSerial* hs = groveSerial();
      hs->begin(baud, SERIAL_8N1, ROT_GROVE_RX_PIN, ROT_GROVE_TX_PIN);
      return hs;
    }
    case ROT_XPORT_USB: {
      UsbRotStream* u = new UsbRotStream();
      if (!u->begin()) { delete u; return nullptr; }
      s_rotOwned = u;
      return u;
    }
    case ROT_XPORT_BRIDGE:
    default: {
      BridgeStream* b = new BridgeStream(ROT_I2C_ADDR, baud);
      if (!b->begin()) { delete b; return nullptr; }  // bridge absent: no rotator
      s_rotOwned = b;
      return b;
    }
  }
}

Rotator* makeRotator(uint8_t type, uint8_t transport, uint32_t baud,
                     const char* host, uint16_t port) {
  // Network backends carry their own socket and never touch a serial Stream, so
  // they are built before any transport exists -- rotTransport is meaningless for
  // them and the Settings UI hides it.
  if (type == ROT_NET) return new RotctlRotator(host, port);
  if (type == ROT_PST) return new PstRotator(host, port);

  // Serial backends: transport first, then the protocol that speaks over it.
  // Never leak a prior one. Through RotWire*, so the virtual destructor runs.
  if (s_rotOwned) { delete s_rotOwned; s_rotOwned = nullptr; }
  s_rotXport = nullptr;
  Stream* xp = makeRotTransport(transport, baud);
  if (!xp) return nullptr;               // no wire -> no rotator; app shows not-ready
  s_rotXport = xp;

  switch (type) {
    case ROT_EASYCOMM1: return new EasycommRotator(xp, 1);
    case ROT_EASYCOMM2: return new EasycommRotator(xp, 2);
    case ROT_EASYCOMM3: return new EasycommRotator(xp, 3);
    case ROT_SPID:      return new SpidRotator(xp);
    default:            return new Gs232Rotator(xp);   // ROT_GS232
  }
}

void freeRotator(Rotator* r) {
  delete r;                    // protocol first: it points at the transport
  // Delete through RotWire*, NOT Stream*. Stream has no virtual destructor,
  // so `delete (Stream*)p` skips ~UsbRotStream() and the USB CDC port is never
  // released -- which is how 0.9.58 shipped still binding the adapter after the
  // rotator was turned off. s_rotOwned is null for Grove (borrowed, never freed).
  if (s_rotOwned) { delete s_rotOwned; s_rotOwned = nullptr; }
  s_rotXport = nullptr;
}
