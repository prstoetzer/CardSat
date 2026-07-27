#!/usr/bin/env bash
# Host-side AO-7 mode-switch estimator test. Compiles the standalone estimator (mirrored
# from App::ao7Estimate's inverse-span-weighted grid search over a 12-30h domain range)
# and asserts it recovers the timer PERIOD and current MODE from a synthetic clean
# free-running-timer dataset (period 24.05h). Passes when >=11/12 now-samples recover the
# mode -- the one expected miss is a now within ~1h of a switch, where the mode is
# genuinely ambiguous and the tool flags it "near a switch". See ao7_realdata_test.sh for
# the grid search's real payoff: a messy real dataset where the OLD fixed-24h-guess
# iterative fit aliased to 340 min RMS, and the grid search recovers 19.5h at 48 min RMS.
set -e
cd "$(dirname "$0")"
g++ -O2 -o /tmp/ao7_estimate_test ao7_estimate_test.cpp
/tmp/ao7_estimate_test | tee /tmp/ao7_out.txt
pass=$(grep -oE '== [0-9]+/12 pass ==' /tmp/ao7_out.txt | grep -oE '[0-9]+' | head -1)
if [ "${pass:-0}" -ge 11 ]; then echo "ao7 estimator: $pass/12 PASS (period+mode recovered)"; else echo "FAIL: only $pass/12"; exit 1; fi
