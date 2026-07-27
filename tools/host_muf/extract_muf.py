#!/usr/bin/env python3
"""Extract the minimufMHz() function from src/app.cpp for host testing.

Like tools/host_basic, this pulls the model from the LIVE source at build time so the
test always exercises the code that ships, never a copy that can drift. minimufMHz is a
file-scope static with no App/Arduino dependencies, so it compiles standalone.

Emits muf_region.inc next to the harness.
"""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(ROOT, 'src', 'app.cpp')

def main():
    src = open(APP, encoding='utf-8').read()
    a = src.find('static double minimufMHz(')
    if a < 0:
        print('extract_muf: minimufMHz not found', file=sys.stderr); return 1
    # brace-match to the closing } of the function
    i = src.find('{', a); depth = 0
    while i < len(src):
        if src[i] == '{': depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0: break
        i += 1
    fn = src[a:i+1]
    open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'muf_region.inc'),
         'w', encoding='utf-8').write(fn)
    print('extract_muf: wrote %d lines' % fn.count('\n'))
    return 0

if __name__ == '__main__':
    sys.exit(main())
