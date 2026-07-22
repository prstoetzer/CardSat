#!/usr/bin/env python3
"""Audit key handlers for duplicate `if (c == 'x')` guards within one handler.

Self-contained: resolves paths relative to the repo root and vendors the small
brace-matcher it needs, so it runs in a clean checkout with no developer-local
helper on sys.path.
"""
import re, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def body_by_sig(src, sig):
    """Return the { ... } body of the function whose definition starts with `sig`,
    matched by brace counting from the opening brace after the signature."""
    i = src.find(sig)
    if i < 0:
        return ""
    b = src.find('{', i)
    if b < 0:
        return ""
    depth = 0
    j = b
    while j < len(src):
        c = src[j]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return src[b:j+1]
        j += 1
    return ""

src=open(os.path.join(ROOT,'src','app.cpp')).read()
handlers=re.findall(r'void App::(key\w+)\(', src)
real=[]
for h in handlers:
    body=body_by_sig(src,'void App::'+h+'(')
    if not body: continue
    # Only count lines that are an actual IF-guard on the key: start with 'if (c == '
    guards={}
    for line in body.split('\n'):
        s=line.strip()
        m=re.match(r"if \(c == '(\\?.)'", s)
        if m:
            ch=m.group(1)
            guards.setdefault(ch,[]).append(s)
    for ch,lines in guards.items():
        if len(lines)>1:
            real.append((h,ch,lines))
if not real:
    print("No real duplicate IF-guard bindings.")
for h,ch,lines in real:
    print(f"** {h}: key '{ch}' has {len(lines)} separate if-guards")
    for l in lines: print(f"     {l[:80]}")
    print()
