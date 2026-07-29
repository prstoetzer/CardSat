#!/usr/bin/env python3
"""Extract the orbital-decay estimator from src/app.cpp for host testing.

Lifts expAtmosphere, the Bessel/scale-height helpers, the calibration constants
and estimateDecayDays into free functions the harness can call, so the code under
test is the code that ships (same idea as host_dualrig and host_geomag).
"""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP  = os.path.join(ROOT, 'src', 'app.cpp')
OUT  = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'decay_region.inc')

def main():
    src = open(APP, encoding='utf-8').read()
    a = src.index('static double expAtmosphere(double hkm)')
    b = src.index('static String fmtDecay(double days)')
    blk = src[a:b]
    # solarDensityScale() references the SOLAR_* enum; the harness defines it.
    open(OUT, 'w', encoding='utf-8').write(
        '// AUTO-EXTRACTED from src/app.cpp by extract_decay.py -- do not edit.\n' + blk)
    print('extract_decay: wrote expAtmosphere + estimateDecayDays')
    return 0

if __name__ == '__main__':
    sys.exit(main())
