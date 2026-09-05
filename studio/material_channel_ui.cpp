// Geekatplay TerraForge - one channel of a material, drawn the way Vue's
// Material Editor draws every channel tab (manual p704-711): a Mode combo
// (None or Constant, Mapped picture, Procedural, and Natural grain for
// colour), and under it whatever that mode needs - the picture with its
// load / rotate / invert / mirror / scale / offset controls, or the function's
// preview and parameters with an Edit function button into the graph, or
// the owner's constant value.
#include "app.hpp"
#include "material_channel_ops.hpp"
#include "material_ui.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <string>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);

namespace {

void touched(App &a, gpx::Node *n) {
  a.graph.mark_dirty(n->id);
  a.request_eval();
  a.uploaded_serial = 0;
}

// the driving node's own controls, by group, so a picture shows its Picture
// group and a fractal its Fractal and Filter groups
void node_groups_ui(App &a, gpx::Node *n, float lw, const char *only_group) {
  std::string last;
  for (gpx::Attribute &at : n->attrs.items) {
    if (only_group && at.group != only_group) continue;
    if (at.type == gpx::AttrType::Filename || at.type == gpx::AttrType::Field) continue;
    if (!only_group && at.group != last) {
      last = at.group;
      if (!last.empty()) ImGui::SeparatorText(last.c_str());
    }
    material_attr_widget(a, n, at.key.c_str(), lw);
  }
}

void picture_ui(App &a, gpx::Node *tf, float lw) {
  std::string path = tf->attrs.get_s("path");
  std::string shown = path.empty() ? "(no picture)" : path.substr(path.find_last_of("/\\") + 1);
  unsigned tex = previews_get(tf->id);
  if (tex) {
    ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(96, 96));
    ImGui::SameLine();
  }
  ImGui::BeginGroup();
  ImGui::TextUnformatted(shown.c_str());
  if (ImGui::IsItemHovered() && !path.empty()) ImGui::SetTooltip("%s", path.c_str());
  if (ImGui::SmallButton("Load...")) {
    std::string p = dialog_open_file("Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr;*.exr\0", "png");
    if (!p.empty()) {
      undo_push_locked(a, "picture file");
      tf->attrs.find("path")->s = p;
      touched(a, tf);
    }
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Rotate")) {
    gpx::Attribute *r = tf->attrs.find("rotate");
    if (r) { r->i = (r->i + 1) % 4; touched(a, tf); }
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Invert")) {
    gpx::Attribute *iv = tf->attrs.find("invert");
    if (iv) { iv->b = !iv->b; touched(a, tf); }
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Edit function")) graph_focus_node(a, tf->id);
  ImGui::EndGroup();
  node_groups_ui(a, tf, lw, "Picture");
}

void procedural_ui(App &a, gpx::Node *src, float lw) {
  unsigned tex = previews_get(src->id);
  if (tex) {
    ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(96, 96));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The function's output. Double-click to open it in the graph.");
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) graph_focus_node(a, src->id);
    ImGui::SameLine();
  }
  ImGui::BeginGroup();
  ImGui::Text("%s #%llu", src->type.c_str(), (unsigned long long)src->id);
  if (ImGui::SmallButton("Edit function")) graph_focus_node(a, src->id);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select the node and show it in the node graph below.");
  ImGui::EndGroup();
  node_groups_ui(a, src, lw, nullptr);
}

} // namespace

