// Geekatplay TerraForge - the shortcut table. See shortcuts.hpp.
#include "shortcuts.hpp"
#include "config.hpp"
#include <algorithm>
#include <cctype>
#include <imgui.h>

namespace studio {

const std::vector<ShortcutCommand> &shortcut_commands() {
  static const std::vector<ShortcutCommand> t = {
      {"file.new", "New project", "File", "Ctrl+N"},
      {"file.open", "Open project", "File", "Ctrl+O"},
      {"file.save", "Save project", "File", "Ctrl+S"},
      {"file.save_as", "Save project as", "File", "Ctrl+Shift+S"},
      {"edit.undo", "Undo", "Edit", "Ctrl+Z"},
      {"edit.redo", "Redo", "Edit", "Ctrl+Y"},
      {"edit.redo_alt", "Redo (alternative)", "Edit", "Ctrl+Shift+Z"},
      {"edit.copy", "Copy nodes", "Edit", "Ctrl+C"},
      {"edit.paste", "Paste nodes", "Edit", "Ctrl+V"},
      {"edit.delete", "Delete selection", "Edit", "Delete"},
      {"graph.recompute", "Recompute all", "Graph", "F5"},
      {"graph.frame", "Frame the graph", "Graph", "F"},
      {"graph.search", "Add node (search)", "Graph", "Tab"},
      {"tool.move", "Move gizmo", "Tools", "W"},
      {"tool.rotate", "Rotate gizmo", "Tools", "E"},
      {"tool.scale", "Scale gizmo", "Tools", "R"},
      {"view.frame_selected", "Frame the selection", "View", "Period"},
      {"view.toggle_grid", "Toggle the grid", "View", "G"},
      {"view.screenshot", "Capture the viewport", "View", "F12"},
      {"render.preview", "Render preview", "Render", "F9"},
      {"render.final", "Render", "Render", "Ctrl+F9"},
      {"ai.assistant", "Ask the assistant", "AI", "Ctrl+Space"},
      {"ai.generate_image", "Generate an image with AI", "AI", "Ctrl+Shift+I"},
      {"ai.generate_model", "Generate a 3D model with AI", "AI", "Ctrl+Shift+M"},
      {"app.settings", "Settings", "Application", "Ctrl+Comma"},
      {"app.escape", "Cancel / close", "Application", "Escape"},
  };
  return t;
}

namespace {

struct KeyName {
  int key;
  const char *name;
};
const KeyName NAMES[] = {
    {ImGuiKey_Tab, "Tab"},         {ImGuiKey_Space, "Space"},   {ImGuiKey_Enter, "Enter"},
    {ImGuiKey_Escape, "Escape"},   {ImGuiKey_Delete, "Delete"}, {ImGuiKey_Backspace, "Backspace"},
    {ImGuiKey_Insert, "Insert"},   {ImGuiKey_Home, "Home"},     {ImGuiKey_End, "End"},
    {ImGuiKey_PageUp, "PageUp"},   {ImGuiKey_PageDown, "PageDown"},
    {ImGuiKey_LeftArrow, "Left"},  {ImGuiKey_RightArrow, "Right"}, {ImGuiKey_UpArrow, "Up"},
    {ImGuiKey_DownArrow, "Down"},  {ImGuiKey_Comma, "Comma"},   {ImGuiKey_Period, "Period"},
    {ImGuiKey_Minus, "Minus"},     {ImGuiKey_Equal, "Equal"},   {ImGuiKey_Slash, "Slash"},
    {ImGuiKey_GraveAccent, "Grave"}, {ImGuiKey_LeftBracket, "LeftBracket"},
    {ImGuiKey_RightBracket, "RightBracket"},
};

const ShortcutCommand *find_cmd(const std::string &id) {
  for (const ShortcutCommand &c : shortcut_commands())
    if (id == c.id) return &c;
  return nullptr;
}

} // namespace

const char *key_name(int k) {
  static char buf[8];
  for (const KeyName &kn : NAMES)
    if (kn.key == k) return kn.name;
  if (k >= ImGuiKey_A && k <= ImGuiKey_Z) {
    buf[0] = (char)('A' + (k - ImGuiKey_A));
    buf[1] = 0;
    return buf;
  }
  if (k >= ImGuiKey_0 && k <= ImGuiKey_9) {
    buf[0] = (char)('0' + (k - ImGuiKey_0));
    buf[1] = 0;
    return buf;
  }
  if (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) {
    snprintf(buf, sizeof buf, "F%d", 1 + (k - ImGuiKey_F1));
    return buf;
  }
  return "";
}

int key_from_name(const std::string &raw) {
  std::string n = raw;
  for (char &c : n) c = (char)std::tolower((unsigned char)c);
  for (const KeyName &kn : NAMES) {
    std::string m = kn.name;
    for (char &c : m) c = (char)std::tolower((unsigned char)c);
    if (m == n) return kn.key;
  }
  if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') return ImGuiKey_A + (n[0] - 'a');
  if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return ImGuiKey_0 + (n[0] - '0');
  if (n.size() >= 2 && n[0] == 'f') {
    int f = std::atoi(n.c_str() + 1);
    if (f >= 1 && f <= 12) return ImGuiKey_F1 + (f - 1);
  }
  return 0;
}

KeyChord chord_parse(const std::string &text) {
  KeyChord c;
  std::string part;
  auto take = [&](const std::string &p) {
    std::string l = p;
    for (char &ch : l) ch = (char)std::tolower((unsigned char)ch);
    if (l == "ctrl" || l == "control" || l == "cmd") c.ctrl = true;
    else if (l == "shift") c.shift = true;
    else if (l == "alt" || l == "option") c.alt = true;
    else if (!l.empty()) c.key = key_from_name(p);
  };
  for (char ch : text) {
    if (ch == '+' || ch == ' ') {
      take(part);
      part.clear();
    } else {
      part.push_back(ch);
    }
  }
  take(part);
  return c;
}

std::string chord_format(const KeyChord &c) {
  if (!c.valid()) return "";
  std::string s;
  if (c.ctrl) s += "Ctrl+";
  if (c.shift) s += "Shift+";
  if (c.alt) s += "Alt+";
  s += key_name(c.key);
  return s;
}

std::string shortcut_chord(const std::string &id) {
  auto it = config().shortcuts.find(id);
  if (it != config().shortcuts.end() && !it->second.empty()) return it->second;
  const ShortcutCommand *c = find_cmd(id);
  return c ? c->chord : "";
}

void shortcut_set(const std::string &id, const std::string &chord) {
  const ShortcutCommand *c = find_cmd(id);
  if (!c) return;
  if (chord.empty() || chord == c->chord) config().shortcuts.erase(id);
  else config().shortcuts[id] = chord_format(chord_parse(chord));
  config_save();
}

bool shortcut_is_default(const std::string &id) {
  return config().shortcuts.find(id) == config().shortcuts.end();
}

bool shortcut_pressed(const std::string &id) {
  ImGuiIO &io = ImGui::GetIO();
  if (io.WantTextInput) return false;
  KeyChord c = chord_parse(shortcut_chord(id));
  if (!c.valid()) return false;
  if (io.KeyCtrl != c.ctrl || io.KeyShift != c.shift || io.KeyAlt != c.alt) return false;
  return ImGui::IsKeyPressed((ImGuiKey)c.key, false);
}

KeyChord chord_captured() {
  ImGuiIO &io = ImGui::GetIO();
  KeyChord c;
  c.ctrl = io.KeyCtrl;
  c.shift = io.KeyShift;
  c.alt = io.KeyAlt;
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
    if (k == ImGuiKey_LeftCtrl || k == ImGuiKey_RightCtrl || k == ImGuiKey_LeftShift ||
        k == ImGuiKey_RightShift || k == ImGuiKey_LeftAlt || k == ImGuiKey_RightAlt ||
        k == ImGuiKey_LeftSuper || k == ImGuiKey_RightSuper)
      continue;
    if (key_name(k)[0] && ImGui::IsKeyPressed((ImGuiKey)k, false)) {
      c.key = k;
      return c;
    }
  }
  return c;
}

std::vector<std::string> shortcut_conflicts(const KeyChord &chord, const std::string &except_id) {
  std::vector<std::string> out;
  if (!chord.valid()) return out;
  std::string want = chord_format(chord);
  for (const ShortcutCommand &c : shortcut_commands())
    if (except_id != c.id && shortcut_chord(c.id) == want) out.push_back(c.id);
  return out;
}

} // namespace studio
