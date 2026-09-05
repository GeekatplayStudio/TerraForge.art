#include "prefs.hpp"
#include <algorithm>
#include "paths.hpp"
#include <fstream>
#include <json.hpp>

using json = nlohmann::json;

namespace studio {

Prefs &prefs() {
  static Prefs p;
  return p;
}

// Kept beside the executable when the application is run from a writable
// working directory (the developer's build/), and in the per-user data
// directory otherwise - an installed copy may sit somewhere read-only, and a
// macOS bundle is launched with the working directory set to "/".
// settings_path() prefers an existing file in the current directory, so an
// existing checkout keeps the preferences it already has.
static std::string prefs_file() {
  return settings_path("terraforge_prefs.json").string();
}

void prefs_load() {
  std::ifstream f(prefs_file());
  if (!f) return;
  try {
    json j = json::parse(f);
    Prefs &p = prefs();
    p.font_size = j.value("font_size", p.font_size);
    p.ui_scale = j.value("ui_scale", p.ui_scale);
    p.ollama_url = j.value("ollama_url", p.ollama_url);
    p.text_model = j.value("text_model", p.text_model);
    p.vision_model = j.value("vision_model", p.vision_model);
    p.interactive_res = j.value("interactive_res", p.interactive_res);
    // view_mask replaced view_count in 2026-09: a saved count of N means
    // the first N views were open, which is exactly the low N bits.
    if (j.contains("view_mask") && j["view_mask"].is_number())
      p.view_mask = j["view_mask"].get<unsigned>();
    else if (j.contains("view_count") && j["view_count"].is_number()) {
      int n = std::clamp(j["view_count"].get<int>(), 1, 8);
      p.view_mask = (1u << n) - 1u;
    }
    if (!p.view_mask) p.view_mask = 1;
    p.current_layout = j.value("current_layout", p.current_layout);
    p.graph_memory_mb = j.value("graph_memory_mb", p.graph_memory_mb);
    p.editor_domains.clear();
    for (const auto &d : j.value("editor_domains", json::array()))
      if (d.is_number_integer()) p.editor_domains.push_back(d.get<int>());
    p.viewport_fps = j.value("viewport_fps", p.viewport_fps);
    p.idle_fps = j.value("idle_fps", p.idle_fps);
    p.preview_fps = j.value("preview_fps", p.preview_fps);
    p.preview_quality = j.value("preview_quality", p.preview_quality);
    p.icon_size = std::clamp(j.value("icon_size", p.icon_size), 0, 2);
    p.language = j.value("language", p.language);
  } catch (...) {
  }
}

void prefs_save() {
  Prefs &p = prefs();
  json j;
  j["font_size"] = p.font_size;
  j["ui_scale"] = p.ui_scale;
  j["ollama_url"] = p.ollama_url;
  j["text_model"] = p.text_model;
  j["vision_model"] = p.vision_model;
  j["interactive_res"] = p.interactive_res;
  j["view_mask"] = p.view_mask;
  j["current_layout"] = p.current_layout;
  j["graph_memory_mb"] = p.graph_memory_mb;
  j["editor_domains"] = p.editor_domains;
  j["viewport_fps"] = p.viewport_fps;
  j["idle_fps"] = p.idle_fps;
  j["preview_fps"] = p.preview_fps;
  j["preview_quality"] = p.preview_quality;
  j["icon_size"] = p.icon_size;
  j["language"] = p.language;
  std::ofstream f(prefs_file());
  f << j.dump(2);
}

} // namespace studio
