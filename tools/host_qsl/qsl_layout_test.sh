#!/bin/sh
# Render the QSL card at every supported sink width and assert nothing overflows.
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
c++ -std=c++17 -O1 -Wall -Wextra -o "$OUT/qsl_layout_test" "$DIR/qsl_layout_test.cpp"
"$OUT/qsl_layout_test"
