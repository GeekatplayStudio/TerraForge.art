#!/usr/bin/env bash
# Geekatplay TerraForge - the full test suite (macOS / Linux)
# The POSIX twin of test.ps1. Every suite must pass before a commit.
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

[ -x build/nodeterrain_tests ] || { echo "Not built yet - running build.sh"; ./build.sh; }

fail=0
run() { # binary, label
  printf '\n\033[33m[%s] %s\033[0m\n' "$2" "$1"
  if ! "./build/$1"; then
    printf '\033[31m[ERROR] %s failed\033[0m\n' "$1"
    fail=1
  fi
}

run nodeterrain_tests  "1/7"
run engine_tests       "2/7"
run undo_tests         "3/7"
run node_tests         "4/7"
run regression_tests   "5/7"
run render_tests       "6/7"

printf '\n\033[33m[7/7] Python / multi-agent suite\033[0m\n'
if command -v python3 >/dev/null 2>&1; then
  python3 -m pytest tests -q || fail=1
else
  echo "python3 not found - skipping the Python suite"
fi

if [ "$fail" -ne 0 ]; then
  printf '\n\033[31m[FAILED] see above\033[0m\n'
  exit 1
fi
printf '\n\033[32m[ALL TESTS PASSED]\033[0m\n'
