// Geekatplay TerraForge — Vue's Terrain Editor on the Terrain object's tab.
//
// The sections follow Vue's window (Reference Manual p519-546): the styles
// down its left edge, the Paint tab's brushes and global settings, the
// Effects tab's erosion and global effects with Rock hardness, the clipping
// slider, the toolbar's global commands, and the Picture button. Every
// control is a graph operation (terrain_editor.hpp): a node in front of the
// Terrain Output, undoable, and reachable by the same name from a script.
#include "app.hpp"
#include "wheel_widgets.hpp"

#include "toolbar_internal.hpp"
#include "icons.hpp"
#include "prop_lengths.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "sculpt.hpp"
#include "terrain_editor.hpp"
#include <algorithm>
#include <cstdio>
#include <imgui.h>
#include <string>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext); // file_dialogs.cpp

namespace {

void tip(const char *t) {
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t);
}

// A row of equal buttons, wrapping to the panel's width.
bool button_grid(const char *label, float w) {
  const float avail = ImGui::GetContentRegionAvail().x;
  const float x = ImGui::GetCursorPosX();
  bool hit = ImGui::Button(label, ImVec2(w, 0));
  const float next = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
  if (next + w <= ImGui::GetWindowPos().x + x + avail) ImGui::SameLine();
  return hit;
}

void styles_ui(App &a) {
  if (!prop_filter_match("Styles")) return;
  ImGui::SeparatorText("Styles");
  ImGui::TextDisabled("Each drops a fresh chain into the graph, wired to the\n"
                      "Terrain Output; the old chain stays, Ctrl+Z undoes it.");
  const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.f;
  std::string err;
  for (const std::string &s : terrain_style_names())
    if (button_grid(s.c_str(), w)) terrain_style_apply(a, s, err);
  ImGui::NewLine();
}

void sculpt_ui(App &a) {
  if (!prop_filter_match("Sculpt")) return;
  ImGui::SeparatorText("Sculpt");
  SculptState &s = sculpt_state();
  bool on = s.active;
  if (Checkbox("Sculpt mode", &on)) {
    sculpt_set_active(a, on);
    if (on && a.workspace != WS_TERRAIN) a.workspace = WS_TERRAIN;
  }
  tip("Brush directly on the terrain in the 3D view. Strokes live in a\n"
      "TerrainSculpt node, so retuning the chain underneath keeps them.\n"
      "[ and ] resize the brush, the wheel does too, Alt inverts.");
  struct B { SculptTool tool; const char *label, *tip; };
  static const B brushes[] = {
      {SculptTool::Raise, "Raise", "Add relief; Invert (or Alt) digs. Vue's Raise."},
      {SculptTool::Plateau, "Plateaus",
       "Pull toward a horizontal plane at the brush centre, recomputed as the\nbrush moves. Vue's Plateaus."},
      {SculptTool::Flatten, "Flatten",
       "Pull toward the height under the first click. Vue's UniSlope: the plane\nis fixed where the stroke started."},
      {SculptTool::Altitude, "Altitude", "Pull the surface toward the target altitude below. Vue's Altitude."},
      {SculptTool::Smooth, "Smooth", "Relax bumps and stroke marks."},
      {SculptTool::Terrace, "Terrace", "Cut the slope into steps."},
      {SculptTool::Noise, "Noise", "Stamp fractal detail; Invert inverts it."},
      {SculptTool::Erase, "Erase", "Remove sculpted strokes, revealing the procedural terrain."},
      {SculptTool::Shade, "Shade", "Paint a chosen grey: dark carves, light raises, mid does nothing."},
  };
  const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.f;
  for (const B &b : brushes) {
    const bool active = s.tool == b.tool;
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.45f, 0.16f, 1.f));
    if (button_grid(b.label, w)) {
      s.tool = b.tool;
      if (!s.active) {
        sculpt_set_active(a, true);
        if (a.workspace != WS_TERRAIN) a.workspace = WS_TERRAIN;
      }
    }
    if (active) ImGui::PopStyleColor();
    tip(b.tip);
  }
  ImGui::NewLine();
  // Vue's Global settings: Invert, Radius, Flow, Falloff, Altitude,
  // Constrain to clipping range
  Checkbox("Invert", &s.invert);
  ImGui::SameLine();
  Checkbox("Constrain to clipping range", &s.constrain_clip);
  tip("The brush cannot push the ground past the two clipping altitudes below.");
  labeled_scalar("Radius", "sr", &s.radius, 0.005f, 0.4f);
  labeled_scalar("Flow", "sf", &s.flow, 0.f, 2.f);
  labeled_scalar("Falloff", "sfo", &s.falloff, 0.2f, 8.f);
  if (s.tool == SculptTool::Altitude) {
    const RenderSettings &rs = render_settings();
    float m = s.altitude * rs.height_scale * rs.terrain_size_m;
    ImGui::TextUnformatted("Target altitude");
    ImGui::SetNextItemWidth(-1);
    if (studio::DragFloatW("##salt", &m, 5.f, 0.f, rs.height_scale * rs.terrain_size_m, "%.0f m"))
      s.altitude = std::clamp(m / std::max(rs.height_scale * rs.terrain_size_m, 1.f), 0.f, 1.f);
  }
  if (s.tool == SculptTool::Shade) labeled_scalar("Shade", "ssh", &s.shade, 0.f, 1.f);
  std::string err;
  if (ImGui::SmallButton("Reset sculpting")) terrain_editor_global(a, "reset_sculpt", err);
  tip("Clears every stroke; the procedural terrain underneath is untouched.");
}

