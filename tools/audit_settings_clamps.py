#!/usr/bin/env python3
"""
audit_settings_clamps.py -- a saved setting must survive a reload.

THE BUG THIS EXISTS TO CATCH (twice now, same shape):
  Settings::load() validates enum-valued fields. When those checks are written as
  a RANGE against whatever the last enumerator happened to be at the time --
  `if (catType > CAT_USB) catType = CAT_WIRED;` -- they silently couple to the
  enum's growth. The next transport added to the enum inherits a bound that
  predates it, so a saved config is discarded on every load: the setting works
  until reboot and then reverts, with no error anywhere.
    * CAT_USB (4) was discarded by a clamp reading `> CAT_RIGCTL` (2).
    * CAT_DUAL (5) was discarded by a clamp reading `> CAT_USB` (4), shipped in
      0.9.68 -- native dual-radio mode reverted to wired CI-V on every reboot.

METHOD
  1. Parse every `enum { ... }` in settings.h into name -> ordered members.
  2. In settings.cpp, find comparisons of the form `<field> > <ENUM_MEMBER>` or
     `>= <ENUM_MEMBER>`.
  3. Flag any whose right-hand side is NOT the last member of its enum. Comparing
     against the last member is what a correct range check looks like; comparing
     against anything earlier means some valid value is being thrown away.

  A `switch` whitelist (what the fixed code uses) contains no such comparison and
  so is inherently clean -- which is the point: the gate pushes toward the form
  that cannot rot.

Exit 1 on any finding.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H = os.path.join(ROOT, 'src', 'settings.h')
C = os.path.join(ROOT, 'src', 'settings.cpp')

# Comparisons that are deliberately not "is this enum value valid" checks.
# Each entry must say why.
ALLOW = {
    # (field, rhs): reason
    ('vfoType', 'VFO_MAIN_UP_SUB_DOWN'):
        'equality-style default selection, not an upper bound',
}


def parse_enums(text):
    """name -> [members in declaration order]. Handles anonymous enums too."""
    out = {}
    for m in re.finditer(r'enum\s+(?:class\s+)?(\w*)\s*(?::\s*\w+\s*)?\{(.*?)\}\s*;', text, re.S):
        name = m.group(1) or f'anon@{m.start()}'
        body = re.sub(r'//[^\n]*', '', m.group(2))
        body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
        members = []
        for part in body.split(','):
            part = part.strip()
            if not part:
                continue
            mm = re.match(r'([A-Za-z_]\w*)', part)
            if mm:
                members.append(mm.group(1))
        if members:
            out[name] = members
    return out


def main():
    hs = open(H, encoding='utf-8').read()
    cs = open(C, encoding='utf-8').read()
    enums = parse_enums(hs)

    member_to_enum = {}
    for ename, members in enums.items():
        for mem in members:
            member_to_enum.setdefault(mem, (ename, members))

    # Strip comments from settings.cpp so documented history isn't parsed as code.
    code = re.sub(r'//[^\n]*', '', cs)
    code = re.sub(r'/\*.*?\*/', '', code, flags=re.S)

    findings = []
    for m in re.finditer(r'\b(\w+)\s*(>=?)\s*([A-Z][A-Z0-9_]+)\b', code):
        field, op, rhs = m.group(1), m.group(2), m.group(3)
        if rhs not in member_to_enum:
            continue
        if (field, rhs) in ALLOW:
            continue
        ename, members = member_to_enum[rhs]
        last = members[-1]
        # `>= LAST` and `> LAST` are both correct upper bounds; `>= <count>` where
        # the enum carries a trailing count member is also fine.
        if rhs == last:
            continue
        if op == '>=' and members.index(rhs) == len(members) - 1:
            continue
        findings.append((field, op, rhs, ename, last,
                         code[:m.start()].count('\n') + 1))

    if findings:
        print('SETTINGS CLAMP: range check against a non-final enumerator')
        for field, op, rhs, ename, last, line in findings:
            print(f'  settings.cpp:{line}: `{field} {op} {rhs}` -- {rhs} is not the last')
            print(f'    member of enum {ename} (last is {last}).')
            print(f'    Any value after {rhs} is silently reset on load, so the setting')
            print(f'    works until reboot and then reverts. Use a switch whitelist.')
        sys.exit(1)

    n = sum(len(v) for v in enums.values())
    print(f'settings clamps OK ({len(enums)} enums, {n} enumerators checked)')


if __name__ == '__main__':
    main()
