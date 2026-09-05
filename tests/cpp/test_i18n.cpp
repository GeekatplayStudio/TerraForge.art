// Geekatplay TerraForge - languages without a window: English is built in
// and complete, a language file translates tags and falls back for the
// rest, switching changes what tr() returns, every tag a shipped file
// translates exists in English (no stale tags), tr_combo builds a Combo
// list, missing tags are counted once each.
//
// Mutation half: a malformed file fails with a reason and leaves English
// active; an unknown code fails; selecting English again drops the file.
#include "i18n.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace studio;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static fs::path repo_lang_dir() {
  if (const char *r = std::getenv("TERRAFORGE_REPO")) return fs::path(r) / "studio" / "resources" / "lang";
  // the test runs from build/: the repo is one level up
  for (fs::path p = fs::current_path(); !p.empty(); p = p.parent_path()) {
    if (fs::exists(p / "studio" / "resources" / "lang")) return p / "studio" / "resources" / "lang";
    if (p == p.root_path()) break;
  }
  return fs::path("studio/resources/lang");
}

static void test_english() {
  std::printf("english...\n");
  i18n_reset();
  check(i18n_language_count() >= 1 && std::string(i18n_language_code(0)) == "en", "English is language 0");
  check(std::string(tr("menu.file")) == "File", "a tagged string resolves");
  check(std::string(tr("Add key")) == "Add key", "an identity tag resolves to itself");
  int before = i18n_missing_count();
  tr("no.such.tag.for.the.test");
  tr("no.such.tag.for.the.test");
  check(i18n_missing_count() == before + 1, "a missing tag is counted once, not per call");
  std::string same = tr("no.such.tag.for.the.test");
  check(same == "no.such.tag.for.the.test", "and returns the tag");
  check(i18n_english_count() > 100, "the dictionary is not empty");
  std::string combo = tr_combo((const char *const[]){"Linear", "menu.file"}, 2);
  check(combo.size() >= std::string("Linear").size() + 1 + std::string("File").size() + 2, "tr_combo holds both items");
  check(combo[6] == '\0' && combo.back() == '\0' && combo[combo.size() - 2] == '\0', "NUL separated, double NUL terminated");
}

static void test_language_files() {
  std::printf("language files...\n");
  fs::path dir = repo_lang_dir();
  check(fs::exists(dir / "de.json") && fs::exists(dir / "fr.json"), (std::string("de.json and fr.json ship in ") + dir.string()).c_str());
  i18n_reset();
  i18n_scan(dir.string());
  check(i18n_language_count() >= 3, "German and French are listed");
  std::string err;
  check(i18n_select_code("de", &err), err.c_str());
  check(i18n_active_code() == "de", "German is active");
  check(std::string(tr("menu.file")) == "Datei", "File is Datei");
  check(std::string(tr("workspace.terrain")) != "", "workspace names exist");
  check(std::string(tr("no.such.tag.for.the.test")) == "no.such.tag.for.the.test", "a missing tag still returns the tag");
  // every tag a shipped file translates must exist in English - a stale
  // tag in a language file is a translation nobody will ever see
  for (const char *code : {"de", "fr"}) {
    check(i18n_select_code(code, &err), err.c_str());
    check(i18n_active_count() > 50, "the file has a real number of entries");
    for (int i = 0; i < i18n_language_count(); ++i) {
      if (std::string(i18n_language_code(i)) != code) continue;
      std::ifstream f(i18n_language_path(i));
      std::string line;
      int stale = 0;
      while (std::getline(f, line)) {
        size_t a = line.find('"');
        if (a == std::string::npos) continue;
        size_t b = line.find('"', a + 1);
        if (b == std::string::npos) continue;
        std::string tag = line.substr(a + 1, b - a - 1);
        if (line.find(':', b) == std::string::npos || tag == "_name") continue;
        if (!i18n_english_has(tag.c_str())) { ++stale; std::printf("  stale tag in %s: %s\n", code, tag.c_str()); }
      }
      check(stale == 0, "no stale tags in the language file");
    }
  }
  check(i18n_select_code("fr", &err) && std::string(tr("menu.file")) == "Fichier", "French: Fichier");
  check(i18n_select(0, &err) && std::string(tr("menu.file")) == "File", "back to English");
  // mutations
  fs::path bad = fs::temp_directory_path() / "terraforge_bad_lang.json";
  { std::ofstream o(bad); o << "{ \"menu.file\": \"X\", "; }
  check(!i18n_load_language(bad.string(), &err) && !err.empty(), "a malformed file fails with a reason");
  check(std::string(tr("menu.file")) == "File", "and English stays active");
  check(!i18n_select_code("xx", &err) && !err.empty(), "an unknown code fails");
  std::error_code ec;
  fs::remove(bad, ec);
  i18n_reset();
}

int main() {
  test_english();
  test_language_files();
  if (failures) {
    std::printf("%d i18n check(s) failed\n", failures);
    return 1;
  }
  std::printf("i18n tests passed\n");
  return 0;
}
