#!/usr/bin/env bash
# Regression test pinned to REAL AO-7 report data pulled from the live AMSAT status API
# on 2026-07-24, when the tool's 24h-anchored fit predicted 17:39Z +/- 340 min but the
# actual observed switch (per AMSAT status) was between 02:00 and 05:15Z. Reconstructs
# the exact Heard-only observation set, MAXGAP=20h bracket rule, and 12-30h weighted grid
# search from App::ao7Estimate, and asserts the fix: period recovered near 19.5h (not the
# old wrong 24h assumption), residual well under the old 340-minute failure, and the
# projected current mode matches what was actually observed.
set -e
cd "$(dirname "$0")"
g++ -O2 -o /tmp/ao7_realdata_test ao7_realdata_test.cpp
/tmp/ao7_realdata_test
