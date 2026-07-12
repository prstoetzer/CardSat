#!/usr/bin/env python3
"""Brace/paren/bracket balance gate (comment- and string-aware). Run from the repo root."""
import glob, sys
def strip(t):
    out=[]; i=0; n=len(t); mode=0  # 0 code,1 //,2 /* */,3 "str",4 'chr',5 R"raw(
    raw_delim=''
    while i<n:
        c=t[i]; nx=t[i+1] if i+1<n else ''
        if mode==0:
            if c=='/' and nx=='/': mode=1; i+=2; continue
            if c=='/' and nx=='*': mode=2; i+=2; continue
            if c=='R' and nx=='"':
                j=t.find('(',i+2)
                if j!=-1 and j-i<20: raw_delim=t[i+2:j]; mode=5; i=j+1; continue
            if c=='"': mode=3; i+=1; continue
            if c=="'": mode=4; i+=1; continue
            out.append(c); i+=1
        elif mode==1:
            if c=='\n': mode=0; out.append(c)
            i+=1
        elif mode==2:
            if c=='*' and nx=='/': mode=0; i+=2
            else: i+=1
        elif mode==3:
            if c=='\\': i+=2
            elif c=='"': mode=0; i+=1
            else: i+=1
        elif mode==4:
            if c=='\\': i+=2
            elif c=="'": mode=0; i+=1
            else: i+=1
        elif mode==5:
            end=')'+raw_delim+'"'
            j=t.find(end,i)
            if j==-1: i=n
            else: i=j+len(end); mode=0
    return ''.join(out)
bad=0; files=sorted(glob.glob('src/*.h')+glob.glob('src/*.cpp')+['CardSat.ino'])
for f in files:
    s=strip(open(f,encoding='utf-8',errors='replace').read())
    d=(s.count('{')-s.count('}'), s.count('(')-s.count(')'), s.count('[')-s.count(']'))
    if d!=(0,0,0): print(f'{f:28s} ({d[0]:+d},{d[1]:+d},{d[2]:+d})  <-- IMBALANCE'); bad+=1
print(f'{len(files)} files scanned, {bad} imbalanced.'); sys.exit(1 if bad else 0)
