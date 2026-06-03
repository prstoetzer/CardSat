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

  bool setMainFreq(uint32_t hz) override;        // uplink (TX) on MAIN
  bool setSubFreq (uint32_t hz) override;        // downlink (RX) on SUB
  bool setMainMode(RigMode m)   override;
  bool setSubMode (RigMode m)   override;
  bool readSubFreq(uint32_t& hzOut) override;
  bool enableSatMode(bool on)   override;
  bool setCtcss(bool on, float toneHz) override;
  void selectSubBand()          override { selectSub(); }

  bool canReadFreq() const override { return RADIOS[_model].canReadFreq; }
  bool hasSatMode()  const override { return RADIOS[_model].hasSatMode; }
  bool hasTone()     const override { return RADIOS[_model].hasTone; }
  bool selVerified() const override { return RADIOS[_model].selVerified; }
  const char* name() const override { return RADIOS[_model].name; }

  void    setAddress(uint8_t a) override { _addr = a; }
  uint8_t address() const       override { return _addr; }

private:
  Stream*    _stream = nullptr;
  RadioModel _model;
  uint8_t    _addr;

  void   selectMain();
  void   selectSub();
  bool   sendFrame(const uint8_t* payload, size_t len);
  bool   setFreqCiv(bool sub, uint32_t hz);
  bool   setModeCiv(bool sub, CivMode m, uint8_t filter = 0x01);
  static CivMode toCiv(RigMode m);
  static void freqToBcd(uint32_t hz, uint8_t out[5]);
  bool   drainEcho(uint32_t timeoutMs = 60);  // CI-V is a shared bus: read back
};
