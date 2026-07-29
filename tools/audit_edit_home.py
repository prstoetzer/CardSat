#!/usr/bin/env python3
"""
audit_edit_home.py -- every SCR_EDIT target must cancel back to the screen it was
launched from.

THE BUG THIS EXISTS TO CATCH (twice now, same shape):
  editHome() maps an edit target to the screen ESC returns to, using ORDERED
  range rules with broad catch-alls at the bottom ("t >= 720 -> SCR_SKEDENTRY",
  "t >= 200 -> SCR_SETTINGS"). A new feature picks fresh target numbers, nobody
  adds a rule, and the numbers silently fall into a catch-all -- so canceling the
  edit teleports the operator into an unrelated editor. It happened to the
  Nearby & DX targets (900s -> the new-activation editor) and again to the
  dual-rig leg targets (920-931 -> the same place).

METHOD
  1. Parse the key dispatch table: `case SCR_X: keyFoo(...)` gives the screen each
     key handler belongs to.
  2. Parse editHome()'s rules in source order into a little interpreter.
  3. Find every `editTarget = <literal>` inside each key handler's body and
     evaluate editHome(target). If the result is not the handler's own screen,
     that's a cancel-teleport -- unless the target is in the allow-list below,
     where returning elsewhere is deliberate.

Exit 1 on any finding.
"""
import re, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC  = os.path.join(ROOT, 'src', 'app.cpp')

# Targets whose cancel screen INTENTIONALLY differs from the launching screen.
# Keep this list short and justified -- each entry is a deliberate exception.
ALLOW = {
    104,   # DX grid: launched from Settings and the Globe; editHome sends it to the Globe
    600,   # LoTW SAT_NAME prompt: launched from the export flow, cancels to the Log
    216,   # QRZ callsign prompt: launched from Settings, cancels to the QRZ screen
    230,   # LoTW key password: launched from Settings, cancels to the LoTW screen
    240,   # LoRa RX frequency: launched from Settings, cancels to the RX monitor
    326,   # CelesTrak query: launched from the sat list, cancels to the search screen
    351,   # Callsign: launched from BOTH GridCalc and QRZ-grid -- one target, two
           # legitimate homes, so no static rule can satisfy both
    203,   # GP source URL: a Settings-owned value editable from the GP source screen
    210,   # Beacon frequency: a Settings-owned value editable from the Orbit screen
    710,   # Note name prompt: launched from the note editor, cancels to the browser
           # (deliberate -- an unnamed new note has nothing to return to)
}

# Handlers that are not real "launchers": keyEdit chains one edit into the next,
# so its targets belong to whatever screen originally opened the chain.
NOT_LAUNCHERS = {'keyEdit'}

src = open(SRC, encoding='utf-8').read()

# ---- 1. handler -> screen ---------------------------------------------------
handler_screen = {}
for scr, fn in re.findall(r'case\s+(SCR_[A-Z0-9_]+):\s*(\w+)\s*\(c,\s*enter,\s*back\)', src):
    handler_screen.setdefault(fn, scr)

# ---- 2. editHome rules, in source order -------------------------------------
m = re.search(r'static Screen editHome\(int t\)\s*\{(.*?)\n\}', src, re.S)
if not m:
    print('FAIL: could not find editHome()'); sys.exit(1)
body = m.group(1)
rules = []   # (predicate, screen)
for line in body.splitlines():
    line = line.split('//')[0]
    r = re.search(r'if\s*\((.*?)\)\s*return\s+(SCR_[A-Z0-9_]+)\s*;', line)
    if r:
        rules.append((r.group(1), r.group(2))); continue
    r = re.search(r'^\s*return\s+(SCR_[A-Z0-9_]+)\s*;', line)
    if r:
        rules.append(('True', r.group(1)))

def edit_home(t):
    for cond, scr in rules:
        expr = cond.replace('&&', ' and ').replace('||', ' or ').replace('t', str(t))
        expr = re.sub(r'\bTrue\b'.replace(str(t), 'True'), 'True', expr) if cond == 'True' else expr
        try:
            if cond == 'True' or eval(expr, {'__builtins__': {}}, {}):
                return scr
        except Exception:
            continue
    return None

# ---- 3. editTarget assignments per handler ----------------------------------
findings = []
checked = 0
for fn, scr in handler_screen.items():
    if fn in NOT_LAUNCHERS:
        continue
    fm = re.search(r'\nvoid App::' + re.escape(fn) + r'\(.*?\)\s*\{', src)
    if not fm:
        continue
    i = fm.end() - 1
    depth, j = 0, i
    while j < len(src):
        if src[j] == '{': depth += 1
        elif src[j] == '}':
            depth -= 1
            if depth == 0: break
        j += 1
    fnbody = src[i:j]
    for tgt in set(int(x) for x in re.findall(r'editTarget\s*=\s*(\d+)\s*;', fnbody)):
        if tgt in ALLOW:
            continue
        checked += 1
        home = edit_home(tgt)
        if home != scr:
            findings.append((fn, scr, tgt, home))

if findings:
    print('EDIT-TARGET CANCEL ROUTING: target(s) do not return to their own screen')
    for fn, scr, tgt, home in sorted(findings, key=lambda x: x[2]):
        print(f'  target {tgt}: launched in {fn} ({scr}) but editHome() -> {home}')
        print(f'    -> add a rule in editHome() ABOVE the catch-alls, or allow-list it')
    sys.exit(1)

print(f'edit-home routing OK ({checked} targets across {len(handler_screen)} handlers)')
