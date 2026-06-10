// ===========================================================================
//  rotator.cpp  -  GS-232 rotator over an SC16IS750/752 I2C->UART bridge
// ===========================================================================
#include "rotator.h"
#include <Wire.h>

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

void Gs232Rotator::wreg(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(_addr);
  Wire1.write((uint8_t)(reg << 3));        // channel 0
  Wire1.write(val);
  Wire1.endTransmission();
}
uint8_t Gs232Rotator::rreg(uint8_t reg) {
  Wire1.beginTransmission(_addr);
  Wire1.write((uint8_t)(reg << 3));
  Wire1.endTransmission();
  Wire1.requestFrom((int)_addr, 1);
  return Wire1.available() ? (uint8_t)Wire1.read() : 0x00;
}

bool Gs232Rotator::bridgeInit() {
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
                _addr, ok ? "ready" : "NOT FOUND", (unsigned long)_baud,
                (unsigned long)div);
#endif
  return ok;
}

void Gs232Rotator::begin() {
  Wire1.begin(ROT_I2C_SDA, ROT_I2C_SCL, ROT_I2C_HZ);
  _ok = bridgeInit();
}

void Gs232Rotator::putc_(char c) {
  uint32_t t0 = millis();
  while (!(rreg(SC_LSR) & 0x20)) {         // wait for THR empty
    if (millis() - t0 > 50) break;         // don't hang if the bridge stalls
  }
  wreg(SC_THR, (uint8_t)c);
}
void Gs232Rotator::puts_(const char* s) {
  while (*s) putc_(*s++);
}
int Gs232Rotator::getc_() {
  if (rreg(SC_RXLVL) == 0) return -1;
  return (int)rreg(SC_RHR);
}
void Gs232Rotator::flushIn() {
  while (rreg(SC_RXLVL)) rreg(SC_RHR);
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
//  rotctld (Hamlib NET rotctl) TCP client backend
// ---------------------------------------------------------------------------
static constexpr uint32_t ROTCTLD_RETRY_MS = 5000;   // don't hammer a down server
static constexpr int32_t  ROTCTLD_CONN_MS  = 1200;   // bounded connect attempt (ms)

void RotctldRotator::begin() {
  _ok = false;
  _lastTry = 0;                 // allow an immediate attempt
  ensure();
}

bool RotctldRotator::ensure() {
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

void RotctldRotator::drainInput() {
  uint32_t t0 = millis();
  while (_client.available() && (millis() - t0) < 30) _client.read();
}

bool RotctldRotator::point(float az, float el) {
  if (!ensure()) return false;
  if (az < 0) az += 360.0f;
  if (el < 0) el = 0;
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

void RotctldRotator::stop() {
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
bool RotctldRotator::readPos(float& az, float& el) {
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

Rotator* makeRotator(uint8_t type, uint32_t baud, const char* host, uint16_t port) {
  if (type == 1) return new RotctldRotator(host, port);   // 1 = ROT_NET (settings.h)
  return new Gs232Rotator(ROT_I2C_ADDR, baud);
}
