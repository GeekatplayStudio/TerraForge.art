#include "prefs.hpp"
#include <fstream>
#include <json.hpp>

using json = nlohmann::json;

namespace studio {

Prefs &prefs() {
  static Prefs p;
  return p;
}

static const char *PREFS_FILE = "terraforge_prefs.json";

void prefs_load() {
  std::ifstream f(PREFS_FILE);
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
    p.view_count = j.value("view_count", p.view_count);
    p.graph_memory_mb = j.value("graph_memory_mb", p.graph_memory_mb);
    p.editor_domains.clear();
    for (const auto &d : j.value("editor_domains", json::array()))
      if (d.is_number_integer()) p.editor_domains.push_back(d.get<int>());
    p.viewport_fps = j.value("viewport_fps", p.viewport_fps);
    p.idle_fps = j.value("idle_fps", p.idle_fps);
    p.preview_fps = j.value("preview_fps", p.preview_fps);
    p.preview_quality = j.value("preview_quality", p.preview_quality);
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
  j["view_count"] = p.view_count;
  j["graph_memory_mb"] = p.graph_memory_mb;
  j["editor_domains"] = p.editor_domains;
  j["viewport_fps"] = p.viewport_fps;
  j["idle_fps"] = p.idle_fps;
  j["preview_fps"] = p.preview_fps;
  j["preview_quality"] = p.preview_quality;
  std::ofstream f(PREFS_FILE);
  f << j.dump(2);
}

} // namespace studio
