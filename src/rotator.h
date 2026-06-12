#pragma once
// ===========================================================================
//  rotator.h  -  az/el antenna rotator interface + GS-232 backend
// ===========================================================================
//
//  Mirrors the Rig abstraction: the app points the rotator through a narrow
//  interface and the backend handles the wire protocol. The only backend so
//  far is GS-232 (Yaesu's de-facto standard, also emulated by SPID, K3NG,
//  RadioArtisan, ERC, etc.), reached through an SC16IS750/752 I2C->UART bridge
//  because all three ESP32-S3 hardware UARTs are already in use.
//
//  GS-232 (per the Yaesu GS-232A/B manuals and Hamlib rotators/gs232a,gs232b):
//      "Waaa eee\r"  point to azimuth aaa (000-360/450) + elevation eee (000-180)
//      "C2\r"        read position -> "+0aaa+0eee" (GS-232A) or
//                                     "AZ=aaaEL=eee" (GS-232B); we parse both
//      "S\r"         all stop
//  Serial is 8N1, no handshake, commonly 9600 baud (the controller's setting).
//
//  Convention: degrees; azimuth 0-360 (0-450 with overlap), elevation 0-90
//  (0-180 in flip mode). "Sub"/"Main" do not apply here.
// ===========================================================================
#include <Arduino.h>
#include <WiFi.h>          // WiFiClient for the rotctld (network) backend
#include <WiFiUdp.h>        // WiFiUDP for the PstRotator (network) backend
#include "config.h"

class Rotator {
public:
  virtual ~Rotator() {}
  virtual void begin() = 0;
  virtual bool ready() const = 0;
  virtual bool point(float az, float el) = 0;     // command absolute position
  virtual bool readPos(float& az, float& el) = 0; // false if no/!valid reply
  virtual void stop() = 0;
  virtual const char* name() const = 0;
  // Optional hooks. service() is a fast closed-loop step for self-driven
  // backends (default no-op); rawPos() exposes uncalibrated position counts for
  // calibration (default unsupported).
  virtual void service() {}
  virtual bool rawPos(int32_t& azCnt, int32_t& elCnt) { (void)azCnt; (void)elCnt; return false; }
};

// GS-232A/B rotator via an SC16IS750/752 I2C->UART bridge on Wire1.
class Gs232Rotator : public Rotator {
public:
  Gs232Rotator(uint8_t i2cAddr, uint32_t baud) : _addr(i2cAddr), _baud(baud) {}
  void begin() override;
  bool ready() const override { return _ok; }
  bool point(float az, float el) override;
  bool readPos(float& az, float& el) override;
  void stop() override;
  const char* name() const override { return "GS-232"; }

private:
  uint8_t  _addr;
  uint32_t _baud;
  bool     _ok = false;

  // SC16IS750 I2C-UART bridge register access (Wire1).
  void    wreg(uint8_t reg, uint8_t val);
  uint8_t rreg(uint8_t reg);
  bool    bridgeInit();
  // Byte-level UART through the bridge.
  void    putc_(char c);
  void    puts_(const char* s);
  int     getc_();                 // -1 if no byte ready
  void    flushIn();
};

// rotctld (Hamlib "NET rotctl") TCP client. CardSat is the client; a rotctld
// server elsewhere on the LAN drives the physical rotator (default port 4533).
//   "P <az> <el>\n"  set_pos  -> "RPRT 0" on success
//   "p\n"            get_pos  -> "<az>\n<el>\n" (or "RPRT -n" on error)
//   "S\n"            stop
// Position control is fire-and-forget: send P, drain the ack. The socket is
// opened lazily and reconnected (throttled) so a missing server never hangs the
// tracking loop.
class RotctlRotator : public Rotator {
public:
  RotctlRotator(const char* host, uint16_t port) : _host(host), _port(port) {}
  void begin() override;
  bool ready() const override { return _ok; }
  bool point(float az, float el) override;
  bool readPos(float& az, float& el) override;
  void stop() override;
  const char* name() const override { return "rotctl"; }

private:
  String     _host;
  uint16_t   _port;
  bool       _ok = false;
  WiFiClient _client;
  uint32_t   _lastTry = 0;          // last (re)connect attempt, for throttling
  bool ensure();                    // (re)connect if needed; updates _ok
  void drainInput();                // consume pending replies without blocking
};

