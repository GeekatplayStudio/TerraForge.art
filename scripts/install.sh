#!/usr/bin/env bash
# Geekatplay TerraForge - one-click install for macOS and Linux.
#
# Run it by double-clicking install.command (macOS), or directly:
#     ./scripts/install.sh
#
# What it does, in order:
#   1. finds or installs the build tools (git, CMake, Ninja, a C++20 compiler)
#   2. fetches the third-party sources into external/
#   3. builds the studio
#   4. macOS: assembles TerraForge.app and copies it to /Applications
#      Linux: installs to ~/.local and writes a .desktop entry
#
# Flags:
#   --no-install-tools   fail instead of installing anything with a package manager
#   --dev                stop after building; do not install anywhere
#   --prefix <path>      install somewhere other than the default
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

say()  { printf '\033[36m%s\033[0m\n' "$*"; }
ok()   { printf '\033[32m%s\033[0m\n' "$*"; }
warn() { printf '\033[33m%s\033[0m\n' "$*"; }
die()  { printf '\n\033[31m%s\033[0m\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

install_tools=1
dev=0
prefix=""
while [ $# -gt 0 ]; do
  case "$1" in
    --no-install-tools) install_tools=0 ;;
    --dev)              dev=1 ;;
    --prefix)           prefix="${2:-}"; shift ;;
    -h|--help)          sed -n '2,20p' "$0"; exit 0 ;;
    *)                  die "unknown option: $1" ;;
  esac
  shift
done

case "$(uname -s)" in
  Darwin) os=mac ;;
  Linux)  os=linux ;;
  *)      die "This script covers macOS and Linux. On Windows, double-click install.bat." ;;
esac

echo
printf '\033[36m======================================================================\033[0m\n'
printf '  Geekatplay TerraForge - installer (%s)\n' "$os"
printf '\033[36m======================================================================\033[0m\n'
echo

# ---------------------------------------------------------------- 1. tooling
say "Checking build tools"

if [ "$os" = mac ]; then
  # The compiler comes from the Command Line Tools, which are a 1-line install
  # and a prerequisite for Homebrew anyway.
  if ! xcode-select -p >/dev/null 2>&1; then
    if [ "$install_tools" -eq 0 ]; then die "Xcode Command Line Tools are missing: xcode-select --install"; fi
    say "  Command Line Tools    installing (a system dialog will appear)..."
    xcode-select --install || true
    echo "  Finish that installer, then run this script again."
    exit 1
  fi
  printf '  %-22s found\n' "Command Line Tools"

  if ! have brew && [ "$install_tools" -eq 1 ]; then
    warn "  Homebrew              not found"
    echo  "  TerraForge needs CMake and Ninja. Install Homebrew with:"
    echo  '    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"'
    echo  "  then run this script again. (Or install cmake and ninja yourself.)"
    have cmake || exit 1
  fi
fi

ensure() { # command, brew formula, apt package, label
  local cmd="$1" brewpkg="$2" aptpkg="$3" label="$4"
  if have "$cmd"; then printf '  %-22s found\n' "$label"; return 0; fi
  if [ "$install_tools" -eq 0 ]; then die "$label is required and not installed."; fi
  if [ "$os" = mac ] && have brew; then
    say "  $label installing via Homebrew..."
    brew install "$brewpkg" >/dev/null || true
  elif have apt-get; then
    say "  $label installing via apt..."
    sudo apt-get install -y "$aptpkg" >/dev/null || true
  elif have dnf; then
    sudo dnf install -y "$aptpkg" >/dev/null || true
  elif have pacman; then
    sudo pacman -S --noconfirm "$aptpkg" >/dev/null || true
  fi
  have "$cmd" || die "$label is still missing. Install it and run this again."
  printf '  %-22s installed\n' "$label"
}

ensure git   git   git   "git"
ensure cmake cmake cmake "CMake"
ensure ninja ninja ninja-build "Ninja"
if [ "$os" = linux ]; then
  # GLFW needs the X11/Wayland development headers to build; without them the
  # CMake configure fails with a message that does not say what to install.
  if have apt-get && ! [ -f /usr/include/X11/Xlib.h ]; then
    say "  X11 headers           installing via apt..."
    sudo apt-get install -y libx11-dev libxrandr-dev libxinerama-dev \
         libxcursor-dev libxi-dev libgl1-mesa-dev >/dev/null || true
  fi
