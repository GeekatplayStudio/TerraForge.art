// Geekatplay TerraForge — string localisation: the lookup, the language
// files and the missing-tag ledger. The English dictionary itself is in
// i18n_en.cpp (tagged strings) and i18n_en_ui*.cpp (strings whose tag is
// the English text).
#include "i18n.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <map>
#include <set>
#include <unordered_map>

namespace studio {

const std::map<std::string, const char *> &i18n_english_tagged();
// identity tags: the English text is the tag (generated lists)
const char *const *i18n_en_ui_a(int *n);
const char *const *i18n_en_ui_b(int *n);

namespace {

struct Language {
  std::string code, name, path;
  std::unordered_map<std::string, std::string> table; // empty for English
};

// English: tagged entries plus the identity list, one lookup table.
const std::unordered_map<std::string, const char *> &english() {
  static const std::unordered_map<std::string, const char *> D = [] {
    std::unordered_map<std::string, const char *> d;
    for (const auto &kv : i18n_english_tagged()) d[kv.first] = kv.second;
    int n = 0;
    const char *const *a = i18n_en_ui_a(&n);
    for (int i = 0; i < n; ++i) d.emplace(a[i], a[i]);
    const char *const *b = i18n_en_ui_b(&n);
    for (int i = 0; i < n; ++i) d.emplace(b[i], b[i]);
    return d;
  }();
  return D;
}

std::vector<Language> &languages() {
  static std::vector<Language> L = [] {
    std::vector<Language> v;
    v.push_back({"en", "English", "", {}});
    return v;
  }();
  return L;
}

int g_active = 0;
std::string g_active_code = "en";
std::set<std::string> g_missing;

// Reads {"tag": "text"} into a table. Keys starting with "_" are metadata
// ("_name" is the display name). Anything that is not an object of strings
// is refused with a message, and nothing is changed.
bool read_table(const std::string &path,
                std::unordered_map<std::string, std::string> &out,
                std::string &name, std::string &err) {
  std::ifstream f(path);
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(f);
  } catch (const std::exception &e) {
    err = path + ": " + e.what();
    return false;
  }
  if (!j.is_object()) {
    err = path + ": the file is not a {\"tag\": \"text\"} object";
    return false;
  }
  std::unordered_map<std::string, std::string> t;
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (!it.value().is_string()) {
      err = path + ": \"" + it.key() + "\" is not a string";
      return false;
    }
    if (it.key() == "_name") name = it.value().get<std::string>();
    else if (!it.key().empty() && it.key()[0] != '_')
      t[it.key()] = it.value().get<std::string>();
  }
  out.swap(t);
  return true;
}

int index_of_code(const std::string &code) {
  auto &L = languages();
  for (size_t i = 0; i < L.size(); ++i)
    if (L[i].code == code) return (int)i;
  return -1;
}

} // namespace

const char *tr(const char *tag) {
  if (!tag) return "";
  if (g_active > 0) {
    const auto &t = languages()[g_active].table;
    auto it = t.find(tag);
    if (it != t.end()) return it->second.c_str();
  }
  const auto &d = english();
  auto it = d.find(tag);
  if (it != d.end()) return it->second;
  g_missing.insert(tag); // shows up in Settings as a translation gap
  return tag;            // visible, never empty
}

std::string tr_combo(const char *const tags[], int n) {
  std::string s;
  for (int i = 0; i < n; ++i) {
    s += tr(tags[i]);
    s.push_back('\0');
  }
  s.push_back('\0');
  return s;
}

int i18n_language_count() { return (int)languages().size(); }

const char *i18n_language_name(int index) {
  auto &L = languages();
  if (index < 0 || index >= (int)L.size()) index = 0;
  return L[index].name.c_str();
}

const char *i18n_language_code(int index) {
  auto &L = languages();
  if (index < 0 || index >= (int)L.size()) index = 0;
  return L[index].code.c_str();
}

const char *i18n_language_path(int index) {
  auto &L = languages();
  if (index < 0 || index >= (int)L.size()) index = 0;
  return L[index].path.c_str();
}

int &i18n_language() { return g_active; }
const std::string &i18n_active_code() { return g_active_code; }

void i18n_scan(const std::string &lang_dir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::is_directory(lang_dir, ec)) return;
  std::vector<fs::path> files;
  for (const auto &e : fs::directory_iterator(lang_dir, ec))
    if (e.is_regular_file(ec) && e.path().extension() == ".json")
      files.push_back(e.path());
  std::sort(files.begin(), files.end());
  for (const fs::path &p : files) {
    std::string code = p.stem().string();
    if (code.empty() || code[0] == '_' || index_of_code(code) >= 0) continue;
    // the display name is read now so the Settings list can show it
    // without keeping every table
    std::unordered_map<std::string, std::string> t;
    std::string name, err;
    if (!read_table(p.string(), t, name, err)) continue;
    languages().push_back({code, name.empty() ? code : name, p.string(), {}});
  }
}

bool i18n_load_language(const std::string &path, std::string *err) {
  std::unordered_map<std::string, std::string> t;
  std::string name, e;
  if (!read_table(path, t, name, e)) {
    if (err) *err = e;
    return false; // English (or whatever was active) stays active
  }
  std::string code = std::filesystem::path(path).stem().string();
  int idx = index_of_code(code);
  if (idx <= 0) {
    languages().push_back({code, name.empty() ? code : name, path, {}});
    idx = (int)languages().size() - 1;
  }
  Language &L = languages()[idx];
  L.table.swap(t);
  L.path = path;
  if (!name.empty()) L.name = name;
  g_active = idx;
  g_active_code = L.code;
  return true;
}

bool i18n_select(int index, std::string *err) {
  auto &L = languages();
  if (index < 0 || index >= (int)L.size()) {
    if (err) *err = "no such language";
    return false;
  }
  if (index == 0) {
    g_active = 0;
    g_active_code = "en";
    return true;
  }
  return i18n_load_language(L[index].path, err);
}

bool i18n_select_code(const std::string &code, std::string *err) {
  int idx = index_of_code(code);
  if (idx < 0) {
    if (err) *err = "unknown language '" + code + "'";
    return false;
  }
  return i18n_select(idx, err);
}

void i18n_reset() {
  languages().resize(1);
  g_active = 0;
  g_active_code = "en";
  g_missing.clear();
}

bool i18n_english_has(const char *tag) { return tag && english().count(tag) != 0; }
int i18n_english_count() { return (int)english().size(); }
int i18n_active_count() {
  return g_active > 0 ? (int)languages()[g_active].table.size() : 0;
}
int i18n_missing_count() { return (int)g_missing.size(); }
std::vector<std::string> i18n_missing_tags() {
  return std::vector<std::string>(g_missing.begin(), g_missing.end());
}

} // namespace studio
