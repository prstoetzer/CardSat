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

// Build the configured rotator backend (GS-232 at ROT_I2C_ADDR). Caller owns it.
Rotator* makeRotator(uint32_t baud);
