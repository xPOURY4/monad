#!/usr/bin/env bash

set -euxo pipefail

./scripts/apply-clang-tidy-fixes.sh build run-clang-tidy-19
if [[ ! -z "$(git status --untracked-files=no --porcelain)" ]]; then
  echo "Fixes applied; please re-run locally and commit!"
  exit 1
fi