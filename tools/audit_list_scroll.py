#!/usr/bin/env python3
"""Every list scroll offset must actually be advanced from its selection.

THE BUG THIS EXISTS FOR
-----------------------
The DX cluster spot list shipped in 0.9.66-wip unable to scroll. `keyDxc` moved
`dxcSel` on up/down and `drawDxc` painted an 8-row window starting at `dxcScroll` --
but nothing anywhere ever assigned `dxcScroll` anything except 0. The cursor walked
off the bottom row and the view stayed on the first page forever.

Nothing caught it. The screen drew, the geometry audit was satisfied, the code
compiled without a warning, and the selection variable really was moving. The only
observable symptom was on hardware.

THE RULE
--------
A member named `<something>Scroll` is a viewport offset. If every write to it in the
whole firmware is a literal reset to 0, that viewport is nailed to the top of its
list and the screen cannot scroll. A scroll offset must be written at least once
from something that is not a constant -- clamped against a selection index
(`aprsScroll = aprsSel`), stepped (`tfOutScroll++`), or computed.

FALSE POSITIVES
---------------
A genuinely single-page list with a vestigial offset member would trip this. There is
no such case today; if one appears, delete the unused member rather than whitelisting
it -- an offset nothing advances is dead state, and dead state is how this bug looked
right up until someone tried to scroll.

Exit 0 = clean, 1 = a scroll offset is never advanced.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = [os.path.join(ROOT, 'src', 'app.cpp'),
           os.path.join(ROOT, 'src', 'app.h')]

# `name = <rhs>;`  -- captures the right-hand side so a literal 0 can be told apart
# from a computed value.
ASSIGN = r'\b{name}\s*=\s*([^;]+);'
# Any form that moves the offset without a plain assignment. BOTH fixities: the
# firmware uses prefix (`++foxTextScroll`) as often as postfix, and matching only the
# postfix form made this audit report four healthy screens on its first run.
STEP = r'(\b{name}\s*(\+\+|--|\+=|-=)|(\+\+|--)\s*{name}\b)'


def main():
    text = ''
    for path in SOURCES:
        if os.path.exists(path):
            with open(path, encoding='utf-8') as fh:
                text += fh.read() + '\n'
    if not text:
        print('audit_list_scroll: no sources found', file=sys.stderr)
        return 1

    # Declared scroll members, e.g. "int dxcN = 0, dxcSel = 0, dxcScroll = 0;"
    names = sorted(set(re.findall(r'\b(\w+Scroll)\b', text)))
    if not names:
        print('audit_list_scroll: no *Scroll members found -- check the naming convention',
              file=sys.stderr)
        return 1

    stuck, unused = [], []
    for name in names:
        if re.search(STEP.format(name=re.escape(name)), text):
            continue                      # stepped somewhere: it moves
        rhs = re.findall(ASSIGN.format(name=re.escape(name)), text)
        if [r.strip() for r in rhs if r.strip() not in ('0', '0, 0')]:
            continue                      # assigned something computed: it moves
        # Never advanced. Is it even READ? An offset that a draw path reads but
        # nothing advances is a STUCK VIEWPORT -- a real, user-visible bug. One that
        # is never read is merely dead state.
        reads = len(re.findall(r'\b%s\b' % re.escape(name), text)) - len(rhs) - 1
        (stuck if reads > 0 else unused).append((name, len(rhs)))

    for name, n in unused:
        print(f'note: {name} is declared and reset ({n} write(s)) but never read -- '
              f'dead state, delete it rather than leaving a scroll that implies a feature')
    if stuck:
        for name, n in stuck:
            print(f'STUCK VIEWPORT  {name}: read by a draw path, {n} write(s), all '
                  f'literal 0 -- this list cannot scroll past its first page')
        print(f'\naudit_list_scroll: FAIL -- {len(stuck)} viewport(s) read but never advanced')
        return 1

    print(f'  list scroll audit: {len(names)} scroll offset(s), all advanced from '
          f'a selection or step.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
