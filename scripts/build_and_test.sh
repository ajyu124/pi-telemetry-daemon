#!/usr/bin/env bash
# =============================================================================
# build_and_test.sh
#
# PURPOSE
#   Compiles the project and runs every test. This is the one command to run
#   after changing any code.
#
# HOW TO RUN (from anywhere inside the project)
#   bash scripts/build_and_test.sh                normal build, in build/
#   bash scripts/build_and_test.sh --sanitizers   self-checking build, in build-sanitizers/
#   bash scripts/build_and_test.sh --clean        delete old build output first
#
# WHAT ARE SANITIZERS?
#   Extra checks the compiler can add to the program so that it detects its
#   own memory errors and undefined behaviour while running. The program
#   runs slower, so it is a separate build. If the tests pass under
#   sanitizers, a whole class of C++ bugs has been ruled out.
#
# THE THREE STEPS THIS SCRIPT RUNS
#   1. cmake -S . -B build     "configure": read CMakeLists.txt, find libraries
#   2. cmake --build build     "build": compile the code
#   3. ctest --test-dir build  "test": run all tests
# =============================================================================

set -euo pipefail

# Move to the project root (the folder above scripts/), so this script works
# no matter which folder it is called from.
cd "$(dirname "$0")/.."

build_directory="build"
sanitizer_setting="OFF"
clean_first="no"

for argument in "$@"; do
    case "$argument" in
        --sanitizers)
            build_directory="build-sanitizers"
            sanitizer_setting="ON"
            ;;
        --clean)
            clean_first="yes"
            ;;
        *)
            echo "Unknown option: $argument"
            echo "Usage: bash scripts/build_and_test.sh [--sanitizers] [--clean]"
            exit 1
            ;;
    esac
done

if ! command -v cmake >/dev/null 2>&1; then
    echo "Error: cmake is not installed. Run this first:  bash scripts/setup_ubuntu.sh"
    exit 1
fi

if [ "$clean_first" = "yes" ]; then
    echo "==> Deleting $build_directory/"
    rm -rf "$build_directory"
fi

echo "==> Step 1 of 3: configure"
cmake -S . -B "$build_directory" -DTELEMETRY_ENABLE_SANITIZERS="$sanitizer_setting"

echo
echo "==> Step 2 of 3: build"
# -j uses several CPU cores so compiling is faster.
cmake --build "$build_directory" -j "$(nproc 2>/dev/null || echo 2)"

echo
echo "==> Step 3 of 3: test"
ctest --test-dir "$build_directory" --output-on-failure

echo
echo "All tests passed. The program is at: $build_directory/telemetry_daemon"
echo "Next step:  bash scripts/run_demo.sh"
