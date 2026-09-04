// Geekatplay TerraForge - Properties: the selected node's attributes.
//
// The widgets are generated from the node's AttrSet, so a node that declares
// a parameter gets an editor for it without a line of UI code. The node is
// mirrored rather than read live: evaluation holds the graph lock for its
// whole run, and a panel that blocks on it blinks out while you are dragging
// a value.
#include "app.hpp"
#include "ai_assist.hpp"
#include "node_library.hpp"
#include "undo.hpp"
#include "gpx/metanode.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <cstring>
#include <imgui.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace studio {

// studio/panel_attr_tips.cpp
const char *attr_tooltip(const std::string &key);


static void show_attr_tooltip(const gpx::Attribute &at) {
  if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
    return;
  const char *tip = !at.tooltip.empty() ? at.tooltip.c_str() : attr_tooltip(at.key);
  if (tip) ImGui::SetTooltip("%s", tip);
}

// Draws the filled progress bar that gives these rows their slider look,
// then leaves the frame transparent so the drag widget renders on top.
static void slider_fill(float value, float mn, float mx) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  float w = ImGui::CalcItemWidth(), h = ImGui::GetFrameHeight();
  float t = (mx - mn) > 1e-9f ? (value - mn) / (mx - mn) : 0.f;
  t = std::clamp(t, 0.f, 1.f);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                    ImGui::GetColorU32(ImGuiCol_FrameBg));
  if (t > 0.f)
    dl->AddRectFilled(p, ImVec2(p.x + w * t, p.y + h),
                      ImGui::GetColorU32(ImVec4(0.55f, 0.33f, 0.13f, 0.85f)));
}

// float row: [-] [slider-look drag: click types, drag slides, wheel steps] [+]
bool scalar_float(const char *id, float *v, float mn, float mx,
                  bool log_scale) {
  bool changed = false;
  float step = (mx - mn) / 200.f;   // wheel/button step: slow, fine control
  ImGui::PushID(id);
  float btn = ImGui::GetFrameHeight();
  if (ImGui::Button("-", ImVec2(btn, btn))) {
    *v = std::max(*v - step, mn);
    changed = true;
  }
  ImGui::SameLine(0, 2);
  ImGui::SetNextItemWidth(-btn - 2);
  slider_fill(*v, mn, mx);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.06f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.10f));
  changed |= ImGui::DragFloat("##v", v, step * 0.5f, mn, mx, "%.3f",
                              (log_scale ? ImGuiSliderFlags_Logarithmic : 0) |
                                  ImGuiSliderFlags_AlwaysClamp);
  ImGui::PopStyleColor(3);
  if (ImGui::IsItemHovered()) {
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // wheel adjusts, not scrolls
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.f) {
      *v = std::clamp(*v + wheel * step, mn, mx);
      changed = true;
    }
  }
  ImGui::SameLine(0, 2);
  if (ImGui::Button("+", ImVec2(btn, btn))) {
    *v = std::min(*v + step, mx);
    changed = true;
  }
  ImGui::PopID();
  return changed;
}

static bool scalar_int(const char *id, int *v, int mn, int mx) {
  bool changed = false;
  int step = std::max(1, (mx - mn) / 200);
  ImGui::PushID(id);
  float btn = ImGui::GetFrameHeight();
  if (ImGui::Button("-", ImVec2(btn, btn))) {
    *v = std::max(*v - step, mn);
    changed = true;
  }
  ImGui::SameLine(0, 2);
  ImGui::SetNextItemWidth(-btn - 2);
  slider_fill((float)*v, (float)mn, (float)mx);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.06f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.10f));
  changed |= ImGui::DragInt("##v", v, 0.25f, mn, mx, "%d",
                            ImGuiSliderFlags_AlwaysClamp);
  ImGui::PopStyleColor(3);
  if (ImGui::IsItemHovered()) {
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.f) {
      *v = std::clamp(*v + (wheel > 0 ? step : -step), mn, mx);
      changed = true;
    }
  }
  ImGui::SameLine(0, 2);
  if (ImGui::Button("+", ImVec2(btn, btn))) {
    *v = std::min(*v + step, mx);
    changed = true;
  }
  ImGui::PopID();
  return changed;
}

