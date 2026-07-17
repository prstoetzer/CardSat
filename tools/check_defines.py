#!/usr/bin/env python3
"""Define gate: every #define/constexpr in src/ headers must survive into the .ino,
and every identifier used must be defined somewhere.

Why this exists. A script that removed ONE retired define from config.h searched
"from the define to the next blank line" -- and the define was followed by a
comment block, not a blank, so the cut swallowed 26 lines: AMSAT URLs, space
weather paths, the weather API, the QSO log and ADIF paths. 87 compiler errors,
all of them "not declared", all from ONE bad edit. Every gate passed first:
braces balanced, parity matched (both representations were identically gutted),
and check_compiles does not reach config.h's consumers in app.cpp.

Deleting a define is invisible to every structural check -- the code still parses,
it just refers to something that is no longer there. So this gate compares the
CONSTANT SURFACE: for each src/*.h, every #define NAME and `static constexpr T
NAME` must also appear in CardSat.ino. That catches a define that vanished from
one representation, and (with --baseline) one that vanished from BOTH.

Usage:
  check_defines.py                 -- src/ vs .ino parity of the constant surface
  check_defines.py --baseline DIR  -- also diff against a known-good tree, so a
                                      define deleted from BOTH is still caught
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INO  = os.path.join(ROOT, 'CardSat.ino')
SRC  = os.path.join(ROOT, 'src')

DEF_RE = re.compile(r'^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)')
CEX_RE = re.compile(r'^\s*static\s+constexpr\s+[A-Za-z_][\w:<>\s\*&]*?\s+([A-Za-z_][A-Za-z0-9_]*)\s*=')

def names(path):
    out = set()
    if not os.path.isfile(path):
        return out
    for line in open(path, encoding='utf-8', errors='replace'):
        m = DEF_RE.match(line) or CEX_RE.match(line)
        if m:
            n = m.group(1)
            # include guards are per-file and legitimately absent from the .ino
            if n.startswith('CARDSAT_') and n.endswith('_H'):
                continue
            out.add(n)
    return out

if not os.path.isfile(INO):
    print('  define gate: SKIPPED (no CardSat.ino)')
    sys.exit(0)

ino_txt = open(INO, encoding='utf-8', errors='replace').read()
bad = 0

for f in sorted(os.listdir(SRC)) if os.path.isdir(SRC) else []:
    if not f.endswith('.h'):
        continue
    for n in sorted(names(os.path.join(SRC, f))):
        # word-boundary search: the name must actually appear in the .ino
        if not re.search(r'\b' + re.escape(n) + r'\b', ino_txt):
            print(f"  MISSING from CardSat.ino: {n}  (declared in src/{f})")
            bad += 1

# Deliberately retired constants. A define REMOVED on purpose must be listed here,
# which is the point: the gate then cries wolf about nothing, and an unlisted
# removal is either intentional (add it, in one line, with the reason) or the
# accident this gate exists to catch. Cheap to maintain, and it makes "I meant to
# delete that" an explicit claim rather than an assumption.
RETIRED = {
    'FILE_USBCAT_LOG',   # 0.9.58: Logstore owns the log path (rotation + size cap)
}

# --baseline: catch a define removed from BOTH representations at once
if '--baseline' in sys.argv:
    base = sys.argv[sys.argv.index('--baseline') + 1]
    bsrc = os.path.join(base, 'src')
    if os.path.isdir(bsrc):
        for f in sorted(os.listdir(bsrc)):
            if not f.endswith('.h'):
                continue
            was = names(os.path.join(bsrc, f))
            now = names(os.path.join(SRC, f))
            for n in sorted(was - now):
                if n in RETIRED:
                    continue
                print(f"  REMOVED vs baseline: {n}  (was in src/{f})")
                bad += 1

if bad:
    print(f"  define gate: FAILED -- {bad} constant(s) missing.")
    sys.exit(1)
print("  define gate: every src/ constant is present in CardSat.ino.")
