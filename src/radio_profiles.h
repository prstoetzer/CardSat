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
//  Icom CI-V addresses (verified against the standard Icom address table and
//  Hamlib backends / live CI-V traces):
//      IC-820 = 0x42   IC-910 = 0x60   IC-9100 = 0x7C
//      IC-821 = 0x4C   IC-970 = 0x2E   IC-9700 = 0xA2
//
//  MAIN/SUB band select (Icom only): CI-V cmd 0x07, sub D0/D1. The IC-821H (D0=MAIN,
//  D1=SUB) and IC-820H (REVERSED: D1=MAIN, D0=SUB) are each confirmed from their own
//  manuals; the IC-9100/9700 D0/D1 mapping is confirmed against Hamlib (PR #97 main/
//  sub 0x07 0xD0/0xD1). The IC-910/IC-970 use the same 0x07 D0/D1 family convention.
//
//  Satellite-mode toggle (CI-V, per-rig command + sub-command):
//    * IC-9100 / IC-9700 : cmd 0x16 sub 0x5A. CONFIRMED from the IC-9700 CI-V
//                          Reference Guide ("5A ... Send/read the satellite mode")
//                          and a live IC-9100 trace "fe fe 7c e0 16 5a fd" (#1656).
//    * IC-910            : cmd 0x1A sub 0x07 -- DIFFERENT command group. CONFIRMED
//                          from the IC-910 manual CONTROL COMMAND table (cmd 1A,
//                          sub 07 = "Set satellite mode"). Hamlib also carries this
//                          as a separate S_MEM_SATMODE910 constant (PR #143).
//    * IC-820/821/970    : no CAT satellite-mode command (hardware switch); hasSatMode
//                          reflects this.
//
//  Tone encoder on/off (CI-V cmd 0x16, per-rig sub toneEncSub): IC-9100/9700 = 0x42
//  (Repeater tone); IC-910 = 0x43 (Subaudible tone -- its 0x42 is the auto-notch
//  filter). Confirmed from the IC-9700 CI-V guide and IC-910 manual command tables.
//
//  Frequency read-back (canReadFreq) enables the "radio knob" One True Rule
//  tuning mode:
//    * Icom (all six)     : CI-V 0x03 reads the operating frequency. The SUB band
//                           is re-selected immediately before each read; if the
//                           radio doesn't reply (common on the IC-821's SUB band),
//                           the read falls back to the last value we commanded so
//                           Doppler tracking continues (knob-follow is skipped that
//                           cycle rather than acting on a bad read). PTT state for
//                           the knob-follow is polled with 0x1C 0x00 (read
//                           transceiver status); rigs that don't answer it are
//                           detected and the poll is dropped after a few misses.
//    * Yaesu FT-847       : "read freq & mode" (opcode 0x03, patched to 0x13 for
//                           SAT-RX) returns 4 BCD bytes + mode. Works only on
//                           firmware-updated units (early ones can't read). true.
//    * Yaesu FT-736R      : CAT cannot report frequency at all (only squelch /
//                           S-meter); Hamlib caches the last set value. false.
//    * Kenwood TS-790/2000: ASCII "FA;" reads the frequency. true.
//
//  IMPORTANT shared limitation of the older sat rigs (IC-820, IC-821, IC-970,
//  FT-736R, TS-790, TS-2000, and the Yaesu/Kenwood pairs generally): CAT cannot
//  switch the BAND PAIR, cannot assign which band sits on MAIN vs SUB, and on the
//  IC-820/821/970 cannot toggle satellite mode either. The operator selects the
//  uplink/downlink bands, sets up MAIN/SUB, engages the rig's own satellite /
//  full-duplex mode, AND sets any uplink CTCSS (PL) tone -- all manually on the
//  radio. CardSat then only Doppler-tunes within that pre-configured pairing.
//  The IC-820/821 sat-mode CI-V command is a no-op on real hardware (verified
//  on an IC-821, N8HM), so hasSatMode is false for them; their D0/D1 bytes are
//  band *access* (which band a read/write targets), NOT a MAIN/SUB assignment.
//  This matches how SatPC32, Gpredict/Hamlib, and OscarWatch drive these radios:
//  e.g. Hamlib's IC-821 backend has no MAIN/SUB or satmode; OscarWatch lists the
//  IC-821 as "Satellite Main/Sub only, uplink tone manual on radio"; and the
//  Kenwood TS-2000 requires the band pair configured on the rig before tracking.
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
  RIG_NONE,     // No radio. CardSat runs as a pure tracker/rotator controller: makeRig()
                // returns nullptr, so all CAT features become no-ops (the code already
                // guards every rig use with a null check).
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
  uint8_t     satModeCmd;    // CI-V satmode command byte (Icom): IC-910 = 0x1A,
                             // IC-9100/9700 = 0x16. 0 = n/a (non-CI-V).
  uint8_t     satModeSub;    // CI-V satmode sub-cmd: IC-910 = 0x07 (under 0x1A),
                             // IC-9100/9700 = 0x5A (under 0x16). 0 = n/a.
  bool        canReadFreq;   // frequency read-back implemented for this rig
  bool        hasTone;       // CAT can set the TX CTCSS (PL) encoder tone
  uint8_t     toneEncSub;    // CI-V tone-encoder on/off sub-cmd under 0x16:
                             // IC-9100/9700 = 0x42 (Repeater tone), IC-910 = 0x43
                             // (Subaudible tone; on the 910, 0x42 is auto-notch). 0 = n/a.
  bool        canAssignBand; // CAT can ASSIGN which band sits on MAIN vs SUB
                             // (Icom CI-V 07 D2). true only for IC-9100/IC-9700;
                             // the 820/821/970 D0/D1 are band *access* only.
                             // UNTESTED on hardware.
};

