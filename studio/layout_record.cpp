// Geekatplay TerraForge - saved window layouts, as data.
// See layout_record.hpp for what a layout is and why this half has no UI in it.
#include "layout_record.hpp"
#include "paths.hpp"
#include <algorithm>
#include <fstream>
#include <json.hpp>
#include <sstream>

using nlohmann::json;

namespace studio {

std::string layout_safe_name(const std::string &name) {
  std::string out;
  for (char c : name) {
    unsigned char u = (unsigned char)c;
    bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
              (u >= '0' && u <= '9') || c == ' ' || c == '-' || c == '_';
    if (ok) out.push_back(c);
  }
  // trim the ends: a leading or trailing space in a file name is a trap
  size_t b = out.find_first_not_of(' ');
  size_t e = out.find_last_not_of(' ');
  out = (b == std::string::npos) ? std::string() : out.substr(b, e - b + 1);
  if (out.size() > 48) out.resize(48);
  if (out.empty()) out = "Layout";
  return out;
}

std::filesystem::path layouts_dir() {
  std::filesystem::path d = settings_path("layouts");
  std::error_code ec;
  std::filesystem::create_directories(d, ec);
  return d;
}

std::string layout_to_json(const LayoutRecord &r) {
  json j;
  j["version"] = 1;
  j["name"] = r.name;
  j["ini"] = r.ini;
  j["view_mask"] = r.view_mask;
  json views = json::array();
  for (const LayoutRecord::View &v : r.views)
    views.push_back({{"camera", v.camera},
                     {"display", v.display},
                     {"scene_camera", v.scene_camera},
                     {"atmosphere", v.atmosphere},
                     {"water", v.water},
                     {"grid", v.grid},
                     {"outlines", v.outlines}});
  j["views"] = std::move(views);
  j["editor_domains"] = r.editor_domains;
  j["panels"] = {{"library", r.library},
                 {"nodelist", r.nodelist},
                 {"properties", r.properties},
                 {"viewport", r.viewport},
                 {"console", r.console},
                 {"timeline", r.timeline},
                 {"preview", r.preview},
                 {"material_editor", r.material_editor}};
  j["workspace"] = r.workspace;
  return j.dump(2);
}

bool layout_from_json(const std::string &text, LayoutRecord &r,
                      std::string &err) {
  json j = json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    err = "not a layout file";
    return false;
  }
  r.name = j.value("name", r.name);
  r.ini = j.value("ini", std::string());
  if (j.contains("view_mask") && j["view_mask"].is_number())
    r.view_mask = j["view_mask"].get<unsigned>();
  if (!r.view_mask) r.view_mask = 1; // never leave the user with no viewport
  r.views.clear();
  for (const auto &v : j.value("views", json::array())) {
    if (!v.is_object()) continue;
    LayoutRecord::View out;
    out.camera = v.value("camera", out.camera);
    out.display = v.value("display", out.display);
    out.scene_camera = v.value("scene_camera", out.scene_camera);
    out.atmosphere = v.value("atmosphere", out.atmosphere);
    out.water = v.value("water", out.water);
    out.grid = v.value("grid", out.grid);
    out.outlines = v.value("outlines", out.outlines);
    r.views.push_back(out);
  }
  r.editor_domains.clear();
  for (const auto &d : j.value("editor_domains", json::array()))
    if (d.is_number_integer()) r.editor_domains.push_back(d.get<int>());
  const json p = j.value("panels", json::object());
  r.library = p.value("library", r.library);
  r.nodelist = p.value("nodelist", r.nodelist);
  r.properties = p.value("properties", r.properties);
  r.viewport = p.value("viewport", r.viewport);
  r.console = p.value("console", r.console);
  r.timeline = p.value("timeline", r.timeline);
  r.preview = p.value("preview", r.preview);
  r.material_editor = p.value("material_editor", r.material_editor);
  r.workspace = j.value("workspace", r.workspace);
  return true;
}

std::vector<std::string> layout_list() {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto &e : std::filesystem::directory_iterator(layouts_dir(), ec)) {
    if (ec) break;
    if (!e.is_regular_file(ec)) continue;
    if (e.path().extension() != ".json") continue;
    names.push_back(e.path().stem().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

bool layout_write(const LayoutRecord &r, std::string &err) {
  const std::string safe = layout_safe_name(r.name);
  std::filesystem::path f = layouts_dir() / (safe + ".json");
  std::ofstream out(f, std::ios::binary);
  if (!out) {
    err = "cannot write " + f.string();
    return false;
  }
  LayoutRecord copy = r;
  copy.name = safe;
  out << layout_to_json(copy);
  return (bool)out;
}

bool layout_read(const std::string &name, LayoutRecord &r, std::string &err) {
  std::filesystem::path f =
      layouts_dir() / (layout_safe_name(name) + ".json");
  std::ifstream in(f, std::ios::binary);
  if (!in) {
    err = "no layout called '" + name + "'";
    return false;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return layout_from_json(ss.str(), r, err);
}

bool layout_erase(const std::string &name, std::string &err) {
  std::filesystem::path f =
      layouts_dir() / (layout_safe_name(name) + ".json");
  std::error_code ec;
  if (!std::filesystem::remove(f, ec)) {
    err = "no layout called '" + name + "'";
    return false;
  }
  return true;
}

} // namespace studio
