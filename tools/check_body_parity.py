#!/usr/bin/env python3
"""Body-level src/ <-> CardSat.ino parity.

WHY THIS EXISTS
---------------
Working rule 1 says every change goes into BOTH src/*.{h,cpp} and the monolithic
CardSat.ino, byte-identically. Two existing guards only half-cover it:

  * check_parity.py proves a SIGNATURE exists in the .ino. It cannot see a body
    that drifted, which is the failure that actually bites.
  * dual_edit.py keeps the two in step for edits made THROUGH it, but says nothing
    about drift that is already there.

This closes the remaining gap: every non-trivial CODE line in src/ must appear in
CardSat.ino at least as many times as it appears in src/. It is a line-multiset
check, not a parser, so it is cheap and has no false negatives for whole lines that
went missing -- which is exactly the shape the real drifts took.

It found three genuine divergences on its first run against 0.9.66-wip:
  * app.cpp's Doppler gate was missing `!catToolEngaged` in the .ino, so opening a
    CAT diagnostic tool DID push Doppler to the radio on the flashed build;
  * usbserial.cpp's scanAdapters() was missing its whole temporary-host release
    block in the .ino, leaking the USB host and the console after every scan;
  * one comment block had drifted (cosmetic).

WHAT IT DELIBERATELY IGNORES
----------------------------
Comments, preprocessor lines, and very short lines. The .ino is a concatenation
WITH ADAPTATION, not a byte copy: it carries its own file header, drops per-module
header comments, and legitimately omits include guards, #include lines, header
prototypes whose definitions it inlines, and cross-TU forward declarations. Those
are structural, not drift. Whitespace is normalised because the .ino re-indents
when it inlines namespaces and classes.

Exit 0 = clean, 1 = drift found.
"""
import collections
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INO = os.path.join(ROOT, 'CardSat.ino')

SKIP_PREFIX = ('#include', '#pragma', '#ifndef', '#ifdef', '#endif', '#else',
               '#define CARDSAT_', '//', '*', '/*')
TRIVIAL = {'{', '}', '};', 'else {', 'return;', 'break;', 'continue;',
           'return true;', 'return false;', '} // namespace Store'}
MIN_LEN = 12

# Lines that are structurally allowed to appear fewer times in the .ino: a header
# prototype whose definition the .ino inlines, and a forward declaration that only a
# separate translation unit needs. Keyed by the normalised line text.
ALLOW_FEWER = {
    # Header prototype; the .ino inlines the definition instead.
    'bool ensureDir();',
    # Cross-TU forward declaration; the .ino has the definition earlier in the file.
    'int lotwSatResolveExt(const char* amsat, char out[7]);',
    # Forward declaration only a separate TU needs; the .ino includes M5GFX itself.
    'class M5Canvas;',
    # Same code, wrapped onto one line in src/ and two in the .ino. Verified by hand.
    'static String b64(const uint8_t* data, size_t len) { size_t olen = 0;',
    'void dxDoppFreqs(time_t t, freq_t& myRx, freq_t& myTx,',
    'freq_t& dxRx, freq_t& dxTx);',
}


def strip_comment(s):
    """Drop a trailing // comment, respecting string and char literals.

    The .ino routinely carries a different trailing comment on an identical line of
    code (it was hand-merged), so comparing them would drown the real signal.
    """
    out, i, n = [], 0, len(s)
    quote = None
    while i < n:
        ch = s[i]
        if quote:
            if ch == '\\' and i + 1 < n:
                out.append(ch); out.append(s[i + 1]); i += 2; continue
            if ch == quote:
                quote = None
        elif ch in ('"', "'"):
            quote = ch
        elif ch == '/' and i + 1 < n and s[i + 1] == '/':
            break
        out.append(ch)
        i += 1
    return ''.join(out)


def norm(s):
    return re.sub(r'\s+', ' ', strip_comment(s).strip())


def main():
    if not os.path.exists(INO):
        print('check_body_parity: CardSat.ino not found', file=sys.stderr)
        return 1
    with open(INO, encoding='utf-8') as fh:
        inoc = collections.Counter(norm(l) for l in fh)

    problems = 0
    files = sorted(glob.glob(os.path.join(ROOT, 'src', '*.cpp')) +
                   glob.glob(os.path.join(ROOT, 'src', '*.h')))
    for path in files:
        with open(path, encoding='utf-8') as fh:
            counts = collections.Counter(norm(l) for l in fh)
        missing = []
        for line, n in counts.items():
            if not line or line in TRIVIAL or line in ALLOW_FEWER:
                continue
            if line.startswith(SKIP_PREFIX) or len(line) < MIN_LEN:
                continue
            have = inoc[line]
            # A prototype in src/ is inlined as a DEFINITION in the .ino, so accept
            # `foo(args) {` as satisfying `foo(args);`.
            if line.endswith(');'):
                have += inoc[line[:-1].rstrip() + ' {']
            if have < n:
                missing.append((line, n, have))
        if missing:
            rel = os.path.relpath(path, ROOT)
            print(f'DRIFT {rel}: {len(missing)} code line(s) short in CardSat.ino')
            for line, ns, ni in sorted(missing)[:20]:
                print(f'    src x{ns}  ino x{ni}  |  {line[:100]}')
            if len(missing) > 20:
                print(f'    ... and {len(missing) - 20} more')
            problems += len(missing)

    if problems:
        print(f'\ncheck_body_parity: FAIL -- {problems} line(s) present in src/ but '
              f'not (or not as often) in CardSat.ino')
        return 1
    print('  body parity gate: every src/ code line is present in CardSat.ino.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
