#!/usr/bin/env bash
# Build CardSat_Manual.pdf from MANUAL.md (pandoc + xelatex).
# Requires: pandoc, a xelatex with TeX Gyre + DejaVu Sans Mono fonts.
# Reproduces the themed book: green palette, ghost chapter numerals, custom
# cover, intro/Status front matter, per-chapter page breaks, boxed code.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
TMP="$(mktemp -d)"
VER="$(grep -oE 'FW_VERSION = "[0-9.]+"' "$ROOT/src/config.h" | grep -oE '[0-9.]+' | head -1)"

# 1) split MANUAL.md: drop H1 title; capture intro+Status (front matter);
#    drop the hand TOC; promote '## N. X' chapters to '# X'.
python3 - "$ROOT/MANUAL.md" "$TMP/body.md" "$TMP/intro.md" <<'PY'
import re,sys
lines=open(sys.argv[1]).read().split('\n')
start=0
for i,l in enumerate(lines):
    if l.strip()=='# CardSat User Manual': start=i+1; break
intro=[]; i=start
while i<len(lines) and lines[i].strip()!='## Contents': intro.append(lines[i]); i+=1
while i<len(lines) and not re.match(r'^## \d',lines[i]): i+=1   # skip Contents
body='\n'.join(lines[i:])
body=re.sub(r'^## (\d+)\.\s+',r'# ',body,flags=re.M)
for f in (body,):
    pass
sub=lambda s:s.replace('\u26a0\ufe0f','**!**').replace('\u26a0','**!**')
open(sys.argv[2],'w').write(sub(body))
open(sys.argv[3],'w').write(sub('\n'.join(intro).strip()))
PY

# 2) stamp version into the cover; build the front-matter include (cover + intro)
sed "s/Firmware v[0-9.]\\+/Firmware v$VER/" "$HERE/manual_cover.tex" > "$TMP/cover.tex"
pandoc "$TMP/intro.md" -t latex -o "$TMP/intro_body.tex"
{ cat "$TMP/cover.tex"; printf '\n%% intro / status front matter\n\\clearpage\n'; \
  cat "$TMP/intro_body.tex"; printf '\n\\vspace{1em}\n'; } > "$TMP/frontmatter.tex"

# 3) patched pandoc template (drop lmodern; fontspec supplies fonts)
pandoc -D latex | sed 's/^\(\s*\)\\usepackage{lmodern}/\1%\\usepackage{lmodern}/' > "$TMP/tpl.latex"

# 4) build
pandoc "$TMP/body.md" -o "$ROOT/CardSat_Manual.pdf" \
  --pdf-engine=xelatex --template="$TMP/tpl.latex" \
  --resource-path="$ROOT" \
  --toc --toc-depth=1 --number-sections \
  -V geometry:"a4paper,margin=2.0cm,top=2.2cm,bottom=2.0cm" -V fontsize=12pt \
  -V mainfont="TeX Gyre Pagella" -V sansfont="TeX Gyre Heros" -V monofont="DejaVu Sans Mono" \
  -V colorlinks=true -H "$HERE/manual_header.tex" --include-before-body="$TMP/frontmatter.tex"

# 5) metadata
python3 - "$ROOT/CardSat_Manual.pdf" <<'PY'
import sys
from pypdf import PdfReader, PdfWriter
r=PdfReader(sys.argv[1]); w=PdfWriter()
for p in r.pages: w.add_page(p)
w.add_metadata({'/Title':'CardSat — The Complete Operating Manual',
  '/Author':'Paul Stoetzer (N8HM)',
  '/Subject':'Amateur radio satellite tracker for the M5Stack Cardputer',
  '/Keywords':'CardSat, amateur radio, satellite, SGP4, M5Stack Cardputer',
  '/Creator':'CardSat book builder'})
open(sys.argv[1],'wb').write(b''); 
with open(sys.argv[1],'wb') as f: w.write(f)
PY

rm -rf "$TMP"
echo "Built $ROOT/CardSat_Manual.pdf (v$VER)"
