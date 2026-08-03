#!/usr/bin/env python3
"""Pull the string helpers out of BasicVM so they can be exercised on the host.

Only the pure text logic is extracted -- MID$/LEFT$/RIGHT$/INSTR semantics and the
1-based indexing that a ported Microsoft BASIC program depends on. The parser itself
needs the whole VM, so what is tested here is the part where an off-by-one would be
silent and would only show up in someone else's program."""
import os, re, sys
ROOT=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
src=open(os.path.join(ROOT,'src','app.cpp'),encoding='utf-8').read()
i=src.index('String strTerm() {')
j=src.index('String strExpr()', i)
open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'strterm.inc'),'w').write(src[i:j])
print('extract: ok')
