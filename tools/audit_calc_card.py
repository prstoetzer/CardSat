#!/usr/bin/env python3
"""
audit_calc_card.py -- every function named on the calculator card and on the on-device
reference must exist in the evaluator.

A reference that lists a function the firmware does not have is worse than no reference:
the operator types it, gets an error, and concludes they made the mistake. The reverse
matters too but is less damaging, so a function present in the evaluator and absent from
the card is reported as a NOTE rather than a failure -- some are deliberately not
advertised.

Truth is src/app.cpp: the names the calculator's atom() matches with word().
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Words on the card that are prose or units, not function names.
PROSE = set("""and or the a an is are for from to in on at by with of it its
dB dBm dBi dBd Hz kHz MHz GHz km m s W kelvin metres degrees radians minutes
ENTER DEL Ans x y n r p u k M G T e pi c mu g0 kB Re csv
CardSat Calculator Card scientific grapher front back
f""".split())   # 'f' as in y=f(x): notation, not a function


def evaluator_names():
    src = open(os.path.join(ROOT, 'src', 'app.cpp'), encoding='utf-8', errors='replace').read()
    i = src.index('double atom() { ws();\n      const double D2R')
    j = src.index('double App::calcEval', i)
    return set(re.findall(r'word\("([A-Za-z0-9_]+)"\)', src[i:j]))


def card_names(path):
    """Function-looking tokens in the card's CONTENT.

    Only the FRONT and BACK literals -- the generator is a Python program and scanning
    the whole file reported reportlab's own API (Paragraph(, HexColor(, ...) as unknown
    calculator functions. Forty-four confident failures about code that was correct;
    the same shape of mistake this project's gates keep making on their first outing.
    """
    if not os.path.exists(path):
        return set()
    src = open(path, encoding='utf-8', errors='replace').read()
    body = ''
    for name in ('FRONT = [', 'BACK = ['):
        if name not in src:
            continue
        i = src.index(name)
        j = src.index('\n]', i)
        body += src[i:j]
    body = re.sub(r'<[^>]+>', '', body)               # strip the HTML markup
    return set(re.findall(r'\b([A-Za-z][A-Za-z0-9_]*)\s*\(', body))


def onscreen_names():
    src = open(os.path.join(ROOT, 'src', 'app.cpp'), encoding='utf-8', errors='replace').read()
    i = src.index('static const char* const CALCREF[] = {')
    j = src.index('};', i)
    return set(re.findall(r'\b([a-z][A-Za-z0-9_]*)\s*\(', src[i:j]))


def main() -> int:
    have = evaluator_names()
    if not have:
        print('audit_calc_card: FAIL -- could not read the evaluator function list')
        return 1

    problems = []
    for label, names in (('printable card', card_names(os.path.join(ROOT, 'tools_make_calccard.py'))),
                         ('on-device CALCREF', onscreen_names())):
        for n in sorted(names):
            if n in PROSE or n in have:
                continue
            problems.append(f'{label}: "{n}(" is not a function the evaluator knows')

    if problems:
        print('audit_calc_card: FAIL')
        for p in problems:
            print('  ' + p)
        print('\n  The operator will type it, get an error, and assume the mistake is theirs.')
        return 1

    missing = sorted(n for n in have
                     if n not in card_names(os.path.join(ROOT, 'tools_make_calccard.py'))
                     and n not in onscreen_names() and n not in PROSE)
    print(f'audit_calc_card: ok  ({len(have)} evaluator names; card and CALCREF list only real ones)')
    if missing:
        print('  note: present in the evaluator, not on either reference: ' + ' '.join(missing))
    return 0


if __name__ == '__main__':
    sys.exit(main())
