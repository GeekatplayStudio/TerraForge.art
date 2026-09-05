// Geekatplay TerraForge - the status bar along the bottom of the window:
// the status line on the left, and on the right everything worth knowing
// about the application's and the machine's health at a glance - frame
// rate and where the frame goes, the last evaluation, memory, CPU, VRAM,
// and what the performance governor has decided. Hover any figure for the
// longer story.
#include "app.hpp"
#include "config.hpp"
#include "perf.hpp"
#include <cstdio>
#include <imgui.h>
#include <string>

namespace studio {

namespace {

const char *human_mb(double mb, char *buf, size_t n) {
  if (mb < 0) snprintf(buf, n, "n/a");
  else if (mb >= 1024) snprintf(buf, n, "%.1f GB", mb / 1024.0);
  else snprintf(buf, n, "%.0f MB", mb);
  return buf;
}

void item(const char *text, const char *tip, ImVec4 col = ImVec4(0.72f, 0.72f, 0.70f, 1.f)) {
  ImGui::TextColored(col, "%s", text);
  if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
  ImGui::SameLine(0, 0);
  ImGui::TextDisabled("  \xC2\xB7  ");
  ImGui::SameLine(0, 0);
}

} // namespace

// The text below is aligned to frame padding, so reserve a complete frame.
// Using a fraction of the frame height clips the bottom of glyphs, especially
// when the UI font scale is above the default.
float statusbar_height() { return ImGui::GetFrameHeight(); }

void draw_statusbar(App &a) {
  const PerfStats &s = perf_stats();
  const PerfConfig &pc = config().perf;
  char b1[64], b2[64], b3[64], b4[64];
  // right side first: measure, then place
  std::string right;
  char buf[256];
  ImGui::BeginChild("##statusbar", ImVec2(0, statusbar_height()), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s", a.status.c_str());
  // the figures, right-aligned: laid out once to know their width
  struct Fig { char text[64]; const char *tip; ImVec4 col; };
  Fig figs[10];
  int n = 0;
  auto add = [&](const char *tip, ImVec4 col, const char *fmt, auto... args) {
    if (n >= 10) return;
    snprintf(figs[n].text, sizeof figs[n].text, fmt, args...);
    figs[n].tip = tip;
    figs[n].col = col;
    ++n;
  };
  const ImVec4 ok(0.62f, 0.78f, 0.52f, 1.f), warn(0.9f, 0.62f, 0.25f, 1.f), dim(0.72f, 0.72f, 0.70f, 1.f);
  bool slow = s.potential_fps > 0 && s.potential_fps < pc.fps_primary;
  add("Frames shown per second in the main window (capped by vsync and the viewport rate),\n"
      "and how many the work would allow if nothing capped it.",
      slow ? warn : ok, "FPS %.0f (%.0f possible)", s.fps, s.potential_fps);
  add("Where a frame goes: interface, the 3D views, node previews, API, GPU uploads.\n"
      "CPU work per frame excludes the time the loop sleeps to the frame rate.",
      dim, "frame %.1f ms  ui %.1f  views %.1f  gpu %.1f", s.work_ms, s.ui_ms, s.views_ms, s.gpu_ms);
  add("The last graph evaluation, and how many terrain patches survived culling this frame.", dim,
      "eval %.0f ms  %d nodes  %d patches", s.eval_ms, s.nodes, s.patches);
  add("This process's working set (peak), and the memory the system still has free.",
      s.system_free_mb > 0 && s.system_free_mb < 1024 ? warn : dim, "RAM %s (peak %s)  free %s / %s",
      human_mb(s.process_mb, b1, sizeof b1), human_mb(s.process_peak_mb, b2, sizeof b2),
      human_mb(s.system_free_mb, b3, sizeof b3), human_mb(s.system_total_mb, b4, sizeof b4));
  add("This process's share of every core.", s.cpu_pct > 80 ? warn : dim, "CPU %.0f%% of %d", s.cpu_pct, s.cpu_cores);
  if (s.vram_total_mb > 0) {
    char v1[32], v2[32];
    add("Video memory in use / total, as the driver reports it.", s.vram_used_mb > s.vram_total_mb * 0.9 ? warn : dim,
        "VRAM %s / %s", human_mb(s.vram_used_mb, v1, sizeof v1), human_mb(s.vram_total_mb, v2, sizeof v2));
  } else {
    add("The driver does not report video memory over OpenGL (NVIDIA and AMD do).", dim, "VRAM n/a");
  }
  add(s.gpu_name.c_str(), dim, "%s", s.gpu_name.size() > 28 ? (s.gpu_name.substr(0, 27) + "~").c_str() : s.gpu_name.c_str());
  add(pc.governor ? "The performance governor: when the work would drop the main view below the\n"
                    "threshold in Settings, it lightens secondary views, the preview, shadows, clouds\n"
                    "and subdivision a step at a time, and gives them back when the frame is comfortable."
                  : "The governor is off (Settings > General).",
      s.governor_level ? warn : ok, "%s", pc.governor ? (s.governor_level ? s.governor_note.c_str() : "governor: full") : "governor off");
  float total = 0.f;
  for (int i = 0; i < n; ++i) total += ImGui::CalcTextSize(figs[i].text).x + ImGui::CalcTextSize("  \xC2\xB7  ").x;
  float x = ImGui::GetWindowContentRegionMax().x - total;
  if (x > ImGui::GetCursorPosX() + 40.f) ImGui::SameLine(x);
  else ImGui::SameLine();
  for (int i = 0; i < n; ++i) {
    ImGui::TextColored(figs[i].col, "%s", figs[i].text);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", figs[i].tip);
    if (i + 1 < n) {
      ImGui::SameLine(0, 0);
      ImGui::TextDisabled("  \xC2\xB7  ");
      ImGui::SameLine(0, 0);
    }
  }
  (void)right;
  (void)buf;
  ImGui::EndChild();
}

} // namespace studio
