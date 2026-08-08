#!/bin/sh
# Build + run the CSUH wire-codec verification against src/csuh_proto.h as shipped.
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
c++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter \
    -o "$OUT/csuh_frames_test" "$DIR/csuh_frames_test.cpp"
"$OUT/csuh_frames_test"
