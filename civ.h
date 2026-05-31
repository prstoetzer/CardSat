#pragma once
// ===========================================================================
//  civ.h  -  Icom CI-V controller (frame building + MAIN/SUB freq/mode set)
// ===========================================================================
#include <Arduino.h>
#include "radio_profiles.h"

// CI-V operating modes (data byte for command 0x06)
enum CivMode : uint8_t {
  CIV_LSB = 0x00, CIV_USB = 0x01, CIV_AM = 0x02, CIV_CW = 0x03,
  CIV_RTTY = 0x04, CIV_FM = 0x05, CIV_CWR = 0x07, CIV_RTTYR = 0x08
};

class CivRadio {
public:
  // begin() opens the CI-V hardware UART (TTL serial) on the given pins.
  void begin(RadioModel model, uint32_t baud,
             int uartNum, int rxPin, int txPin);

  void setModel(RadioModel m);
  void setAddress(uint8_t addr) { _addr = addr; }
  uint8_t address() const       { return _addr; }
  const RadioProfile& profile() const { return RADIOS[_model]; }

  // Independent MAIN (downlink/RX) and SUB (uplink/TX) control.
  bool setMainFreq(uint32_t hz);
  bool setSubFreq (uint32_t hz);
  bool setMainMode(CivMode m, uint8_t filter = 0x01);
  bool setSubMode (CivMode m, uint8_t filter = 0x01);

  // Convenience: push both legs of a Doppler update at once.
  bool updateDoppler(uint32_t rxHz, uint32_t txHz);

  // Try to put a sat-capable radio into satellite mode (no-op if unsupported).
  bool enableSatMode(bool on);

  // Read the SUB-band (downlink) operating frequency via CI-V 0x03.
  // Returns false on radios that don't support reading (IC-820/821/970) or on
  // timeout/parse failure.
  bool readSubFreq(uint32_t& hzOut);

  // Map a SatNOGS/AMSAT mode string ("FM","USB","CW","DATA"...) to a CivMode.
  static CivMode modeFromString(const String& s);

  // Leave CI-V band-access on SUB (so the operator's dial stays on the downlink).
  void selectSubBand();

  bool ready() const { return _stream != nullptr; }

private:
  Stream*    _stream = nullptr;
  RadioModel _model  = RIG_IC9700;
  uint8_t    _addr   = 0xA2;

  void   selectMain();
  void   selectSub();
  bool   sendFrame(const uint8_t* payload, size_t len);
  static void freqToBcd(uint32_t hz, uint8_t out[5]);
  bool   drainEcho(uint32_t timeoutMs = 60);  // CI-V is a shared bus: read back
};
