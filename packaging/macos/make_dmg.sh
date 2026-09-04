#!/usr/bin/env bash
# Geekatplay TerraForge - build the macOS disk image.
#
#   ./packaging/macos/make_dmg.sh [--build] [--sign "Developer ID Application: ..."]
#
# Produces dist/TerraForge-<version>.dmg containing TerraForge.app beside a
# shortcut to /Applications - the drag-to-install layout every Mac user already
# knows, which is why the DMG is the installer here rather than a .pkg. A .pkg
# would need an installer package signature to be anything other than more
# friction.
#
# hdiutil is part of macOS, so there is no third-party dependency.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
[ "$(uname -s)" = Darwin ] || { echo "make_dmg.sh only runs on macOS" >&2; exit 1; }

args=()
identity="-"
while [ $# -gt 0 ]; do
  case "$1" in
    --build) args+=(--build) ;;
    --sign)  identity="$2"; args+=(--sign "$2"); shift ;;
    *) echo "unknown option: $1" >&2; exit 1 ;;
  esac
  shift
done

version="$(sed -n 's/.*project(.*VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
[ -n "$version" ] || version="0.0.0"

./packaging/macos/make_app.sh --out "$root/dist" "${args[@]}"

dist="$root/dist"
dmg="$dist/TerraForge-$version.dmg"
staging="$(mktemp -d)/TerraForge"
mkdir -p "$staging"

cp -R "$dist/TerraForge.app" "$staging/"
ln -s /Applications "$staging/Applications"
cat > "$staging/Read me first.txt" <<'EOF'
TerraForge for macOS
====================

1. Drag TerraForge.app onto the Applications folder shown beside it.
2. The first time you launch it, macOS will say it cannot check the app for
   malicious software, because this build is not notarised by Apple.
   Right-click TerraForge in Applications, choose Open, then Open again.
   You only have to do this once.

Requirements
  macOS 11 or newer, and a Mac that supports OpenGL 4.1 (every Mac since 2012
  does, Apple silicon included).

Optional extras, for the offline renderers and the AI assistant:
  python3 -m pip install mitsuba
  brew install ollama && ollama pull llama3.1

Your settings, logs and downloaded materials live in
  ~/Library/Application Support/GeekatplayTerraForge

To uninstall, drag TerraForge.app to the Trash.

Geekatplay Studio - Vladimir Shopine
https://github.com/GeekatplayStudio/TerraForge.art
EOF

rm -f "$dmg"
hdiutil create -volname "TerraForge $version" \
               -srcfolder "$staging" \
               -ov -format UDZO \
               "$dmg" >/dev/null

if [ "$identity" != "-" ]; then
  codesign --force --sign "$identity" "$dmg" || echo "  (dmg signing failed)"
fi

rm -rf "$(dirname "$staging")"
printf '\n\033[32mBuilt %s\033[0m\n' "$dmg"
