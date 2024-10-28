#!/usr/bin/env bash
set -euxo pipefail

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

if [ ! -f "$SCRIPT_DIR/../third_party/AFLplusplus/afl-fuzz" ]; then
    cd "$SCRIPT_DIR/../third_party/AFLplusplus"
    make
fi

if [ ! -f "$SCRIPT_DIR/../src/test/fuzz/build" ]; then
    cmake -S "$SCRIPT_DIR/../" -B "$SCRIPT_DIR/../src/test/fuzz/build" -DCMAKE_C_COMPILER="$SCRIPT_DIR/../third_party/AFLplusplus/afl-clang-lto" -DCMAKE_CXX_COMPILER="$SCRIPT_DIR/../third_party/AFLplusplus/afl-clang-lto++" -DFUZZ_TESTING=ON
fi

cmake --build "$SCRIPT_DIR/../src/test/fuzz/build"

mkdir -p "$SCRIPT_DIR/../src/test/fuzz/inference/out"


export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_SKIP_CPUFREQ=1

"$SCRIPT_DIR/../third_party/AFLplusplus/afl-fuzz" -i "$SCRIPT_DIR/../src/test/fuzz/inference/in" -o "$SCRIPT_DIR/../src/test/fuzz/inference/out" "$SCRIPT_DIR/../src/test/fuzz/build/src/test/fuzz/inference/fuzz-inference"