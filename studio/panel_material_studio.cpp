// Geekatplay TerraForge - the Material Studio, laid out as Vue's Advanced
// Material Editor (manual p683-694): the preview with its options on the
// left; name, type, mapping and New / Load / Save with the material options
// beside it; the material hierarchy in the middle with the stored previews
// to its right; and under all of it the tabs of whichever hierarchy line is
// selected. The graph stays the truth - every control here edits a node -
// which is why the node editor sits directly below this window.
#include "app.hpp"
#include "gpx/serialization.hpp"
#include "material_channel_ops.hpp"
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
  if (!ImGui::BeginPopupModal("Unsaved material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::Text("'%s' was changed since it was opened.", st.prompt_name.c_str());
  ImGui::TextDisabled("Save it to the library before switching?");
  ImGui::Spacing();
  auto finish = [&](bool open_next) {
    if (open_next) {
      uint64_t next = st.pending_open;
      st.pending_open = 0;
      st.material = 0;
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

// Vue's material options sub-dialog (p688-690), as a popup off one button
void options_popup(App &a, gpx::Node *mat) {
  if (ImGui::SmallButton("Options...")) ImGui::OpenPopup("##matopts");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Shadows, one-sided, hidden from camera or reflections, lighting and "
                      "atmosphere, anti-aliasing.");
  if (ImGui::BeginPopup("##matopts")) {
    ImGui::TextDisabled("Material options");
    for (gpx::Attribute &at : mat->attrs.items)
      if (at.group == "Options") material_attr_widget(a, mat, at.key.c_str(), 10.f);
    ImGui::EndPopup();
  }
}

void header(App &a, MaterialStudioState &st, gpx::Node *&mat) {
  std::vector<MatEntry> mats = collect_materials(a);
  std::string label = mat ? mat->attrs.get_s("name") : "(choose a material)";
  ImGui::SetNextItemWidth(200);
  if (ImGui::BeginCombo("##mat", label.c_str())) {
    for (const MatEntry &m : mats)
      if (ImGui::Selectable(m.name.c_str(), mat && m.id == mat->id)) material_studio_open(a, m.id);
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
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("A clean material to start from.");
  ImGui::SameLine();
  if (ImGui::Button("Load...")) a.show_material_browser = true;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("The Material Browser: project materials, the library, assets.");
  ImGui::SameLine();
  ImGui::BeginDisabled(!mat);
  if (ImGui::Button("Save")) {
    std::string err;
    std::string path = material_library_save(a, mat->id, err);
    a.status = path.empty() ? "SAVE FAILED: " + err : "saved " + path;
    if (!path.empty()) material_studio_mark_saved(a);
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save to the library as a stand-alone file, thumbnail included.");
  ImGui::EndDisabled();
  if (mat && material_studio_modified(a)) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.87f, 0.62f, 0.24f, 1.f), "modified");
  }
  if (!mat) return;
  // name, type, mapping, options on the second row
  char buf[128];
  snprintf(buf, sizeof buf, "%s", mat->attrs.get_s("name").c_str());
  ImGui::SetNextItemWidth(200);
  if (ImGui::InputText("##name", buf, sizeof buf))
    if (gpx::Attribute *na = mat->attrs.find("name")) na->s = buf;
  ImGui::SameLine();
  int type = material_type_of(a.graph, mat);
  ImGui::SetNextItemWidth(150);
  if (ImGui::BeginCombo("##type", material_type_name(type))) {
    for (int t = 0; t < MAT_TYPE_COUNT; ++t) {
      if (ImGui::Selectable(material_type_name(t), t == type) && t != type) {
        undo_push_locked(a, std::string("material type: ") + material_type_name(t));
        material_set_type(a, mat, t);
        st.selected = mat->id;
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", material_type_blurb(t));
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", material_type_blurb(type));
  ImGui::SameLine();
  if (gpx::Attribute *mp = mat->attrs.find("mapping")) {
    std::vector<const char *> items;
    for (const std::string &l : mp->labels) items.push_back(l.c_str());
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("##mapping", &mp->i, items.data(), (int)items.size())) {
      a.graph.mark_dirty(mat->id);
      a.request_eval();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", mp->tooltip.c_str());
  }
  ImGui::SameLine();
  options_popup(a, mat);
}

// the preview and Vue's preview options: object, background, local light,
// randomize, zoom, store
void preview(App &a, MaterialStudioState &st, gpx::Node *mat, float side) {
  const char *shapes[3] = {"Sphere", "Cube", "Plane"};
  ImGui::SetNextItemWidth(78);
  ImGui::Combo("##shape", &st.shape, shapes, 3);
  ImGui::SameLine();
  const char *bgs[3] = {"dark", "grey", "light"};
  ImGui::SetNextItemWidth(60);
  ImGui::Combo("##bg", &st.background, bgs, 3);
  ImGui::SameLine();
  studio::Checkbox("spin", &st.turntable);
  if (st.turntable) st.spin += ImGui::GetIO().DeltaTime * 0.5f;
  if (!mat) {
    ImGui::Dummy(ImVec2(side, side));
    return;
  }
  MaterialPreviewSpec spec = material_preview_spec(a, mat);
  unsigned tex = renderer_material_preview_of(spec, (int)std::max(side, 64.f), st.shape, st.spin);
  ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(side, side), ImVec2(0, 1), ImVec2(1, 0));
  if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f)) {
    st.spin += ImGui::GetIO().MouseDelta.x * 0.01f;
    st.turntable = false;
  }
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) st.show_zoom = true;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Lit by the scene's sun and sky. Drag to turn; double-click to zoom.");
  if (ImGui::SmallButton("Randomize")) {
    undo_push_locked(a, "randomize material");
    int n = material_randomize(a.graph, mat, (uint32_t)ImGui::GetFrameCount());
    a.status = n ? std::to_string(n) + " node(s) reseeded" : "nothing to randomize";
    a.request_eval();
    a.uploaded_serial = 0;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("A random change to every fractal and noise in the material.");
  ImGui::SameLine();
  if (ImGui::SmallButton("Zoom")) st.show_zoom = true;
  ImGui::SameLine();
  if (ImGui::SmallButton("Store")) {
    MaterialStudioState::Snapshot s;
    s.json = gpx::material_to_json(a.graph, mat->id);
    s.name = mat->attrs.get_s("name") + " #" + std::to_string(st.snapshots.size() + 1);
    s.tex = renderer_material_thumbnail(spec, 64, 0);
    st.snapshots.push_back(s);
    a.status = "stored " + s.name;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy the material into a stored preview, to come back to.");
}

// Vue's stored previews beside the hierarchy: double-click restores
void snapshots(App &a, MaterialStudioState &st, gpx::Node *mat, float height) {
  ImGui::BeginChild("##snaps", ImVec2(0, height), ImGuiChildFlags_Borders);
  ImGui::TextDisabled("Stored");
  int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / 72.f));
  for (size_t i = 0; i < st.snapshots.size(); ++i) {
    if (i % cols) ImGui::SameLine();
    ImGui::PushID((int)i);
    MaterialStudioState::Snapshot &s = st.snapshots[i];
    if (s.tex) ImGui::ImageButton("##s", (ImTextureID)(intptr_t)s.tex, ImVec2(56, 56), ImVec2(0, 1), ImVec2(1, 0));
    else ImGui::Button("##s", ImVec2(64, 64));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nDouble-click: restore this version.", s.name.c_str());
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && mat) {
      undo_push_locked(a, "restore material snapshot");
      std::string err;
      if (material_replace_from_json(a.graph, mat->id, s.json, err)) {
        st.selected = mat->id;
        a.graph_layout_serial++;
        a.request_eval();
        a.uploaded_serial = 0;
        a.status = "restored " + s.name;
      } else {
        a.status = "restore failed: " + err;
      }
    }
    if (ImGui::BeginPopupContextItem("##sctx")) {
      if (ImGui::MenuItem("Remove")) {
        st.snapshots.erase(st.snapshots.begin() + (long)i);
        ImGui::EndPopup();
        ImGui::PopID();
        break;
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  if (st.snapshots.empty()) ImGui::TextDisabled("Store keeps versions here.");
  ImGui::EndChild();
}

void zoom_window(App &a, MaterialStudioState &st, gpx::Node *mat) {
  if (!st.show_zoom || !mat) return;
  ImGui::SetNextWindowSize(ImVec2(540, 580), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Material preview", &st.show_zoom)) {
    float side = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - 4);
    MaterialPreviewSpec spec = material_preview_spec(a, mat);
    unsigned tex = renderer_material_preview_of(spec, 512, st.shape, st.spin);
    ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(side, side), ImVec2(0, 1), ImVec2(1, 0));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f)) {
      st.spin += ImGui::GetIO().MouseDelta.x * 0.01f;
      st.turntable = false;
    }
  }
  ImGui::End();
}

} // namespace

