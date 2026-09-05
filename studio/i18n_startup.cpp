// Geekatplay TerraForge — languages at startup: where the shipped files are
// and which one the preferences ask for. Kept apart from i18n.cpp so the
// dictionary tests link without paths or preferences.
#include "i18n.hpp"
#include "paths.hpp"
#include "prefs.hpp"

namespace studio {

void i18n_init() {
  // resources/ is copied beside the executable at build time and into the
  // bundle's Resources on macOS; install_dir() answers both.
  i18n_scan((install_dir() / "resources" / "lang").string());
  i18n_scan((exe_dir() / "resources" / "lang").string());
  const std::string &want = prefs().language;
  if (!want.empty() && want != "en") {
    std::string err;
    if (!i18n_select_code(want, &err)) prefs().language = "en";
  }
}

} // namespace studio
