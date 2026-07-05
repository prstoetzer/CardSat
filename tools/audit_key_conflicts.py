import re, sys
sys.path.insert(0,'/home/claude/cardsat')
import mirror_tool
src=open('src/app.cpp').read()
handlers=re.findall(r'void App::(key\w+)\(', src)
real=[]
for h in handlers:
    body=mirror_tool.body_by_sig(src,'void App::'+h+'(')
    if not body: continue
    # Only count lines that are an actual IF-guard on the key: start with 'if (c == '
    guards={}
    for line in body.split('\n'):
        s=line.strip()
        m=re.match(r"if \(c == '(\\?.)'", s)
        if m:
            ch=m.group(1)
            guards.setdefault(ch,[]).append(s)
    for ch,lines in guards.items():
        if len(lines)>1:
            real.append((h,ch,lines))
if not real:
    print("No real duplicate IF-guard bindings.")
for h,ch,lines in real:
    print(f"** {h}: key '{ch}' has {len(lines)} separate if-guards")
    for l in lines: print(f"     {l[:80]}")
    print()
