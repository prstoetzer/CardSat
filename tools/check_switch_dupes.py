#!/usr/bin/env python3
"""Duplicate-case gate: no switch may use the same case value twice.

Why this exists. The 0.9.58 rotator work added a "Rot wire" settings row and gave
it ID 48 -- which cfg.bright already owned. `error: duplicate case value`, and the
build died on the bench. Every gate passed: braces balanced, parity matched (both
representations were identically wrong), and the compile gate does not cover
app.cpp -- which is 30k lines and pulls in the entire M5/WiFi/SGP4 world, so
stubbing it is not realistic.

This gate does not need a compiler. It parses each `switch (...) { ... }` body and
checks that no case value repeats WITHIN that switch -- which is exactly what the
compiler complains about, and is a pure text property. Nested switches are handled
by brace depth: a case belongs to the innermost switch enclosing it.

Run from the repo root. Checks src/*.cpp and the .ino.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def switch_bodies(s):
    """Yield (start, end, body) for each switch, innermost-aware."""
    for m in re.finditer(r'\bswitch\s*\(', s):
        i = m.start()
        j = s.find('{', i)
        if j < 0:
            continue
        depth = 0
        k = j
        while k < len(s):
            if s[k] == '{':
                depth += 1
            elif s[k] == '}':
                depth -= 1
                if depth == 0:
                    break
            k += 1
        yield i, k, s[j:k]

def cases_at_top_level(body):
    """Case labels belonging to THIS switch (depth 1), not a nested one."""
    out = []
    depth = 0
    for m in re.finditer(r'[{}]|case\s+([0-9]+)\s*:', body):
        t = m.group(0)
        if t == '{':
            depth += 1
        elif t == '}':
            depth -= 1
        elif depth == 1:
            out.append((int(m.group(1)), m.start()))
    return out

def line_of(s, pos):
    return s.count('\n', 0, pos) + 1

bad = 0
files = [os.path.join(ROOT, 'CardSat.ino')]
srcd = os.path.join(ROOT, 'src')
if os.path.isdir(srcd):
    files += [os.path.join(srcd, f) for f in sorted(os.listdir(srcd)) if f.endswith('.cpp')]

for path in files:
    if not os.path.isfile(path):
        continue
    s = open(path, encoding='utf-8', errors='replace').read()
    for si, se, body in switch_bodies(s):
        seen = {}
        for val, off in cases_at_top_level(body):
            if val in seen:
                print(f"  DUPLICATE case {val} in switch at "
                      f"{os.path.basename(path)}:{line_of(s, si)} "
                      f"(lines {line_of(s, si + seen[val])} and {line_of(s, si + off)})")
                bad += 1
            else:
                seen[val] = off

if bad:
    print(f"  switch gate: FAILED -- {bad} duplicate case value(s).")
    sys.exit(1)
print("  switch gate: no duplicate case values.")
