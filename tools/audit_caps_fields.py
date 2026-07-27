#!/usr/bin/env python3
"""Grid and callsign edit fields must type in capitals like every other one.

THE BUG THIS EXISTS FOR
-----------------------
keyEdit() has an explicit list of editTargets that uppercase-as-you-type (shift gives
lowercase), and its own comment says: "Keep this list in sync when adding a grid/
callsign entry field." The two grid fields added in 0.9.66-wip -- 900 (APRS center)
and 901 (ADS-B scatter target) -- were never added. They alone typed lowercase while
every other grid field on the device typed uppercase, and because both still called
toUpperCase() on COMMIT the stored value was right and only the typing looked wrong.

A comment asking a human to keep two lists in sync is a defect waiting to happen. This
turns it into a gate.

THE RULE
--------
If an editTarget's commit handler calls gridToLatLon() or toUpperCase(), the operator
is entering a grid square or a callsign, and that target must appear in the
type-as-caps list. Both facts are read out of keyEdit() itself, so the check cannot
drift from the code the way the comment did.

Exit 0 = clean, 1 = a grid/callsign target types in lowercase.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'src', 'app.cpp')

# Targets that are uppercase-on-commit but are NOT free-text grid/callsign entry, so
# they do not belong in the type-as-caps list. Each needs a reason.
EXEMPT = {}


def main():
    if not os.path.exists(APP):
        print('audit_caps_fields: src/app.cpp not found', file=sys.stderr)
        return 1
    text = open(APP, encoding='utf-8').read()

    # Scope to keyEdit(): other switches in the file use overlapping case numbers.
    start = text.find('void App::keyEdit(')
    if start < 0:
        print('audit_caps_fields: keyEdit() not found -- has it been renamed?',
              file=sys.stderr)
        return 1
    nxt = re.search(r'\nvoid App::(?!keyEdit)', text[start + 10:])
    body = text[start:start + 10 + nxt.start()] if nxt else text[start:]

    # The type-as-caps list: a chain of `editTarget == N ||` inside the printable-char
    # branch, terminated by the `{` that opens the case-conversion block.
    m = re.search(r"if \(c >= 32 && c < 127\).*?\{(.*?)\n\s*\}", body, re.S)
    if not m:
        print('audit_caps_fields: could not find the printable-character branch',
              file=sys.stderr)
        return 1
    caps_block = m.group(1)
    caps_block = caps_block[:caps_block.find('{')] if '{' in caps_block else caps_block
    caps = set(int(n) for n in re.findall(r'editTarget\s*==\s*(\d+)', caps_block))
    if not caps:
        print('audit_caps_fields: type-as-caps list came back empty', file=sys.stderr)
        return 1

    # Commit handlers: `case N:` up to the next `case N:` in the same switch.
    cases = list(re.finditer(r'\bcase\s+(\d+):', body))
    offenders = []
    for i, cm in enumerate(cases):
        num = int(cm.group(1))
        end = cases[i + 1].start() if i + 1 < len(cases) else len(body)
        chunk = body[cm.end():end]
        # Strip comments first: the prose introducing a case routinely NAMES these
        # functions ("every other grid editor validates through gridToLatLon"), which
        # would attribute the previous case's rationale to this one.
        chunk = re.sub(r'/\*.*?\*/', ' ', chunk, flags=re.S)
        chunk = re.sub(r'//[^\n]*', ' ', chunk)
        if 'gridToLatLon' in chunk or 'toUpperCase' in chunk:
            if num not in caps and num not in EXEMPT:
                why = 'gridToLatLon' if 'gridToLatLon' in chunk else 'toUpperCase'
                offenders.append((num, why))

    if offenders:
        for num, why in sorted(offenders):
            print(f'LOWERCASE FIELD  editTarget {num}: commit calls {why}() but the '
                  f'target is missing from the type-as-caps list -- it will type in '
                  f'lowercase while every other grid/callsign field types in capitals')
        print(f'\naudit_caps_fields: FAIL -- {len(offenders)} grid/callsign field(s) '
              f'out of sync with the type-as-caps list')
        return 1

    print(f'  caps field audit: {len(caps)} target(s) type as caps; every '
          f'grid/callsign commit handler is covered.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
