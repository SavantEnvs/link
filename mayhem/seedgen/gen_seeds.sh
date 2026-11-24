#!/usr/bin/env bash
# Regenerates the committed seeds (mayhem/<target>/testsuite/) from gen_seeds.cpp.
# Run from the repo root inside the commit image. Not called by build.sh.
set -euo pipefail
SRC="${SRC:-/mayhem}"
cd "$SRC"
for t in link-audio-buffer link-audio-messenger discovery-messenger \
         ping-responder measurement; do mkdir -p "mayhem/$t/testsuite"; done
tmp="$(mktemp -d)"
"${CXX:-clang++}" -std=c++17 -w -DLINK_PLATFORM_LINUX=1 -DLINK_PLATFORM_UNIX=1 -DASIO_STANDALONE=1 \
  -Iinclude -isystem modules/asio-standalone/include -Imayhem/harnesses \
  mayhem/seedgen/gen_seeds.cpp src/ableton/test/serial_io/SchedulerTree.cpp -pthread -o "$tmp/gen"
"$tmp/gen" mayhem
rm -rf "$tmp"
