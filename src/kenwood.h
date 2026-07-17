#pragma once
// ===========================================================================
//  kenwood.h  -  Kenwood ASCII CAT backend (KenwoodRig : Rig)  TS-790, TS-2000
// ===========================================================================
//
//  Wire format (per Hamlib kenwood/ and the TS-2000 CAT manual): two-letter
//  ASCII commands with optional parameters, terminated by ';'. Frequencies are
//  an 11-digit Hz field. Reads echo the same command back with data.
//      FA<11 digits>;   set VFO A frequency      FA;  -> FA<11 digits>;  (read)
//      FB<11 digits>;   set VFO B frequency
//      MD<n>;           set mode  (1 LSB 2 USB 3 CW 4 FM 5 AM 6 FSK 7 CWR)
//      IF;              read transceiver status
//  Serial is 8N1 over RS-232 levels, so a MAX3232-class interface is required
//  between the radio's DB-9 and the 3.3 V UART (NOT the Icom CI-V circuit).
//
//  Sat mapping: this backend puts the downlink (RX) on VFO A (FA) and the
//  uplink (TX) on VFO B (FB). The rig's own satellite / split mode and the
//  uplink/downlink BANDS are selected by the operator on the radio (CAT can't
//  switch bands on these rigs); CardSat Doppler-tunes within that setup. On the
//  TS-2000 in particular, verify the VFO-A/B vs main/sub-band behaviour for your
//  firmware. The TS-790 supports a subset of these commands.
// ===========================================================================
#include <Arduino.h>
#include "rig.h"

class KenwoodRig : public Rig {
public:
  explicit KenwoodRig(RadioModel m) : _model(m) {}

  void begin(uint32_t baud, int uartNum, int rxPin, int txPin) override;
  bool ready() const override { return _stream != nullptr; }
  bool sendRaw(const uint8_t* b, size_t n) override;

  bool setMainFreq(uint32_t hz) override { return setVfoFreq("FB", hz); } // uplink/TX
  bool setSubFreq (uint32_t hz) override { return setVfoFreq("FA", hz); } // downlink/RX
  bool setMainMode(RigMode m)   override { return setModeKw(m); }
  bool setSubMode (RigMode m)   override { return setModeKw(m); }
  bool readSubFreq(uint32_t& hzOut) override;
  bool readMainFreq(uint32_t& hzOut) override { (void)hzOut; return false; }
  bool enableSatMode(bool)      override { return false; } // operator-set on radio
  bool setCtcss(bool on, float toneHz) override;
  void selectSubBand()          override {}
  void selectMainBand()         override {}

  bool canReadFreq() const override { return RADIOS[_model].canReadFreq; }
  bool hasSatMode()  const override { return RADIOS[_model].hasSatMode; }
  bool hasTone()     const override { return RADIOS[_model].hasTone; }
  bool selVerified() const override { return RADIOS[_model].selVerified; }
  const char* name() const override { return RADIOS[_model].name; }

  // Clear the cached transport too (see Rig::setExternalStream). begin() copies
  // extStream into _stream, so the base-class version alone would leave this
  // backend pointing at a Stream the caller is about to delete.
  void setExternalStream(Stream* s) override { Rig::setExternalStream(s); _stream = s; }

private:
  Stream*    _stream = nullptr;
  RadioModel _model;

  void   drainStale();                 // bounded RX flush (never spins)
  bool   sendCmd(const String& cmd);
  bool   setVfoFreq(const char* vfo, uint32_t hz);
  bool   setModeKw(RigMode m);
  static char modeDigit(RigMode m);
};
