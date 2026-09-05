// Geekatplay TerraForge — string localisation.
//
// Every user-visible string goes through tr("tag"). The English dictionary
// (i18n_en.cpp, i18n_en_ui*.cpp) is built in and is the fallback; other
// languages are JSON files, resources/lang/<code>.json, holding
// {"tag": "translation"} and read at startup or from Settings > General.
// An unknown tag falls back to English, then to the tag itself, so a missing
// translation is visible but never crashes or blanks the UI.
#pragma once
#include <string>
#include <vector>

namespace studio {

// the localised text for a tag: active language, else English, else the tag
const char *tr(const char *tag);

// An ImGui::Combo item list built from translated tags: the items separated
// by NUL and the list closed by a second NUL, as Combo expects. Keep the
// returned string alive across the Combo call.
std::string tr_combo(const char *const tags[], int n);

// ---- languages. Index 0 is always English (built in); the rest are the
// JSON files found by i18n_scan, in name order.
int i18n_language_count();
const char *i18n_language_name(int index); // display name ("Deutsch")
const char *i18n_language_code(int index); // "en", "de", "fr"...
const char *i18n_language_path(int index); // "" for English
int &i18n_language();                      // the active index
// Lists the languages in a folder (English plus every *.json there). Calling
// it again with another folder adds to the list; duplicates by code are kept
// once, the first path winning.
void i18n_scan(const std::string &lang_dir);
// Reads one language file and makes it active. On any failure `err` says
// why and the active language is unchanged (English stays English).
bool i18n_load_language(const std::string &path, std::string *err = nullptr);
// Selects by index (0 = English, drops any loaded file) or by code.
bool i18n_select(int index, std::string *err = nullptr);
bool i18n_select_code(const std::string &code, std::string *err = nullptr);
const std::string &i18n_active_code();
// Forgets every language but English and switches back to it (tests).
void i18n_reset();

// ---- dictionary checks
bool i18n_english_has(const char *tag);
int i18n_english_count();
// number of tags in the active (non-English) table; 0 for English
int i18n_active_count();
// distinct tags that tr() was asked for and English does not declare
int i18n_missing_count();
std::vector<std::string> i18n_missing_tags();

// Startup: scans the shipped resources/lang folder and applies the
// preferences' language (i18n_startup.cpp).
void i18n_init();

} // namespace studio
