#!/usr/bin/env python3
"""
audit_basic_examples.py -- every token in examples/basic/*.BAS must exist in the
interpreter.

Why this gate exists. The example programs ship in the REPOSITORY, not in flash, so
nothing compiles them and nothing runs them in CI. A typo, or a function that was
renamed in app.cpp, produces an example that fails on the operator's device -- and the
operator has no way to tell whether the mistake is theirs or ours. An example that does
not run is worse than no example: it teaches that the interpreter is unreliable.

The known-name lists are derived from src/app.cpp where possible (the system-data table
is read directly), so adding a SYS field needs no change here. Keywords and function
names are listed explicitly because they are parsed with kw()/kwb() literals scattered
through the evaluator, and pattern-matching those would be guessing.

Single letters (A) and single letters with $ (A$) are variables, and are always allowed.
Text inside string literals is ignored, as are REM lines.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

KEYWORDS = set("""REM END PRINT LET IF THEN GOTO GOSUB RETURN FOR NEXT TO STEP DIM ERASE
INPUT DATA READ RESTORE CLS LINE CIRCLE TEXT SHOW LPRINT FPRINT FOPEN FCLOSE FILES
SATSEL TXSEL GPAGE STOP AND OR NOT PSET MOD""".split())

FUNCS = set("""ABS INT SQR SIN COS TAN ATN LOG EXP SGN MIN MAX RND ASN ACS LOG10 ROUND
FRAC HYP ATN2 LEN ASC VAL INSTR LEFT$ RIGHT$ MID$ CHR$ STR$ UCASE$ LCASE$ TRIM$ GRID$
DXCC$ TIME$ DATE$ GCDIST GCAZ DXCCLAT DXCCLON FSPL HPBW PASSAOS PASSLOS PASSMAX
PI TWOPI DEG RAD CLIGHT KBOLT REARTH""".split())


def main() -> int:
    app = open(os.path.join(ROOT, 'src', 'app.cpp'), encoding='utf-8', errors='replace').read()
    sysnames = set(re.findall(r'\{"([A-Z0-9]+)"', app))
    if not sysnames:
        print('audit_basic_examples: FAIL -- could not read the SYS name table')
        return 1

    exdir = os.path.join(ROOT, 'examples', 'basic')
    if not os.path.isdir(exdir):
        print('audit_basic_examples: no examples directory')
        return 0

    problems = []
    files = sorted(f for f in os.listdir(exdir) if f.upper().endswith('.BAS'))
    for f in files:
        for raw in open(os.path.join(exdir, f), encoding='utf-8', errors='replace'):
            m = re.match(r'\s*(\d+)\s+(.*)', raw)
            if not m:
                continue
            line, body = m.group(1), m.group(2)
            if body.upper().lstrip().startswith('REM'):
                continue
            body = re.sub(r'"[^"]*"', '""', body)          # string literals are prose
            # A trailing "... : REM note" is a comment, not code. Missing this made the
            # gate reject every existing example on its own prose.
            body = re.split(r'\bREM\b', body, 1, flags=re.I)[0]
            # Scientific notation: 1E8 is ONE number. Tokenising it as "E8" invented an
            # unknown name in perfectly valid arithmetic -- the gate's own bug, and the
            # kind that makes a gate untrustworthy rather than useful.
            body = re.sub(r'\d+\.?\d*[Ee][-+]?\d+', ' 0 ', body)
            for tok in re.findall(r'(?<![A-Za-z0-9.])[A-Za-z][A-Za-z0-9]*\$?', body.upper()):
                if tok in KEYWORDS or tok in FUNCS or tok in sysnames:
                    continue
                if len(tok) == 1 or (len(tok) == 2 and tok.endswith('$')):
                    continue                               # A, A$ -- variables
                problems.append(f'{f}:{line}  unknown token "{tok}"')

    if problems:
        print('audit_basic_examples: FAIL')
        for p in problems[:40]:
            print('  ' + p)
        if len(problems) > 40:
            print(f'  ... and {len(problems) - 40} more')
        print('\n  Either the example has a typo, or the interpreter lost a name the')
        print('  example relies on. Both ship broken; neither is visible without this.')
        return 1

    print(f'audit_basic_examples: ok  ({len(files)} programs, every token known)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
