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
  std::ofstream f(PREFS_FILE);
  f << j.dump(2);
}

} // namespace studio
