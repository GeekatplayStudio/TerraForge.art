// Geekatplay TerraForge — native Windows open/save dialogs
#include "app.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif
#include <cstring>

namespace studio {

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
#endif
  (void)filter;
  (void)def_ext;
  return {};
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
#endif
  (void)filter;
  (void)def_ext;
  (void)suggested;
  return {};
}

} // namespace studio
