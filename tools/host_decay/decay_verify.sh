#!/usr/bin/env bash
# Score CardSat's orbital-decay estimator against real re-entries. The code under
# test is extracted from src/app.cpp at build time, so this cannot drift from the
# firmware. Cases are catalogued objects that ACTUALLY re-entered (Space-Track TIP
# decay epochs + gp_history element sets), plus eccentric-orbit sanity cases.
set -e
cd "$(dirname "$0")"
python3 extract_decay.py
g++ -O2 -Wall -Wno-unused-function -o /tmp/decay_verify decay_verify.cpp -lm
/tmp/decay_verify