void draw_panel_material_studio(App &a) {
  if (!a.show_material_studio) return;
  ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
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
  if (!mat) {
    std::vector<MatEntry> mats = collect_materials(a);
    if (!mats.empty()) {
      st.material = mats.front().id;
      st.saved_fingerprint = material_fingerprint(a.graph, st.material);
      mat = a.graph.find_node(st.material);
    }
  }

  // top: preview on the left, header + hierarchy + stored on the right
  const float top_h = 236.f;
  const float side = 160.f;
  ImGui::BeginChild("##left", ImVec2(side + 44, top_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
  preview(a, st, mat, side);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##mid", ImVec2(-150, top_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
  header(a, st, mat);
  if (mat) material_hierarchy_ui(a, mat, top_h - 96);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##right", ImVec2(0, top_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
  snapshots(a, st, mat, top_h - 4);
  ImGui::EndChild();
  ImGui::Separator();

  // bottom: the tabs of the selected hierarchy line
  ImGui::BeginChild("##tabs", ImVec2(0, 0));
  if (mat) material_tabs_ui(a, mat);
  else
    ImGui::TextDisabled("No material in the project yet. Press New, or load one from the browser below.");
  ImGui::EndChild();

  zoom_window(a, st, mat);
  save_prompt(a, st);
  ImGui::End();
}

} // namespace studio