// Also used by the Material Editor, which shows one layer's settings
// with the same widgets the Properties editor uses for any node.
bool draw_attribute(gpx::Attribute &at) {
  bool changed = false;
  ImGui::PushID(at.key.c_str());
  const float label_w = 130.f;
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(at.label.c_str());
  show_attr_tooltip(at);
  ImGui::SameLine(label_w);
  ImGui::SetNextItemWidth(-1);
  switch (at.type) {
    case gpx::AttrType::Float:
      changed = scalar_float("f", &at.f, at.fmin, at.fmax, at.log_scale);
      break;
    case gpx::AttrType::Int:
      changed = scalar_int("i", &at.i, at.imin, at.imax);
      break;
    case gpx::AttrType::Bool:
      changed = studio::Checkbox("##v", &at.b);
      break;
    case gpx::AttrType::Choice: {
      std::string items;
      for (auto &l : at.labels) {
        items += l;
        items += '\0';
      }
      changed = ImGui::Combo("##v", &at.i, items.c_str());
    } break;
    case gpx::AttrType::Seed: {
      int s = (int)at.seed;
      ImGui::SetNextItemWidth(-64);
      if (ImGui::InputInt("##v", &s)) {
        at.seed = (uint32_t)std::max(s, 0);
        changed = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("dice", ImVec2(-1, 0))) {
        at.seed = (uint32_t)(ImGui::GetTime() * 120943.0) ^ (at.seed * 2654435761u + 1);
        changed = true;
      }
    } break;
    case gpx::AttrType::Range:
      changed = ImGui::DragFloatRange2("##v", &at.v2[0], &at.v2[1],
                                       (at.v2max - at.v2min) / 300.f, at.v2min,
                                       at.v2max, "%.3f", "%.3f");
      break;
    case gpx::AttrType::Vec2:
      changed = ImGui::DragFloat2("##v", at.v2, (at.v2max - at.v2min) / 300.f,
                                  at.v2min, at.v2max, "%.3f");
      break;
    case gpx::AttrType::Color:
      changed = ImGui::ColorEdit4("##v", at.col, ImGuiColorEditFlags_NoInputs);
      break;
    case gpx::AttrType::Gradient: {
      // gradient strip preview + per-stop editing
      ImDrawList *dl = ImGui::GetWindowDrawList();
      ImVec2 p = ImGui::GetCursorScreenPos();
      float gw = ImGui::GetContentRegionAvail().x, gh = 18.f;
      const int SEGS = 64;
      for (int i = 0; i < SEGS; ++i) {
        float t0 = i / float(SEGS), t1 = (i + 1) / float(SEGS);
        auto eval = [&](float t, float *rgb) {
          rgb[0] = rgb[1] = rgb[2] = t;
          if (at.stops.empty()) return;
          if (t <= at.stops.front().t) {
            rgb[0] = at.stops.front().r; rgb[1] = at.stops.front().g;
            rgb[2] = at.stops.front().b;
            return;
          }
          for (size_t k = 0; k + 1 < at.stops.size(); ++k)
            if (t <= at.stops[k + 1].t) {
              float f = (t - at.stops[k].t) /
                        std::max(at.stops[k + 1].t - at.stops[k].t, 1e-6f);
              rgb[0] = at.stops[k].r + (at.stops[k + 1].r - at.stops[k].r) * f;
              rgb[1] = at.stops[k].g + (at.stops[k + 1].g - at.stops[k].g) * f;
              rgb[2] = at.stops[k].b + (at.stops[k + 1].b - at.stops[k].b) * f;
              return;
            }
          rgb[0] = at.stops.back().r; rgb[1] = at.stops.back().g;
          rgb[2] = at.stops.back().b;
        };
        float rgb[3];
        eval((t0 + t1) * 0.5f, rgb);
        dl->AddRectFilled(ImVec2(p.x + t0 * gw, p.y), ImVec2(p.x + t1 * gw, p.y + gh),
                          IM_COL32((int)(rgb[0] * 255), (int)(rgb[1] * 255),
                                   (int)(rgb[2] * 255), 255));
      }
      ImGui::Dummy(ImVec2(gw, gh + 2));
      for (size_t k = 0; k < at.stops.size(); ++k) {
        ImGui::PushID((int)k);
        float col[4] = {at.stops[k].r, at.stops[k].g, at.stops[k].b, at.stops[k].a};
        ImGui::SetNextItemWidth(60);
        if (ImGui::DragFloat("##t", &at.stops[k].t, 0.005f, 0.f, 1.f, "%.2f"))
          changed = true;
        ImGui::SameLine();
        if (ImGui::ColorEdit4("##c", col, ImGuiColorEditFlags_NoInputs)) {
          at.stops[k].r = col[0]; at.stops[k].g = col[1];
          at.stops[k].b = col[2]; at.stops[k].a = col[3];
          changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x") && at.stops.size() > 2) {
          at.stops.erase(at.stops.begin() + k);
          changed = true;
          ImGui::PopID();
          break;
        }
        ImGui::PopID();
      }
      if (ImGui::SmallButton("+ stop")) {
        gpx::GradientStop s = at.stops.empty() ? gpx::GradientStop{0.5f, 0.5f, 0.5f, 0.5f, 1}
                                               : at.stops.back();
        s.t = std::min(s.t + 0.1f, 1.f);
        at.stops.push_back(s);
        changed = true;
      }
    } break;
    case gpx::AttrType::Filename: {
      char buf[512];
      std::strncpy(buf, at.s.c_str(), sizeof buf - 1);
      buf[sizeof buf - 1] = 0;
      ImGui::SetNextItemWidth(-34);
      if (ImGui::InputText("##v", buf, sizeof buf)) {
        at.s = buf;
        changed = true;
      }
      ImGui::SameLine(0, 2);
      if (ImGui::Button("...", ImVec2(-1, 0))) {
        extern std::string dialog_open_file(const char *, const char *);
        std::string p = dialog_open_file(
            "Images/files\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.raw\0All files\0*.*\0",
            nullptr);
        if (!p.empty()) {
          at.s = p;
          changed = true;
        }
      }
    } break;
    case gpx::AttrType::Text: {
      char buf[512];
      std::strncpy(buf, at.s.c_str(), sizeof buf - 1);
      buf[sizeof buf - 1] = 0;
      if (ImGui::InputText("##v", buf, sizeof buf)) {
        at.s = buf;
        changed = true;
      }
    } break;
  }
  ImGui::PopID();
  return changed;
}


