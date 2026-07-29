#!/usr/bin/env bash
# Verify the IGRF-14 field evaluator against reference values computed by an
# INDEPENDENT implementation (ppigrf, IGRF14.shc), and check the field-line shell
# geometry that the Van Allen belt zones are built on. Like host_dualrig, the code
# under test is EXTRACTED from src/app.cpp at build time -- no second copy to drift.
set -e
cd "$(dirname "$0")"
{
  sed -n '/^static const uint8_t IGRF_NMAX/,/^};$/p;' ../../src/app.cpp | head -200
} > /dev/null
python3 - <<'PY' > /tmp/igrf_extract.inc
import re
s = open('../../src/app.cpp').read()
i = s.index('static const uint8_t IGRF_NMAX')
j = s.index('// Years past the IGRF epoch')
blk = s[i:j]
blk = blk.replace('void App::igrfField', 'void igrfField')
open('/tmp/igrf_extract.inc','w').write(blk)
PY
g++ -O2 -Wall -o /tmp/geomag_test geomag_test.cpp -lm
/tmp/geomag_test
