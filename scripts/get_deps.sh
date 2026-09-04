#!/usr/bin/env bash
# TerraForge - fetch third-party dependencies into external/  (macOS / Linux)
#
# The POSIX twin of scripts/get_deps.ps1. The two must stay in step: same
# repositories, same pinned files, same imgui-node-editor patch. Nothing here
# is committed to the repository, which is why this script exists at all.
#
# Run once after cloning:  ./scripts/get_deps.sh
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ext="$root/external"
mkdir -p "$ext"
cd "$ext"

say()  { printf '\033[36m%s\033[0m\n' "$*"; }
skip() { printf '\033[90m%s\033[0m\n' "$*"; }
die()  { printf '\033[31m%s\033[0m\n' "$*" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || die "$1 is required but not installed"; }
need git

# macOS and most Linux distributions call it python3; some environments (Git
# Bash on Windows, a few older images) only have `python`. Either will do - the
# only thing it is used for is generating the OpenGL loader.
PY=""
for c in python3 python; do command -v "$c" >/dev/null 2>&1 && { PY="$c"; break; }; done
[ -n "$PY" ] || die "python 3 is required but not installed"

# curl ships with macOS; wget is the usual Linux alternative.
fetch() { # url, file
  [ -f "$2" ] && { skip "$2 already present"; return; }
  say "downloading $2..."
  if command -v curl >/dev/null 2>&1; then curl -fsSL "$1" -o "$2"
  else wget -q "$1" -O "$2"; fi
}

clone() { # url, dir, branch(optional)
  [ -d "$2" ] && { skip "$2 already present"; return; }
  say "cloning $2..."
  if [ -n "${3:-}" ]; then git clone --depth 1 --branch "$3" "$1" "$2"
  else git clone --depth 1 "$1" "$2"; fi
}

clone https://github.com/ocornut/imgui.git              imgui docking
clone https://github.com/glfw/glfw.git                  glfw
clone https://github.com/thedmd/imgui-node-editor.git   imgui-node-editor
clone https://github.com/g-truc/glm.git                 glm
# Manifold: the mesh module's solid-reconstruction engine (Apache-2.0),
# pinned to the tag the build names in GPX_MANIFOLD_VERSION. Optional -
# without it the application builds and that one repair stage reports
# itself unavailable rather than silently doing something weaker.
clone https://github.com/elalish/manifold.git           manifold v3.5.2

fetch https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp json.hpp
fetch https://raw.githubusercontent.com/nothings/stb/master/stb_image.h       stb_image.h
fetch https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h stb_image_write.h

# miniz: zip reading for the ambientCG material downloader
if [ ! -d miniz ]; then
  say "downloading miniz..."
  fetch https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip miniz.zip
  mkdir -p miniz && (cd miniz && unzip -oq ../miniz.zip)
  rm -f miniz.zip
fi

# glad OpenGL loader. Generated at 4.6 on every platform on purpose: the loader
# only resolves what the driver actually exports, so a macOS 4.1 context simply
# leaves the newer pointers null, and one generated tree serves all three
# platforms.
if [ ! -d glad ]; then
  say "generating glad loader..."
  "$PY" -m pip install --quiet --user glad2 2>/dev/null || "$PY" -m pip install --quiet glad2
  "$PY" -m glad --api "gl:core=4.6" --out-path glad c
fi

# imgui-node-editor ships an operator that current Dear ImGui already defines.
inl="$ext/imgui-node-editor/imgui_extra_math.inl"
if [ -f "$inl" ] && ! grep -q "IMGUI_VERSION_NUM < 19105" "$inl"; then
  say "patching imgui-node-editor for current Dear ImGui"
  "$PY" - "$inl" <<'PYPATCH'
import re, sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
s = re.sub(r"(inline ImVec2 operator\*\(const float lhs, const ImVec2& rhs\)\s*\{[^}]*\})",
           r"# if IMGUI_VERSION_NUM < 19105\n\1\n# endif", s, count=1, flags=re.S)
open(p, "w", encoding="utf-8").write(s)
PYPATCH
fi

printf '\n\033[32mDependencies ready. Now run: ./build.sh\033[0m\n'