// Order MUST match RadioModel.
static const RadioProfile RADIOS[RIG_COUNT] = {
  // name       proto         addr   baud    selMain        selSub         len verf satM satCmd satSub read tone tnEnc
  // NOTE: MAIN/SUB band-select differs between these two otherwise-similar rigs,
  // each confirmed from its own manual's CI-V command table (cmd 07):
  //   IC-821H: Main band access = D0, Sub band access = D1  (addr 4C)
  //   IC-820H: Main band access = D1, Sub band access = D0  (addr 42)  <- REVERSED
  // So selMain/selSub are intentionally swapped between the two rows below.
  //
  // IC-910 is DIFFERENT from the 9700-style "main access / sub access" model.
  // Its 07 group is: D1 = Select MAIN VFO, D0 = Switch VFO A and VFO B (swap),
  // and a separate "Select SUB VFO" entry. (Confirmed from the IC-910 CONTROL
  // COMMAND table and a live Hamlib/gpredict trace: 07 D1 selects MAIN, 07 D0
  // swaps.) So for the 910 selMain = {07,D1}. There is no clean independent
  // "sub access" byte that the Hamlib traces use -- they reach SUB by selecting
  // MAIN then swapping. We set selSub = {07,D0} (the swap) as the least-wrong
  // value; the 910's addressed-SUB read/write path is UNVERIFIED on hardware and
  // a 910 owner should confirm it. (Earlier this row had selMain = {07,D0},
  // i.e. it was issuing a SWAP where a MAIN-select was intended -- now fixed.)
  // satCmd/satSub: satellite-mode toggle. IC-9100/9700 = 0x16/0x5A (confirmed: 9700
  // CI-V Reference Guide & live 9100 trace fe fe 7c e0 16 5a fd). IC-910 is DIFFERENT:
  // 0x1A/0x07 (verified from the IC-910 CONTROL COMMAND table, cmd 1A sub 07
  // "Set satellite mode"). 0/0 where there's no CAT satmode.
  // tnEnc: tone-encoder on/off sub under 0x16. IC-9100/9700 = 0x42 (Repeater tone);
  // IC-910 = 0x43 (Subaudible tone; its 0x42 is auto-notch). 0 where no CAT tone.
  { "IC-820",   PROTO_CIV,    0x42,  9600,  {0x07,0xD1,0}, {0x07,0xD0,0},  2,  true, false, 0x00, 0x00, true, false, 0x00, false },
  { "IC-821",   PROTO_CIV,    0x4C,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, false, 0x00, 0x00, true, false, 0x00, false },
  { "IC-910",   PROTO_CIV,    0x60,  19200, {0x07,0xD1,0}, {0x07,0xD0,0},  2,  true, true, 0x1A, 0x07, true, true,  0x43, true  },
  { "IC-970",   PROTO_CIV,    0x2E,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, false,0x16, 0x5A, true, false, 0x00, false },
  { "IC-9100",  PROTO_CIV,    0x7C,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, true, 0x16, 0x5A, true, true,  0x42, true },
  { "IC-9700",  PROTO_CIV,    0xA2,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, true, 0x16, 0x5A, true, true,  0x42, true },
  // Yaesu: 5-byte CAT. baud is the radio's CAT menu setting. No CI-V select.
  { "FT-847",   PROTO_YAESU,  0x00,  57600, {0,0,0},       {0,0,0},        0,  true, true, 0x00, 0x00, true, true,  0x00, false },
  { "FT-736R",  PROTO_YAESU,  0x00,  4800,  {0,0,0},       {0,0,0},        0,  true, true, 0x00, 0x00, false,false, 0x00, false },
  // Kenwood: ASCII CAT over RS-232 (needs a MAX3232-class level interface).
  { "TS-790",   PROTO_KENWOOD,0x00,  4800,  {0,0,0},       {0,0,0},        0,  true, true, 0x00, 0x00, true, false, 0x00, false },
  { "TS-2000",  PROTO_KENWOOD,0x00,  57600, {0,0,0},       {0,0,0},        0,  true, true, 0x00, 0x00, true, true,  0x00, false },
  // RIG_NONE: placeholder so RADIOS[RIG_NONE] is a valid dereference (the name is shown
  // in Settings). makeRig() returns nullptr for it, so none of the other fields are used.
  { "None",     PROTO_CIV,    0x00,  9600,  {0,0,0},       {0,0,0},        0,  false,false,0x00, 0x00, false,false, 0x00, false },
};

