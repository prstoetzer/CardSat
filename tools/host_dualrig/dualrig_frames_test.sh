#!/usr/bin/env bash
# Build and run the dual-rig leg CAT encoder vectors. Unlike the other harnesses,
# this one does NOT carry a copy of the code under test: it extracts the pure
# builder block straight out of src/rig.cpp at build time, so the bytes verified
# here are the bytes the firmware sends -- one source of truth, zero drift.
set -e
cd "$(dirname "$0")"
awk '/^static void legCivPackFreq/{f=1} /^\/\/  PlainCatRig$/{exit} f' \
    ../../src/rig.cpp > /tmp/leg_builders.inc
g++ -O2 -Wall -o /tmp/dualrig_frames_test dualrig_frames_test.cpp
/tmp/dualrig_frames_test
