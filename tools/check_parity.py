#!/usr/bin/env python3
"""src<->CardSat.ino parity gate: every App:: definition and app.h method decl must appear in the .ino.

Also checks TYPE declarations (enum/struct/class) in the src headers that the .ino
must mirror. That third check exists because it was missing: 0.9.58-wip added an
`enum Stage` to usbserial.h and not to the .ino, every gate passed, and the Arduino
build failed with "'Stage' does not name a type". The src build gets these from the
header; the monolithic .ino has no header, so a type added to one and not the other
breaks exactly one of the two representations -- which is the invariant this gate exists
to protect.
"""
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

# ---- type declarations in inlined headers ----
# These headers are inlined verbatim into the .ino, so every type they declare must
# appear there too. (app.h is excluded: the .ino mirrors its class bodies differently.)
INLINED_HEADERS = ['src/usbserial.h', 'src/rig.h', 'src/lora.h', 'src/config.h']
miss3 = 0
for hdrp in INLINED_HEADERS:
    try: text = open(hdrp, encoding='utf-8', errors='replace').read()
    except FileNotFoundError: continue
    for m in re.finditer(r'^\s*(enum(?:\s+class)?|struct|class)\s+(\w+)', text, re.M):
        kind, name = m.group(1), m.group(2)
        if not re.search(r'^\s*' + re.escape(kind) + r'\s+' + re.escape(name) + r'\b', ino, re.M):
            print(f'MISSING type: {hdrp}: {kind} {name}'); miss3 += 1
print('  all inlined-header type declarations present in .ino.' if not miss3 else f'  {miss3} types missing.')
sys.exit(1 if (missing or miss2 or miss3) else 0)
