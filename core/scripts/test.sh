#!/bin/bash

set -Eeuo pipefail

ctest -E ^ethash/* \
  --output-on-failure \
  --test-dir build

if readelf -p .comment build/*.so | grep clang; then
  pytest --pyargs monad
fi
