// Geekatplay TerraForge — where the application's own files live. See paths.hpp.
#include "paths.hpp"
#include <cstdlib>
#include <string>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#else
#include <climits>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace studio {

namespace {

fs::path find_exe_dir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof *buf));
  if (n == 0) return fs::current_path();
  return fs::path(std::wstring(buf, n)).parent_path();
#elif defined(__APPLE__)
  // _NSGetExecutablePath may hand back a path with symlinks and `..` in it;
  // weakly_canonical resolves those without throwing when a component is gone.
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buf(size ? size : PATH_MAX, '\0');
  if (_NSGetExecutablePath(buf.data(), &size) != 0) return fs::current_path();
  buf.resize(std::char_traits<char>::length(buf.c_str()));
  std::error_code ec;
  fs::path p = fs::weakly_canonical(fs::path(buf), ec);
  if (ec) p = fs::path(buf);
  return p.parent_path();
#else
  std::error_code ec;
  fs::path p = fs::read_symlink("/proc/self/exe", ec);
  if (ec) return fs::current_path();
  return p.parent_path();
#endif
}

// The shipped tree is recognised by `orchestrator`, because that is the part
// the application actually looks up at run time (the offline renderers). Any
// marker would do; this one fails loudly rather than silently if the Python
// layer was left out of a package.
bool looks_like_install(const fs::path &p) {
  std::error_code ec;
  return fs::exists(p / "orchestrator", ec) || fs::exists(p / "resources", ec);
}

fs::path find_install_dir() {
  const fs::path &exe = exe_dir();
#ifdef __APPLE__
  // .app/Contents/MacOS/geekatplay_studio -> .app/Contents/Resources
  fs::path bundle = exe.parent_path() / "Resources";
  if (looks_like_install(bundle)) return bundle;
#endif
  if (looks_like_install(exe)) return exe;
  // the developer layout: the binary is in build/, the tree is its parent
  fs::path up = exe.parent_path();
  if (looks_like_install(up)) return up;
  return exe;
}

fs::path find_data_dir() {
  fs::path base;
#ifdef _WIN32
  if (const char *p = std::getenv("LOCALAPPDATA"); p && *p) base = p;
#elif defined(__APPLE__)
  if (const char *p = std::getenv("HOME"); p && *p)
    base = fs::path(p) / "Library" / "Application Support";
#else
  if (const char *p = std::getenv("XDG_DATA_HOME"); p && *p) base = p;
  else if (const char *h = std::getenv("HOME"); h && *h)
    base = fs::path(h) / ".local" / "share";
#endif
  if (base.empty()) base = fs::temp_directory_path();
  fs::path dir = base / "GeekatplayTerraForge";
  std::error_code ec;
  fs::create_directories(dir, ec);
  // A machine with no writable home is not worth crashing over: the temp
  // directory always exists, and losing preferences beats losing the session.
  if (ec) {
    dir = fs::temp_directory_path() / "GeekatplayTerraForge";
    fs::create_directories(dir, ec);
  }
  return dir;
}

} // namespace

const fs::path &exe_dir() {
  static const fs::path p = find_exe_dir();
  return p;
}

const fs::path &install_dir() {
  static const fs::path p = find_install_dir();
  return p;
}

const fs::path &data_dir() {
  static const fs::path p = find_data_dir();
  return p;
}

std::string ui_font_path() {
  // In preference order per platform. Segoe UI is what the Windows build has
  // always used; SF is not shipped as a file on macOS, so Helvetica Neue is
  // the interface font that actually exists on disk there.
  static const char *candidates[] = {
#ifdef _WIN32
      "C:/Windows/Fonts/segoeui.ttf",
      "C:/Windows/Fonts/tahoma.ttf",
#elif defined(__APPLE__)
      "/System/Library/Fonts/SFNS.ttf",
      "/System/Library/Fonts/Helvetica.ttc",
      "/Library/Fonts/Arial.ttf",
#else
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/gnu-free/FreeSans.ttf",
#endif
  };
  std::error_code ec;
  for (const char *c : candidates)
    if (fs::exists(c, ec)) return c;
  return {};
}

fs::path settings_path(const std::string &filename) {
  std::error_code ec;
  if (fs::exists(fs::path(filename), ec)) return fs::path(filename);
  return data_dir() / filename;
}

} // namespace studio
