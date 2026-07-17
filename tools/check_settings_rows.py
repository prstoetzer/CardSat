#!/usr/bin/env python3
"""Settings-rows gate: the rows[] bound must exceed every index used.

Why this exists. drawSettings() builds its labels into a STACK array of Strings
whose size is a hand-maintained constant:

    const int N = 99;      // must exceed the highest rows[] index used below
    String rows[N];

Adding the USB picker/scan rows at 99, 100 and 101 wrote three String objects
PAST THE END of that array. It surfaced as "the new settings rows have no visible
label" -- but a missing label was the harmless symptom; the real behaviour was
undefined, constructing Strings over whatever followed on the stack.

Nothing structural catches it. Braces balance. Parity matches (both
representations carry the same overflow). check_compiles cannot reach app.cpp.
The compiler cannot either: rows[101] on a String[99] is not a diagnosable
constant-index error once N is a variable, and even as a constant gcc only warns.

The comment said "must exceed the highest index used below" and was wrong the
moment someone added a row -- which is exactly the kind of invariant a human
maintains badly and a script maintains perfectly. So: parse drawSettings(), find
N, find every rows[i], and check.

Also verifies every id in the SET_* menu lists has a matching rows[] assignment --
a menu entry with no row renders as a blank line, which is the other way a row can
be invisible.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def slice_fn(s, sig):
    i = s.find(sig)
    if i < 0:
        return None
    j = s.find('\nvoid App::', i + 10)
    return s[i:j if j > 0 else len(s)]

bad = 0
for rel in ('src/app.cpp', 'CardSat.ino'):
    path = os.path.join(ROOT, rel)
    if not os.path.isfile(path):
        continue
    s = open(path, encoding='utf-8', errors='replace').read()

    body = slice_fn(s, 'void App::drawSettings()')
    if body is None:
        print(f"  settings-rows gate: SKIPPED ({rel}: no drawSettings)")
        continue

    m = re.search(r'const\s+int\s+N\s*=\s*(\d+)\s*;', body)
    if not m:
        print(f"  {rel}: cannot find the rows[] bound N")
        bad += 1
        continue
    N = int(m.group(1))

    idx = sorted(set(int(x) for x in re.findall(r'\browsUnused\b|rows\[(\d+)\]', body) if x))
    if idx and max(idx) >= N:
        over = [i for i in idx if i >= N]
        print(f"  {rel}: rows[] bound N={N} but indices {over} are OUT OF BOUNDS "
              f"(highest used: {max(idx)})")
        bad += 1

    # every menu id must have a row assignment, or it renders blank
    assigned = set(idx)
    for mm in re.finditer(r'static const int SET_([A-Z]+)\[\]\s*=\s*\{([^}]*)\}', s):
        menu, ids = mm.group(1), mm.group(2)
        for tok in ids.split(','):
            tok = tok.strip()
            if not tok.isdigit():
                continue
            if int(tok) not in assigned:
                print(f"  {rel}: SET_{menu} lists row {tok} but rows[{tok}] is never assigned "
                      f"(renders blank)")
                bad += 1

if bad:
    print(f"  settings-rows gate: FAILED -- {bad} problem(s).")
    sys.exit(1)
print("  settings-rows gate: rows[] bound is sufficient; every menu id has a row.")
