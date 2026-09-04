#!/usr/bin/env bash
# Geekatplay TerraForge - assemble TerraForge.app.
#
#   ./packaging/macos/make_app.sh [--out dist] [--build] [--sign "Developer ID Application: ..."]
#
# The bundle is assembled here rather than left to CMake's MACOSX_BUNDLE
# because an installed TerraForge is more than one executable: the Python
# render layer, the examples and the node reference travel with it, and the
# application looks for them in Contents/Resources (studio/paths.cpp).
#
# Layout produced:
#   TerraForge.app/Contents/
#     Info.plist
#     MacOS/TerraForge          the studio binary
#     Resources/terraforge.icns the icon, generated from resources/icon_256.png
#     Resources/orchestrator    offline renderers (Mitsuba, Cycles, LuxCore)
#     Resources/mcp_server      scripting and MCP tools
#     Resources/examples        sample projects and macros
#     Resources/resources       the application's own icons
#
# Signing: the bundle is always ad-hoc signed ("codesign -s -"). On Apple
# silicon an unsigned binary does not merely warn, it refuses to launch, so
# this is not optional. Pass --sign with a real Developer ID to produce
# something distributable without the right-click-Open dance.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="$root/dist"
do_build=0
identity="-"

while [ $# -gt 0 ]; do
  case "$1" in
    --out)   out="$2"; shift ;;
    --build) do_build=1 ;;
    --sign)  identity="$2"; shift ;;
    *) echo "unknown option: $1" >&2; exit 1 ;;
  esac
  shift
done

[ "$(uname -s)" = Darwin ] || { echo "make_app.sh only runs on macOS" >&2; exit 1; }
cd "$root"

version="$(sed -n 's/.*project(.*VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
[ -n "$version" ] || version="0.0.0"

[ "$do_build" -eq 1 ] && ./build.sh

# CMake may produce a bare executable or its own bundle depending on the
# MACOSX_BUNDLE property; take whichever is there.
bin=""
for c in build/geekatplay_studio \
         build/geekatplay_studio.app/Contents/MacOS/geekatplay_studio; do
  [ -f "$c" ] && { bin="$c"; break; }
done
[ -n "$bin" ] || { echo "not built: run ./build.sh first" >&2; exit 1; }

app="$out/TerraForge.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"

cp "$bin" "$app/Contents/MacOS/TerraForge"
chmod +x "$app/Contents/MacOS/TerraForge"

# The build carries -g so a crash report resolves to file:line; that is most of
# the binary's size and none of it is needed on a user's machine, because the
# addresses in a report are module offsets that resolve against the unstripped
# build. Keep those symbols beside the bundle instead of discarding them.
mkdir -p "$out/symbols"
cp "$bin" "$out/symbols/geekatplay_studio"
if command -v dsymutil >/dev/null 2>&1; then
  dsymutil "$bin" -o "$out/symbols/TerraForge.dSYM" >/dev/null 2>&1 || true
fi
strip -S "$app/Contents/MacOS/TerraForge" 2>/dev/null || true
printf 'APPL????' > "$app/Contents/PkgInfo"

sed -e "s/@VERSION@/$version/g" \
    "$root/packaging/macos/Info.plist.in" > "$app/Contents/Info.plist"

# The icon. sips + iconutil ship with macOS, so no third-party tool is needed;
# a missing source icon is a warning, not a failure - the app runs either way.
icon_src="$root/studio/resources/icon_256.png"
if [ -f "$icon_src" ]; then
  iconset="$(mktemp -d)/terraforge.iconset"
  mkdir -p "$iconset"
  for size in 16 32 64 128 256 512; do
    sips -z $size $size "$icon_src" --out "$iconset/icon_${size}x${size}.png" >/dev/null 2>&1
    half=$((size / 2))
    sips -z $size $size "$icon_src" --out "$iconset/icon_${half}x${half}@2x.png" >/dev/null 2>&1
  done
  iconutil -c icns "$iconset" -o "$app/Contents/Resources/terraforge.icns" 2>/dev/null \
    || echo "  (iconutil failed; the app will use the default icon)"
  rm -rf "$(dirname "$iconset")"
else
  echo "  (no $icon_src; the app will use the default icon)"
fi

for tree in orchestrator mcp_server examples; do
  [ -d "$root/$tree" ] && cp -R "$root/$tree" "$app/Contents/Resources/"
done
cp -R "$root/studio/resources" "$app/Contents/Resources/resources"
for doc in README.md LICENSE requirements.txt docs/NODES.md docs/INSTALL.md; do
  [ -f "$root/$doc" ] && cp "$root/$doc" "$app/Contents/Resources/"
done
find "$app" -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true

# Signing must come last: anything copied in afterwards invalidates it.
codesign --force --deep --sign "$identity" --timestamp=none "$app" 2>/dev/null \
  || echo "  (codesign failed - the app may refuse to launch on Apple silicon)"

printf '\n\033[32mBuilt %s (version %s)\033[0m\n' "$app" "$version"
if [ "$identity" = "-" ]; then
  echo "Ad-hoc signed. On another Mac, macOS will ask the first time:"
  echo "  right-click TerraForge.app -> Open -> Open"
fi