// The node's attributes are mirrored here so the panel keeps drawing (and
// stays editable) while evaluation holds the graph lock. Edits land in the
// mirror and are flushed to the real node as soon as the lock is free —
// that is what stops the panel blinking out while you scroll a value.
struct NodeMirror {
  uint64_t id = 0;
  std::string type, category, error;
  gpx::AttrSet attrs;
  bool enabled = true;
  bool valid = false;
  bool pending = false; // mirror holds edits not yet written to the node
  bool was_active = false;
};
// One mirror per node rather than one for the panel: several node editors
// can each show a different node's parameters in the same frame, and a
// single mirror re-synced by each in turn would drop the other's edits.
static std::map<uint64_t, NodeMirror> g_mirrors;

void node_properties_ui(App &a) { node_properties_ui(a, a.selected_node, false); }

void node_properties_ui(App &a, uint64_t node_id, bool any_workspace) {
  NodeMirror &g_mirror = g_mirrors[node_id];
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (lk.owns_lock()) {
    gpx::Node *live = a.graph.find_node(node_id);
    if (live) {
      if (g_mirror.pending && g_mirror.id == live->id) {
        // write the queued edits through, then re-sync
        for (const auto &src : g_mirror.attrs.items)
          if (gpx::Attribute *dst = live->attrs.find(src.key)) *dst = src;
        g_mirror.pending = false;
        a.graph.mark_dirty(live->id);
        a.request_eval();
      }
      g_mirror.id = live->id;
      g_mirror.type = live->type;
      g_mirror.category = live->category;
      g_mirror.error = live->error;
      g_mirror.enabled = live->enabled;
      g_mirror.attrs = live->attrs;
      g_mirror.valid = true;
    } else {
      g_mirror.valid = false;
      g_mirror.pending = false;
    }
  }
  // Everything below works on the mirror. The unlock has to be conditional:
  // a unique_lock taken with try_to_lock owns nothing while the evaluation
  // worker holds the graph, and unlock() on a lock you do not own throws
  // std::system_error — on the UI thread, in the middle of a frame, which is
  // std::terminate and the "0xc0000409 in ucrtbase" crash on record three
  // times over. Every one was someone looking at a node while it computed.
  if (lk.owns_lock()) lk.unlock();

  if (!g_mirror.valid || g_mirror.id != node_id) {
    ImGui::TextDisabled("No node selected.");
    ImGui::TextDisabled("Click a node in the graph.");
    return;
  }
  NodeMirror *n = &g_mirror;
  // never show a node that belongs to a different workspace — that was the
  // source of "terrain texture showing under Terrain"
  if (!any_workspace && domain_of_category(n->category) != a.workspace) {
    ImGui::TextDisabled("%s belongs to the %s workspace.", n->type.c_str(),
                        workspace_name(domain_of_category(n->category)));
    if (ImGui::Button("Go to that workspace"))
      a.workspace = domain_of_category(n->category);
    ImGui::SameLine();
    if (ImGui::Button("Inspect object instead")) a.prop_tab = TAB_OBJECT;
    return;
  }
  // Bypass sits at the top of every node, the way Terragen puts Enable there:
  // it is the fastest way to answer "what is this node actually doing?"
  {
    bool on = n->enabled;
    if (studio::Checkbox("##enabled", &on)) {
      undo_push(a, on ? "Enable " + n->type : "Bypass " + n->type);
      std::lock_guard<std::mutex> lk(a.graph_mtx);
      if (gpx::Node *live = a.graph.find_node(n->id)) {
        live->enabled = on;
        n->enabled = on;
        a.graph.mark_dirty(live->id);
        a.request_eval();
      }
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Bypass this node.\n"
                        "The graph is evaluated as though it were not there:\n"
                        "whatever reads its output reads its input instead.");
    ImGui::SameLine();
  }
  ImGui::Text("%s", n->type.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("· %s", n->category.c_str());
  if (!n->enabled) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.55f, 0.20f, 1.f));
    ImGui::TextUnformatted("bypassed");
    ImGui::PopStyleColor();
  }
  // A MetaNode's own controls: what it contains, how to take it apart, and how
  // to keep it. Its published parameters render below as ordinary attributes.
  if (n->type == "MetaNode") {
    static char save_name[64] = "";
    static char save_note[160] = "";
    int inner_count = 0;
    if (const gpx::Attribute *ia = n->attrs.find("inner_graph")) {
      // cheap: count the node records without building a graph
      for (size_t p = ia->s.find("\"type\""); p != std::string::npos;
           p = ia->s.find("\"type\"", p + 1))
        ++inner_count;
    }
    ImGui::TextDisabled("%d nodes inside", inner_count);
    if (ImGui::SmallButton("expand")) {
      undo_push(a, "Expand MetaNode");
      std::lock_guard<std::mutex> lk(a.graph_mtx);
      std::string err;
      std::vector<uint64_t> back = gpx::metanode_ungroup(a.graph, n->id, err);
      a.status = back.empty() ? "expand failed: " + err : "expanded";
      a.selected_node = back.empty() ? 0 : back.front();
      a.graph_layout_serial++;
      a.request_eval();
      // (an ImGui::End() used to sit here, inside a child of a window this
      // function never began - a stack imbalance waiting for a MetaNode)
      return;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Put the nodes back into the graph (Ctrl+Shift+G).");
    ImGui::SameLine();
    if (ImGui::SmallButton("save to library...")) ImGui::OpenPopup("save_meta");
    if (ImGui::BeginPopup("save_meta")) {
      ImGui::TextUnformatted("Save this group as a reusable node");
      ImGui::SetNextItemWidth(240);
      ImGui::InputTextWithHint("##nm", "name", save_name, sizeof save_name);
      ImGui::SetNextItemWidth(240);
      ImGui::InputTextWithHint("##nt", "what it is for", save_note,
                               sizeof save_note);
      if (ImGui::Button("Save")) {
        std::string err;
        if (node_library_save(a, n->id, save_name, save_note, err)) {
          a.status = std::string("saved '") + save_name + "' to your nodes";
          save_name[0] = save_note[0] = 0;
          ImGui::CloseCurrentPopup();
        } else {
          a.status = "save failed: " + err;
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Keep this group in the Library, so you can drop it\n"
                        "into any project like a built-in node.");
    ImGui::Separator();
  }
  if (ImGui::SmallButton("view in 3D")) {
    a.view_node = n->id;
    a.uploaded_serial = 0;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("reset")) {
    for (auto &at : n->attrs.items) {
      at.f = at.fdefault;
      at.i = at.idefault;
      at.b = at.bdefault;
      at.v2[0] = at.v2default[0];
      at.v2[1] = at.v2default[1];
    }
    g_mirror.pending = true;
  }
  if (!n->error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.3f, 0.2f, 1.f));
    ImGui::TextWrapped("%s", n->error.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::Separator();

  bool changed = false;
  std::string open_group = "\x01"; // sentinel: not yet in a group
  bool group_open = true;
  bool searching = prop_filter_active();
  for (auto &at : n->attrs.items) {
    if (searching) { // search flattens the groups, like Blender's filter
      if (!prop_filter_match(at.label.c_str()) &&
          !prop_filter_match(at.key.c_str()))
        continue;
      if (draw_attribute(at)) changed = true;
      continue;
    }
    if (at.group != open_group) {
      open_group = at.group;
      if (open_group.empty()) {
        group_open = true;
      } else {
        group_open = ImGui::CollapsingHeader(open_group.c_str(),
                                             ImGuiTreeNodeFlags_DefaultOpen);
      }
    }
    if (!group_open) continue;
    if (draw_attribute(at)) changed = true;
  }
  // silky adjustments: while a control is held, evaluate at low resolution;
  // on release, run one full-quality pass. Edits are queued in the mirror and
  // written through on the next frame that can take the lock.
  bool item_active = ImGui::IsAnyItemActive();
  bool &was_active = g_mirror.was_active;
  if (changed) {
    if (!was_active) undo_push(a, "Edit " + n->type);
    a.eval_interactive.store(item_active);
    g_mirror.pending = true;
  }
  if (was_active && !item_active) {
    a.eval_interactive.store(false);
    g_mirror.pending = true; // force one final full-resolution pass
  }
  was_active = item_active;
}

} // namespace studio
