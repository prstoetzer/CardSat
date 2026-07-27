#!/usr/bin/env bash
# Build and run the APRS position decoder vectors. Mirrors tools/host_orbit_audit/
# convention: self-contained, no device, no network, non-zero exit on failure.
set -e
cd "$(dirname "$0")"
g++ -O2 -Wall -o /tmp/aprs_parse_test aprs_parse_test.cpp
/tmp/aprs_parse_test