// ===========================================================================
//  Dual-rig LEG catalog (CAT_DUAL) -- the CardSatDualRig companion's radio set,
//  absorbed into the main firmware (DUALRIG_MAINFW_INTEGRATION_SCOPE.md, Model A).
//
//  These are the half-duplex transceivers and receive-only radios that pair up as
//  a downlink + uplink leg. They are driven with PLAIN single-VFO CAT (one freq,
//  one mode -- no MAIN/SUB, no satellite mode), which is why they live in their
//  own table instead of RADIOS[]: the RadioProfile machinery above is all about
//  MAIN/SUB band access on full-duplex sat rigs and does not apply here.
//
//  Four CAT dialects cover every leg radio (same families as the companion):
//    LEGF_CIV   Icom binary CI-V, addressed          (cmd 05 freq, 06 mode, 03 read)
//    LEGF_YBIN  Yaesu "old" 5-byte binary CAT        (4-byte BCD @10 Hz + opcode)
//    LEGF_YTXT  Yaesu "new" ASCII CAT                (FA/MD ';'-terminated)
//    LEGF_KWHT  Kenwood TH-D74/D75 handheld CAT      (FQ<band>,<Hz> + CR; Band B)
//
//  Table data (names, dialects, default bauds, CI-V addresses, RX-only flags) is
//  ported verbatim from companion/CardSatDualRig (RADIO_TABLE[]), which is the
//  bench-validated source of truth for these radios.
//
//  hasLan: the radio has native Icom network CAT that CardSat's IcomNetRig can
//  target (same RS-BA1-family UDP protocol the IC-9700 path speaks). Set for the
//  IC-705 (the requested + supported LAN target, over the radio's own Wi-Fi) and
//  the IC-905 (same protocol family per Icom's docs -- UNTESTED on hardware).
// ===========================================================================
enum LegFamily : uint8_t { LEGF_CIV, LEGF_YBIN, LEGF_YTXT, LEGF_KWHT };

