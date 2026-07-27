#!/usr/bin/env bash
# Build and run the Tiny BASIC host harness against the LIVE VM in src/app.cpp.
# Mirrors tools/host_aprs: self-contained, no device, non-zero exit on failure.
# Asserts that adversarial inputs (OOB-write via non-alpha LET/FOR var, OOB-read via a
# bare function keyword, and unbounded expression recursion) all yield clean errors,
# and that a set of legitimate programs still produce the right output.
set -e
cd "$(dirname "$0")"
python3 extract_vm.py
g++ -O1 -fsanitize=address -o /tmp/basic_fuzz_test basic_fuzz_test.cpp
/tmp/basic_fuzz_test
