#!/usr/bin/env bash
# Host-side orbital/Doppler audit: compiles the REAL src/predict.cpp and the REAL
# gpToTle (extracted verbatim at build time) against the Hopperpop Sgp4 library,
# runs them on a fresh TLE, and cross-checks every output against skyfield.
#
# Usage:  tools/host_orbit_audit/build.sh [SGP4_LIB_SRC_DIR]
#   default lib dir: ~/Arduino/libraries/Sgp4/src (or Sgp4-Library/src)
# Needs: g++, python3 + `pip install skyfield sgp4`, network for the TLE
# (falls back to AMSAT nasabare.txt if CelesTrak is unreachable) and the JPL
# de421.bsp ephemeris (auto-downloaded via curl on first run).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/../.."
LIB="${1:-$HOME/Arduino/libraries/Sgp4/src}"
[ -d "$LIB" ] || LIB="$HOME/Arduino/libraries/Sgp4-Library/src"
[ -d "$LIB" ] || { echo "Sgp4 library src dir not found; pass it as arg 1"; exit 1; }
W="$HERE/work"; rm -rf "$W"; mkdir -p "$W"; cd "$W"

for f in predict.h predict.cpp location.h satdb.h; do cp "$ROOT/src/$f" .; done
cp -r "$LIB" sgp4src
# Host-only identifier renames (glibc `daylight` extern; gcc `unix` macro):
sed -i 's/\bdaylight\b/sgp4_daylight/g; s/\bunix\b/unixt/g' sgp4src/*.h sgp4src/*.cpp

cat > Arduino.h <<'EOH'
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
class String : public std::string {
public:
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  String(double v) : std::string(std::to_string(v)) {}
  String(int v) : std::string(std::to_string(v)) {}
};
class Stream {};
class File;    // referenced by satdb.h signatures the harness never calls
EOH
cat > config.h <<'EOH'
#pragma once
#include <stdint.h>
static constexpr double C_LIGHT = 299792458.0;
static constexpr int MAX_SATS = 150;
static constexpr int MAX_TX_PER_SAT = 64;
static constexpr int MUTUAL_PASS_SCAN = 64;
static constexpr int MUTUAL_HORIZON_DAYS = 10;
EOH

python3 - "$ROOT/src/satdb.cpp" <<'EOP'
import sys
src = open(sys.argv[1]).read()
def grab(sig):
    i = src.index(sig); b = src.index('{', i); d = 0; j = b
    while j < len(src):
        if src[j] == '{': d += 1
        elif src[j] == '}':
            d -= 1
            if d == 0: break
        j += 1
    return src[i:j+1]
sigs = ['static void encCatalog', 'static void encNdot', 'static void encExp',
        'static void putAt', 'static int tleChecksum', 'bool SatDb::gpToTle']
open('gptotle_extracted.cpp', 'w').write(
    '// EXTRACTED VERBATIM from src/satdb.cpp at build time\n'
    '#include "satdb.h"\n#include <string.h>\n#include <math.h>\n'
    '#include <time.h>\n#include <stdio.h>\n\n' + '\n\n'.join(grab(s) for s in sigs) + '\n')
EOP

cp "$HERE/main.cpp" "$HERE/compare.py" .
curl -s --max-time 20 "https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE" -o iss.tle || true
grep -q "^1 25544" iss.tle 2>/dev/null || \
  curl -s --max-time 20 "https://www.amsat.org/tle/current/nasabare.txt" | grep -A2 "^ISS" | head -3 > iss.tle
grep -q "^1 25544" iss.tle || { echo "TLE fetch failed"; exit 1; }
[ -f "$HERE/de421.bsp" ] && cp "$HERE/de421.bsp" . || \
  curl -sk --max-time 180 -o de421.bsp "https://ssd.jpl.nasa.gov/ftp/eph/planets/bsp/de421.bsp"

CXX="g++ -std=c++17 -O2 -w -I. -Isgp4src"
$CXX -c predict.cpp
$CXX -c gptotle_extracted.cpp
for f in sgp4unit sgp4io sgp4ext sgp4coord sgp4pred brent visible; do
  $CXX -include Arduino.h -c sgp4src/$f.cpp -o $f.o
done
$CXX -c main.cpp
g++ -o orbtest *.o -lm
./orbtest > run1.txt
echo "---- harness ----"; sed -n '1,12p' run1.txt
echo "---- skyfield cross-check ----"; python3 compare.py