enum LegModel : uint8_t {
  // --- Icom CI-V transceivers ---
  LEG_IC705 = 0, LEG_IC905, LEG_IC7100, LEG_IC7000, LEG_IC706MK2G, LEG_IC275, LEG_IC475,
  // --- Icom CI-V receivers (RX only) ---
  LEG_ICR10, LEG_ICR20, LEG_ICR30,
  LEG_ICR7000, LEG_ICR7100, LEG_ICR8500, LEG_ICR8600, LEG_ICR9000, LEG_ICR9500,
  // --- Yaesu old binary ---
  LEG_FT817, LEG_FT818, LEG_FT857, LEG_FT897, LEG_FT100,
  // --- Yaesu receiver (old-binary CAT family) ---
  LEG_VR5000,
  // --- Yaesu new ASCII ---
  LEG_FT991, LEG_FT991A, LEG_FTX1,
  // --- Kenwood handhelds (all-mode receiver on Band B, RX only) ---
  LEG_THD74, LEG_THD75,
  LEG_NONE,      // leg unassigned; makeLegRig() returns nullptr
  LEG_COUNT
};

struct LegProfile {
  const char* name;
  LegFamily   family;
  uint32_t    baud;      // default CAT baud (0 in cfg = use this)
  uint8_t     civAddr;   // default CI-V bus address (LEGF_CIV only; 0 otherwise)
  bool        rxOnly;    // receive-only: warn if assigned to the uplink leg
  bool        hasLan;    // Icom network CAT available as a leg transport
};

// Order MUST match LegModel. Data ported from the companion's RADIO_TABLE[].
static const LegProfile LEG_RADIOS[LEG_COUNT] = {
  //  name           family     baud   addr  rxOnly lan
  { "IC-705",      LEGF_CIV,  19200, 0xA4, false, true  },
  { "IC-905",      LEGF_CIV,  19200, 0xAC, false, true  },
  { "IC-7100",     LEGF_CIV,  19200, 0x88, false, false },
  { "IC-7000",     LEGF_CIV,  19200, 0x70, false, false },
  { "IC-706MKIIG", LEGF_CIV,   9600, 0x58, false, false },
  { "IC-275",      LEGF_CIV,   9600, 0x10, false, false },
  { "IC-475",      LEGF_CIV,   9600, 0x14, false, false },
  { "IC-R10",      LEGF_CIV,   9600, 0x52, true,  false },
  { "IC-R20",      LEGF_CIV,   9600, 0x6C, true,  false },
  { "IC-R30",      LEGF_CIV,   9600, 0x9C, true,  false },
  { "IC-R7000",    LEGF_CIV,   1200, 0x08, true,  false },
  { "IC-R7100",    LEGF_CIV,   9600, 0x34, true,  false },
  { "IC-R8500",    LEGF_CIV,   9600, 0x4A, true,  false },
  { "IC-R8600",    LEGF_CIV,  19200, 0x96, true,  false },
  { "IC-R9000",    LEGF_CIV,   1200, 0x2A, true,  false },
  { "IC-R9500",    LEGF_CIV,  19200, 0x72, true,  false },
  { "FT-817",      LEGF_YBIN,  9600, 0x00, false, false },
  { "FT-818",      LEGF_YBIN,  9600, 0x00, false, false },
  { "FT-857",      LEGF_YBIN,  9600, 0x00, false, false },
  { "FT-897",      LEGF_YBIN,  9600, 0x00, false, false },
  { "FT-100",      LEGF_YBIN,  9600, 0x00, false, false },
  // VR-5000: Yaesu 5-byte family; opcodes close to the FT-817's but VERIFY on
  // hardware (carried over from the companion's own caveat).
  { "VR-5000",     LEGF_YBIN,  9600, 0x00, true,  false },
  { "FT-991",      LEGF_YTXT, 38400, 0x00, false, false },
  { "FT-991A",     LEGF_YTXT, 38400, 0x00, false, false },
  { "FTX-1",       LEGF_YTXT, 38400, 0x00, false, false },
  { "TH-D74",      LEGF_KWHT,  9600, 0x00, true,  false },
  { "TH-D75",      LEGF_KWHT,  9600, 0x00, true,  false },
  { "None",        LEGF_CIV,   9600, 0x00, false, false },
};

// Which physical bus a dual-rig leg rides. One Grove UART and one USB CAT port
// exist, so two legs may not share either; Wi-Fi (LAN) is shareable.
enum LegBus : uint8_t { LEGBUS_GROVE = 0, LEGBUS_USB = 1, LEGBUS_LAN = 2, LEGBUS_N = 3 };
