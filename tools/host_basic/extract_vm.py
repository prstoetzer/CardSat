#!/usr/bin/env python3
"""Extract the Tiny BASIC VM region from src/app.cpp for host testing.

The VM has no header and shares no state with the app, so it can be compiled and
driven on the host WITHOUT a device -- the same trick tools/host_aprs uses for the
APRS decoder, but pulled from the LIVE SOURCE at build time rather than copied. A
copy would drift (host_aprs's already has); extracting means the test always exercises
the code that actually ships.

Emits vm_region.inc next to the harness: the file-scope bounds constants plus the
anonymous namespace holding BasicSys / BasicVM / basicParse, with esp_random() mapped
to rand() for the host.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(ROOT, 'src', 'app.cpp')


def main():
    src = open(APP, encoding='utf-8').read()
    b0 = src.find('static const int  BASIC_MAX_LINES')
    b1 = src.find('static const int  BASIC_OUT_MAX', b0)
    if b0 < 0 or b1 < 0:
        print('extract_vm: BASIC bounds not found', file=sys.stderr)
        return 1
    b1 = src.find('\n', b1) + 1
    bounds = src[b0:b1]

    n0 = src.find('namespace {', b1)
    if n0 < 0:
        print('extract_vm: VM namespace not found', file=sys.stderr)
        return 1
    i = src.find('{', n0)
    depth = 0
    while i < len(src):
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                break
        i += 1
    region = bounds + '\n' + src[n0:i + 1]
    region = region.replace('esp_random()', '(unsigned)rand()')

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vm_region.inc')
    open(out, 'w', encoding='utf-8').write(region)
    print(f'extract_vm: wrote {region.count(chr(10))} lines to {os.path.basename(out)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
