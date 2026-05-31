#pragma once
// ===========================================================================
//  radio_profiles.h  -  per-radio CI-V address + capability table
// ===========================================================================
//
//  Default CI-V addresses (verified against the standard Icom CI-V address
//  table):
//      IC-820  = 0x42      IC-910  = 0x60      IC-9100 = 0x7C
//      IC-821  = 0x4C      IC-970  = 0x2E      IC-9700 = 0xA2
//
//  MAIN/SUB band selection
//  -----------------------
//  Full-duplex satellite radios keep the downlink on the MAIN receiver and the
//  uplink on the SUB receiver. To drive both independently we must select the
//  band before sending a "set frequency" (0x05) command.
//
//   * IC-9700 / IC-9100 : CI-V cmd 0x07 with sub-code 0xD0 = select MAIN band,
//       0xD1 = select SUB band. (IC-9700 CI-V reference guide.)
//   * IC-910            : also accepts 0x07 0xD0/0xD1.
//   * IC-821            : VERIFIED from the IC-821H instruction manual CI-V
//       COMMAND TABLE (cmd 0x07): B0 = MAIN/SUB swap, D0 = Main band access,
//       D1 = Sub band access. Address 0x4C also confirmed there.
//   * IC-820            : same command family as the 821 (0x07 D0/D1); its
//       user manual omits the command table but confirms address 0x42. Treated
//       as verified on the strength of the identical IC-821 command set.
//   * IC-970            : third member of the same satellite family. Its user
//       manual also omits the command table, but it confirms the behavioural
//       model -- simultaneous MAIN/SUB receive and TX only on the MAIN band --
//       which matches our uplink=MAIN / downlink=SUB mapping. Verified on the
//       strength of the IC-821 family command set (0x07 D0/D1).
//
//  Everything radio-specific lives in this one table so it is easy to tune.
// ===========================================================================
#include <Arduino.h>

enum RadioModel : uint8_t {
  RIG_IC820 = 0,
  RIG_IC821,
  RIG_IC910,
  RIG_IC970,
  RIG_IC9100,
  RIG_IC9700,
  RIG_COUNT
};

struct RadioProfile {
  const char* name;
  uint8_t     civAddr;       // default CI-V address
  uint32_t    defaultBaud;   // typical default CI-V baud
  // Band-select command sequences (sent after 0xFE 0xFE addr 0xE0):
  uint8_t     selMain[3];    // bytes, length in selLen
  uint8_t     selSub[3];
  uint8_t     selLen;        // number of valid bytes in selMain/selSub
  bool        selVerified;   // true if the select sequence is documented
  bool        hasSatMode;    // radio has a dedicated satellite mode
  bool        canReadFreq;   // CI-V 0x03 (read operating freq) supported?
};

// Order MUST match RadioModel enum.
//   canReadFreq: all six support CI-V 0x03 (read operating frequency). The
//   IC-821H manual COMMAND TABLE doesn't enumerate 0x03, but 0x00/0x03/0x04 are
//   part of the base Icom CI-V command set and work on the IC-820/821/970 --
//   confirmed by Hamlib's stable ic820h/ic821h/ic970 backends, which read these
//   radios. Read-back enables the "radio knob" One True Rule tuning mode.
static const RadioProfile RADIOS[RIG_COUNT] = {
  // name        addr   baud    selMain        selSub         len verf satM read
  { "IC-820",   0x42,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, false,true },
  { "IC-821",   0x4C,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, false,true },
  { "IC-910",   0x60,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, false,true },
  { "IC-970",   0x2E,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, false,true },
  { "IC-9100",  0x7C,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, true, true },
  { "IC-9700",  0xA2,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, true, true },
};
