#!/usr/bin/env bash
# Verify the Doppler / passband math against physics, not against itself. The code
# under test is extracted from src/predict.cpp at build time.
set -e
cd "$(dirname "$0")"
python3 extract_doppler.py
g++ -O2 -Wall -o /tmp/doppler_test doppler_test.cpp -lm
/tmp/doppler_test
