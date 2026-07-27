#!/usr/bin/env bash
# Host-side test for the two AO-7 estimator improvements: (1) preferring a recency-
# limited subset fit over the full-window fit when it's substantially tighter, and
# (2) the illumination-window cutoff that filters observations from before the
# satellite's most recent continuous-full-sun start. Mirrors the fitRange() logic and
# the startIdx filter from App::ao7Estimate verbatim.
set -e
cd "$(dirname "$0")"
g++ -O2 -o /tmp/ao7_recency_test ao7_recency_test.cpp
/tmp/ao7_recency_test | tee /tmp/ao7_recency_out.txt
grep -q "TEST1 consistent-data:.*PASS" /tmp/ao7_recency_out.txt || { echo "FAIL test1 (no false recency trigger on a perfect fit)"; exit 1; }
grep -q "TEST2 contaminated-old:.*PASS" /tmp/ao7_recency_out.txt || { echo "FAIL test2 (recency preference)"; exit 1; }
grep -q "recovered period from recent fit:.*PASS" /tmp/ao7_recency_out.txt || { echo "FAIL test2 (period recovery)"; exit 1; }
grep -q "TEST3 illumination-filter:.*PASS" /tmp/ao7_recency_out.txt || { echo "FAIL test3 (illumination-window filter)"; exit 1; }
echo "ao7 recency + illumination-filter: all tests PASS"
