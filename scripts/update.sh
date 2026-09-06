#!/bin/sh
# Geekatplay TerraForge - update and rebuild (macOS, Linux). Started from
# Settings > Updates. Waits for the application to close, pulls, rebuilds
# with build.sh and starts the new build.
#   update.sh <source dir> [pid] [branch]
src="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
pid="${2:-0}"
branch="${3:-main}"
cd "$src" || exit 1
if [ "$pid" -gt 0 ] 2>/dev/null; then
  i=0
  while kill -0 "$pid" 2>/dev/null && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done
fi
git fetch origin "$branch" || exit 1
git pull --ff-only origin "$branch" || exit 1
./build.sh || exit 1
if [ -x build/geekatplay_studio ]; then
  (cd build && nohup ./geekatplay_studio >/dev/null 2>&1 &)
elif [ -d build/geekatplay_studio.app ]; then
  open build/geekatplay_studio.app
fi
