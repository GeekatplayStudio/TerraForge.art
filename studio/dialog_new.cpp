// Geekatplay TerraForge - the New terrain dialog.
//
// "New" used to silently reuse whatever size the last project happened to
// have, which is how you end up modelling a mountain range on a 500 m tile
// and wondering why the erosion looks wrong. The size of the ground, the
// height range above it and the sampling resolution are the three numbers
// that decide what a terrain can be, so they are asked for once, up front,
// in real units.
#include "app.hpp"
#include "render_settings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>

namespace studio {

namespace {

bool g_open = false;

// Ground covered, highest point, and what each is for. The presets are real
// places at real scale, because "5000" means nothing and "a large valley"
// means something.
struct Preset {
  const char *name;
  float size_m;
  float relief_m;
  int res;
  const char *note;
};
const Preset PRESETS[] = {
    {"Quarry", 500.f, 90.f, 512, "a pit, a cliff face, a few buildings"},
    {"Valley", 5000.f, 1100.f, 1024, "one valley, ridge to ridge"},
    {"Range", 25000.f, 3400.f, 2048, "a mountain range with its foothills"},
    {"Region", 120000.f, 8000.f, 2048, "a whole region, Himalaya-scale relief"},
};

float g_size_m = 5000.f;
float g_relief_m = 1100.f;
int g_res = 1024;

} // namespace

void new_terrain_request() {
  RenderSettings &rs = render_settings();
  // open on what the current project uses, so "New" starts from where you are
  g_size_m = rs.terrain_size_m;
  g_relief_m = rs.height_scale * rs.terrain_size_m;
  g_open = true;
}

void new_terrain_dialog(App &a) {
  if (g_open) {
    ImGui::OpenPopup("New terrain");
    g_open = false;
    g_res = a.graph.resolution;
  }
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("New terrain", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    return;

  ImGui::SeparatorText("Start from");
  for (int i = 0; i < (int)(sizeof PRESETS / sizeof PRESETS[0]); ++i) {
    const Preset &p = PRESETS[i];
    if (i) ImGui::SameLine(0, 4);
    bool on = std::fabs(g_size_m - p.size_m) < 1.f &&
              std::fabs(g_relief_m - p.relief_m) < 1.f;
    if (on)
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
    if (ImGui::Button(p.name, ImVec2(108, 0))) {
      g_size_m = p.size_m;
      g_relief_m = p.relief_m;
      g_res = p.res;
    }
    if (on) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s\n%.0f m across, up to %.0f m high, %d samples",
                        p.note, p.size_m, p.relief_m, p.res);
  }

  ImGui::SeparatorText("Ground");
  ImGui::TextUnformatted("Across");
  ImGui::SetNextItemWidth(340);
  ImGui::DragFloat("##size", &g_size_m, 25.f, 10.f, 4.0e7f, "%.0f m",
                   ImGuiSliderFlags_Logarithmic);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How much ground the terrain tile covers, edge to edge.\n"
                      "Every other length in the application is measured\n"
                      "against this.");
  ImGui::TextUnformatted("Highest point above the base");
  ImGui::SetNextItemWidth(340);
  ImGui::DragFloat("##relief", &g_relief_m, 5.f, 1.f, 3.0e5f, "%.0f m",
                   ImGuiSliderFlags_Logarithmic);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The vertical range the heightfield spans. Real ranges\n"
                      "rise about 1 m for every 4 to 8 m travelled; much more\n"
                      "than that and erosion has nothing plausible to do.");

  ImGui::SeparatorText("Detail");
  ImGui::TextUnformatted("Resolution");
  const char *res_labels[] = {"256", "512", "1024", "2048", "4096", "8192"};
  const int res_values[] = {256, 512, 1024, 2048, 4096, 8192};
  for (int i = 0; i < 6; ++i) {
    if (i) ImGui::SameLine(0, 3);
    bool on = g_res == res_values[i];
    if (on)
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
    if (ImGui::Button(res_labels[i], ImVec2(52, 0))) g_res = res_values[i];
    if (on) ImGui::PopStyleColor();
  }

  // What the numbers actually mean, in one line each: the ground each sample
  // covers, the steepness the range implies, and what the buffers will cost.
  float per_sample = g_size_m / (float)std::max(g_res, 1);
  double mb = (double)g_res * g_res * 4.0 / (1024.0 * 1024.0);
  ImGui::Spacing();
  if (per_sample < 1.f)
    ImGui::TextDisabled("one sample every %.0f cm", per_sample * 100.f);
  else
    ImGui::TextDisabled("one sample every %.1f m", per_sample);
  ImGui::TextDisabled("slope budget: 1 m up for every %.1f m across",
                      g_size_m / std::max(g_relief_m, 0.001f));
  ImGui::TextDisabled("%.1f MB per cached heightfield", mb);
  if (g_relief_m > g_size_m * 0.5f)
    ImGui::TextColored(ImVec4(0.85f, 0.55f, 0.24f, 1.f),
                       "That is steeper than any real landscape.");

  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button("Create", ImVec2(150, 0))) {
    RenderSettings &rs = render_settings();
    rs.terrain_size_m = g_size_m;
    rs.height_scale = std::clamp(g_relief_m / std::max(g_size_m, 1.f), 0.001f,
                                 4.f);
    project_new(a);
    {
      std::unique_lock<std::mutex> lk(a.graph_mtx);
      a.graph.resolution = std::clamp(g_res, 64, 8192);
    }
    project_default_graph(a);
    a.graph_layout_serial++;
    a.request_eval();
    char st[128];
    std::snprintf(st, sizeof st, "new terrain: %.0f m across, %.0f m high, %d",
                  g_size_m, g_relief_m, g_res);
    a.status = st;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

} // namespace studio
