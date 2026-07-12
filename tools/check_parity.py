#!/usr/bin/env python3
"""src<->CardSat.ino parity gate: every App:: definition and app.h method decl must appear in the .ino."""
import re, sys
ino = open('CardSat.ino', encoding='utf-8', errors='replace').read()
missing = 0
for src in ['src/app.cpp','src/net.cpp','src/satdb.cpp']:
    for m in re.finditer(r'^[A-Za-z_][\w:<>*& ]*\b(\w+::\w+)\s*\(', open(src, encoding='utf-8', errors='replace').read(), re.M):
        if m.group(1) + '(' not in ino: print('MISSING def:', src, m.group(1)); missing += 1
print('All src/ signatures present in CardSat.ino.' if not missing else f'{missing} missing.')
hdr = open('src/app.h', encoding='utf-8', errors='replace').read(); miss2 = 0
for m in re.finditer(r'^\s+(?:void|bool|int|String|uint\d+_t)\s+(\w+)\s*\(', hdr, re.M):
    if m.group(1) + '(' not in ino: print('MISSING decl:', m.group(1)); miss2 += 1
print('  all app.h class-method declarations present in .ino.' if not miss2 else f'  {miss2} decls missing.')
sys.exit(1 if (missing or miss2) else 0)
