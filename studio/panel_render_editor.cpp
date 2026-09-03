// Geekatplay TerraForge — the render editor's panel sections: output format
// and passes, and the HDR backdrop dome. Drawn inside the Render properties
// tab (panel_render.cpp) and mirrored by the RenderOutput / RenderPasses /
// RenderBackdrop nodes, which overwrite these settings after every
// evaluation when they are present in the graph.
#include "app.hpp"
#include "render_settings.hpp"
#include <imgui.h>
#include <cstdio>
#include <string>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);
std::string renderer_backdrop_status();

namespace {

bool node_present(App &a, const char *type) {
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (!lk.owns_lock()) return false;
  for (auto &n : a.graph.nodes)
    if (n->type == type) return true;
  return false;
}

void driven_hint(App &a, const char *type) {
  if (node_present(a, type)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.55f, 0.24f, 1.f));
    ImGui::Text("driven by the %s node", type);
    ImGui::PopStyleColor();
  }
}

} // namespace

void render_passes_ui(App &a) {
  RenderSettings &rs = render_settings();
  ImGui::SeparatorText("Output & passes");
  driven_hint(a, "RenderOutput");
  ImGui::SetNextItemWidth(-90);
  ImGui::Combo("Beauty", &rs.render_format,
               "PNG 8-bit (tone mapped)\0EXR float (linear)\0HDR Radiance (linear)\0");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The finished image. Passes are always linear float EXR\n"
                      "beside it, named <file>_<pass>.exr.");
  driven_hint(a, "RenderPasses");
  int on = 0;
  for (int i = 0; i < RENDER_PASS_COUNT; ++i)
    if (rs.pass_mask & (1 << i)) ++on;
  char head[64];
  snprintf(head, sizeof head, "Passes (%d of %d)###passes", on, RENDER_PASS_COUNT);
  if (ImGui::TreeNodeEx(head, ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::SmallButton("all")) rs.pass_mask = (1 << RENDER_PASS_COUNT) - 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("none")) rs.pass_mask = 0;
    ImGui::SameLine();
    if (ImGui::SmallButton("compositing set"))
      rs.pass_mask = PASS_DEPTH | PASS_NORMAL | PASS_OBJECT_ID | PASS_ALBEDO |
                     PASS_DIRECT | PASS_AMBIENT | PASS_SPECULAR | PASS_ATMOSPHERE;
    for (int i = 0; i < RENDER_PASS_COUNT; ++i) {
      bool b = (rs.pass_mask & (1 << i)) != 0;
      if (studio::Checkbox(render_pass_label(i), &b))
        rs.pass_mask = b ? (rs.pass_mask | (1 << i)) : (rs.pass_mask & ~(1 << i));
    }
    ImGui::TextDisabled("Depth and position are in metres. Object id: 0 sky,\n"
                        "1 terrain, 2 water, 3+ scene objects in tree order.");
    ImGui::TreePop();
  }
}

void render_backdrop_ui(App &a) {
  RenderSettings &rs = render_settings();
  RenderSettings::Backdrop &b = rs.backdrop;
  ImGui::SeparatorText("Backdrop dome");
  driven_hint(a, "RenderBackdrop");
  studio::Checkbox("Enabled", &b.enabled);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("An HDR image at infinite distance behind everything.\n"
                      "The atmosphere still acts on it: horizon haze, clouds in\n"
                      "front, the fade to space, and it lights water and\n"
                      "terrain reflections.");
  static char path[1024] = {0};
  if (std::string(path) != b.file) snprintf(path, sizeof path, "%s", b.file.c_str());
  ImGui::SetNextItemWidth(-70);
  if (ImGui::InputText("##bdfile", path, sizeof path, ImGuiInputTextFlags_EnterReturnsTrue))
    b.file = path;
  ImGui::SameLine();
  if (ImGui::SmallButton("browse")) {
    std::string p = dialog_open_file(
        "HDR images\0*.hdr;*.exr;*.pic;*.png;*.jpg;*.jpeg\0All files\0*.*\0", "hdr");
    if (!p.empty()) {
      b.file = p;
      b.enabled = true;
    }
  }
  std::string status = renderer_backdrop_status();
  if (!status.empty()) ImGui::TextDisabled("%s", status.c_str());
  ImGui::SetNextItemWidth(-90);
  ImGui::Combo("Mapping", &b.mapping,
               "Equirectangular (lat-long)\0Angular map (light probe)\0Mirror ball\0"
               "Cube map cross\0Cylindrical panorama\0Sky dome (hemisphere)\0"
               "Planar backdrop\0");
  if (b.mapping == 4 || b.mapping == 6) {
    ImGui::SetNextItemWidth(-90);
    ImGui::SliderFloat("Vertical FOV", &b.vfov, 5.f, 179.f, "%.0f\xC2\xB0");
  }
  studio::Checkbox("Mirror horizontally", &b.flip);
  ImGui::SetNextItemWidth(-90);
  ImGui::SliderFloat("Rotate", &b.yaw, -180.f, 180.f, "%.0f\xC2\xB0");
  ImGui::SetNextItemWidth(-90);
  ImGui::SliderFloat("Tilt", &b.pitch, -90.f, 90.f, "%.0f\xC2\xB0");
  ImGui::SetNextItemWidth(-90);
  ImGui::SliderFloat("Exposure", &b.exposure_ev, -10.f, 10.f, "%.1f EV");
  ImGui::SetNextItemWidth(-90);
  ImGui::ColorEdit3("Tint", b.tint, ImGuiColorEditFlags_NoInputs);
  ImGui::SetNextItemWidth(-90);
  ImGui::SliderFloat("Blend over sky", &b.blend, 0.f, 1.f);
  ImGui::SetNextItemWidth(-90);
  ImGui::SliderFloat("Atmosphere on dome", &b.haze, 0.f, 1.f);
  studio::Checkbox("Hide the sun disc", &b.hide_sun);
}

} // namespace studio
