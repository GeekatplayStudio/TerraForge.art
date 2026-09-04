#!/usr/bin/env bash
# Geekatplay TerraForge - one-click install for macOS.
#
# Double-click this file in Finder. If macOS refuses because it came from the
# internet, right-click it and choose Open, or run this once in Terminal:
#     chmod +x install.command
#
# It checks the tools TerraForge needs, offers to install the missing ones with
# Homebrew, fetches the dependencies, builds, and puts TerraForge.app in
# /Applications.
cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
chmod +x scripts/install.sh scripts/get_deps.sh build.sh test.sh start.sh 2>/dev/null
./scripts/install.sh "$@"
status=$?
echo
if [ $status -ne 0 ]; then
  echo "Installation did not finish. The messages above say why."
fi
read -r -p "Press Return to close this window. "
exit $status
