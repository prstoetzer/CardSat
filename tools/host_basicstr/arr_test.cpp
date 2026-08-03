// Array budget and bounds semantics for CardSat BASIC.
//
// Two things here are safety, not style. A subscript outside the array must STOP the
// program: BASIC's traditional silent out-of-range write is not survivable on a device
// that is simultaneously tuning a radio and driving a USB host. And the element budget
// is TOTAL across all arrays, not per array -- 26 separate 1024-element arrays would be
// 208 KB of doubles on a device with ~76 KB of free heap, so a per-array limit would let
// a program that looks reasonable line by line starve the whole firmware.
#include <cstdio>
#include <cstring>

static const int ARR_TOTAL_MAX = 2048;
static int narrN[26];
static bool alloc[26];
static int narrTotal = 0;
static const char* err = nullptr;

static bool dimArray(int idx, int n) {
  err = nullptr;
  if (idx < 0 || idx > 25) { err = "array A-Z"; return false; }
  if (n < 1 || n > 1024)   { err = "size 1..1024"; return false; }
  int after = narrTotal - narrN[idx] + n;
  if (after > ARR_TOTAL_MAX) { err = "arrays too big (2048 total)"; return false; }
  narrTotal = after; narrN[idx] = n; alloc[idx] = true;
  return true;
}
static bool cellOk(int idx, double dv) {
  err = nullptr;
  if (idx < 0 || idx > 25 || !alloc[idx]) { err = "array not DIMmed"; return false; }
  int i = (int)dv;
  if (i < 0 || i >= narrN[idx]) { err = "subscript out of range"; return false; }
  return true;
}

static int fails = 0;
static void chk(bool cond, const char* what) {
  printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  if (!cond) fails++;
}

int main() {
  memset(narrN, 0, sizeof narrN); memset(alloc, 0, sizeof alloc);
  chk(dimArray(0, 10),                  "DIM A(10)");
  chk(cellOk(0, 0),                     "A(0) in range (0-based)");
  chk(cellOk(0, 9),                     "A(9) last element");
  chk(!cellOk(0, 10) && err,            "A(10) refused, program stops");
  chk(!cellOk(0, -1) && err,            "A(-1) refused");
  chk(!cellOk(1, 0)  && err,            "B(0) without DIM refused");
  chk(dimArray(1, 1024),                "DIM B(1024) at the per-array cap");
  chk(!dimArray(2, 1024) && err,        "third big array refused: TOTAL budget");
  chk(narrTotal == 1034,                "budget accounts 10 + 1024");
  chk(dimArray(0, 5),                   "re-DIM A(5) allowed");
  chk(narrTotal == 1029,                "re-DIM releases the old size, not adds");
  chk(!dimArray(3, 0) && err,           "DIM C(0) refused");
  chk(!dimArray(3, 2000) && err,        "DIM C(2000) over per-array cap refused");
  printf(fails ? "\n%d FAILURE(S)\n" : "\nall array budget/bounds vectors pass\n", fails);
  return fails ? 1 : 0;
}
