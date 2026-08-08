#!/bin/sh
# Compile and run src/usbhelper.cpp against a mock helper, using the minimal
# Arduino shim in shim/. The shipped source is copied into a scratch directory so
# its quoted includes ("config.h", "rig.h", "Arduino.h") resolve to the shim
# rather than to the real firmware headers -- which is what lets the link layer be
# tested without dragging in M5GFX, the CAT backends, or an ESP32 toolchain.
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

cp "$ROOT/src/usbhelper.cpp" "$ROOT/src/usbhelper.h" "$ROOT/src/csuh_proto.h" "$OUT/"
cp "$DIR/shim/Arduino.h" "$DIR/shim/config.h" "$DIR/shim/rig.h" "$OUT/"
cp "$DIR/csuh_link_test.cpp" "$OUT/"

c++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -I"$OUT" \
    -o "$OUT/csuh_link_test" "$OUT/usbhelper.cpp" "$OUT/csuh_link_test.cpp"
"$OUT/csuh_link_test"
