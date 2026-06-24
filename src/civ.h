#pragma once
// ===========================================================================
//  civ.h  -  Icom CI-V backend (CivRig : Rig)
// ===========================================================================
#include <Arduino.h>
#include "rig.h"

// CI-V operating modes (data byte for command 0x06)
enum CivMode : uint8_t {
  CIV_LSB = 0x00, CIV_USB = 0x01, CIV_AM = 0x02, CIV_CW = 0x03,
  CIV_RTTY = 0x04, CIV_FM = 0x05, CIV_CWR = 0x07, CIV_RTTYR = 0x08
};

class CivRig : public Rig {
public:
  explicit CivRig(RadioModel m) : _model(m), _addr(RADIOS[m].civAddr) {}

  void begin(uint32_t baud, int uartNum, int rxPin, int txPin) override;
  bool ready() const override { return _stream != nullptr; }

  // CI-V wiring mode. Call BEFORE begin(). 0 = separate TX/RX (default),
  // 1 = single shared open-drain wire on the txPin, 2 = single wire on the rxPin.
  // Single-pin uses one GPIO for both directions (true CI-V one-wire bus) and is
  // UNVERIFIED; the separate path is recommended. Ignored by other backends.
  void setPinMode(uint8_t mode) override { _pinMode = mode; }

  // Raw byte write for the serial-terminal diagnostic.
  bool sendRaw(const uint8_t* b, size_t n) override;

  bool setMainFreq(uint32_t hz) override;        // uplink (TX) on MAIN
  bool setSubFreq (uint32_t hz) override;        // downlink (RX) on SUB
  bool setMainMode(RigMode m)   override;
  bool setSubMode (RigMode m)   override;
  bool readSubFreq(uint32_t& hzOut) override;
  bool readMainFreq(uint32_t& hzOut) override;
  bool readPtt(bool& tx) override;
  bool enableSatMode(bool on)   override;
  bool setCtcss(bool on, float toneHz) override;
  void selectSubBand()          override { selectSub(); }
  void selectMainBand()         override { selectMain(); }
  bool assignBands(uint32_t mainHz, uint32_t subHz) override;

  bool canReadFreq() const override { return RADIOS[_model].canReadFreq; }
  bool hasSatMode()  const override { return RADIOS[_model].hasSatMode; }
  bool hasTone()     const override { return RADIOS[_model].hasTone; }
  bool canAssignBand() const override { return RADIOS[_model].canAssignBand; }
  bool selVerified() const override { return RADIOS[_model].selVerified; }
  const char* name() const override { return RADIOS[_model].name; }

  void    setAddress(uint8_t a) override { _addr = a; }
  uint8_t address() const       override { return _addr; }

private:
  Stream*    _stream = nullptr;
  RadioModel _model;
  uint8_t    _addr;
  int8_t     _pttRead = -1;   // -1 unknown, 0 unsupported (stop polling), 1 supported
  uint8_t    _pttFails = 0;   // consecutive read misses before marking unsupported
  uint8_t    _pinMode = 0;    // 0 separate TX/RX, 1 single-pin on tx, 2 single-pin on rx
  uint32_t   _lastMainHz = 0; // last frequency we COMMANDED on MAIN (uplink)
  uint32_t   _lastSubHz  = 0; // last frequency we COMMANDED on SUB  (downlink)

  void   selectMain();
  void   selectSub();
  bool   sendFrame(const uint8_t* payload, size_t len);
  bool   setFreqCiv(bool sub, uint32_t hz);
  bool   setModeCiv(bool sub, CivMode m, uint8_t filter = 0x01);
  bool   readFreqCiv(bool sub, uint32_t& hzOut);
  static CivMode toCiv(RigMode m);
  static void freqToBcd(uint32_t hz, uint8_t out[5]);
  bool   drainEcho(uint32_t timeoutMs = 60);  // CI-V is a shared bus: read back
};
