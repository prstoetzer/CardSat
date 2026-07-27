#!/usr/bin/env python3
"""Extract the orbital-zone membership math (lShellAt + zoneContains) from src/app.cpp for
host testing. Both are App methods, so we lift their bodies into free functions that the
harness can call, keeping the tested code the code that ships (same idea as host_basic and
host_muf). We rewrite the two method signatures and drop the App:: qualifier and the
timeIsSet()/nowUtc() branch (the harness passes an explicit drift year instead)."""
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(ROOT, 'src', 'app.cpp')
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'zones_region.inc')

def grab(src, sig):
    a = src.find(sig)
    if a < 0: raise SystemExit('extract_zones: not found: ' + sig)
    i = src.find('{', a); depth = 0
    while i < len(src):
        if src[i] == '{': depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0: return src[a:i+1]
        i += 1
    raise SystemExit('extract_zones: unbalanced: ' + sig)

def main():
    src = open(APP, encoding='utf-8').read()
    lshell = grab(src, 'double App::lShellAt(')
    zc     = grab(src, 'bool App::zoneContains(')
    # De-methodise: drop App::.
    lshell = lshell.replace('double App::lShellAt(', 'double lShellAt(')
    zc     = zc.replace('bool App::zoneContains(', 'bool zoneContains(')
    # Replace the ZONE_* enum names with literals so we don't need app.h.
    for name, val in [('ZONE_SAA','0'),('ZONE_ECLIPSE','1'),('ZONE_POLAR','2'),
                      ('ZONE_INNER','3'),('ZONE_OUTER','4')]:
        zc = zc.replace(name, val)
    # Replace the timeIsSet()/nowUtc() drift block with a fixed test hook: the harness
    # sets a global g_testYears; substitute the whole guarded assignment.
    zc = re.sub(r'double yrs = 0;\s*\n\s*if \(timeIsSet\(\).*?2025\.0; \}',
                'double yrs = g_testYears;', zc, flags=re.S)
    open(OUT, 'w', encoding='utf-8').write(
        '// AUTO-EXTRACTED from src/app.cpp by extract_zones.py -- do not edit.\n'
        'static double g_testYears = 0.0;\n\n' + lshell + '\n\n' + zc + '\n')
    print('extract_zones: wrote lShellAt + zoneContains')
    return 0

if __name__ == '__main__':
    sys.exit(main())
