#!/usr/bin/env python3
"""Catch blocks inlined TWICE into CardSat.ino.

The .ino is one translation unit built by re-inlining every src/ header and .cpp.
When a src/ file is edited, its .ino copy has to be re-synced -- and a re-sync that
INSERTS instead of REPLACING leaves two copies. Braces stay balanced and every
declaration is still present, so check_balance.py and check_parity.py both pass; the
build only fails later, at link time or on a redefinition, and possibly only when a
feature flag is turned on.

That happened with usbserial: two copies, one of them stale (missing a later fix).
This gate makes it loud.
"""
import re, sys

t = open('CardSat.ino').read()
bad = 0

# 1. No src/ file should be inlined more than once.
for m in set(re.findall(r'inlined from (src/[\w.]+)', t)):
    n = len(re.findall(re.escape(f'inlined from {m}'), t))
    if n > 1:
        print(f"  DUPLICATE: {m} inlined {n}x"); bad += 1

# 2. Definitions that must be unique in a single translation unit.
for sym in ['EspUsbHost           s_host;', 'static App app;']:
    n = t.count(sym)
    if n > 1:
        print(f"  DUPLICATE definition ({n}x): {sym}"); bad += 1

# 3. Every guarded include should appear once.
for inc in re.findall(r'#include <(EspUsbHost|RadioLib|Sgp4)\.h>', t):
    n = len(re.findall(rf'#include <{inc}\.h>', t))
    if n > 1:
        print(f"  DUPLICATE include ({n}x): <{inc}.h>"); bad += 1

print("CardSat.ino: no duplicated blocks." if not bad else f"CardSat.ino: {bad} duplication(s).")
sys.exit(1 if bad else 0)
