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

// ===========================================================================
//  Rotator serial transports
// ===========================================================================
// Every serial rotator protocol below talks through a plain Arduino Stream, so
// the wire it runs on is a runtime choice (settings: rotTransport), not a
// compile-time class. Three transports exist:
//
//   ROT_XPORT_BRIDGE : SC16IS750/752 I2C->UART bridge on Wire1 (the original
//                      path; all three ESP32-S3 hardware UARTs are spoken for).
//   ROT_XPORT_GROVE  : Cardputer Grove HY2.0-4P on G1/G2 via UART1. SHARED with
//                      wired CI-V CAT and the Grove GPS -- the app enforces the
//                      exclusion (see App::rotTransportConflict).
//   ROT_XPORT_USB    : a USB<->serial adapter on the resident EspUsbHost, the
//                      same host USB CAT uses.
//
// The backends never learn which is which; they see a Stream. That is the same
// trick rig.h plays with setExternalStream(), for the same reason: it keeps every
// protocol working over every wire with no per-transport duplication.

// Base for rotator transports that OWN something and must clean up.
//
// Arduino's Stream has NO virtual destructor, and freeRotator() deletes its
// transport through a Stream* -- so a derived destructor would never run. That
// silently defeated UsbRotStream's rotEnd() in 0.9.58: the CDC port stayed bound
// after the rotator was disabled, and the radio's picker kept skipping that
// adapter until reboot. GCC warns about exactly this
// (-Wdelete-non-virtual-dtor); the build passes -w, so the warning never
// surfaced. Deleting through THIS type is well-defined.
class RotWire : public Stream {
public:
  virtual ~RotWire() {}
};

// SC16IS750/752 I2C->UART bridge, presented as a Stream.
// The register-level code is lifted verbatim from what Gs232Rotator/
// EasycommRotator/SpidRotator each used to carry privately -- one copy now.
class BridgeStream : public RotWire {
public:
  BridgeStream(uint8_t i2cAddr, uint32_t baud) : _addr(i2cAddr), _baud(baud) {}
  bool begin();                       // Wire1 + bridge init + presence test
  bool ok() const { return _ok; }
  // Stream/Print
  int  available() override;
  int  read() override;
  int  peek() override;
  void flush() override {}            // TX is pushed byte-by-byte; nothing buffered
  size_t write(uint8_t c) override;
  using Print::write;
private:
  uint8_t  _addr;
  uint32_t _baud;
  bool     _ok    = false;
  int      _peek  = -1;               // one-byte pushback for peek()
  void    wreg(uint8_t reg, uint8_t val);
  uint8_t rreg(uint8_t reg);
  bool    bridgeInit();
};

// A USB<->serial adapter on the resident EspUsbHost, presented as a Stream.
// Thin: UsbSerial owns the host and the CDC object; this just forwards. The
// rotator's CDC is a SECOND port on the same host the radio may be using --
// see usbserial.h (rotator port) for the address-binding rules that keep the
// two from stealing each other's adapter.
class UsbRotStream : public RotWire {
public:
  // RAII: this object owns the rotator's CDC port for its lifetime. The
  // destructor is not optional -- freeRotator() deletes the transport whenever
  // the rotator is rebuilt, disabled or moved to another wire, and without a
  // dtor UsbSerial kept the port bound to a Stream that no longer existed. The
  // adapter then stayed reserved (the radio's picker skips the rotator's) until
  // reboot, which is the "turning the rotator off permanently binds the adapter"
  // bug. Whoever owns the Stream owns the port; that is the whole invariant.
  ~UsbRotStream() override;
  bool begin();                       // bind the rotator's CDC port
  bool ok() const;
  int  available() override;
  int  read() override;
  int  peek() override;
  void flush() override;
  size_t write(uint8_t c) override;
  using Print::write;
private:
  int _peek = -1;
};

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

// GS-232A/B rotator over any Stream (bridge, Grove UART or USB adapter).
// The caller owns the Stream and must keep it alive for the rotator's lifetime;
// makeRotator() builds the pair and the app deletes them together.
class Gs232Rotator : public Rotator {
public:
  explicit Gs232Rotator(Stream* s) : _s(s) {}
  void begin() override;
  bool ready() const override { return _ok; }
  bool point(float az, float el) override;
  bool readPos(float& az, float& el) override;
  void stop() override;
  const char* name() const override { return "GS-232"; }

private:
  Stream* _s;
  bool    _ok = false;
  void    puts_(const char* s) { if (_s) _s->print(s); }
  int     getc_()              { return (_s && _s->available()) ? _s->read() : -1; }
  void    flushIn()            { if (_s) while (_s->available()) _s->read(); }
};

