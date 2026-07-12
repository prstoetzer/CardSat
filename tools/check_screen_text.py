#!/usr/bin/env python3
"""On-screen text audit: width budgets + array integrity. Budgets derive from each
block's draw cursor x (chars = (240 - x) // 6). Anchored by line number where names collide."""
import re, sys
src = open('src/app.cpp', encoding='utf-8', errors='replace').read()
lines = src.split('\n')
def block_at(anchor_snippet):
    i = src.index(anchor_snippet); j = src.index('};', i)
    body = src[i:j]
    outs = []
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', body):
        s = m.group(1).replace('\\"','"').replace('\\\\','\\')
        outs.append(s)
    return outs
CHECKS = [
    ("Help hub H[]",        'static const char* H[] = {\n    "GLOBAL"', 39),
    ("Glossary G[]",        'static const char* G[] = {', 39),
    ("User guide U[]",      'static const char* U[] = {', 39),
    ("Tech help T[]",       'static const char* T[] = {', 39),
    # Band plan is a columnar renderer that clips fields by design (band capped at
    # 21 chars in drawBandPlan; notes drawn only when room remains) -- so only the
    # band field is width-checked here.
    ("Band plan (band field)", 'BANDPLAN_STRUCTURED', 21),
    ("Tools names",         'static const char* const TOOLS_NAMES[] = {', 34),
    ("CubeSim ref",         'static const char* const CUBESIM_LINES[] = {', 38),
    ("Fox primer",          'static const char* const FOXTEXT_LINES[] = {', 38),
    ("Sim intro",           'static const char* const CSIM_LINES[] = {', 38),
]
bad = 0
for label, anchor, budget in CHECKS:
    if anchor == 'BANDPLAN_STRUCTURED':
        arr = [s.split('|')[0] for s in block_at('static const char* const BANDPLAN[] = {')
               if not s.startswith('#')]
    else:
        try: arr = block_at(anchor)
        except ValueError: print(f'{label:16s} ANCHOR NOT FOUND'); bad += 1; continue
    over = [(len(s), s) for s in arr if len(s) > budget]
    print(f'{label:16s} {len(arr):3d} lines, budget {budget}: ' +
          ('OK' if not over else f'{len(over)} OVER'))
    for n, s in over: print(f'    {n:2d} ch: {s!r}'); bad += 1
# FOX_LBL: 4 text fields, 16-char budget (x=144)
i = src.index('const FoxLbl FOX_LBL[]'); j = src.index('};', i)
flds = re.findall(r'"((?:[^"\\]|\\.)*)"', src[i:j])
over = [(len(s), s) for s in flds if len(s.replace('\\"','"')) > 16]
print(f'{"Fox callouts":16s} {len(flds):3d} fields, budget 16: ' + ('OK' if not over else f'{len(over)} OVER'))
for n, s in over: print(f'    {n:2d} ch: {s!r}'); bad += 1
sys.exit(1 if bad else 0)
