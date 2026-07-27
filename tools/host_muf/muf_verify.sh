#!/usr/bin/env bash
# Build and run the MINIMUF-3.5 host verification against the LIVE model in src/app.cpp.
# Mirrors tools/host_basic and tools/host_aprs: self-contained, no device, non-zero exit
# on failure.
set -e
cd "$(dirname "$0")"
python3 extract_muf.py
g++ -O2 -Wall -o /tmp/muf_verify muf_verify.cpp
/tmp/muf_verify
