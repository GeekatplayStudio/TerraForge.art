// Geekatplay TerraForge - the Material Studio: one material, its preview and
// every one of its properties, at the top of the Materials workspace.
//
// This is the shape of Vue's Material Editor (manual p683-694): New / Load /
// Save along the top, the type of material, a preview you can spin and
// re-light, and underneath it the material's own controls. The graph stays
// the truth - every control here edits a node - which is why the node editor
// sits directly below this window in the workspace.
#include "app.hpp"
#include "gpx/serialization.hpp"
#include "material_library.hpp"
#include "material_ui.hpp"
#include "panel_float.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <mutex>

namespace studio {

namespace {

// The "unsaved changes" prompt. Save writes to the library; Discard keeps the
// edits in the graph (they are not lost, only no longer what the studio
// compares against); Cancel stays on the current material.
void save_prompt(App &a, MaterialStudioState &st) {
  if (st.pending_open) ImGui::OpenPopup("Unsaved material");
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("Unsaved material", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::Text("'%s' was changed since it was opened.", st.prompt_name.c_str());
  ImGui::TextDisabled("Save it to the library before switching?");
  ImGui::Spacing();
  auto finish = [&](bool open_next) {
    if (open_next) {
      uint64_t next = st.pending_open;
      st.pending_open = 0;
      st.material = 0; // so open() does not ask again
      material_studio_open(a, next);
    }
    st.pending_open = 0;
    ImGui::CloseCurrentPopup();
  };
  if (ImGui::Button("Save", ImVec2(110, 0))) {
    std::string err;
    std::string path = material_library_save(a, st.material, err);
    a.status = path.empty() ? "SAVE FAILED: " + err : "saved " + path;
    if (!path.empty()) material_studio_mark_saved(a);
    finish(true);
  }
  ImGui::SameLine();
  if (ImGui::Button("Don't save", ImVec2(110, 0))) finish(true);
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(110, 0))) finish(false);
  ImGui::EndPopup();
}

void header(App &a, MaterialStudioState &st, gpx::Node *&mat) {
  std::vector<MatEntry> mats = collect_materials(a);
  std::string label = mat ? mat->attrs.get_s("name") : "(choose a material)";
  ImGui::SetNextItemWidth(220);
  if (ImGui::BeginCombo("##mat", label.c_str())) {
    for (const MatEntry &m : mats)
      if (ImGui::Selectable(m.name.c_str(), mat && m.id == mat->id))
        material_studio_open(a, m.id);
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("New")) {
    undo_push_locked(a, "new material");
    float x = 900, y = 120;
    for (auto &n : a.graph.nodes)
      if (n->type == "MaterialOutput") x = std::max(x, n->pos_x + 260);
    gpx::Node *nn = a.graph.add_node("MaterialOutput", x, y);
    if (nn) {
      if (gpx::Attribute *na = nn->attrs.find("name"))
        na->s = "Material " + std::to_string(mats.size() + 1);
      a.graph_layout_serial++;
      a.request_eval();
      st.material = 0;
      material_studio_open(a, nn->id);
    }
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!mat);
  if (ImGui::Button("Save")) {
    std::string err;
    std::string path = material_library_save(a, mat->id, err);
    a.status = path.empty() ? "SAVE FAILED: " + err : "saved " + path;
    if (!path.empty()) material_studio_mark_saved(a);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Save this material to the library, thumbnail included.");
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Load...")) a.show_material_browser = true;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The Material Browser: project materials and the library.");
  if (mat && material_studio_modified(a)) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.87f, 0.62f, 0.24f, 1.f), "modified");
  }
  if (!mat) return;

  // name and type on the second row
  char buf[128];
  snprintf(buf, sizeof buf, "%s", mat->attrs.get_s("name").c_str());
  ImGui::SetNextItemWidth(220);
  if (ImGui::InputText("##name", buf, sizeof buf))
    if (gpx::Attribute *na = mat->attrs.find("name")) na->s = buf;
  ImGui::SameLine();
  int type = material_type_of(a.graph, mat);
  ImGui::SetNextItemWidth(160);
  if (ImGui::BeginCombo("##type", material_type_name(type))) {
    for (int t = 0; t < MAT_TYPE_COUNT; ++t) {
      if (ImGui::Selectable(material_type_name(t), t == type) && t != type) {
        undo_push_locked(a, std::string("material type: ") + material_type_name(t));
        material_set_type(a, mat, t);
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", material_type_blurb(t));
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", material_type_blurb(type));
}

void preview(App &a, MaterialStudioState &st, gpx::Node *mat, float side) {
  const char *shapes[3] = {"Sphere", "Cube", "Flat"};
  for (int s = 0; s < 3; ++s) {
    bool on = st.shape == s;
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
    if (ImGui::SmallButton(shapes[s])) st.shape = s;
    if (on) ImGui::PopStyleColor();
    ImGui::SameLine();
  }
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  const char *bgs[3] = {"dark", "grey", "light"};
  ImGui::SetNextItemWidth(70);
  ImGui::Combo("##bg", &st.background, bgs, 3);
  ImGui::SameLine();
  studio::Checkbox("spin", &st.turntable);
  if (st.turntable) st.spin += ImGui::GetIO().DeltaTime * 0.5f;

  if (!mat) {
    ImGui::Dummy(ImVec2(side, side));
    return;
  }
  MaterialPreviewSpec spec = material_preview_spec(a, mat);
  unsigned tex =
      renderer_material_preview_of(spec, (int)std::max(side, 64.f), st.shape, st.spin);
  ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(side, side), ImVec2(0, 1),
               ImVec2(1, 0));
  if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f)) {
    st.spin += ImGui::GetIO().MouseDelta.x * 0.01f;
    st.turntable = false;
  }
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
    graph_focus_node(a, mat->id);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Lit by the scene's sun and sky. Drag to turn;\n"
                      "double-click to jump to its node.");
}

