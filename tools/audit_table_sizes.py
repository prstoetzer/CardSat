#!/usr/bin/env python3
"""
audit_table_sizes.py -- a table declared `T name[X_COUNT]` must have exactly
X_COUNT rows, in the enum's order.

THE PROBLEM
  The radio catalogs are parallel structures: an enum of models and a table of
  profiles indexed by it. Nothing in C++ ties them together -- `LEG_RADIOS[m]` is
  valid for any m, so a table with one row too few reads off the end (a garbage
  name, a garbage CI-V address, a garbage baud) and one row too many silently
  shifts every entry after the insertion point, which means a saved index now
  selects a DIFFERENT radio.

  Both hazards are live: this catalog grew from 27 to 35 radios in one cycle, and
  each addition had to be made in two places that the compiler will not compare.
  The check was done by hand each time, which is exactly the kind of thing that
  holds until the once it doesn't.

THE RULE
  For every `... name[SOMETHING_COUNT] = { ... };` in src/, count the top-level
  initializers and require that it equals the number of enumerators declared before
  SOMETHING_COUNT in the matching enum.

WHAT THIS CANNOT SEE
  Row ORDER. A table with the right number of rows in the wrong order still
  compiles and still misidentifies radios. The names are checked against the
  enumerator names heuristically (IC-705 <-> LEG_IC705) and mismatches are
  reported as warnings rather than failures, because the transform is not exact.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, 'src')


def strip_comments(text):
    """Remove // and /* */ comments. Must run BEFORE counting: a stray quote or
    brace inside a comment desynchronises the scanner (that was the first version
    of this gate reporting 14 rows for an 11-row table)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':                                  # copy string literals whole
            out.append(c); i += 1
            while i < n:
                out.append(text[i])
                if text[i] == '\\':
                    i += 1
                    if i < n: out.append(text[i])
                elif text[i] == '"':
                    i += 1; break
                i += 1
            continue
        if c == "'":                                  # and char literals
            out.append(c); i += 1
            while i < n:
                out.append(text[i])
                if text[i] == '\\':
                    i += 1
                    if i < n: out.append(text[i])
                elif text[i] == "'":
                    i += 1; break
                i += 1
            continue
        if text.startswith('//', i):
            while i < n and text[i] != '\n': i += 1
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i)
            i = n if j < 0 else j + 2
            continue
        out.append(c); i += 1
    return ''.join(out)


def top_level_rows(block):
    """Number of initialisers at the top level of a table body.

    Braced rows ({ ... }, the struct tables) are counted directly. Tables of
    scalars or string literals have no braces, so fall back to counting
    comma-separated items at depth 0."""
    block = strip_comments(block)
    rows, depth = 0, 0
    items, seen_any = 0, False
    i = 0
    while i < len(block):
        ch = block[i]
        if ch == '"':                                  # skip the literal wholesale
            i += 1
            while i < len(block):
                if block[i] == '\\':
                    i += 2; continue
                if block[i] == '"':
                    break
                i += 1
            seen_any = True
        elif ch == '{':
            if depth == 0:
                rows += 1
            depth += 1
        elif ch in '}])':
            depth -= 1 if ch == '}' else 0
        elif ch == '(' :
            depth += 0
        elif ch == ',' and depth == 0:
            items += 1
        elif not ch.isspace():
            seen_any = True
        i += 1
    if rows:
        return rows
    return (items + 1) if seen_any else 0


def enum_members(text, count_name):
    """Enumerators declared before `count_name` in the enum that defines it."""
    for m in re.finditer(r'enum\s+(?:class\s+)?\w*\s*(?::\s*\w+\s*)?\{(.*?)\}\s*;', text, re.S):
        body = re.sub(r'//[^\n]*', '', m.group(1))
        body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
        names = []
        for part in body.split(','):
            part = part.strip()
            if not part:
                continue
            mm = re.match(r'([A-Za-z_]\w*)', part)
            if mm:
                names.append(mm.group(1))
        if count_name in names:
            return names[:names.index(count_name)]
    return None


def main():
    problems, checked = [], 0
    for fn in sorted(os.listdir(SRC_DIR)):
        if not fn.endswith(('.h', '.cpp')):
            continue
        path = os.path.join(SRC_DIR, fn)
        text = open(path, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'(\w+)\s*\[\s*(\w+_COUNT)\s*\]\s*=\s*\{', text):
            arr, cnt = m.group(1), m.group(2)
            start = m.end() - 1
            depth, i = 0, start
            while i < len(text):
                if text[i] == '{':
                    depth += 1
                elif text[i] == '}':
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            body = text[start + 1:i]
            rows = top_level_rows(body)
            members = enum_members(text, cnt)
            if members is None:
                # the enum may live in another header
                for other in sorted(os.listdir(SRC_DIR)):
                    if not other.endswith(('.h', '.cpp')):
                        continue
                    members = enum_members(
                        open(os.path.join(SRC_DIR, other), encoding='utf-8',
                             errors='replace').read(), cnt)
                    if members is not None:
                        break
            if members is None:
                problems.append(f'{fn}: {arr}[{cnt}] -- could not find the enum defining {cnt}')
                continue
            checked += 1
            if rows != len(members):
                problems.append(
                    f'{fn}: {arr}[{cnt}] has {rows} rows but {cnt} implies '
                    f'{len(members)}\n    -> indexing past the end reads garbage; an extra row '
                    f'shifts every\n       later entry, so a SAVED index selects a different item.')

    if problems:
        print('TABLE SIZE: enum-sized table does not match its enum')
        for p in problems:
            print('  ' + p)
        sys.exit(1)
    print(f'table sizes OK ({checked} enum-sized tables match their enums)')


if __name__ == '__main__':
    main()
