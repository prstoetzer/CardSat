#!/usr/bin/env python3
"""
audit_text_screens.py — a screen that accepts free text must not lose letters to the
global bare-letter hotkeys.

Why this gate exists. Two globals fire on a bare letter: 'b' (screenshot) and 'h'
(help). They are suppressed on screens that take typed text, via the `lettersFree`
predicate. SCR_BASICIMM — the BASIC immediate-mode prompt — was never added to that
list, so at the prompt 'b' took a screenshot and 'h' opened Help, and it was simply
IMPOSSIBLE to type either letter. Every other text screen was listed correctly, which is
exactly why the omission survived: the pattern was right everywhere the author looked.

The rule: if a screen's key handler accumulates printable characters (`c >= 32 && c <
127`, or `c >= 0x20`), that screen must appear in the `lettersFree` exclusion list.

This is derived from the source each run — the dispatch switch gives screen -> handler,
the handler body says whether it takes text — so a NEW text screen is covered
automatically, with no list here to keep in sync.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'src', 'app.cpp')

# Both spellings of "is this a printable character". The `> 32` form was missed on the
# first version of this gate, and SCR_BASICASK -- a genuine text-entry screen added
# later -- slipped straight past it: the operator could not type B or H into a callsign
# field, which is the precise bug this gate exists to prevent. A gate that only catches
# one spelling of the thing it is looking for is worse than none, because it is
# believed.
TEXTUAL = (
    re.compile(r'c\s*>=?\s*32\s*&&\s*c\s*<\s*127'),
    re.compile(r'c\s*>=?\s*0x20'),
)


def body_of(src, fn):
    m = re.search(r'\nvoid App::' + re.escape(fn) + r'\s*\([^)]*\)\s*\{', src)
    if not m:
        return ''
    i = m.start()
    j = src.index('{', i)
    depth, k = 0, j
    while k < len(src):
        if src[k] == '{':
            depth += 1
        elif src[k] == '}':
            depth -= 1
            if depth == 0:
                break
        k += 1
    return src[i:k]


def main() -> int:
    src = open(APP, encoding='utf-8', errors='replace').read()

    dispatch = dict(re.findall(r'case (SCR_[A-Z0-9]+):\s*(\w+)\(c,', src))
    if not dispatch:
        print('audit_text_screens: FAIL — could not find the key dispatch switch')
        return 1

    try:
        blk = src[src.index('const bool lettersFree ='):]
        blk = blk[:blk.index(';')]
    except ValueError:
        print('audit_text_screens: FAIL — could not find the lettersFree predicate')
        return 1
    # Strip // comments FIRST. The predicate carries explanatory comments that name
    # screens, and counting those would let a comment satisfy the gate -- which it did
    # on the first attempt, passing a tree where the real exclusion had been removed.
    code = '\n'.join(re.sub(r'//.*$', '', ln) for ln in blk.split('\n'))
    excluded = set(re.findall(r'SCR_[A-Z0-9]+', code))

    missing = []
    checked = 0
    for scr, fn in sorted(dispatch.items()):
        body = body_of(src, fn)
        if not body or not any(p.search(body) for p in TEXTUAL):
            continue
        checked += 1
        if scr not in excluded:
            missing.append((scr, fn))

    if missing:
        print('audit_text_screens: FAIL')
        for scr, fn in missing:
            print(f'  {scr} ({fn}) accepts printable characters but is NOT in lettersFree')
        print('\n  On these screens the global bare-letter hotkeys steal keystrokes:')
        print("  'b' screenshots and 'h' opens Help, so those letters cannot be typed.")
        print('  Add the screen to the lettersFree exclusion list in App::onKey().')
        return 1

    print(f'audit_text_screens: ok  ({checked} text-entry screen(s), all excluded '
          f'from the bare-letter globals)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
