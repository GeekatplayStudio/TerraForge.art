// Geekatplay TerraForge — turning a typed command into an action document.
//
// Two input forms, because both are the natural one at different moments:
//
//   add_node type=Noise octaves=9      shorthand, what a person types
//   {"op":"add_node","type":"Noise"}   JSON, what a script pastes
//
// Anything starting with { or [ is JSON and goes through untouched. Everything
// else is the shorthand: first word is the op, then key=value pairs, where a
// value that parses as a number or a bool becomes one and the rest are
// strings. A bare `key=[0,1,2]` or `key={"a":1}` is passed through as JSON, so
// the ops taking vectors stay reachable.
//
// No window in this file on purpose — the panel is console_cmd.cpp. This half
// is the one that can be wrong quietly, so it is tested.
#include "console_shorthand.hpp"
#include <json.hpp>
#include <cctype>
#include <string>
#include <vector>

using nlohmann::json;

namespace studio {

namespace {

// One `key=value` into a JSON member. Numbers and bools become themselves so
// that set_attr on a float attribute gets a float and not "0.5".
void assign(json &obj, const std::string &key, const std::string &val) {
  if (val.empty()) {
    obj[key] = "";
    return;
  }
  const char c = val[0];
  if (c == '{' || c == '[' || c == '"') {
    // a JSON fragment: a vector, an object, or a string that wants its quotes
    json parsed = json::parse(val, nullptr, false);
    if (!parsed.is_discarded()) {
      obj[key] = parsed;
      return;
    }
  }
  if (val == "true") { obj[key] = true; return; }
  if (val == "false") { obj[key] = false; return; }
  try {
    size_t used = 0;
    double d = std::stod(val, &used);
    if (used == val.size()) {
      // an integer stays an integer: choice attributes take an index, and
      // 3.0 is not an index
      if (val.find_first_of(".eE") == std::string::npos)
        obj[key] = (long long)d;
      else
        obj[key] = d;
      return;
    }
  } catch (const std::exception &) {
  }
  obj[key] = val;
}

// The op names worth completing on. Not exhaustive by design — it is the set a
// person drives the application with, and `help` points at the full schema.
const char *const COMMON_OPS[] = {
    "add_node",      "set_attr",      "connect",        "disconnect",
    "delete_node",   "move_node",     "bypass",         "view_node",
    "select_node",   "find_nodes",    "set_resolution", "clear_graph",
    "evaluate",      "graph",         "import_object",  "import_mesh",
    "assign_material", "set_material", "open_material", "list_materials",
    "preset_material",
    "add_camera",    "set_camera",    "set_workspace",  "show_panel",
    "set_viewport",  "save_layout",   "load_layout",    "arrange_views",
    "set_sun",       "set_sky",       "set_fog",        "set_water",
    "set_clouds",    "set_render",    "render",         "bake",
    "save_project",  "open_project",  "set_frame",      "play",
    "paint_save",    "paint_load",    "paint_clear",    "probe_height",
    "perf_report",   "list_settings", "set_setting",  "crash_reports",
    "crash_mark_fixed", "terrain_clip", "terrain_effect", "terrain_style",
    "terrain_global", "terrain_import_picture", "set_sculpt", "add_component",
    "undo",          "redo",          nullptr};

} // namespace

const char *const *console_common_ops() { return COMMON_OPS; }

std::vector<std::string> console_tokenize(const std::string &s) {
  std::vector<std::string> out;
  std::string cur;
  bool quoted = false, any = false;
  int depth = 0; // so key=[1, 2, 3] survives the split
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '"' && (i == 0 || s[i - 1] != '\\')) {
      quoted = !quoted;
      cur += c;
      any = true;
      continue;
    }
    if (!quoted) {
      if (c == '[' || c == '{') ++depth;
      if (c == ']' || c == '}') --depth;
      if (std::isspace((unsigned char)c) && depth <= 0) {
        if (any) out.push_back(cur);
        cur.clear();
        any = false;
        continue;
      }
    }
    cur += c;
    any = true;
  }
  if (any) out.push_back(cur);
  return out;
}

std::string console_shorthand_to_json(const std::string &line, std::string &err) {
  const size_t first = line.find_first_not_of(" \t");
  if (first == std::string::npos) return "";
  if (line[first] == '{' || line[first] == '[') return line; // already JSON

  std::vector<std::string> tok = console_tokenize(line);
  if (tok.empty()) return "";
  json act;
  act["op"] = tok[0];
  for (size_t i = 1; i < tok.size(); ++i) {
    const size_t eq = tok[i].find('=');
    if (eq == std::string::npos || eq == 0) {
      err = "expected key=value, got '" + tok[i] + "'";
      return "";
    }
    std::string key = tok[i].substr(0, eq), val = tok[i].substr(eq + 1);
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
      val = val.substr(1, val.size() - 2);
    // `attrs.octaves=9` sets a member of the attrs object, which is how
    // add_node carries node parameters
    const size_t dot = key.find('.');
    if (dot != std::string::npos && dot > 0) {
      std::string outer = key.substr(0, dot), inner = key.substr(dot + 1);
      if (!act.contains(outer) || !act[outer].is_object()) act[outer] = json::object();
      assign(act[outer], inner, val);
    } else {
      assign(act, key, val);
    }
  }
  // add_node's parameters live under "attrs"; typing them at the top level is
  // what everyone does first, so move the ones that are not the op's own.
  if (act["op"] == "add_node") {
    static const char *own[] = {"op", "type", "x", "y", "alias", "attrs"};
    json attrs = act.value("attrs", json::object());
    for (auto it = act.begin(); it != act.end();) {
      bool is_own = false;
      for (const char *o : own) is_own = is_own || it.key() == o;
      if (is_own) { ++it; continue; }
      attrs[it.key()] = it.value();
      it = act.erase(it);
    }
    if (!attrs.empty()) act["attrs"] = attrs;
  }
  return json::array({act}).dump();
}

} // namespace studio
