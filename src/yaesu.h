#pragma once
// ===========================================================================
//  yaesu.h  -  Yaesu 5-byte CAT backend (YaesuRig : Rig)  FT-847, FT-736R
// ===========================================================================
//
//  Wire format (per Hamlib rigs/yaesu/ft847.c and the FT-847 CAT manual):
//    every command is exactly 5 bytes -- four parameter bytes P1..P4 followed
//    by the OPCODE byte. Frequencies are big-endian BCD at 10 Hz resolution
//    (8 digits in 4 bytes). Satellite operation targets two VFOs by patching
//    the opcode: MAIN = 0x0-, SAT-RX (downlink) = opcode|0x10, SAT-TX (uplink)
//    = opcode|0x20. CAT must be switched on (00 00 00 00 00) before use.
//    Serial is 8 data bits, no parity, 2 stop bits (8N2).
//
//  FT-847 read-back: "read freq & mode" is opcode 0x03, patched to 0x13 for the
//  SAT-RX (downlink) VFO; the radio replies with 4 big-endian BCD bytes (10 Hz)
//  plus a mode byte. This works only on firmware-updated FT-847s -- early units
//  have no read capability and stay silent (we time out gracefully).
//
//  FT-736R note: it shares the 5-byte framing and the 8-digit/10 Hz BCD (which
//  is why 1240 MHz wraps), but it has NO frequency read-back (Hamlib caches the
//  last set value) and its native opcodes differ from the FT-847. The proven
//  community path is to drive it through an FT-847-emulating CAT interface
//  (KA6BFB / HS-736USB), which is what this backend assumes. To talk to a bare
//  FT-736R, confirm its opcodes against the FT-736R CAT manual / Hamlib ft736.c.
// ===========================================================================
#include <Arduino.h>
#include "rig.h"

class YaesuRig : public Rig {
public:
  explicit YaesuRig(RadioModel m)
    : _model(m), _postMs(m == RIG_FT736R ? 60 : 10) {}

  void begin(uint32_t baud, int uartNum, int rxPin, int txPin) override;
  bool ready() const override { return _stream != nullptr; }

  bool setMainFreq(uint32_t hz) override { return setFreq(0x21, hz); } // SAT TX
  bool setSubFreq (uint32_t hz) override { _lastSubHz = hz;            // SAT RX
                                           return setFreq(0x11, hz); }
  bool setMainMode(RigMode m)   override { return setMode(0x27, m); }  // SAT TX
  bool setSubMode (RigMode m)   override { return setMode(0x17, m); }  // SAT RX
  bool readSubFreq(uint32_t& hzOut) override;          // FT-847 only (0x13)
  bool enableSatMode(bool)      override { return false; }             // operator-set
  bool setCtcss(bool on, float toneHz) override;
  void selectSubBand()          override {}                            // n/a

  bool canReadFreq() const override { return RADIOS[_model].canReadFreq; }
  bool hasSatMode()  const override { return RADIOS[_model].hasSatMode; }
  bool hasTone()     const override { return RADIOS[_model].hasTone; }
  bool selVerified() const override { return RADIOS[_model].selVerified; }
  const char* name() const override { return RADIOS[_model].name; }

private:
  Stream*    _stream = nullptr;
  RadioModel _model;
  uint16_t   _postMs;          // inter-command delay (FT-736R needs more)
  uint32_t   _lastSubHz = 0;   // last downlink we commanded (wrong-VFO guard)

  bool   send(const uint8_t cmd[5]);
  bool   setFreq(uint8_t opcode, uint32_t hz);
  bool   setMode(uint8_t opcode, RigMode m);
  static uint8_t modeCode(RigMode m);
  static void    freqToBcd(uint32_t hz, uint8_t out[4]);
  static uint32_t bcdToFreq(const uint8_t in[4]);   // big-endian BCD * 10 Hz
};
