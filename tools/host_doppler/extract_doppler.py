#!/usr/bin/env python3
"""Extract the Doppler / passband math from src/predict.cpp for host testing.

The four functions under test are static members of Predictor with no I/O and no
hardware dependency, so they lift out cleanly. Extracting rather than copying is
the point: the harness tests the code that ships.
"""
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC  = os.path.join(ROOT, 'src', 'predict.cpp')
OUT  = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'doppler_region.inc')

WANT = ['void Predictor::dopplerFreqs(',
        'freq_t Predictor::uplinkForFixedDownlink(',
        'freq_t Predictor::downlinkForFixedUplink(',
        'void Predictor::passbandFreqs(']

def grab(src, sig):
    i = src.index(sig)
    j = src.index('{', i)
    d, k = 0, j
    while k < len(src):
        if src[k] == '{': d += 1
        elif src[k] == '}':
            d -= 1
            if d == 0: break
        k += 1
    return src[i:k+1]

def main():
    src = open(SRC, encoding='utf-8').read()
    parts = []
    for sig in WANT:
        blk = grab(src, sig).replace('Predictor::', '')
        parts.append(blk)
    open(OUT, 'w', encoding='utf-8').write(
        '// AUTO-EXTRACTED from src/predict.cpp by extract_doppler.py -- do not edit.\n'
        + '\n\n'.join(parts) + '\n')
    print('extract_doppler: wrote 4 functions')
    return 0

if __name__ == '__main__':
    sys.exit(main())
