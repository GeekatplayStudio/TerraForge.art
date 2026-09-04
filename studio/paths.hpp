// Geekatplay TerraForge — where the application's own files live.
//
// Running from `build/` these questions have trivial answers, which is why
// they were never asked: the preferences file, the Python layer and the logs
// were all "the current directory" or near it. An *installed* copy breaks
// every one of those assumptions. A macOS bundle is launched with the working
// directory set to `/`, and a Windows install may sit in a folder the user
// cannot write to. So the two directories are named here, once, and every
// caller asks rather than guesses.
//
//   install_dir()  — read-only: the executable, its resources, the Python
//                    render layer, the examples. Inside a .app bundle this is
//                    Contents/Resources, not Contents/MacOS.
//   data_dir()     — writable, per user: preferences, autosaves, anything the
//                    application decides to keep.
//
// Both are cached after the first call and created on demand.
#pragma once
#include <filesystem>
#include <string>

namespace studio {

// Directory holding the running executable.
const std::filesystem::path &exe_dir();

// Where the application's shipped files are: resources/, orchestrator/,
// mcp_server/, examples/. Found by looking for `orchestrator` beside the
// executable, one level up (the developer's `build/` layout), and — on macOS —
// in the bundle's Resources directory.
const std::filesystem::path &install_dir();

// A writable per-user directory:
//   Windows  %LOCALAPPDATA%\GeekatplayTerraForge
//   macOS    ~/Library/Application Support/GeekatplayTerraForge
//   Linux    $XDG_DATA_HOME/GeekatplayTerraForge, else ~/.local/share/...
// Created if missing; falls back to the temp directory if it cannot be.
const std::filesystem::path &data_dir();

// The UI font: the platform's own interface typeface, so TerraForge looks
// like an application on the system it is running on rather than like a port.
// Returns an empty string when none of the candidates exist, which the caller
// answers with Dear ImGui's built-in font.
std::string ui_font_path();

// Full path of a per-user file, e.g. settings_path("terraforge_prefs.json").
//
// A file of that name in the current directory wins, so a developer running
// from the repository keeps the settings sitting there and nothing about the
// existing workflow changes.
std::filesystem::path settings_path(const std::string &filename);

} // namespace studio
