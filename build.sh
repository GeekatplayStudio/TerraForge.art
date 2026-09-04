#!/usr/bin/env bash
# Geekatplay TerraForge - configure + build (macOS / Linux)
#
# The POSIX twin of build.ps1: it fails loudly rather than leaving a stale
# binary behind, because "it still does the old thing" is the most expensive
# kind of build failure.
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ ! -d external/imgui ]; then
  printf '\033[31mDependencies missing. Run scripts/get_deps.sh first.\033[0m\n' >&2
  exit 1
fi

GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)

cmake -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build --parallel "$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

printf '\n\033[32mBuild complete: build/geekatplay_studio\033[0m\n'
printf '\033[90mRun tests:      ./test.sh\033[0m\n'
printf '\033[90mStart app:      ./start.sh\033[0m\n'
