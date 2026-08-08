#!/usr/bin/env python3
"""CSUH parity gate: the shared protocol header must be byte-identical on both ends.

src/csuh_proto.h and companion/CardSatUsbHelper/csuh_proto.h are ONE file kept in
two places, because CardSat and the companion are separate Arduino sketches with no
shared include path. If they drift, the failure is not a build error: both sides
compile, frames decode cleanly, and the fields mean different things. A payload
offset that moved by one byte would present as a radio that tunes to the wrong
frequency -- which nobody would trace back to a header.

Also checks the two constants the host-test shim duplicates
(tools/host_usbhelper/shim/config.h), for the same reason at lower stakes: a Grove
pin that changes in config.h and not in the shim makes the link test pass against
hardware the firmware does not drive.

Exit status is 0 when everything matches, 1 otherwise, so it can gate a release.
"""
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
A = os.path.join(ROOT, 'src', 'csuh_proto.h')
B = os.path.join(ROOT, 'companion', 'CardSatUsbHelper', 'csuh_proto.h')

fail = 0


def read(p):
    with open(p, 'rb') as f:
        return f.read()


# ---- 1. the shared header ------------------------------------------------
if not os.path.exists(A) or not os.path.exists(B):
    print('MISSING: one side of csuh_proto.h is absent')
    print(f'  src:       {"ok" if os.path.exists(A) else "MISSING"}  {A}')
    print(f'  companion: {"ok" if os.path.exists(B) else "MISSING"}  {B}')
    sys.exit(1)

a, b = read(A), read(B)
ha, hb = hashlib.md5(a).hexdigest(), hashlib.md5(b).hexdigest()
if a == b:
    print(f'csuh_proto.h: identical on both sides (md5 {ha})')
else:
    fail = 1
    print('DRIFT: csuh_proto.h differs between CardSat and the companion')
    print(f'  src/csuh_proto.h                        md5 {ha}  {len(a)} bytes')
    print(f'  companion/CardSatUsbHelper/csuh_proto.h md5 {hb}  {len(b)} bytes')
    la = a.decode('utf-8', 'replace').splitlines()
    lb = b.decode('utf-8', 'replace').splitlines()
    import difflib
    shown = 0
    for line in difflib.unified_diff(la, lb, 'src', 'companion', lineterm='', n=1):
        print('  ' + line)
        shown += 1
        if shown > 60:
            print('  ... (truncated)')
            break
    print('\n  FIX: cp src/csuh_proto.h companion/CardSatUsbHelper/csuh_proto.h')
    print('  src/ is the source of truth; the companion copy is generated from it.')

# ---- 2. protocol version must be declared, and the two ends must agree ----
m = re.search(r'#define\s+CSUH_PROTO_VER\s+(\d+)', a.decode('utf-8', 'replace'))
if not m:
    fail = 1
    print('MISSING: CSUH_PROTO_VER is not defined in src/csuh_proto.h')
else:
    print(f'  protocol version: v{m.group(1)}')

# ---- 3. the host-test shim's duplicated constants -------------------------
SHIM = os.path.join(ROOT, 'tools', 'host_usbhelper', 'shim', 'config.h')
REAL = os.path.join(ROOT, 'src', 'config.h')
if os.path.exists(SHIM) and os.path.exists(REAL):
    shim = read(SHIM).decode('utf-8', 'replace')
    real = read(REAL).decode('utf-8', 'replace')
    for name in ('CIV_UART_NUM', 'CIV_RX_PIN', 'CIV_TX_PIN'):
        rs = re.search(rf'{name}\s*=\s*(-?\d+)', real)
        ss = re.search(rf'{name}\s*=\s*(-?\d+)', shim)
        if not rs or not ss:
            fail = 1
            print(f'MISSING: {name} not found in '
                  f'{"src/config.h" if not rs else "the host-test shim"}')
        elif rs.group(1) != ss.group(1):
            fail = 1
            print(f'DRIFT: {name} is {rs.group(1)} in src/config.h '
                  f'but {ss.group(1)} in the host-test shim')
    if not fail:
        print('  host-test shim: Grove UART constants match src/config.h')
else:
    print('  note: host-test shim not present; skipping its constant check')

print('check_csuh_parity: PASS' if not fail else 'check_csuh_parity: FAIL')
sys.exit(fail)
