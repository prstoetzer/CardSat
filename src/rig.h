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
#include <WiFi.h>            // WiFiClient for the rigctl (network) backend
#include "radio_profiles.h"

// Protocol-neutral operating modes; each backend maps these to its own codes.
enum RigMode : uint8_t { RM_LSB, RM_USB, RM_CW, RM_FM, RM_AM, RM_DATA };

class Rig {
public:
  virtual ~Rig() {}

  // Open the CAT serial port. The backend already knows its own model/params.
  virtual void begin(uint32_t baud, int uartNum, int rxPin, int txPin) = 0;
  virtual bool ready() const = 0;

  // Pumped every loop tick. Network backends (Icom LAN) advance their connection
  // state machine and answer keepalives here; wired backends need nothing.
  virtual void service() {}

  // Inter-command pacing: pause this many ms after each CAT frame (CAT Delay),
  // so a slow radio keeps up. Overwritten from the CAT Delay setting at engage.
  void setCmdDelay(uint16_t ms) { cmdDelayMs = ms; }
  // Upper bound (ms) on how long a single blocking CAT read may wait, so slow
  // I/O (especially the LAN backend) can't stall the cooperative main loop.
  // 0 = use the backend's built-in default. Set from the CAT cycle rate at engage.
  void setReadBudgetMs(uint16_t ms) { readBudgetMs = ms; }
protected:
  uint16_t cmdDelayMs = 70;
  uint16_t readBudgetMs = 0;
public:

  // Independent downlink (Sub/RX) and uplink (Main/TX) control.
  virtual bool setMainFreq(uint32_t hz) = 0;   // uplink (TX)
  virtual bool setSubFreq (uint32_t hz) = 0;   // downlink (RX)
  virtual bool setMainMode(RigMode m)   = 0;
  virtual bool setSubMode (RigMode m)   = 0;

  // Read the downlink (Sub/RX) frequency. Returns false if unsupported.
  virtual bool readSubFreq(uint32_t& hzOut) = 0;
  virtual bool readMainFreq(uint32_t& hzOut) = 0;

  // Read the rig's PTT/transmit state into tx. Returns false if the backend
  // can't report it (the default), in which case the caller must not assume
  // anything about transmit state. The Doppler loop uses this to skip the
  // downlink knob read while transmitting (a rig often reports the TX VFO then).
  virtual bool readPtt(bool& tx) { (void)tx; return false; }

  // Toggle the rig's own satellite mode. Icom: actively forced OFF (we drive
  // MAIN/SUB ourselves). Yaesu/Kenwood: no-op -- their full-duplex/sat mode is
  // set up by the operator and must NOT be disturbed.
  virtual bool enableSatMode(bool on) = 0;

  // Leave band access on the downlink so the operator's dial stays on RX
  // (meaningful for Icom; no-op elsewhere).
  virtual void selectSubBand() = 0;
  virtual void selectMainBand() = 0;

  // Assign which frequency BAND (2 m / 70 cm / 23 cm) sits on the MAIN vs SUB
  // VFO, so the uplink/downlink land on the correct bands when radio control is
  // engaged. Only radios with a CAT band-assignment command implement this
  // (IC-9100/IC-9700 via CI-V 07 D2); everyone else returns false and the
  // operator sets the band pair up on the radio. mainHz/subHz are the actual
  // frequencies whose bands should be placed on MAIN/SUB respectively.
  // UNTESTED on hardware -- see canAssignBand() and the CivRig implementation.
  virtual bool assignBands(uint32_t mainHz, uint32_t subHz) {
    (void)mainHz; (void)subHz; return false;
  }

  // Set the transmit CTCSS (PL) tone encoder. Used for FM satellites whose
  // uplink requires a subaudible tone (SO-50, AO-91, ISS, PO-101...). The tone
  // is applied to the uplink (Main/TX). on=false disables it. Backends that
  // can't drive CTCSS over CAT return false (the default).
  virtual bool setCtcss(bool on, float toneHz) { (void)on; (void)toneHz; return false; }

  // Capabilities / identity (read from the model's RadioProfile).
  virtual bool canReadFreq() const = 0;
  virtual bool hasSatMode()  const = 0;
  virtual bool hasTone()     const { return false; }   // CAT CTCSS supported
  virtual bool canAssignBand() const { return false; } // CAT MAIN/SUB band assign
  virtual bool selVerified() const = 0;
  virtual const char* name() const = 0;

  // CI-V address (Icom only; harmless no-ops on other backends).
  virtual void    setAddress(uint8_t) {}
  virtual uint8_t address() const { return 0; }

  // Map a SatNOGS/AMSAT mode string ("FM","USB","CW","DATA"...) to a RigMode.
  static RigMode modeFromString(const String& s);
};

// Index (0..38) of the nearest standard CTCSS tone to hz, or -1 if hz<=0 or no
// tone is within tolerance. The 39-tone EIA list is shared by the Yaesu code
// table and the Kenwood tone numbers; Icom encodes the frequency directly.
int  ctcssToneIndex(float hz);
// The standard tone (in Hz) at a given index, or 0 if out of range.
float ctcssToneHz(int index);

// rigctld (Hamlib "NET rigctl") TCP CLIENT. CardSat drives a radio attached to
// a rigctld server elsewhere on the LAN (default port 4532). To carry both legs
// of a satellite QSO over one link it uses Hamlib split semantics: the downlink
// (Sub/RX) is the main VFO (F/f set/get_freq, M set_mode) and the uplink
// (Main/TX) is the split/TX VFO (I/i set/get_split_freq, X set_split_mode).
// The socket opens lazily and reconnects (throttled) so a missing server never
// hangs the Doppler loop. Model-agnostic: the remote rigctld owns the radio.
class RigctlRig : public Rig {
public:
  RigctlRig(const char* host, uint16_t port) : _host(host), _port(port) {}
  void begin(uint32_t, int, int, int) override { _lastTry = 0; ensure(); }
  bool ready() const override { return _ok; }
  void service() override { ensure(); }
  bool setMainFreq(uint32_t hz) override;   // uplink   -> set_split_freq
  bool setSubFreq (uint32_t hz) override;   // downlink -> set_freq
  bool setMainMode(RigMode m)   override;   // uplink   -> set_split_mode
  bool setSubMode (RigMode m)   override;   // downlink -> set_mode
  bool readSubFreq (uint32_t& hzOut) override;   // get_freq
  bool readMainFreq(uint32_t& hzOut) override;   // get_split_freq
  bool readPtt(bool& tx) override;               // get_ptt
  bool enableSatMode(bool) override { return false; }  // remote rig is operator-configured
  void selectSubBand()  override {}
  void selectMainBand() override {}
  bool canReadFreq() const override { return true; }
  bool hasSatMode()  const override { return false; }
  bool selVerified() const override { return _ok; }
  const char* name() const override { return "rigctl"; }
private:
  String     _host;
  uint16_t   _port;
  bool       _ok = false;
  WiFiClient _c;
  uint32_t   _lastTry = 0;
  bool   ensure();                       // (re)connect if needed; updates _ok
  String xchg(const String& tx);         // send one line, return one reply line ("" on fail)
  static const char* modeName(RigMode m);
};

// Construct the backend for a model. Caller owns the returned pointer.
// catType 0 = wired CI-V/CAT (UART); 1 = Icom LAN (network) for CI-V models, in
// which case host/port/user/pass configure the RS-BA1 UDP connection.
Rig* makeRig(RadioModel model, uint8_t catType = 0, const char* host = "",
             uint16_t port = 50001, const char* user = "", const char* pass = "");
