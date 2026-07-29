#!/usr/bin/env python3
"""Extract the orbital-zone membership math from src/app.cpp for host testing:
lShellAt, the IGRF-14 table and evaluator, the field-line shell walk (shellAt /
maybeInBelt) and zoneContains. All are App methods or file statics, so we lift them
into free functions the harness can call, keeping the tested code the code that ships
(same idea as host_basic and host_muf). We drop the App:: qualifier and replace the
timeIsSet()/nowUtc() clock reads with an explicit test hook (g_testYears), so a host
run is deterministic. Field-model correctness itself is checked separately, against an
independent implementation, in tools/host_geomag."""
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(ROOT, 'src', 'app.cpp')
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'zones_region.inc')

def grab(src, sig):
    a = src.find(sig)
    if a < 0: raise SystemExit('extract_zones: not found: ' + sig)
    i = src.find('{', a); depth = 0
    while i < len(src):
        if src[i] == '{': depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0: return src[a:i+1]
        i += 1
    raise SystemExit('extract_zones: unbalanced: ' + sig)

def main():
    src = open(APP, encoding='utf-8').read()
    # The IGRF coefficient table + the geometry helpers the belt test now needs.
    ti = src.index('static const uint8_t IGRF_NMAX')
    tj = src.index('// Years past the IGRF epoch')
    table = src[ti:tj]
    shellf = grab(src, 'App::ShellInfo App::shellAt(')
    maybef = grab(src, 'bool App::maybeInBelt(')
    lshell = grab(src, 'double App::lShellAt(')
    zc     = grab(src, 'bool App::zoneContains(')
    table  = table.replace('void App::igrfField', 'void igrfField')
    shellf = shellf.replace('App::ShellInfo App::shellAt(', 'ShellInfo shellAt(')
    maybef = maybef.replace('bool App::maybeInBelt(', 'bool maybeInBelt(')
    # De-methodise: drop App::.
    lshell = lshell.replace('double App::lShellAt(', 'double lShellAt(')
    zc     = zc.replace('bool App::zoneContains(', 'bool zoneContains(')
    # Replace the ZONE_* enum names with literals so we don't need app.h.
    for name, val in [('ZONE_SAA','0'),('ZONE_ECLIPSE','1'),('ZONE_POLAR','2'),
                      ('ZONE_INNER','3'),('ZONE_OUTER','4')]:
        zc = zc.replace(name, val)
    # Replace the timeIsSet()/nowUtc() drift block with a fixed test hook: the harness
    # sets a global g_testYears; substitute the whole guarded assignment.
    zc = re.sub(r'double yrs = 0;\s*\n\s*if \(timeIsSet\(\).*?2025\.0; \}',
                'double yrs = g_testYears;', zc, flags=re.S)
    # shellAt() reads the clock through igrfYears(); the harness pins it instead.
    shellf = shellf.replace('const float yrs = igrfYears();',
                            'const float yrs = (float)g_testIgrfYears;')
    open(OUT, 'w', encoding='utf-8').write(
        '// AUTO-EXTRACTED from src/app.cpp by extract_zones.py -- do not edit.\n'
        'static double g_testYears = 0.0;      // SAA ellipse drift (years past 2025.0)\n'
        'static double g_testIgrfYears = 1.5;  // IGRF secular-variation epoch offset\n'
        'static constexpr float ZONE_BRATIO_MAX = 3.0f;\n'
        'struct ShellInfo { float bSat=0, b0=0, shellL=0, bRatio=1; };\n'
        'static void igrfVec(const float p[3], float yrs, float b[3]);\n\n'
        + table + '\n'
        '''static void igrfVec(const float p[3], float yrs, float b[3]) {
  float r = sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
  if (r < 1.0f) { b[0]=b[1]=b[2]=0; return; }
  float colat = acosf(p[2]/r)*57.2957795f, lon = atan2f(p[1],p[0])*57.2957795f;
  float Br,Bt,Bp; igrfField(r,colat,lon,yrs,Br,Bt,Bp);
  float th=colat/57.2957795f, ph=lon/57.2957795f;
  float st=sinf(th),ct=cosf(th),sp=sinf(ph),cp=cosf(ph);
  b[0]=Br*st*cp+Bt*ct*cp-Bp*sp; b[1]=Br*st*sp+Bt*ct*sp+Bp*cp; b[2]=Br*ct-Bt*st;
}
''' + '\n' + maybef + '\n\n' + shellf + '\n\n' + lshell + '\n\n' + zc + '\n')
    print('extract_zones: wrote IGRF table + shellAt + maybeInBelt + lShellAt + zoneContains')
    return 0

if __name__ == '__main__':
    sys.exit(main())
