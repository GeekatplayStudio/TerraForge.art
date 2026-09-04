// Geekatplay TerraForge — user preferences (persisted JSON)
#pragma once
#include <string>
#include <vector>

namespace studio {

struct Prefs {
  float font_size = 17.f;      // base font pixel size (needs restart note if font reloads)
  float ui_scale = 1.f;        // live global scale
  std::string ollama_url = "http://127.0.0.1:11434";
  std::string text_model = "llama3.1";
  std::string vision_model = "llava";
  int interactive_res = 256;   // eval resolution while dragging sliders
  // Which viewport windows are open, one bit per slot (bit 0 = View 1). A set
  // rather than a count, so closing View 2 of four leaves the other three
  // exactly where they were instead of renumbering them. Never 0: the app
  // reopens View 1 rather than leave the user with no viewport at all.
  unsigned view_mask = 1;
  // The named layout that was last loaded or saved, so the Layouts menu can
  // show which one you are working in. Empty means an unnamed arrangement.
  std::string current_layout;
  // Ceiling on the cached node output buffers, in megabytes. Every output port
  // holds its buffer for the graph's lifetime, so a deep graph at high
  // resolution can hold gigabytes nothing will read again: at 4096 one
  // heightmap output is 64 MB and one RGBA texture output is 256 MB.
  //
  // The default is high enough that ordinary work never reaches it and
  // behaviour is unchanged; it is a floor under the worst case, not a
  // day-to-day constraint. 0 means no ceiling.
  int graph_memory_mb = 1024;
  // Extra node editor windows, one domain each (0 terrain, 1 materials,
  // 2 atmosphere, 3 render, 4 all), so the layout comes back on launch.
  std::vector<int> editor_domains;
  // Frame pacing. The viewports redraw every frame up to this rate; when
  // nothing is happening (no input, no evaluation, no playback) the app
  // idles at idle_fps so the GPU is free for the things that are.
  int viewport_fps = 60;
  int idle_fps = 15;
  // The Preview panel's own rate and render scale (0 = 25 %, 1 = 50 %,
  // 2 = 100 %): the live picture is the most expensive thing on screen.
  int preview_fps = 10;
  int preview_quality = 1;
};

Prefs &prefs();
void prefs_load();
void prefs_save();

} // namespace studio
