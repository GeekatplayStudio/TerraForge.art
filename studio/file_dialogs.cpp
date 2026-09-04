// Geekatplay TerraForge — native open/save dialogs, and handing a finished
// file to the desktop.
//
// There is no portable answer here and no library worth the dependency:
// Win32 has comdlg32, macOS answers through osascript (which puts up the real
// Cocoa panel without dragging Objective-C into the build), and a Linux
// desktop ships whichever of zenity or kdialog its toolkit came with.
#include "app.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#else
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#endif
#include <cstring>

namespace studio {

#ifndef _WIN32
namespace {

// Everything that reaches a /bin/sh command line goes through this. A file
// called  don't stop.png  has to arrive as a filename; unquoted it would end
// one argument and start a command.
std::string sh_quote(const std::string &s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''"; // close, escape the quote, reopen
    else out += c;
  }
  return out + "'";
}

// The Win32 filter is one double-NUL-terminated blob of description/pattern
// pairs ("PNG image\0*.png\0"). Both POSIX helpers want the extensions on
// their own. An "All files" entry (*.*) is the caller saying it does not want
// a filter at all, so it cancels the whole list rather than adding to it.
std::vector<std::string> filter_exts(const char *filter) {
  std::vector<std::string> out;
  if (!filter) return out;
  for (const char *p = filter; *p;) {
    p += std::strlen(p) + 1; // step over the human-readable description
    if (!*p) break;          // a description with no pattern: nothing to read
    std::string pat = p;
    p += pat.size() + 1;
    size_t i = 0;
    while (i < pat.size()) {
      size_t semi = pat.find(';', i); // "*.tif;*.tiff" is one entry
      std::string one =
          pat.substr(i, semi == std::string::npos ? semi : semi - i);
      size_t dot = one.rfind('.');
      if (dot != std::string::npos) {
        std::string e = one.substr(dot + 1);
        if (e == "*") return {};
        if (!e.empty()) out.push_back(e);
      }
      if (semi == std::string::npos) break;
      i = semi + 1;
    }
  }
  return out;
}

// Runs a helper and takes the path it printed. A cancelled dialog exits
// non-zero, which is the only way any of these tools says "no file".
std::string run_capture(const std::string &cmd) {
  std::FILE *p = ::popen(cmd.c_str(), "r");
  if (!p) return {};
  std::string out;
  char buf[512];
  while (std::fgets(buf, sizeof buf, p)) out += buf;
  if (::pclose(p) != 0) return {};
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
    out.pop_back();
  return out;
}

#ifndef __APPLE__
// Ask only for a tool that is installed: a missing helper and a cancelled
// dialog both exit non-zero, so without this check cancelling zenity would
// put a kdialog on screen straight afterwards.
bool have_tool(const char *name) {
  std::string probe = std::string("command -v ") + name + " >/dev/null 2>&1";
  return std::system(probe.c_str()) == 0;
}
#else
// A string about to sit inside an AppleScript "..." literal: a quote or a
// backslash would end it early.
std::string as_quote(const std::string &s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

std::string osa(const std::string &script) {
  return run_capture("osascript -e " + sh_quote(script) + " 2>/dev/null");
}
#endif

std::string posix_open(const char *filter) {
  std::vector<std::string> ext = filter_exts(filter);
#ifdef __APPLE__
  std::string types;
  for (const std::string &e : ext)
    types += (types.empty() ? "" : ", ") + as_quote(e);
  // An unparsable filter shows everything rather than nothing: a dialog the
  // wanted file cannot appear in is worse than one with no filter.
  std::string script = "POSIX path of (choose file with prompt \"Open\"";
  if (!types.empty()) script += " of type {" + types + "}";
  return osa(script + ")");
#else
  std::string flt;
  for (const std::string &e : ext) flt += (flt.empty() ? "" : " ") + ("*." + e);
  if (have_tool("zenity")) {
    std::string cmd = "zenity --file-selection --title=Open";
    if (!flt.empty())
      // the second filter is the "All files" entry the user can switch to
      cmd += " --file-filter=" + sh_quote(flt) + " --file-filter=" + sh_quote("*");
    return run_capture(cmd + " 2>/dev/null");
  }
  if (have_tool("kdialog"))
    return run_capture("kdialog --getopenfilename . " +
                       sh_quote(flt.empty() ? "*" : flt) + " 2>/dev/null");
  // Neither helper is installed. Nothing to fall back to that is better than
  // the caller's own "no file chosen" path.
  return {};
#endif
}

std::string posix_save(const char *filter, const char *suggested) {
  (void)filter;
  std::string sug = suggested ? suggested : "";
#ifdef __APPLE__
  // "choose file name" takes no type list — it names a file that does not
  // exist yet — so the caller's default extension is applied by
  // dialog_save_file below, exactly as OFN_lpstrDefExt does on Windows.
  std::string dir, name = sug;
  if (size_t slash = sug.rfind('/'); slash != std::string::npos) {
    dir = sug.substr(0, slash);       // the panel opens where the caller meant
    name = sug.substr(slash + 1);
  }
  std::string script = "POSIX path of (choose file name with prompt \"Save\"";
  if (!name.empty()) script += " default name " + as_quote(name);
  if (!dir.empty())
    script += " default location (POSIX file " + as_quote(dir) + ")";
  return osa(script + ")");
#else
  if (have_tool("zenity")) {
    std::string cmd = "zenity --file-selection --save --confirm-overwrite "
                      "--title=Save";
    if (!sug.empty()) cmd += " --filename=" + sh_quote(sug);
    return run_capture(cmd + " 2>/dev/null");
  }
  if (have_tool("kdialog")) {
    std::string flt;
    for (const std::string &e : filter_exts(filter))
      flt += (flt.empty() ? "" : " ") + ("*." + e);
    return run_capture("kdialog --getsavefilename " +
                       sh_quote(sug.empty() ? "." : sug) + " " +
                       sh_quote(flt.empty() ? "*" : flt) + " 2>/dev/null");
  }
  return {};
#endif
}

} // namespace
#endif // !_WIN32

std::string dialog_open_file(const char *filter, const char *def_ext) {
#ifdef _WIN32
  char buf[MAX_PATH] = "";
  OPENFILENAMEA ofn{};
  ofn.lStructSize = sizeof ofn;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = def_ext;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameA(&ofn)) return buf;
  (void)filter;
  (void)def_ext;
  return {};
#else
  // Opening picks a file that exists, so a default extension has nothing to
  // add: the user chose the name rather than typing it.
  (void)def_ext;
  return posix_open(filter);
#endif
}

