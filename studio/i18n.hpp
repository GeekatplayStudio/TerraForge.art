// Geekatplay TerraForge — string localisation.
//
// Every user-visible string goes through tr("tag"). The English dictionary
// is the source of truth; other languages are added as further tables and
// selected at runtime. An unknown tag falls back to the tag itself, so a
// missing translation is visible but never crashes or blanks the UI.
#pragma once
#include <string>

namespace studio {

// returns the localised text for a tag, or the tag when it is unknown
const char *tr(const char *tag);

// language handling (currently English only; the plumbing is in place)
int i18n_language_count();
const char *i18n_language_name(int index);
int &i18n_language();
// number of tags the active language is missing, for a quick sanity check
int i18n_missing_count();

} // namespace studio
