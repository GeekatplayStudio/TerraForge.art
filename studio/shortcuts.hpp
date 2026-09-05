// Geekatplay TerraForge - keyboard shortcuts as a table, not as scattered
// key tests: every command has an id, a label, a default chord, and the
// chord the user gave it in the configuration. The menus show the chord,
// the settings screen rebinds it, and the code asks by id.
//
// A chord is text - "Ctrl+S", "Shift+Alt+F5", "W" - so it can live in
// config.json and be typed by a person; parse and format are exact inverses.
#pragma once
#include <string>
#include <vector>

namespace studio {

struct ShortcutCommand {
  const char *id;       // "file.save"
  const char *label;    // "Save project"
  const char *category; // "File", "Edit", "View", "Tools"...
  const char *chord;    // the default
};

struct KeyChord {
  int key = 0;          // ImGuiKey
  bool ctrl = false, shift = false, alt = false;
  bool valid() const { return key != 0; }
};

// The whole table: what the application can be told to do from the keyboard.
const std::vector<ShortcutCommand> &shortcut_commands();

// The chord currently bound to a command (the user's, else the default).
std::string shortcut_chord(const std::string &id);
// Bind, or "" to restore the default. Saves the configuration.
void shortcut_set(const std::string &id, const std::string &chord);
bool shortcut_is_default(const std::string &id);

// Text <-> chord. parse() returns an invalid chord for text it cannot read.
KeyChord chord_parse(const std::string &text);
std::string chord_format(const KeyChord &c);
// The name of a key as it appears in a chord ("F5", "Space", "A").
const char *key_name(int imgui_key);
int key_from_name(const std::string &name);

// Was the command's chord pressed this frame? Modifier-exact, so "S" does
// not fire on Ctrl+S; false while a text field has the keyboard.
bool shortcut_pressed(const std::string &id);
// The chord being pressed right now, for a rebinding capture; invalid if
// only modifiers are down.
KeyChord chord_captured();

// Every command whose chord collides with `chord`, except `except_id`.
std::vector<std::string> shortcut_conflicts(const KeyChord &chord, const std::string &except_id);

} // namespace studio