std::string dialog_save_file(const char *filter, const char *def_ext,
                             const char *suggested) {
#ifdef _WIN32
  char buf[MAX_PATH] = "";
  if (suggested) {
    std::strncpy(buf, suggested, MAX_PATH - 1);
    buf[MAX_PATH - 1] = 0;
  }
  OPENFILENAMEA ofn{};
  ofn.lStructSize = sizeof ofn;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = def_ext;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  if (GetSaveFileNameA(&ofn)) return buf;
  (void)filter;
  (void)def_ext;
  (void)suggested;
  return {};
#else
  std::string path = posix_save(filter, suggested);
  // OFN_lpstrDefExt: a name typed without an extension gets the caller's.
  // Neither POSIX helper does this, and a project saved as "terrain" instead
  // of "terrain.gpxt" does not come back through the open dialog's filter.
  if (!path.empty() && def_ext && *def_ext) {
    size_t slash = path.rfind('/'), dot = path.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
      path += std::string(".") + def_ext;
  }
  return path;
#endif
}

// Hands a finished file to whatever the desktop opens it with. Not a dialog,
// but the same shape of problem: one call per platform and no library.
void open_in_desktop(const std::string &path) {
#ifdef _WIN32
  ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
#ifdef __APPLE__
  std::string cmd = "open " + sh_quote(path);
#else
  std::string cmd = "xdg-open " + sh_quote(path);
#endif
  // Detached: the opener may be the image viewer itself, and the UI thread
  // must not sit and wait for the user to close a picture.
  (void)std::system((cmd + " >/dev/null 2>&1 &").c_str());
#endif
}

} // namespace studio
