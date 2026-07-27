#!/usr/bin/env bash
# Regression test for the AO-7 mode-AGREEMENT fit (Tier 1), pinned to real AMSAT report
# data pulled 2026-07-24 and mirroring App::ao7Estimate's objective, grid, and refinement.
# Runs twice -- positives only, then with horizon-gated "Not Heard" negatives -- and
# asserts both that the fit explains the evidence and that it never places a switch inside
# a stretch where the mode was repeatedly confirmed unchanged (the structural blind spot
# of the previous boundary-midpoint least-squares fit).
set -e
cd "$(dirname "$0")"
g++ -O2 -o /tmp/ao7_agree_test ao7_agree_test.cpp
echo "--- positives only ---";   /tmp/ao7_agree_test --noneg
echo "--- with negatives ---";   /tmp/ao7_agree_test
echo "ao7 agreement fit: PASS"
