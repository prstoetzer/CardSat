#!/usr/bin/env python3
"""
audit_session_latches.py — a "once per session" flag that is never cleared is a bug.

Why this gate exists. `PlainCatRig::_kwSession` guards the TH-D75's per-session
preconditions (VM = VFO mode, BC = control band). It was set true in kwEnsureSession()
and cleared nowhere, so the FIRST engage after boot configured the radio and every
engage after that silently skipped the setup — the radio then refused every command and
the failure looked exactly like a dead USB transport. It cost a full bench round to
find, and the reset had in fact been written earlier and lost during an unrelated repair
of the .ino. Nothing caught that.

The rule: any bool member whose name marks it as a one-shot latch (…Session, …Inited,
…Configured, …Applied, …Armed, …Latched, …Done) and which is assigned `true` somewhere
must also be assigned `false` somewhere OTHER than its declaration. A latch with no
reset path outside its initialiser can only ever fire once per boot.

Deliberate exceptions go in ALLOW with a reason — some latches genuinely are
boot-once.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')

# name -> why a once-per-boot latch is correct here
ALLOW = {
    'bannerDone': 'diagnostic banner: one line per boot is the intent',
}

LATCH = re.compile(
    r'\bbool\s+(_?\w*(?:Session|Inited|Initialised|Initialized|Configured|Applied|'
    r'Armed|Latched|Done))\s*(?:=\s*(?:true|false)\s*)?;')


def main() -> int:
    text = {}
    for d, _, files in os.walk(SRC):
        for f in files:
            if f.endswith(('.h', '.cpp')):
                p = os.path.join(d, f)
                text[p] = open(p, encoding='utf-8', errors='replace').read()

    blob = '\n'.join(text.values())
    problems = []

    for path, body in text.items():
        for m in LATCH.finditer(body):
            name = m.group(1)
            if name in ALLOW:
                continue
            # Does anything set it true? If not, it is not a latch we care about.
            if not re.search(r'\b' + re.escape(name) + r'\s*=\s*true\b', blob):
                continue
            # Assignments to false, EXCLUDING the declaration's own initialiser.
            resets = 0
            for r in re.finditer(r'\b' + re.escape(name) + r'\s*=\s*false\b', blob):
                line_start = blob.rfind('\n', 0, r.start()) + 1
                line = blob[line_start:blob.find('\n', r.start())]
                if re.match(r'\s*(?:static\s+)?bool\s', line):
                    continue          # the declaration initialiser
                resets += 1
            if resets == 0:
                ln = body[:m.start()].count('\n') + 1
                problems.append(
                    f'{os.path.relpath(path, ROOT)}:{ln}: '
                    f'`{name}` is set true but never reset outside its declaration -- '
                    f'it can only fire once per boot')

    if problems:
        print('audit_session_latches: FAIL')
        for p in problems:
            print('  ' + p)
        print('\n  A one-shot latch with no reset path means the guarded work happens')
        print('  once per BOOT rather than once per SESSION. If that is intended, add')
        print('  the name to ALLOW in this script with the reason.')
        return 1

    print('audit_session_latches: ok  (every session latch has a reset path)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
