#!/usr/bin/env python3
"""
audit_list_wrap.py -- a selectable list must wrap at both ends.

THE PROBLEM
  Pressing down at the bottom of a list should return to the top, and up at the top
  should go to the bottom. Most of CardSat's ~100 list screens do this; a handful
  CLAMPED instead, which on a 240x135 screen is indistinguishable from a stuck key
  -- there is no scrollbar to show you are already at the end. Reported on the MUF
  region list; an audit then found the same in the DX cluster (worse: it clamped
  inside a FILTERED list, so the reachable range depended on the band filter), the
  BASIC file list, the EME mutual-window list, and both Dual-Rig cursors.

THE RULE, and why it is checkable
  The codebase already distinguishes two kinds of moving index by NAME:
      *Sel     a SELECTION -- which item of a list is highlighted  -> must wrap
      *Scroll  a text OFFSET -- how far down a document we are     -> clamps
  Clamping is correct for a scroller (a manual that jumps from the last line back
  to the first is worse, not better), so this gate checks only *Sel.

  A movement counts as wrapping if it uses modulo, or one of the explicit idioms
  the codebase already uses:
      x = (x + N - 1) % N;      x = (x + 1) % N;
      if (--x < 0) x = N - 1;   if (++x >= N) x = 0;
      if (x == 0) x = N; else x--;
      x ^= 1;                   (a two-item toggle is trivially a wrap)

EXEMPTIONS
  Listed explicitly below with a reason. "It is not really a list" is a reason;
  "it was easier" is not.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src', 'app.cpp')

# handler -> why its *Sel legitimately does not wrap in the key handler itself.
EXEMPT = {
    'keyTgtSearch':
        'tsPickSel is normalized (and wrapped) in drawTgtSearch, where the count '
        'lives; the handler deliberately just steps it',
}

WRAP_IDIOMS = (
    r'%\s*\w',                       # (x + 1) % N
    r'--\s*\w+\s*<\s*0',             # if (--x < 0) x = N-1
    r'\+\+\s*\w+\s*>=?\s*\w',        # if (++x >= N) x = 0
    r'==\s*0\s*\)\s*\w+\s*=',        # if (x == 0) x = N; else x--
    r'\^=\s*1',                      # two-item toggle
    r'\bt\s*<\s*0\b',                # "no next match -> wrap to the far end" search
)


def body_of(src, name):
    m = re.search(r'\nvoid App::' + re.escape(name) + r'\([^)]*\)\s*\{', src)
    if not m:
        return ''
    i = m.end() - 1
    depth, j = 0, i
    while j < len(src):
        if src[j] == '{':
            depth += 1
        elif src[j] == '}':
            depth -= 1
            if depth == 0:
                break
        j += 1
    return src[i:j]


def main():
    src = open(SRC, encoding='utf-8', errors='replace').read()
    handlers = [n for n, _ in re.findall(r'\nvoid App::(key\w+)\(([^)]*)\)\s*\{', src)]
    findings = []
    checked = 0
    for name in handlers:
        if name in EXEMPT:
            continue
        body = body_of(src, name)
        for line in body.split('\n'):
            if not re.search(r'\bisUp\(|\bisDown\(', line):
                continue
            sels = {v for v in re.findall(r'\b(\w*[Ss]el)\b', line)
                    if not v.endswith('Scroll')}
            if not sels:
                continue
            checked += 1
            if any(re.search(p, line) for p in WRAP_IDIOMS):
                continue
            findings.append((name, ','.join(sorted(sels)), line.strip()[:70]))

    if findings:
        print('LIST WRAP: selection does not wrap at the ends')
        for name, var, line in findings:
            print(f'  {name}: {var}')
            print(f'    {line}')
            print('    -> down at the last item must return to the first (and up at the')
            print('       first to the last). On this screen there is no scrollbar, so a')
            print('       clamped list is indistinguishable from a key that stopped working.')
        sys.exit(1)

    print(f'list wrap OK ({checked} selection movements across '
          f'{len(handlers)} handlers, {len(EXEMPT)} exempt)')


if __name__ == '__main__':
    main()