void effects_ui(App &a) {
  if (!prop_filter_match("Effects")) return;
  ImGui::SeparatorText("Effects");
  static float hardness = 0.5f;
  labeled_scalar("Rock hardness", "rh", &hardness, 0.f, 1.f);
  tip("Vue's Rock hardness: influences every erosion. Hard rock erodes\n"
      "less, keeps steeper scree and narrower streams.");
  ImGui::TextDisabled("Erosion");
  const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4.f;
  std::string err;
  for (const std::string &e : terrain_editor_effect_names(true))
    if (button_grid(e.c_str(), w)) terrain_editor_effect(a, e, hardness, err);
  ImGui::NewLine();
  ImGui::TextDisabled("Global effects");
  for (const std::string &e : terrain_editor_effect_names(false))
    if (button_grid(e.c_str(), w)) terrain_editor_effect(a, e, hardness, err);
  ImGui::NewLine();
  const int n = terrain_editor_effect_count(a);
  ImGui::TextDisabled("Each click adds one pass in front of the Terrain Output\n"
                      "(Vue's iteration count is clicking again). %d in the chain.", n);
  if (n > 0 && ImGui::SmallButton("Remove all effects")) terrain_editor_global(a, "remove_effects", err);
  if (!err.empty()) ImGui::TextColored(ImVec4(0.85f, 0.3f, 0.2f, 1.f), "%s", err.c_str());
}

void clipping_ui(App &a) {
  if (!prop_filter_match("Clipping")) return;
  ImGui::SeparatorText("Clipping altitudes");
  const RenderSettings &rs = render_settings();
  const float top_m = std::max(rs.height_scale * rs.terrain_size_m, 1.f);
  TerrainClipState st = terrain_editor_clip_get(a);
  static TerrainClipState draft;
  static bool drafted = false;
  if (st.present || !drafted) {
    draft = st;
    if (!st.present) { draft.low = 0.f; draft.high = 1.f; }
    drafted = true;
  }
  ImGui::TextDisabled("Anything beyond the two altitudes is left out of the\n"
                      "terrain: low ground becomes a hole (or a flat), high\n"
                      "ground a flat top. Vue's clip slider, one end each.");
  float lo_m = draft.low * top_m, hi_m = draft.high * top_m;
  bool changed = false;
  ImGui::SetNextItemWidth(-1);
  if (ImGui::DragFloatRange2("##cliprange", &lo_m, &hi_m, 5.f, 0.f, top_m, "low %.0f m", "high %.0f m")) {
    draft.low = std::clamp(lo_m / top_m, 0.f, 1.f);
    draft.high = std::clamp(hi_m / top_m, 0.f, 1.f);
    changed = true;
  }
  tip("Drag either end; drag the middle to move both together.");
  ImGui::TextUnformatted("Below the low mark");
  ImGui::SetNextItemWidth(-1);
  if (ImGui::Combo("##clow", &draft.low_mode, "Hole\0Flatten\0")) changed = true;
  ImGui::TextUnformatted("Above the high mark");
  ImGui::SetNextItemWidth(-1);
  if (ImGui::Combo("##chigh", &draft.high_mode, "Flatten\0Hole\0")) changed = true;
  float soft = draft.softness;
  labeled_scalar("Edge softness", "csoft", &soft, 0.f, 0.2f);
  if (soft != draft.softness) { draft.softness = soft; changed = true; }
  std::string err;
  if (changed) {
    if (!terrain_editor_clip_set(a, draft, err)) a.status = err;
  }
  if (st.present) {
    ImGui::TextDisabled("A TerrainClip node in front of the Terrain Output.");
    if (ImGui::SmallButton("Remove clipping")) {
      terrain_editor_clip_clear(a, err);
      drafted = false;
    }
  } else {
    ImGui::TextDisabled("Not clipping. Move an end of the slider to start.");
  }
}

