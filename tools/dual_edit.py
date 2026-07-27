#!/usr/bin/env python3
"""Apply one exact edit to BOTH src/<file> and CardSat.ino, or refuse.

Working rule 1 says every change goes into both representations byte-identically,
and a stale .ino has silently shipped fixes-that-weren't more than once. check_parity
only proves a signature EXISTS in the .ino -- it cannot see a body that drifted. This
closes that gap mechanically: the same old->new substitution is applied to both files,
and any occurrence-count mismatch aborts before either file is written.

  dual_edit.py <src-relative-path> <<'PY'
  OLD = r'''...exact text...'''
  NEW = r'''...replacement...'''
  PY

Or as a module: apply(srcpath, old, new, count=1).
"""
import sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INO = os.path.join(ROOT, 'CardSat.ino')


def apply(srcrel, old, new, count=1, ino_optional=False):
    srcpath = os.path.join(ROOT, srcrel)
    src = open(srcpath, encoding='utf-8').read()
    ino = open(INO, encoding='utf-8').read()

    ns, ni = src.count(old), ino.count(old)
    if ns != count:
        raise SystemExit(f"ABORT {srcrel}: expected {count} occurrence(s), found {ns}")
    if ni != count:
        if ino_optional and ni == 0:
            print(f"  note: not present in .ino (ino_optional): {srcrel}")
        else:
            raise SystemExit(f"ABORT CardSat.ino: expected {count} occurrence(s), found {ni}"
                             f"  [src had {ns}] -- representations already differ here")

    open(srcpath, 'w', encoding='utf-8').write(src.replace(old, new))
    if ni == count:
        open(INO, 'w', encoding='utf-8').write(ino.replace(old, new))
    print(f"  ok  {srcrel} + CardSat.ino  ({count}x)")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    ns = {}
    exec(sys.stdin.read(), ns)
    apply(sys.argv[1], ns['OLD'], ns['NEW'], ns.get('COUNT', 1))
