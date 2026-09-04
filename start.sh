#!/usr/bin/env bash
# Geekatplay TerraForge - launch the studio (macOS / Linux)
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ ! -x build/geekatplay_studio ]; then
  printf '\033[33mNot built yet - running build.sh first...\033[0m\n'
  ./build.sh
fi

# Run from build/, the way the Windows launcher does: the application finds the
# Python render layer relative to its own location, and logs land in ./logs.
cd build
exec ./geekatplay_studio "$@"
