// Host harness for the Tiny BASIC VM. The VM region is extracted from the LIVE
// src/app.cpp by extract_vm.py (see basic_fuzz_test.sh), so this always exercises the
// code that ships -- not a copy that can drift. No device, no network.
//
// Two classes of check:
//   * SECURITY: adversarial inputs that, before the 0.9.66 hardening, caused an
//     out-of-bounds vars[] write (LET/FOR with a non-alpha "variable"), an OOB read +
//     NUL-walk (a bare function keyword like SIN with no arg), or an unbounded
//     expression recursion that stack-overflowed the device. Each must now be a clean
//     error string, never a crash.
//   * REGRESSION: ordinary programs must still run and produce exact output.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <string>
struct String : std::string {
  String() {}
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  bool isEmpty() const { return empty(); }
  const char* c_str() const { return std::string::c_str(); }
};
static String operator+(const String& a, const String& b){ String r(a); r+=b; return r; }
static String operator+(const char* a, const String& b){ String r(a); r+=b; return r; }
static String operator+(const String& a, const char* b){ String r(a); r+=b; return r; }
static void yield() {}
#include "vm_region.inc"


// ---- stub hooks so the example programs can actually run on the host ----------
static bool hSatsel(void*, int idx, double o[14]) {
  if (idx < 0 || idx >= 24) return false;
  bool up = (idx % 5) == 0;                       // a few "up" birds
  o[0] = up ? 120.0 : 40.0;                       // az
  o[1] = up ? 35.0 : -20.0;                       // el
  o[2] = 1200; o[3] = (idx % 2) ? 3.1 : -2.4;     // range, range rate
  o[4] = 51.5 - idx; o[5] = -0.1 + idx; o[6] = 500 + idx * 10;
  o[7] = 1; o[8] = 97; o[9] = 0.001; o[10] = 120; o[11] = 15.2;
  o[12] = 40000 + idx;
  o[13] = (idx % 3 == 0) ? 2 : 0;                 // transponder count
  return true;
}
static bool hTxsel(void*, int idx, double o[5]) {
  if (idx < 0 || idx >= 2) return false;
  o[0] = 145940000; o[1] = 435150000; o[2] = 40000; o[3] = 1; o[4] = 1;
  return true;
}
static bool hLpr(void*, const char*, int) { return true; }
static void hGfx(void*, int, double, double, double, double, double, const char*) {}
static int  hFile(void*, int, const char*, String*) { return 0; }

static int runFile(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) { printf("  cannot open %s\n", path); return 1; }
  std::string prog; char buf[512]; size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) prog.append(buf, n);
  fclose(f);
  BasicVM* vm = new BasicVM();
  String work, perr;
  if (!basicParse(*vm, String(prog), work, perr)) {
    printf("  PARSE ERROR: %s\n", perr.c_str()); delete vm; return 1;
  }
  vm->satselCb = &hSatsel; vm->txselCb = &hTxsel;
  vm->lprCb = &hLpr; vm->gfxCb = &hGfx; vm->fileCb = &hFile;
  // plausible system values so branches are exercised
  // A device WITH a fix and a clock: several programs read MYLAT/UTCH etc., and the
  // interpreter raises "no position"/"no position/clock" for those names when the
  // corresponding flag is false. Leaving them false made two examples look broken
  // when in fact the STUB was incomplete -- the programs were fine.
  vm->sys.posOk = true; vm->sys.timeOk = true;
  vm->sys.myLat = 51.5; vm->sys.myLon = -0.12; vm->sys.myAlt = 30;
  vm->sys.utcH = 14; vm->sys.utcM = 32; vm->sys.utcS = 9;
  vm->sys.utcDay = 30; vm->sys.utcMon = 7; vm->sys.utcYr = 2026;
  vm->sys.sunAz = 210; vm->sys.sunEl = 28; vm->sys.moonAz = 95; vm->sys.moonEl = 12;
  vm->sys.lstHr = 9.4; vm->sys.magDecl = 0.6;
  vm->sys.sfi = 148; vm->sys.kp = 3; vm->sys.aIdx = 12; vm->sys.ssn = 92;
  vm->sys.spwxOk = true; vm->sys.wxOk = true;
  vm->sys.wxTemp = 14; vm->sys.wxWind = 11; vm->sys.wxDir = 240; vm->sys.wxHum = 68;
  vm->sys.satOk = true; vm->sys.satEl = 35; vm->sys.satRR = -2.4;
  vm->sys.satLat = 51.5; vm->sys.satLon = -0.1; vm->sys.satAlt = 560;
  vm->sys.nSat = 24; vm->sys.nTx = 2;
  vm->sys.lshell = 5.49; vm->sys.bratio = 184.0; vm->sys.bfield = 21000;
  vm->sys.inBelt = 0; vm->sys.inSaa = 0;
  vm->sys.decayD = 812; vm->sys.decaySrc = 1;
  vm->sys.batt = 74; vm->sys.battMv = 3910; vm->sys.charging = 0;
  vm->sys.heapFree = 120000; vm->sys.heapBlk = 31000;
  vm->sys.upTime = 5400; vm->sys.gpAge = 1.4;
  vm->run();
  int rc = 0;
  if (!vm->err.isEmpty()) { printf("  RUN ERROR: %s\n", vm->err.c_str()); rc = 1; }
  else {
    std::string o = vm->out;
    if (o.size() > 300) o = o.substr(0, 300) + "...";
    printf("  ok  output: %s\n", o.empty() ? "(graphics only)" : o.c_str());
  }
  delete vm;
  return rc;
}
int main(int argc, char** argv) {
  int bad = 0;
  for (int i = 1; i < argc; ++i) { printf("%s\n", argv[i]); bad += runFile(argv[i]); }
  printf(bad ? "\n%d program(s) FAILED\n" : "\nall programs run\n", bad);
  return bad ? 1 : 0;
}