// PstRotator UDP control. CardSat sends "<PST>...</PST>" datagrams to a
// PstRotator instance on the LAN (default UDP port 12000): set-position uses
// <AZIMUTH>/<ELEVATION>, stop uses <STOP>, and AZ?/EL? queries are answered on
// port+1. Connectionless, so "ready" just means WiFi is up and a host is set.
class PstRotator : public Rotator {
public:
  PstRotator(const char* host, uint16_t port) : _host(host), _port(port) {}
  void begin() override;
  bool ready() const override { return _ok; }
  bool point(float az, float el) override;
  bool readPos(float& az, float& el) override;
  void stop() override;
  const char* name() const override { return "PstRotator"; }

private:
  String   _host;
  uint16_t _port;
  bool     _ok = false;
  bool     _bound = false;
  WiFiUDP  _udp;                    // bound to _port+1 to receive AZ?/EL? replies
  bool ensure();                    // verify WiFi + socket; updates _ok
  bool send(const char* msg);       // fire-and-forget datagram to host:_port
};

// Yaesu az/el rotator wired DIRECTLY (no GS-232 box): an ADS1115 reads the
// controller's two position-feedback voltages (AIN0 az, AIN1 el, via dividers)
// and a PCF8574 drives four opto-isolated/relay direction lines, both on Wire1
// (the same bus the GS-232 bridge uses). CardSat runs the closed loop itself:
// read position, drive toward the target within a deadband, stop; with a stall
// watchdog and soft limits. Calibration (ADC counts at each axis endpoint) is
// supplied from settings. *** UNTESTED hardware -- use entirely at your own
// risk; the author accepts no liability for damage. See ROTOR_INTERFACE.md.
class YaesuRotator : public Rotator {
public:
  YaesuRotator(int azFullDeg, int azCnt0, int azCntF,
               int elCnt0, int elCntF, int deadbandDeg)
    : _azFull(azFullDeg), _azC0(azCnt0), _azCF(azCntF),
      _elC0(elCnt0), _elCF(elCntF), _db(deadbandDeg) {}
  void begin() override;
  bool ready() const override { return _ok; }
  bool point(float az, float el) override;       // set target (clamped); driven in service()
  bool readPos(float& az, float& el) override;   // mapped degrees
  void stop() override;
  void service() override;                        // closed-loop step (call frequently)
  bool rawPos(int32_t& azCnt, int32_t& elCnt) override;  // raw counts for calibration
  const char* name() const override { return "Yaesu"; }

private:
  int      _azFull, _azC0, _azCF, _elC0, _elCF, _db;
  bool     _ok    = false;
  bool     _have  = false;        // a target is set (drive until stop())
  float    _tAz = 0, _tEl = 0;    // target degrees
  uint8_t  _out   = 0;            // shadow of the active direction bits
  uint32_t _lastSvc = 0;          // last closed-loop step (rate limit)
  uint32_t _stallMs = 0;          // last time the position made progress
  int32_t  _lastAzCnt = 0, _lastElCnt = 0;
  int32_t  adcRead(uint8_t ch);                  // ADS1115 single-shot; <0 on fail
  void     outWrite(uint8_t bits);               // PCF8574 (handles active-low)
  void     allStop() { outWrite(0); }
  static bool cnt2deg(int32_t c, int c0, int cF, float dmax, float& outDeg);
};

// Build the configured rotator backend. Caller owns the returned object.
//   type 0 = GS-232 (ROT_GS232) on the I2C->UART bridge
//   type 1 = rotctld (ROT_NET) to host:port over TCP
//   type 2 = PstRotator (ROT_PST) UDP control to host:port
// (ROT_YAESU is built by the app, which supplies calibration -- not here.)
Rotator* makeRotator(uint8_t type, uint32_t baud, const char* host, uint16_t port);
