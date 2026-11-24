#!/usr/bin/env bash
#
# mayhem/build.sh — build Ableton Link's fuzz harnesses and its unit-test suite.
#
# Link is header-only C++17 (plus the bundled asio submodule, modules/asio-standalone),
# so "the project" is compiled inside each harness TU with $SANITIZER_FLAGS $DEBUG_FLAGS.
# Outputs:
#   build/fuzz/<harness>            libFuzzer binaries (Mayhem targets)
#   build/fuzz/<harness>-standalone run-once reproducers
#   build/tests/bin/Link*Test       upstream Catch unit tests, normal flags (mayhem/test.sh runs them)
# build/ is gitignored by upstream, so `git clean -ffdX` removes all of it.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

SRC="${SRC:-/mayhem}"
cd "$SRC"

ASIO_INC="$SRC/modules/asio-standalone/include"
if [ ! -f "$ASIO_INC/asio/version.hpp" ]; then
  echo "build.sh: bundled asio submodule missing ($ASIO_INC) — check out with submodules" >&2
  exit 1
fi

# 1) Upstream unit tests (LinkCoreTest, LinkDiscoveryTest, LinkAudioTest, LinkUtilTest),
#    upstream's own CMake + flags (Release), no sanitizers.
cmake -S . -B build/tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DLINK_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" -DCMAKE_EXE_LINKER_FLAGS="$COVERAGE_FLAGS"
cmake --build build/tests -j"$MAYHEM_JOBS" \
  --target LinkCoreTest LinkDiscoveryTest LinkAudioTest LinkUtilTest

# 2) Fuzz harnesses, each linked twice (libFuzzer + standalone reproducer).
OUT="$SRC/build/fuzz"
mkdir -p "$OUT"
CXXFLAGS_FUZZ=(-std=c++17 -O1 $SANITIZER_FLAGS $DEBUG_FLAGS
  -DLINK_PLATFORM_LINUX=1 -DLINK_PLATFORM_UNIX=1 -DASIO_STANDALONE=1 -DASIO_NO_TYPEID=1
  -Wno-multichar -I"$SRC/include" -isystem "$ASIO_INC" -I"$SRC/mayhem/harnesses")

$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OUT/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"

# Link's code is all headers, so it is compiled (and SanCov-instrumented) inside each harness TU:
# `-fsanitize=fuzzer-no-link` for the libFuzzer object, plain for the standalone object.
# SchedulerTree.cpp is the deterministic serial_io test scheduler the harnesses drive.
for v in fuzz standalone; do
  extra=(); [ "$v" = fuzz ] && extra=(-fsanitize=fuzzer-no-link)
  $CXX "${CXXFLAGS_FUZZ[@]}" "${extra[@]}" -c "$SRC/src/ableton/test/serial_io/SchedulerTree.cpp" \
    -o "$OUT/SchedulerTree.$v.o"
done

HARNESSES=(fuzz_link_audio_messenger fuzz_link_audio_buffer fuzz_discovery_messenger
           fuzz_ping_responder fuzz_measurement fuzz_link_audio_announce)
for h in "${HARNESSES[@]}"; do
  $CXX "${CXXFLAGS_FUZZ[@]}" -fsanitize=fuzzer-no-link -c "$SRC/mayhem/harnesses/$h.cpp" \
    -o "$OUT/$h.fuzz.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE \
    "$OUT/$h.fuzz.o" "$OUT/SchedulerTree.fuzz.o" "$OUT/lsan_off.o" -pthread -o "$OUT/$h"
  $CXX "${CXXFLAGS_FUZZ[@]}" -c "$SRC/mayhem/harnesses/$h.cpp" -o "$OUT/$h.standalone.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS \
    "$OUT/$h.standalone.o" "$OUT/SchedulerTree.standalone.o" "$OUT/standalone_main.o" \
    "$OUT/lsan_off.o" -pthread -o "$OUT/$h-standalone"
done