fi
have python3 && printf '  %-22s found\n' "Python 3" || warn "  Python 3              missing (optional: offline renderers, AI)"

compiler=""
for c in c++ g++ clang++; do have "$c" && { compiler="$c"; break; }; done
[ -n "$compiler" ] || die "No C++ compiler found."
printf '  %-22s %s\n' "C++ compiler" "$compiler"

# ------------------------------------------------------------ 2. dependencies
echo
say "Fetching third-party sources into external/"
chmod +x scripts/get_deps.sh build.sh test.sh start.sh 2>/dev/null
./scripts/get_deps.sh || die "Dependency download failed (see above)."

# ------------------------------------------------------------------ 3. build
echo
say "Building (a few minutes the first time)"
./build.sh || die "Build failed (see the errors above)."

if have python3; then
  echo
  say "Installing the optional Python layer (offline renderers, AI assistant)"
  python3 -m pip install --quiet --disable-pip-version-check --user -r requirements.txt 2>/dev/null \
    || python3 -m pip install --quiet --disable-pip-version-check --break-system-packages -r requirements.txt 2>/dev/null \
    || warn "  pip failed; TerraForge still runs, the offline renderers will not."
fi

if [ "$dev" -eq 1 ]; then
  echo; ok "Built. Run it with ./start.sh"
  exit 0
fi

# ---------------------------------------------------------------- 4. install
echo
if [ "$os" = mac ]; then
  dest="${prefix:-/Applications}"
  say "Assembling TerraForge.app"
  ./packaging/macos/make_app.sh --out "$root/dist" || die "Could not assemble the app bundle."
  app="$root/dist/TerraForge.app"
  say "Installing to $dest"
  if [ -w "$dest" ]; then
    rm -rf "$dest/TerraForge.app"
    cp -R "$app" "$dest/" || die "Could not copy the app to $dest."
  else
    warn "  $dest is not writable; installing to ~/Applications instead"
    dest="$HOME/Applications"
    mkdir -p "$dest"
    rm -rf "$dest/TerraForge.app"
    cp -R "$app" "$dest/" || die "Could not copy the app to $dest."
  fi
  echo
  ok "TerraForge is installed."
  echo "  Application:  $dest/TerraForge.app"
  echo "  Uninstall:    drag it to the Trash"
  echo "  Your settings live in ~/Library/Application Support/GeekatplayTerraForge"
  echo
  read -r -p "Start TerraForge now? [Y/n] " answer
  case "${answer:-Y}" in [Yy]*|"") open "$dest/TerraForge.app" ;; esac
else
  dest="${prefix:-$HOME/.local}"
  say "Installing to $dest"
  mkdir -p "$dest/lib/terraforge" "$dest/bin" "$dest/share/applications"
  cp build/geekatplay_studio "$dest/lib/terraforge/"
  [ -f build/nodeterrain_cli ] && cp build/nodeterrain_cli "$dest/lib/terraforge/"
  for tree in orchestrator mcp_server examples scripts; do
    rm -rf "$dest/lib/terraforge/$tree"
    [ -d "$tree" ] && cp -R "$tree" "$dest/lib/terraforge/"
  done
  cp -R studio/resources "$dest/lib/terraforge/resources"
  cp requirements.txt "$dest/lib/terraforge/" 2>/dev/null || true
  cat > "$dest/bin/terraforge" <<EOF
#!/usr/bin/env bash
# The working directory matters: TerraForge finds its Python layer relative to
# its own location and writes logs beside it.
cd "$dest/lib/terraforge" && exec ./geekatplay_studio "\$@"
EOF
  chmod +x "$dest/bin/terraforge"
  cat > "$dest/share/applications/terraforge.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=TerraForge
Comment=Node-based 3D terrain and environment studio
Exec=$dest/bin/terraforge %f
Icon=$dest/lib/terraforge/resources/icon_256.png
Categories=Graphics;3DGraphics;
Terminal=false
EOF
  echo
  ok "TerraForge is installed."
  echo "  Command:   terraforge   (make sure $dest/bin is on your PATH)"
  echo "  Folder:    $dest/lib/terraforge"
  echo "  Settings:  \${XDG_DATA_HOME:-~/.local/share}/GeekatplayTerraForge"
fi
