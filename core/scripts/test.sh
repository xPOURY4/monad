#!/bin/bash

set -Eeuo pipefail

ctest -E ^ethash/* \
  --test-dir build

if readelf -p .comment build/*.so | grep clang; then
  pytest --pyargs monad
fi
