#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
python3 extract_zones.py
g++ -O2 -Wall -o /tmp/zones_verify zones_verify.cpp
/tmp/zones_verify