void properties(App &a, gpx::Node *mat) {
  // Vue's tabs: Color, Bump, Highlights, Transparency, Reflection,
  // Translucency, Effects, and the type's own tab (panel_material_tabs.cpp)
  material_tabs_ui(a, mat);
}

} // namespace

void draw_panel_material_studio(App &a) {
  if (!a.show_material_studio) return;
  ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_FirstUseEver);
  panel_float_prepare(a, "Material Studio");
  if (!ImGui::Begin("Material Studio", &a.show_material_studio)) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, "Material Studio");
  MaterialStudioState &st = material_studio();
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (!lk.owns_lock()) {
    ImGui::TextDisabled("computing...");
    ImGui::End();
    return;
  }
  gpx::Node *mat = a.graph.find_node(st.material);
  if (mat && mat->type != "MaterialOutput") mat = nullptr;
  // A material that vanished from the graph: fall back to the first one.
  if (!mat) {
    std::vector<MatEntry> mats = collect_materials(a);
    if (!mats.empty()) {
      st.material = mats.front().id;
      st.saved_fingerprint = material_fingerprint(a.graph, st.material);
      mat = a.graph.find_node(st.material);
    }
  }

  header(a, st, mat);
  ImGui::Separator();

  // preview on the left, properties on the right
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float side = std::clamp(std::min(avail.x * 0.42f, avail.y - 34.f), 120.f, 520.f);
  ImGui::BeginChild("##prev", ImVec2(side + 8, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar);
  preview(a, st, mat, side);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##props", ImVec2(0, 0));
  if (mat)
    properties(a, mat);
  else
    ImGui::TextDisabled("No material in the project yet. Press New, or load "
                        "one from the browser below.");
  ImGui::EndChild();

  save_prompt(a, st);
  ImGui::End();
}

} // namespace studio
