#pragma once
// ===========================================================================
//  radio_profiles.h  -  per-radio protocol + capability table
// ===========================================================================
//
//  CardSat speaks three CAT dialects, one per manufacturer family:
//    PROTO_CIV     Icom CI-V          (binary, FE FE framing, BCD, addressed)
//    PROTO_YAESU   Yaesu CAT          (5-byte binary: 4 data + opcode, BCD)
//    PROTO_KENWOOD Kenwood/Elecraft   (ASCII text commands, ';'-terminated)
//
//  Protocol details are taken from the Hamlib backends (icom, yaesu/ft847.c,
//  yaesu/ft736.c, kenwood/ts2000.c, kenwood/ts790.c) and the radios' CAT
//  manuals. See civ.cpp / yaesu.cpp / kenwood.cpp for the wire-level encoders.
//
//  Icom CI-V addresses (verified against the standard Icom address table):
//      IC-820 = 0x42   IC-910 = 0x60   IC-9100 = 0x7C
//      IC-821 = 0x4C   IC-970 = 0x2E   IC-9700 = 0xA2
//
//  MAIN/SUB band select (Icom only): CI-V cmd 0x07, D0 = MAIN, D1 = SUB,
//  verified from the IC-821H manual command table and shared across the family.
//
//  Frequency read-back (canReadFreq) enables the "radio knob" One True Rule
//  tuning mode:
//    * Icom (all six)     : CI-V 0x03 reads the operating frequency.
//    * Yaesu FT-847       : "read freq & mode" (opcode 0x03, patched to 0x13 for
//                           SAT-RX) returns 4 BCD bytes + mode. Works only on
//                           firmware-updated units (early ones can't read). true.
//    * Yaesu FT-736R      : CAT cannot report frequency at all (only squelch /
//                           S-meter); Hamlib caches the last set value. false.
//    * Kenwood TS-790/2000: ASCII "FA;" reads the frequency. true.
//
//  IMPORTANT shared limitation of the older sat rigs (FT-736R, TS-790, and the
//  Yaesu/Kenwood pairs generally): CAT cannot switch the BAND PAIR. The operator
//  selects the uplink/downlink bands (and engages the rig's own satellite / full-
//  duplex mode) manually on the radio; CardSat only Doppler-tunes within them.
//  This is exactly how SatPC32 drives these radios.
// ===========================================================================
#include <Arduino.h>

enum RigProtocol : uint8_t { PROTO_CIV, PROTO_YAESU, PROTO_KENWOOD };

enum RadioModel : uint8_t {
  RIG_IC820 = 0,
  RIG_IC821,
  RIG_IC910,
  RIG_IC970,
  RIG_IC9100,
  RIG_IC9700,
  RIG_FT847,
  RIG_FT736R,
  RIG_TS790,
  RIG_TS2000,
  RIG_COUNT
};

struct RadioProfile {
  const char* name;
  RigProtocol proto;
  uint8_t     civAddr;       // CI-V address (Icom only; 0 otherwise)
  uint32_t    defaultBaud;   // typical default CAT baud
  uint8_t     selMain[3];    // CI-V MAIN band-select bytes (Icom only)
  uint8_t     selSub[3];     // CI-V SUB  band-select bytes (Icom only)
  uint8_t     selLen;        // valid bytes in selMain/selSub (0 = n/a)
  bool        selVerified;   // CI-V select sequence documented (Icom only)
  bool        hasSatMode;    // radio has a dedicated full-duplex / sat mode
  uint8_t     satModeSub;    // CI-V satmode sub-cmd under 0x16 (Icom): IC-910 = 0x07,
                             // IC-9100/9700 = 0x5A. 0 = n/a (non-CI-V).
  bool        canReadFreq;   // frequency read-back implemented for this rig
  bool        hasTone;       // CAT can set the TX CTCSS (PL) encoder tone
};

// Order MUST match RadioModel.
static const RadioProfile RADIOS[RIG_COUNT] = {
  // name       proto         addr   baud    selMain        selSub         len verf satM satSub read tone
  { "IC-820",   PROTO_CIV,    0x42,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, true, 0x5A, true, false },
  { "IC-821",   PROTO_CIV,    0x4C,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, true, 0x5A, true, false },
  { "IC-910",   PROTO_CIV,    0x60,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, true, 0x07, true, true  },
  { "IC-970",   PROTO_CIV,    0x2E,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, false,0x5A, true, false },
  { "IC-9100",  PROTO_CIV,    0x7C,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, true, 0x5A, true, true  },
  { "IC-9700",  PROTO_CIV,    0xA2,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, true, 0x5A, true, true  },
  // Yaesu: 5-byte CAT. baud is the radio's CAT menu setting. No CI-V select.
  { "FT-847",   PROTO_YAESU,  0x00,  57600, {0,0,0},       {0,0,0},        0,  true, true, 0x00, true, true  },
  { "FT-736R",  PROTO_YAESU,  0x00,  4800,  {0,0,0},       {0,0,0},        0,  true, true, 0x00, false,false },
  // Kenwood: ASCII CAT over RS-232 (needs a MAX3232-class level interface).
  { "TS-790",   PROTO_KENWOOD,0x00,  4800,  {0,0,0},       {0,0,0},        0,  true, true, 0x00, true, false },
  { "TS-2000",  PROTO_KENWOOD,0x00,  57600, {0,0,0},       {0,0,0},        0,  true, true, 0x00, true, true  },
};