void global_ui(App &a) {
  if (!prop_filter_match("Global")) return;
  ImGui::SeparatorText("Global");
  std::string err;
  const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.f;
  if (button_grid("Invert", w)) terrain_editor_global(a, "invert", err);
  tip("Inverts every altitude: low becomes high. A node; click again to undo it, or Ctrl+Z.");
  bool ze = false;
  {
    std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::try_to_lock);
    if (lk.owns_lock())
      for (auto &n : a.graph.nodes)
        if (n->type == "TerrainOutput") ze = n->attrs.get_f("zero_edges", 0.f) > 0.f;
  }
  if (button_grid(ze ? "Zero edges: on" : "Zero edges: off", w)) terrain_editor_global(a, "zero_edges", err);
  tip("Lowers the altitudes near the edges so they reach zero at the border\n"
      "(the Terrain Output's own fade). Off when the tile sits on the planet.");
  if (button_grid("Retopologize (smooth all)", w)) terrain_editor_global(a, "smooth_all", err);
  tip("Smooths the entire terrain: a Smooth node in front of the output.");
  if (button_grid("Remove effects", w)) terrain_editor_global(a, "remove_effects", err);
  ImGui::NewLine();
  int res = 0;
  {
    std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::try_to_lock);
    if (lk.owns_lock()) res = a.graph.resolution;
  }
  ImGui::Text("Resolution %d x %d", res, res);
  ImGui::SameLine();
  if (ImGui::SmallButton("Halve")) terrain_editor_global(a, "halve", err);
  ImGui::SameLine();
  if (ImGui::SmallButton("Double")) terrain_editor_global(a, "double", err);
  tip("Vue's halve / double resolution buttons. 512 is detailed; 1024 is for close-ups.");
  if (!err.empty()) ImGui::TextColored(ImVec4(0.85f, 0.3f, 0.2f, 1.f), "%s", err.c_str());
}

void picture_ui(App &a) {
  if (!prop_filter_match("Picture")) return;
  ImGui::SeparatorText("Picture");
  static int mode = 0;
  static float proportion = 1.f;
  ImGui::TextDisabled("Vue's Picture button: mix an image or elevation file\n"
                      "into the terrain. The brighter, the higher.");
  ImGui::TextUnformatted("Mixing mode");
  ImGui::SetNextItemWidth(-1);
  ImGui::Combo("##pmix", &mode, "Blend\0Add\0Subtract\0Multiply\0Min\0Max\0");
  labeled_scalar("Proportions", "pprop", &proportion, 0.f, 1.f);
  if (ImGui::Button("Picture...", ImVec2(-1, 0))) {
    const std::string path = dialog_open_file(
        "Images and elevation\0*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.exr;*.hgt;*.r16;*.raw\0All files\0*.*\0", "png");
    if (!path.empty()) {
      static const char *modes[] = {"blend", "add", "subtract", "multiply", "min", "max"};
      std::string err;
      if (!terrain_editor_import_picture(a, path, modes[mode], proportion, err)) a.status = err;
    }
  }
}

} // namespace

void terrain_editor_ui(App &a, SceneObject &o) {
  (void)o;
  ImGui::Spacing();
  ImGui::SeparatorText("Terrain editor");
  ImGui::TextDisabled("Vue's Terrain Editor, on this tab: every control is a node\n"
                      "in front of the Terrain Output, and a script can do the same.");
  styles_ui(a);
  sculpt_ui(a);
  effects_ui(a);
  clipping_ui(a);
  global_ui(a);
  picture_ui(a);
}

} // namespace studio