// Easycomm I/II/III rotator over the same SC16IS750/752 I2C->UART bridge.
// Easycomm is the open, plain-ASCII tracking protocol used by SatNOGS, K3NG,
// ERC and most homebrew controllers (Hamlib rotators/easycomm):
//   II/III set:   "AZ<az.a> EL<el.a>\r"  (decimal degrees, 0.1 resolution)
//   II/III query: "AZ EL\r"  ->  "AZ<az.a> EL<el.a>" (some controllers append VE...)
//   stop:         "SA SE\r"  (stop azimuth + stop elevation)
//   I set:        "AZ<az> EL<el>"        (integer; the older variant)
// A single backend covers all three for tracking: II and III share the same
// positioning grammar (III only adds velocity/config commands we don't need),
// and I is the integer-format variant selected by `ver`.
class EasycommRotator : public Rotator {
public:
  // ver: 1 = Easycomm I (integer AZ/EL), 2 = II, 3 = III (II/III identical here).
  EasycommRotator(Stream* s, uint8_t ver) : _s(s), _ver(ver ? ver : 2) {}
  void begin() override;
  bool ready() const override { return _ok; }
  bool point(float az, float el) override;
  bool readPos(float& az, float& el) override;
  void stop() override;
  const char* name() const override {
    return _ver == 1 ? "Easycomm I" : _ver == 3 ? "Easycomm III" : "Easycomm II";
  }
private:
  Stream* _s; uint8_t _ver; bool _ok = false;
  void puts_(const char* s) { if (_s) _s->print(s); }
  int  getc_()              { return (_s && _s->available()) ? _s->read() : -1; }
  void flushIn()            { if (_s) while (_s->available()) _s->read(); }
};

// SPID Rot2Prog (MD-01/02, ROT2PROG) rotator over the same I2C->UART bridge.
// Binary protocol (per the Alfa/RFHamDesign spec and Hamlib rotators/spid
// "rot2prog"): fixed 13-byte command frames, 12-byte status replies.
//   START 0x57 | H1 H2 H3 H4 PH | V1 V2 V3 V4 PV | CMD | END 0x20
//   az/el are sent as (deg + 360) * resolution, each digit one ASCII byte;
//   CMD 0x2F = set, 0x1F = status/query, 0x0F = stop.
// Status reply decodes az/el back from the same digit encoding. Resolution is
// the controller's pulses-per-degree (commonly 1 or 2); we use 1 (whole-degree).
class SpidRotator : public Rotator {
public:
  explicit SpidRotator(Stream* s) : _s(s) {}
  void begin() override;
  bool ready() const override { return _ok; }
  bool point(float az, float el) override;
  bool readPos(float& az, float& el) override;
  void stop() override;
  const char* name() const override { return "SPID Rot2Prog"; }
private:
  Stream* _s; bool _ok = false;
  static constexpr int RES = 1;            // pulses/degree (whole-degree control)
  void putb_(uint8_t b) { if (_s) _s->write(b); }
  int  getb_(uint32_t toMs);               // blocking-with-timeout read
  void flushIn()        { if (_s) while (_s->available()) _s->read(); }
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

// Build the configured rotator backend AND its transport. The caller owns both
// and must free them together -- freeRotator() does exactly that, and the app
// calls it rather than a bare delete, because deleting the Rotator alone would
// leak the Stream it is still pointing at.
//
//   type      : ROT_* from settings.h (GS232 / NET / PST / EASYCOMM1..3 / SPID)
//   transport : ROT_XPORT_* -- ignored by the network backends (NET, PST), which
//               carry their own socket and never touch a serial Stream.
// (ROT_YAESU is built by the app, which supplies calibration -- not here.)
Rotator* makeRotator(uint8_t type, uint8_t transport, uint32_t baud,
                     const char* host, uint16_t port);
// Free a rotator built by makeRotator(), including its transport Stream.
void     freeRotator(Rotator* r);
