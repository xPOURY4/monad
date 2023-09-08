#!/bin/bash

set -Eeuo pipefail

CTEST_PARALLEL_LEVEL=${CTEST_PARALLEL_LEVEL:-$(nproc)} \
  ctest --output-on-failure --test-dir build

if readelf -p .comment build/*.so | grep clang; then
  pytest --pyargs monad
fi