void material_attr_widget(App &a, gpx::Node *n, const char *key, float label_w) {
  gpx::Attribute *at = n->attrs.find(key);
  if (!at) return;
  ImGui::PushID(key);
  bool changed = false;
  ImGui::SetNextItemWidth(-label_w);
  switch (at->type) {
  case gpx::AttrType::Float:
    changed = ImGui::SliderFloat(at->label.c_str(), &at->f, at->fmin, at->fmax, "%.3f",
                                 at->log_scale ? ImGuiSliderFlags_Logarithmic : 0);
    break;
  case gpx::AttrType::Int:
    changed = ImGui::SliderInt(at->label.c_str(), &at->i, at->imin, at->imax);
    break;
  case gpx::AttrType::Bool:
    changed = studio::Checkbox(at->label.c_str(), &at->b);
    break;
  case gpx::AttrType::Color:
    changed = ImGui::ColorEdit3(at->label.c_str(), at->col, ImGuiColorEditFlags_NoInputs);
    break;
  case gpx::AttrType::Choice: {
    std::vector<const char *> items;
    for (const std::string &s : at->labels) items.push_back(s.c_str());
    changed = ImGui::Combo(at->label.c_str(), &at->i, items.data(), (int)items.size());
    break;
  }
  case gpx::AttrType::Vec2:
    changed = ImGui::DragFloat2(at->label.c_str(), at->v2, 0.01f, at->v2min, at->v2max);
    break;
  case gpx::AttrType::Range:
    changed = ImGui::DragFloatRange2(at->label.c_str(), &at->v2[0], &at->v2[1],
                                     (at->v2max - at->v2min) / 300.f, at->v2min, at->v2max);
    break;
  case gpx::AttrType::Seed: {
    int s = (int)(at->seed & 0x7fffffff);
    if (ImGui::InputInt(at->label.c_str(), &s)) { at->seed = (uint32_t)std::max(s, 0); changed = true; }
    break;
  }
  case gpx::AttrType::Text: {
    char buf[256];
    snprintf(buf, sizeof buf, "%s", at->s.c_str());
    if (ImGui::InputText(at->label.c_str(), buf, sizeof buf)) { at->s = buf; changed = true; }
    break;
  }
  default:
    break;
  }
  if (ImGui::IsItemHovered() && !at->tooltip.empty()) ImGui::SetTooltip("%s", at->tooltip.c_str());
  if (changed) touched(a, n);
  ImGui::PopID();
}

bool material_channel_ui(App &a, gpx::Node *owner, const char *port, const char *human,
                         int kind, const char *constant_key) {
  bool changed = false;
  ImGui::PushID(port);
  gpx::Node *src = channel_source(a.graph, owner, port);
  int mode = channel_mode_of(a.graph, owner, port);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(human);
  ImGui::SameLine(160);
  ImGui::SetNextItemWidth(200);
  const char *none_label = (kind == CHAN_COLOR || kind == CHAN_VALUE) ? "Constant" : "None";
  const char *shown = mode == CH_NONE ? none_label : channel_mode_name(mode);
  if (ImGui::BeginCombo("##mode", shown)) {
    if (ImGui::Selectable(none_label, mode == CH_NONE) && mode != CH_NONE) {
      undo_push_locked(a, std::string(human) + ": none");
      channel_set_mode(a.graph, owner, port, CH_NONE);
      changed = true;
    }
    if (ImGui::Selectable("Mapped picture...", mode == CH_PICTURE)) {
      std::string p = dialog_open_file("Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr;*.exr\0", "png");
      if (!p.empty()) {
        undo_push_locked(a, std::string(human) + ": picture");
        channel_set_mode(a.graph, owner, port, CH_PICTURE, p);
        changed = true;
      }
    }
    if (ImGui::Selectable("Procedural", mode == CH_PROCEDURAL) && mode != CH_PROCEDURAL) {
      undo_push_locked(a, std::string(human) + ": procedural");
      channel_set_mode(a.graph, owner, port, CH_PROCEDURAL);
      changed = true;
    }
    if (kind == CHAN_COLOR && ImGui::Selectable("Natural grain", mode == CH_GRAIN) && mode != CH_GRAIN) {
      undo_push_locked(a, std::string(human) + ": natural grain");
      channel_set_mode(a.graph, owner, port, CH_GRAIN);
      changed = true;
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Constant / None: a single value, or nothing.\nMapped picture: an "
                      "image file.\nProcedural: a function - a fractal, a noise, any node.\n"
                      "Natural grain: two colours through a noise, for ground and rock.");
  if (changed) {
    a.graph_layout_serial++;
    touched(a, owner);
    src = channel_source(a.graph, owner, port);
    mode = channel_mode_of(a.graph, owner, port);
  }
  const float lw = 170.f;
  ImGui::Indent(12.f);
  if (mode == CH_NONE) {
    if (constant_key) material_attr_widget(a, owner, constant_key, lw);
  } else if (mode == CH_PICTURE && src) {
    picture_ui(a, src, lw);
  } else if ((mode == CH_PROCEDURAL || mode == CH_GRAIN) && src) {
    procedural_ui(a, src, lw);
  } else if (src) {
    ImGui::TextDisabled("fed by %s #%llu", src->type.c_str(), (unsigned long long)src->id);
    ImGui::SameLine();
    if (ImGui::SmallButton("Edit")) graph_focus_node(a, src->id);
  }
  ImGui::Unindent(12.f);
  ImGui::PopID();
  return changed;
}

} // namespace studio
