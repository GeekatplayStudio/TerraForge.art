// Geekatplay TerraForge - the Material Studio, laid out as Vue's Advanced
// Material Editor (manual p683-694): the preview with its options on the
// left; name, type, mapping and New / Load / Save with the material options
// beside it; the material hierarchy in the middle with the stored previews
// to its right; and under all of it the tabs of whichever hierarchy line is
// selected. The graph stays the truth - every control here edits a node -
// which is why the node editor sits directly below this window.
#include "app.hpp"
#include "console.hpp"
#include "graph_lease.hpp"
#include "icons.hpp"
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
    std::string path = material_library_save_locked(a, st.material, err);
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
  // the pickers share the row with the buttons: they take what the buttons
  // leave, never the other way round
  const float row_w = ImGui::GetContentRegionAvail().x;
  const float buttons_w = ImGui::CalcTextSize("New").x + ImGui::CalcTextSize("Load...").x +
                          ImGui::CalcTextSize("Save").x + ImGui::CalcTextSize("modified").x +
                          ImGui::GetStyle().FramePadding.x * 6.f + ImGui::GetStyle().ItemSpacing.x * 4.f;
  ImGui::SetNextItemWidth(std::clamp(row_w - buttons_w, 90.f, 260.f));
  if (ImGui::BeginCombo("##mat", label.c_str())) {
    for (const MatEntry &m : mats)
      if (ImGui::Selectable(m.name.c_str(), mat && m.id == mat->id)) material_studio_open(a, m.id);
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  // Follow the Objects tree, or hold this material while clicking elsewhere.
  // A locked studio has to say what it is holding, or it just looks broken.
  {
    const float bw = ImGui::GetFrameHeight();
    const std::string who = st.showing.empty() ? std::string("no object") : st.showing;
    const std::string tip =
        st.locked ? "Locked to " + who +
                        ".\nThe studio stays on this material while you select "
                        "other objects.\nClick to follow the selection again."
                  : "Following the Objects tree" +
                        (st.showing.empty() ? std::string()
                                            : " - showing " + who) +
                        ".\nClick to lock the studio to this material.";
    if (IconButton(st.locked ? Icon::Lock : Icon::Unlock, "##matlock", tip.c_str(),
                   st.locked, bw))
      st.locked = !st.locked;
    ImGui::SameLine();
    // Whose material this is, always - the label is how you know the studio is
    // pointed where you think it is.
    if (!st.showing.empty()) {
      ImGui::AlignTextToFramePadding();
      if (st.locked)
        ImGui::TextColored(ImVec4(0.85f, 0.55f, 0.20f, 1.f), "%s", who.c_str());
      else
        ImGui::TextDisabled("%s", who.c_str());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip.c_str());
      ImGui::SameLine();
    }
  }
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
    std::string path = material_library_save_locked(a, mat->id, err);
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
  // name : type : mapping : options, sharing the row by weight
  const float row2 = ImGui::GetContentRegionAvail().x;
  const float opt_w = ImGui::CalcTextSize("Options...").x + ImGui::GetStyle().FramePadding.x * 2.f;
  const float free_w = std::max(row2 - opt_w - ImGui::GetStyle().ItemSpacing.x * 3.f, 180.f);
  ImGui::SetNextItemWidth(std::clamp(free_w * 0.42f, 80.f, 240.f));
  if (ImGui::InputText("##name", buf, sizeof buf))
    if (gpx::Attribute *na = mat->attrs.find("name")) na->s = buf;
  ImGui::SameLine();
  int type = material_type_of(a.graph, mat);
  ImGui::SetNextItemWidth(std::clamp(free_w * 0.32f, 70.f, 170.f));
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
    ImGui::SetNextItemWidth(std::clamp(free_w * 0.26f, 60.f, 140.f));
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
  // the preview's own controls fit the preview's width: two combos sharing
  // it, the spin switch under them. Nothing here is wider than the picture.
  const char *shapes[3] = {"Sphere", "Cube", "Plane"};
  const float half = (side - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  ImGui::SetNextItemWidth(half);
  ImGui::Combo("##shape", &st.shape, shapes, 3);
  ImGui::SameLine();
  const char *bgs[3] = {"dark", "grey", "light"};
  ImGui::SetNextItemWidth(half);
  ImGui::Combo("##bg", &st.background, bgs, 3);
  studio::Checkbox("spin", &st.turntable);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turntable: the preview keeps turning.");
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
  // Store goes beside Zoom when the column is wide enough, under it when not
  if (ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize("Store").x + 12.f) ImGui::SameLine();
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
  if (st.snapshots.empty()) ImGui::TextWrapped("Store keeps versions here.");
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
  GraphLease lk(a);
  if (!lk.owns_lock()) {
    // Counted, because this frame is the flicker: the panel's body is gone
    // for it. The lease waits through a drag now, so this should be rare;
    // if the log fills with it, the wait is too short for the evaluation.
    log_trace("studio", "material studio skipped a frame (graph busy)");
    ImGui::TextDisabled("computing...");
    ImGui::End();
    return;
  }
  // Selecting an object in the Objects tree opens its material here, unless
  // the studio is locked. Before find_node, so the header draws the material
  // that was just chosen rather than the previous one.
  material_studio_follow_selection(a);
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

  // The preview down the whole left side; everything else in a column to its
  // right - the header and the hierarchy in a band across the top, the stored
  // previews at the band's right end, and the tabs beneath, which are the
  // only thing that scrolls. The preview used to sit in the top band only, so
  // with the tabs scrolled to a slider it was off the top of the window: you
  // changed a value and could not see what it did. Now it is always beside the
  // control being moved. Every measure follows the window and the font; the
  // band's height can be dragged; nothing is drawn where it cannot fit.
  const ImGuiStyle &sty = ImGui::GetStyle();
  const float fh = ImGui::GetFrameHeightWithSpacing();
  const float avail_w = ImGui::GetContentRegionAvail().x;
  const float side = std::clamp(avail_w * 0.18f, 120.f, 220.f);
  const float left_w = side + sty.WindowPadding.x * 2.f;
  const float preview_h = side + fh * 2.f + ImGui::GetFrameHeight() + sty.WindowPadding.y * 2.f + 4.f;
  static float band_extra = 0.f; // the user's drag on the splitter
  const float top_h = std::max(preview_h, fh * 5.f + 60.f) + band_extra;
  const float right_w = std::clamp(avail_w * 0.14f, 90.f, 180.f);

  ImGui::BeginChild("##left", ImVec2(left_w, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar);
  preview(a, st, mat, side);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##column", ImVec2(0, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::BeginChild("##mid", ImVec2(-right_w, top_h), ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar);
  header(a, st, mat);
  // two header rows above, one row of layer buttons below, and the air
  const float hier_h = top_h - fh * 2.f - ImGui::GetFrameHeight() - sty.WindowPadding.y * 2.f -
                       sty.ItemSpacing.y * 2.f - 4.f;
  if (mat) material_hierarchy_ui(a, mat, std::max(hier_h, fh * 2.f));
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##right", ImVec2(0, top_h), ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar);
  snapshots(a, st, mat, top_h - sty.WindowPadding.y * 2.f);
  ImGui::EndChild();

  // the splitter between the band and the tabs: drag to give either more room
  {
    ImGui::InvisibleButton("##band_split", ImVec2(-1.f, 6.f));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive()) band_extra = std::max(-60.f, band_extra + ImGui::GetIO().MouseDelta.y);
    ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p0.x, (p0.y + p1.y) * 0.5f), ImVec2(p1.x, (p0.y + p1.y) * 0.5f),
                                        ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered
                                                                                  : ImGuiCol_Separator));
  }

  // bottom: the tabs of the selected hierarchy line - the scrolling part
  ImGui::BeginChild("##tabs", ImVec2(0, 0));
  if (mat) material_tabs_ui(a, mat);
  else
    ImGui::TextDisabled("No material in the project yet. Press New, or load one from the browser below.");
  ImGui::EndChild();
  ImGui::EndChild(); // ##column

  zoom_window(a, st, mat);
  save_prompt(a, st);
  ImGui::End();
}

} // namespace studio
