#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
python3 extract.py
g++ -O2 -Wall -o /tmp/cardsat_semtest sem_test.cpp
/tmp/cardsat_semtest
g++ -O2 -Wall -o /tmp/cardsat_arrtest arr_test.cpp
/tmp/cardsat_arrtest
