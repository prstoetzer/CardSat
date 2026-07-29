#!/usr/bin/env python3
"""
Regenerate the IGRF coefficient table embedded in src/app.cpp (and CardSat.ino).

Source of truth: the IAGA/NGDC distribution file
  https://www.ngdc.noaa.gov/IAGA/vmod/coeffs/igrf14coeffs.txt
a copy of which is kept in tools/host_geomag/ so the table can be regenerated and
audited offline. Prints the C block; paste it (via tools/dual_edit.py) when the
model is superseded -- IGRF is reissued every 5 years, and the secular-variation
coefficients embedded here are only valid to 2030.
"""
import sys
NMAX, SVMAX = 13, 8
def idx(n, m): return n*(n+1)//2 + m
rows = []
for line in open(sys.argv[1] if len(sys.argv) > 1 else 'host_geomag/igrf14coeffs.txt'):
    if line.startswith(('#', 'c/s', 'g/h')): continue
    p = line.split()
    if p: rows.append((p[0], int(p[1]), int(p[2]), float(p[-2]), float(p[-1])))
N, NSV = idx(NMAX, NMAX)+1, idx(SVMAX, SVMAX)+1
G, H, GSV, HSV = ([0.0]*N for _ in range(4))
for gh, n, m, v, sv in rows:
    (G if gh == 'g' else H)[idx(n, m)] = v
    (GSV if gh == 'g' else HSV)[idx(n, m)] = sv
def fmt(a):
    return '\n'.join('  ' + ' '.join(f'{x:>10.2f}f,' for x in a[i:i+6])
                     for i in range(0, len(a), 6))
print(f'static const float IGRF_G[{N}] = {{\n{fmt(G)}\n}};')
print(f'static const float IGRF_H[{N}] = {{\n{fmt(H)}\n}};')
print(f'static const float IGRF_GSV[{NSV}] = {{\n{fmt(GSV[:NSV])}\n}};')
print(f'static const float IGRF_HSV[{NSV}] = {{\n{fmt(HSV[:NSV])}\n}};')
