#!/usr/bin/env bash
# Run EVERY shipped BASIC example through the real interpreter.
#
# The examples are documentation people paste into a device, so a broken one is a
# broken instruction. This harness extracts the VM from src/app.cpp (so the dialect
# under test is the shipped dialect), supplies stub host hooks, and fails if any
# program reports a parse or run error. It found TWO real problems on its first run:
# a reference to TXOK, which the firmware set but never exposed as a readable name,
# and two stale rules in the examples README (':' does chain statements; THEN does
# accept any statement).
#
# What it cannot check: whether the PICTURE is right. Graphics calls are accepted and
# discarded, so a program that draws nonsense passes here. Eyes on hardware for that.
set -e
cd "$(dirname "$0")"
python3 ../host_basic/extract_vm.py
g++ -O1 -I../host_basic -o /tmp/cardsat_examples_test examples_test.cpp
/tmp/cardsat_examples_test ../../examples/basic/*.BAS
