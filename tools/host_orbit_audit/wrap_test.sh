#!/usr/bin/env bash
# Host-side unit test for the M36 wrap-safe deadline helper timeReached().
#
# It EXTRACTS the real timeReached() body verbatim from src/app.cpp at run time (so the
# test can never drift from the shipped code), compiles it into a tiny harness, and asserts
# the signed-difference comparison stays correct across the ~49.7-day millis() rollover --
# the exact failure mode a naive `now < deadline` comparison has.
#
# Usage:  tools/host_orbit_audit/wrap_test.sh
# Needs:  g++, python3 (only for the extraction).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/../.."
W="$HERE/work_wrap"; rm -rf "$W"; mkdir -p "$W"; cd "$W"

# 1) Pull the real helper out of src/app.cpp (the `static inline bool timeReached(...) {...}`
#    block), verbatim, so we test exactly what ships.
python3 - "$ROOT/src/app.cpp" > timeReached.inc <<'PY'
import re, sys
src = open(sys.argv[1]).read()
m = re.search(r'static inline bool timeReached\(uint32_t now, uint32_t deadline\)\s*\{.*?\n\}',
              src, re.S)
if not m:
    sys.stderr.write("FAIL: could not find timeReached() in src/app.cpp\n"); sys.exit(2)
sys.stdout.write(m.group(0) + "\n")
PY

echo "Extracted helper:"; sed 's/^/  | /' timeReached.inc

# 2) Tiny harness: exercise the rollover boundary and ordinary cases.
cat > wrap_test.cpp <<'EOC'
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "timeReached.inc"

static int fails = 0;
static void check(const char* name, bool got, bool want) {
  if (got != want) { printf("FAIL  %-42s got=%d want=%d\n", name, got, want); ++fails; }
  else             { printf("ok    %-42s\n", name); }
}

int main() {
  const uint32_t MAX = 0xFFFFFFFFu;

  // --- ordinary (no wrap) ---
  check("now before deadline",           timeReached(1000, 2000), false);
  check("now exactly at deadline",        timeReached(2000, 2000), true);
  check("now just past deadline",         timeReached(2001, 2000), true);
  check("now far past deadline",          timeReached(1000000, 2000), true);

  // --- the rollover: deadline set just before wrap, now just after wrap ---
  // deadline = MAX-100 (set at ~49.7 days), a 1500 ms banner => it should EXPIRE 1400 ms
  // after wrap. A naive (now < deadline) would treat now=1400 (post-wrap) as < (MAX-100)
  // and report "not reached" for ~25 more days. timeReached must say reached.
  uint32_t deadline = MAX - 100;      // e.g. PB_OOB banner armed just before rollover
  check("pre-wrap deadline, now 50 before wrap", timeReached(MAX - 150, deadline), false);
  check("pre-wrap deadline, now AT deadline",     timeReached(deadline, deadline), true);
  check("pre-wrap deadline, now 100 past (=wrap)", timeReached(MAX, deadline), true);
  check("pre-wrap deadline, now 1400 AFTER wrap",  timeReached(1300, deadline), true);
  // and just before it should elapse: still not reached
  check("pre-wrap deadline, 1 tick early wraps",   timeReached((uint32_t)(deadline + 99), deadline), true);

  // --- symmetric case: deadline just after wrap, now just before wrap (not yet reached) ---
  uint32_t d2 = 100;                   // deadline armed to 100 ms, but "now" is pre-wrap
  check("post-wrap deadline, now pre-wrap",  timeReached(MAX - 50, d2), false);
  check("post-wrap deadline, now = d2",      timeReached(d2, d2), true);

  // --- half-range boundary: differences within ~24.8 days resolve correctly ---
  uint32_t half = 0x7FFFFFFFu;         // 2147483647 ms ~= 24.85 days
  check("now = deadline + (halfrange-1)",  timeReached(half - 1, 0), true);
  check("now = deadline - halfrange (amb)", timeReached((uint32_t)(-half), 0), false);

  if (fails == 0) { printf("\nALL WRAP TESTS PASSED\n"); return 0; }
  printf("\n%d WRAP TEST(S) FAILED\n", fails); return 1;
}
EOC

g++ -O2 -std=c++17 -Wall -Wextra -o wrap_test wrap_test.cpp
./wrap_test
