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
enum LegFamily : uint8_t { LEGF_CIV, LEGF_YBIN, LEGF_YTXT, LEGF_KWHT };
enum RigMode   : uint8_t { RM_LSB, RM_USB, RM_CW, RM_FM, RM_AM, RM_DATA };

// Declarations the extracted block defines.
size_t legBuildFreqFrame(LegFamily, uint8_t, uint64_t, uint8_t*, size_t);
size_t legBuildModeFrame(LegFamily, uint8_t, RigMode, uint8_t*, size_t);
size_t legBuildReadFreqFrame(LegFamily, uint8_t, uint8_t*, size_t);
bool   legParseFreqReply(LegFamily, uint8_t, const uint8_t*, size_t, uint64_t&);

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

  // ---- Kenwood TH-D74 (Band B): FQ/MD ----
  n = legBuildFreqFrame(LEGF_KWHT, 0, 145825000ULL, b, sizeof(b));
  expectStr("KWHT freq (Band B)", b, n, "FQ1,0145825000\r");
  n = legBuildModeFrame(LEGF_KWHT, 0, RM_USB, b, sizeof(b));
  expectStr("KWHT mode USB", b, n, "MD1,4\r");
  n = legBuildReadFreqFrame(LEGF_KWHT, 0, b, sizeof(b));
  expectStr("KWHT read query", b, n, "FQ1\r");
  { const char* rx = "FQ1,0145825000\r";
    bool ok = legParseFreqReply(LEGF_KWHT, 0, (const uint8_t*)rx, strlen(rx), hz);
    expectHz("KWHT parse reply", ok, hz, true, 145825000ULL); }

  // ---- capacity guards ----
  n = legBuildFreqFrame(LEGF_CIV, 0xA4, 145900000ULL, b, 10);
  expectHz("CIV freq cap-too-small returns 0", n == 0, 0, true, 0);

  printf(fails ? "\n%d FAILURE(S)\n" : "\nall dual-rig frame vectors pass\n", fails);
  return fails ? 1 : 0;
}
