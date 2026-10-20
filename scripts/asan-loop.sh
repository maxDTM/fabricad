#!/usr/bin/env bash
# FabriCAD: build the GUI tool tests with AddressSanitizer and run them in a loop.
#
# Guards against the class of crash fixed in PropertyPanel::rebuild() (a widget deleted while
# it was still emitting the signal that triggered the rebuild), which only showed up in about
# one run out of forty without a sanitizer. Intended for CI; on the dev box keep -j2.
#
#   scripts/asan-loop.sh [iterations=50] [build-dir=build-asan]
set -euo pipefail
iterations="${1:-50}"
dir="${2:-build-asan}"
root="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$root" -B "$root/$dir" -DCMAKE_BUILD_TYPE=Debug -DFABRICAD_SANITIZE=ON >/dev/null
cmake --build "$root/$dir" -j2 --target fabricad_part_test_tools fabricad_app_test_app_logic fabricad_assembly_test_assembly_tools
export QT_QPA_PLATFORM=offscreen
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
for t in fabricad_part_test_tools fabricad_app_test_app_logic fabricad_assembly_test_assembly_tools; do
  for ((i = 1; i <= iterations; i++)); do
    if ! "$root/$dir/tests/$t" >"/tmp/fabricad-asan-$t.log" 2>&1; then
      echo "FAILED: $t on iteration $i (log: /tmp/fabricad-asan-$t.log)"
      tail -40 "/tmp/fabricad-asan-$t.log"
      exit 1
    fi
  done
  echo "ok: $t x $iterations"
done
