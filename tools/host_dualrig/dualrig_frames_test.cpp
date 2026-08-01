// Host harness for the dual-rig (CAT_DUAL) leg CAT frame builders.
//
// The builders under test are EXTRACTED from src/rig.cpp by the .sh wrapper (see
// there), not copied here. The expected bytes come from two independent places:
// the CardSatDualRig companion's bench-validated encoders (this code's ancestor)
// and the radios' own CAT documentation (Icom CI-V cmd 05/06/03 BCD layout;
// Yaesu 5-byte CAT; Yaesu FA/MD ASCII; Kenwood TH-D74 FQ/MD).
//
// Build/run:  ./dualrig_frames_test.sh   (non-zero exit on any mismatch)
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>

// Minimal mirrors of the firmware enums (values must match radio_profiles.h /
// rig.h; the extracted code is compiled against these).
enum LegFamily : uint8_t { LEGF_CIV, LEGF_YBIN, LEGF_Y100, LEGF_YVR5,
                          LEGF_YTXT, LEGF_KWHT, LEGF_KWTS };
enum RigMode   : uint8_t { RM_LSB, RM_USB, RM_CW, RM_FM, RM_AM, RM_DATA };

// Declarations the extracted block defines.
size_t legBuildFreqFrame(LegFamily, uint8_t, uint64_t, uint8_t*, size_t, bool wideFreq = false);
size_t legBuildModeFrame(LegFamily, uint8_t, RigMode, uint8_t*, size_t, bool withFilter = true);
size_t legBuildReadFreqFrame(LegFamily, uint8_t, uint8_t*, size_t);
bool   legParseFreqReply(LegFamily, uint8_t, const uint8_t*, size_t, uint64_t&);
size_t legKwFoPatch(const uint8_t*, size_t, uint64_t, uint8_t*, size_t);

#include "/tmp/leg_builders.inc"

static int fails = 0;
static void expectBytes(const char* what, const uint8_t* got, size_t gn,
                        const uint8_t* want, size_t wn) {
  if (gn == wn && memcmp(got, want, wn) == 0) { printf("ok   %s\n", what); return; }
  printf("FAIL %s\n  got  (%zu):", what, gn);
  for (size_t i = 0; i < gn; i++) printf(" %02X", got[i]);
  printf("\n  want (%zu):", wn);
  for (size_t i = 0; i < wn; i++) printf(" %02X", want[i]);
  printf("\n");
  fails++;
}
static void expectStr(const char* what, const uint8_t* got, size_t gn, const char* want) {
  expectBytes(what, got, gn, (const uint8_t*)want, strlen(want));
}
static void expectHz(const char* what, bool ok, uint64_t hz, bool wantOk, uint64_t wantHz) {
  if (ok == wantOk && (!wantOk || hz == wantHz)) { printf("ok   %s\n", what); return; }
  printf("FAIL %s: ok=%d hz=%llu (want ok=%d hz=%llu)\n",
         what, (int)ok, (unsigned long long)hz, (int)wantOk, (unsigned long long)wantHz);
  fails++;
}

