#pragma once
// ===========================================================================
//  rig.h  -  abstract transceiver interface (one backend per CAT family)
// ===========================================================================
//
//  The whole application talks to the radio through this narrow interface, so
//  SGP4 prediction, the One True Rule Doppler loop, calibration and the UI are
//  all protocol-agnostic. Concrete backends:
//      CivRig     (civ.cpp)     Icom CI-V          IC-820/821/910/970/9100/9700
//      YaesuRig   (yaesu.cpp)   Yaesu 5-byte CAT   FT-847, FT-736R
//      KenwoodRig (kenwood.cpp) Kenwood ASCII CAT  TS-790, TS-2000
//
//  Convention used everywhere in the app (kept regardless of how a given rig
//  labels its VFOs): "Sub" = downlink / RX, "Main" = uplink / TX.
// ===========================================================================
#include <Arduino.h>
#include "radio_profiles.h"

// Protocol-neutral operating modes; each backend maps these to its own codes.
enum RigMode : uint8_t { RM_LSB, RM_USB, RM_CW, RM_FM, RM_AM, RM_DATA };

class Rig {
public:
  virtual ~Rig() {}

  // Open the CAT serial port. The backend already knows its own model/params.
  virtual void begin(uint32_t baud, int uartNum, int rxPin, int txPin) = 0;
  virtual bool ready() const = 0;

  // Independent downlink (Sub/RX) and uplink (Main/TX) control.
  virtual bool setMainFreq(uint32_t hz) = 0;   // uplink (TX)
  virtual bool setSubFreq (uint32_t hz) = 0;   // downlink (RX)
  virtual bool setMainMode(RigMode m)   = 0;
  virtual bool setSubMode (RigMode m)   = 0;

  // Read the downlink (Sub/RX) frequency. Returns false if unsupported.
  virtual bool readSubFreq(uint32_t& hzOut) = 0;

  // Toggle the rig's own satellite mode. Icom: actively forced OFF (we drive
  // MAIN/SUB ourselves). Yaesu/Kenwood: no-op -- their full-duplex/sat mode is
  // set up by the operator and must NOT be disturbed.
  virtual bool enableSatMode(bool on) = 0;

  // Leave band access on the downlink so the operator's dial stays on RX
  // (meaningful for Icom; no-op elsewhere).
  virtual void selectSubBand() = 0;

  // Capabilities / identity (read from the model's RadioProfile).
  virtual bool canReadFreq() const = 0;
  virtual bool hasSatMode()  const = 0;
  virtual bool selVerified() const = 0;
  virtual const char* name() const = 0;

  // CI-V address (Icom only; harmless no-ops on other backends).
  virtual void    setAddress(uint8_t) {}
  virtual uint8_t address() const { return 0; }

  // Map a SatNOGS/AMSAT mode string ("FM","USB","CW","DATA"...) to a RigMode.
  static RigMode modeFromString(const String& s);
};

// Construct the backend for a model. Caller owns the returned pointer.
Rig* makeRig(RadioModel model);
