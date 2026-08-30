// Geekatplay TerraForge — user preferences (persisted JSON)
#pragma once
#include <string>

namespace studio {

struct Prefs {
  float font_size = 17.f;      // base font pixel size (needs restart note if font reloads)
  float ui_scale = 1.f;        // live global scale
  std::string ollama_url = "http://127.0.0.1:11434";
  std::string text_model = "llama3.1";
  std::string vision_model = "llava";
  int interactive_res = 256;   // eval resolution while dragging sliders
  int view_count = 1;          // viewport windows (1..6), remembered
};

Prefs &prefs();
void prefs_load();
void prefs_save();

} // namespace studio