int main() {
  uint8_t b[32]; size_t n; uint64_t hz;

  // ---- Icom CI-V (IC-705 addr 0xA4): 145.900 MHz, USB, read, reply parse ----
  n = legBuildFreqFrame(LEGF_CIV, 0xA4, 145900000ULL, b, sizeof(b));
  { const uint8_t w[] = {0xFE,0xFE,0xA4,0xE0,0x05,0x00,0x00,0x90,0x45,0x01,0xFD};
    expectBytes("CIV freq 145.900 MHz", b, n, w, sizeof(w)); }
  n = legBuildModeFrame(LEGF_CIV, 0xA4, RM_USB, b, sizeof(b));
  { const uint8_t w[] = {0xFE,0xFE,0xA4,0xE0,0x06,0x01,0x01,0xFD};
    expectBytes("CIV mode USB", b, n, w, sizeof(w)); }
  n = legBuildModeFrame(LEGF_CIV, 0xA4, RM_FM, b, sizeof(b));
  { const uint8_t w[] = {0xFE,0xFE,0xA4,0xE0,0x06,0x05,0x01,0xFD};
    expectBytes("CIV mode FM", b, n, w, sizeof(w)); }
  // A few Icoms reject cmd 06 when it carries a filter byte (Hamlib names the
  // IC-910, IC-7000 and IC-475 among them). Those take the two-byte form, and
  // getting it wrong is invisible on the wire because no ACK is checked.
  n = legBuildModeFrame(LEGF_CIV, 0x14, RM_USB, b, sizeof(b), false);
  { const uint8_t w[] = {0xFE,0xFE,0x14,0xE0,0x06,0x01,0xFD};
    expectBytes("CIV mode, no filter byte", b, n, w, sizeof(w)); }
  n = legBuildModeFrame(LEGF_CIV, 0x14, RM_USB, b, sizeof(b), true);
  { const uint8_t w[] = {0xFE,0xFE,0x14,0xE0,0x06,0x01,0x01,0xFD};
    expectBytes("CIV mode, with filter byte", b, n, w, sizeof(w)); }
  // IC-905 only: SIX-byte frequency above 5.85 GHz. Five bytes is ten BCD digits,
  // which cannot express the 10 GHz band; below the threshold the radio takes the
  // ordinary five-byte form, so the choice is per-frequency, not per-radio.
  n = legBuildFreqFrame(LEGF_CIV, 0xAC, 10368000000ULL, b, sizeof(b), true);
  { // 10368000000 -> 12 BCD digits "010368000000", LSB-first pairs.
    const uint8_t w[] = {0xFE,0xFE,0xAC,0xE0,0x05,
                         0x00,0x00,0x00,0x68,0x03,0x01, 0xFD};
    expectBytes("CIV 10.368 GHz -> six bytes", b, n, w, sizeof(w)); }
  n = legBuildFreqFrame(LEGF_CIV, 0xAC, 435500000ULL, b, sizeof(b), true);
  { const uint8_t w[] = {0xFE,0xFE,0xAC,0xE0,0x05,0x00,0x00,0x50,0x35,0x04,0xFD};
    expectBytes("CIV 435 MHz stays five bytes", b, n, w, sizeof(w)); }
  { const uint8_t rx[] = {0xFE,0xFE,0xE0,0xAC,0x03,
                          0x00,0x00,0x00,0x68,0x03,0x01, 0xFD};
    bool ok = legParseFreqReply(LEGF_CIV, 0xAC, rx, sizeof(rx), hz);
    expectHz("CIV parse six-byte reply", ok, hz, true, 10368000000ULL); }

  n = legBuildReadFreqFrame(LEGF_CIV, 0xA4, b, sizeof(b));
  { const uint8_t w[] = {0xFE,0xFE,0xA4,0xE0,0x03,0xFD};
    expectBytes("CIV read-freq query", b, n, w, sizeof(w)); }
  { // interface echo (H6) followed by the real reply
    const uint8_t rx[] = {0xFE,0xFE,0xA4,0xE0,0x03,0xFD,
                          0xFE,0xFE,0xE0,0xA4,0x03,0x00,0x00,0x90,0x45,0x01,0xFD};
    bool ok = legParseFreqReply(LEGF_CIV, 0xA4, rx, sizeof(rx), hz);
    expectHz("CIV parse reply (with echo)", ok, hz, true, 145900000ULL); }

  // ---- Yaesu 5-byte binary (FT-817): 435.500 MHz, FM, read, parse ----
  n = legBuildFreqFrame(LEGF_YBIN, 0, 435500000ULL, b, sizeof(b));
  { const uint8_t w[] = {0x43,0x55,0x00,0x00,0x01};
    expectBytes("YBIN freq 435.500 MHz", b, n, w, sizeof(w)); }
  n = legBuildModeFrame(LEGF_YBIN, 0, RM_FM, b, sizeof(b));
  { const uint8_t w[] = {0x08,0x00,0x00,0x00,0x07};
    expectBytes("YBIN mode FM", b, n, w, sizeof(w)); }
  n = legBuildReadFreqFrame(LEGF_YBIN, 0, b, sizeof(b));
  { const uint8_t w[] = {0x00,0x00,0x00,0x00,0x03};
    expectBytes("YBIN read query", b, n, w, sizeof(w)); }
  { const uint8_t rx[] = {0x43,0x55,0x00,0x00,0x08};   // 4 BCD + mode
    bool ok = legParseFreqReply(LEGF_YBIN, 0, rx, sizeof(rx), hz);
    expectHz("YBIN parse reply", ok, hz, true, 435500000ULL); }

  // ---- FT-100 (LEGF_Y100): its OWN dialect -----------------------------------
  // 0.9.68 filed the FT-100 under the FT-817 family. A 0.9.70 audit against Hamlib
  // ft100.c found every single element differs: opcode 0x0A not 0x01, LITTLE-endian
  // BCD not big, the mode byte in data[3] not data[0] with opcode 0x0C not 0x07,
  // its own mode values (FM = 0x06), read opcode 0x10 not 0x03, and the frequency
  // at offset 1 of the reply behind a band number. Nothing the FT-817 dialect sends
  // means anything to an FT-100.
  n = legBuildFreqFrame(LEGF_Y100, 0, 435500000ULL, b, sizeof(b));
  { // 435500000 Hz / 10 = 43550000; LSB-first BCD pairs -> 00 00 55 43.
    const uint8_t w[] = {0x00,0x00,0x55,0x43,0x0A};
    expectBytes("Y100 freq 435.500 MHz (LE BCD)", b, n, w, sizeof(w)); }
  n = legBuildModeFrame(LEGF_Y100, 0, RM_FM, b, sizeof(b));
  { const uint8_t w[] = {0x00,0x00,0x00,0x06,0x0C};      // FM = 06, in data[3]
    expectBytes("Y100 mode FM (06 in data[3])", b, n, w, sizeof(w)); }
  n = legBuildModeFrame(LEGF_Y100, 0, RM_USB, b, sizeof(b));
  { const uint8_t w[] = {0x00,0x00,0x00,0x01,0x0C};
    expectBytes("Y100 mode USB", b, n, w, sizeof(w)); }
  n = legBuildReadFreqFrame(LEGF_Y100, 0, b, sizeof(b));
  { const uint8_t w[] = {0x00,0x00,0x00,0x00,0x10};
    expectBytes("Y100 read query (0x10)", b, n, w, sizeof(w)); }
  { // status block: band_no, freq[4] LE BCD, mode, ...
    const uint8_t rx[] = {0x02, 0x00,0x00,0x55,0x43, 0x01, 0x00};
    bool ok = legParseFreqReply(LEGF_Y100, 0, rx, sizeof(rx), hz);
    expectHz("Y100 parse (freq at offset 1)", ok, hz, true, 435500000ULL); }

  // ---- VR-5000 (LEGF_YVR5): FT-817 framing, different FM ---------------------
  // Hamlib vr5000.c maps RIG_MODE_FM to MODE_FMN = 0x88; plain 0x08 is not in this
  // receiver's table. It also has NO frequency read command (get_freq is answered
  // from Hamlib's own cache), which is why its catalog row sets canRead = false.
  n = legBuildFreqFrame(LEGF_YVR5, 0, 145900000ULL, b, sizeof(b));
  { const uint8_t w[] = {0x14,0x59,0x00,0x00,0x01};
    expectBytes("YVR5 freq (same as FT-817)", b, n, w, sizeof(w)); }
  n = legBuildModeFrame(LEGF_YVR5, 0, RM_FM, b, sizeof(b));
  { const uint8_t w[] = {0x88,0x00,0x00,0x00,0x07};
    expectBytes("YVR5 mode FM = 0x88 (not 0x08)", b, n, w, sizeof(w)); }
  n = legBuildModeFrame(LEGF_YVR5, 0, RM_USB, b, sizeof(b));
  { const uint8_t w[] = {0x01,0x00,0x00,0x00,0x07};
    expectBytes("YVR5 mode USB", b, n, w, sizeof(w)); }

  // ---- Yaesu ASCII (FT-991A): FA/MD ----
  n = legBuildFreqFrame(LEGF_YTXT, 0, 435500000ULL, b, sizeof(b));
  expectStr("YTXT freq", b, n, "FA435500000;");
  n = legBuildModeFrame(LEGF_YTXT, 0, RM_CW, b, sizeof(b));
  expectStr("YTXT mode CW", b, n, "MD03;");
  n = legBuildReadFreqFrame(LEGF_YTXT, 0, b, sizeof(b));
  expectStr("YTXT read query", b, n, "FA;");
  { const char* rx = "FA;FA435500000;";                // echo then answer
    bool ok = legParseFreqReply(LEGF_YTXT, 0, (const uint8_t*)rx, strlen(rx), hz);
    expectHz("YTXT parse (with echo)", ok, hz, true, 435500000ULL); }

  // ---- Kenwood all-mode BASE stations, TS-711/TS-811 (LEGF_KWTS) -----------
  // Generic Kenwood ASCII CAT -- the same encoding this firmware already uses for
  // the TS-790/TS-2000 as a full-duplex rig, verified against Hamlib's kenwood.c:
  // eleven frequency digits, and the mode table 1 LSB, 2 USB, 3 CW, 4 FM, 5 AM,
  // 6 FSK. A TS-711 + TS-811 pair is the classic two-radio all-mode satellite
  // station, which is exactly the case native dual-rig exists for.
  n = legBuildFreqFrame(LEGF_KWTS, 0, 145900000ULL, b, sizeof(b));
  expectStr("KWTS freq (11 digits)", b, n, "FA00145900000;");
  n = legBuildFreqFrame(LEGF_KWTS, 0, 435500000ULL, b, sizeof(b));
  expectStr("KWTS freq 435.5 MHz", b, n, "FA00435500000;");
  n = legBuildModeFrame(LEGF_KWTS, 0, RM_USB, b, sizeof(b));
  expectStr("KWTS mode USB", b, n, "MD2;");
  n = legBuildModeFrame(LEGF_KWTS, 0, RM_LSB, b, sizeof(b));
  expectStr("KWTS mode LSB", b, n, "MD1;");
  n = legBuildModeFrame(LEGF_KWTS, 0, RM_FM, b, sizeof(b));
  expectStr("KWTS mode FM", b, n, "MD4;");
  n = legBuildReadFreqFrame(LEGF_KWTS, 0, b, sizeof(b));
  expectStr("KWTS read query", b, n, "FA;");
  { const char* rx = "FA;FA00145900000;";          // echo then answer
    bool ok = legParseFreqReply(LEGF_KWTS, 0, (const uint8_t*)rx, strlen(rx), hz);
    expectHz("KWTS parse (11 digits)", ok, hz, true, 145900000ULL); }
  { // The 9-digit Yaesu form must NOT be accepted as a Kenwood answer.
    const char* rx = "FA145900000;";
    bool ok = legParseFreqReply(LEGF_KWTS, 0, (const uint8_t*)rx, strlen(rx), hz);
    expectHz("KWTS rejects a 9-digit reply", ok, 0, false, 0); }

  // ---- Kenwood TH-D74/D75 (Band B = VFO B): FO / MD ------------------------
  // Protocol reference: Hamlib rigs/kenwood/thd74.c, the reference implementation
  // for this family. THREE things were wrong before 0.9.70 and the radio ignored
  // everything: the command is FO (there is no FQ), "MD" takes a SPACE before its
  // parameters, and AM/DV were transposed in the mode map. A set is a
  // read-modify-write of the whole FO record -- there is no set-frequency command.
  n = legBuildReadFreqFrame(LEGF_KWHT, 0, b, sizeof(b));
  expectStr("KWHT read query (FO)", b, n, "FO 1\r");
  n = legBuildFreqFrame(LEGF_KWHT, 0, 145825000ULL, b, sizeof(b));
  expectHz("KWHT has no single-frame freq set", n == 0, 0, true, 0);
  n = legBuildModeFrame(LEGF_KWHT, 0, RM_USB, b, sizeof(b));
  expectStr("KWHT mode USB", b, n, "MD 1,4\r");
  n = legBuildModeFrame(LEGF_KWHT, 0, RM_AM, b, sizeof(b));
  expectStr("KWHT mode AM (was DV!)", b, n, "MD 1,1\r");
  n = legBuildModeFrame(LEGF_KWHT, 0, RM_DATA, b, sizeof(b));
  expectStr("KWHT mode DV", b, n, "MD 1,2\r");
  n = legBuildModeFrame(LEGF_KWHT, 0, RM_FM, b, sizeof(b));
  expectStr("KWHT mode FM", b, n, "MD 1,0\r");
  { // A representative FO record; the frequency is the ten digits at offset 5.
    const char* rec = "FO 1,0145825000,0,0,0,0,0,0,00600000,0,0,0,000,0";
    bool ok = legParseFreqReply(LEGF_KWHT, 0, (const uint8_t*)rec, strlen(rec), hz);
    expectHz("KWHT parse FO record", ok, hz, true, 145825000ULL);
    // Patch to 435.100 MHz: only those ten digits change, the rest is echoed back.
    uint8_t out[96];
    size_t on = legKwFoPatch((const uint8_t*)rec, strlen(rec), 435100000ULL, out, sizeof(out));
    expectStr("KWHT FO patch (RMW set)", out, on,
              "FO 1,0435100000,0,0,0,0,0,0,00600000,0,0,0,000,0\r"); }
  { // With the query echoed back first, the LAST record must win.
    const char* rx = "FO 1\rFO 1,0145825000,0,0,0,0,0,0,00600000,0,0,0,000,0\r";
    uint8_t out[96];
    size_t on = legKwFoPatch((const uint8_t*)rx, strlen(rx), 435100000ULL, out, sizeof(out));
    expectStr("KWHT FO patch (skips echo)", out, on,
              "FO 1,0435100000,0,0,0,0,0,0,00600000,0,0,0,000,0\r"); }
  { // Garbage in, nothing out -- never send a half-patched record to a radio.
    uint8_t out[96];
    size_t on = legKwFoPatch((const uint8_t*)"?\r", 2, 435100000ULL, out, sizeof(out));
    expectHz("KWHT FO patch refuses junk", on == 0, 0, true, 0); }

  // ---- capacity guards ----
  n = legBuildFreqFrame(LEGF_CIV, 0xA4, 145900000ULL, b, 10);
  expectHz("CIV freq cap-too-small returns 0", n == 0, 0, true, 0);

  printf(fails ? "\n%d FAILURE(S)\n" : "\nall dual-rig frame vectors pass\n", fails);
  return fails ? 1 : 0;
}
