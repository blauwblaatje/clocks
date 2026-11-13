#!/bin/sh
# Builds the pipe around firmware/lib/crgclient and runs live.py against a CRG
# on localhost:PORT (default 4331). Start a throwaway server first, see
# tools/crg-capture/README.md.
set -e
here=$(cd "$(dirname "$0")" && pwd)
fw="$here/../../firmware"
out=${TMPDIR:-/tmp}/crgclient-pipe
g++ -std=c++17 -O1 -Wall -I"$fw/lib/crgclient" -I"$fw/lib/boxcore" -I"$fw/lib/boxctl" "$here/pipe.cpp" \
    "$fw"/lib/crgclient/*.cpp "$fw/lib/boxcore/boxcore.cpp" "$fw/lib/boxctl/boxctl.cpp" -o "$out"
python3 "$here/live.py" "$out" "${1:-4331}"
