// Geekatplay TerraForge — the Timeline: scrub, play, and keyframe. The
// engine's Track machinery (every attribute can carry keys, Graph::time
// drives them through apply_animation) has existed since the animation
// hooks landed; this panel is the hands on it. Playback advances the
// graph clock and re-requests evaluation; the async eval coalesces the
// requests, so scrubbing a heavy graph degrades to lower frame rates
// rather than freezing anything.
#include "app.hpp"
#include "render_settings.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace studio {

void draw_panel_timeline(App &a) {
  if (!a.show_timeline) return;
  if (!ImGui::Begin("Timeline", &a.show_timeline)) {
    ImGui::End();
    return;
  }

  // ---- transport -----------------------------------------------------------
  bool at_start = a.graph.time <= a.anim_start + 1e-6f;
  if (ImGui::Button("|<")) {
    a.graph.time = a.anim_start;
    a.request_eval();
  }
  ImGui::SameLine();
  if (ImGui::Button(a.anim_playing ? "||" : ">")) a.anim_playing = !a.anim_playing;
  ImGui::SameLine();
  if (ImGui::Button("[]")) {
    a.anim_playing = false;
    a.graph.time = a.anim_start;
    a.request_eval();
  }
  (void)at_start;
  ImGui::SameLine();
  ImGui::Text("%6.2fs", a.graph.time);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  ImGui::DragFloat("##t0", &a.anim_start, 0.1f, 0.f, 3600.f, "%.1fs");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  ImGui::DragFloat("##t1", &a.anim_end, 0.1f, a.anim_start + 0.1f, 3600.f,
                   "%.1fs");
  ImGui::SameLine();
  ImGui::Checkbox("Loop", &a.anim_loop);

  if (a.anim_playing) {
    a.graph.time += ImGui::GetIO().DeltaTime;
    if (a.graph.time >= a.anim_end) {
      if (a.anim_loop) a.graph.time = a.anim_start;
      else {
        a.graph.time = a.anim_end;
        a.anim_playing = false;
      }
    }
    a.request_eval();
  }

  ImGui::SameLine();
  if (a.seq_active) {
    ImGui::TextDisabled("capturing %d/%d", a.seq_frame, a.seq_total);
  } else if (ImGui::SmallButton("Render PNG sequence")) {
    a.seq_dir = "sequence";
    std::error_code ec;
    std::filesystem::create_directories(a.seq_dir, ec);
    a.seq_fps = 30.f;
    a.seq_total =
        (int)std::max((a.anim_end - a.anim_start) * a.seq_fps + 0.5f, 1.f);
    a.seq_frame = 0;
    a.anim_playing = false;
    a.graph.time = a.anim_start;
    a.seq_active = true;
    a.request_eval();
  }
  if (ImGui::IsItemHovered() && !a.seq_active)
    ImGui::SetTooltip("Viewport-engine capture of the loop range at 30 fps,\n"
                      "one PNG per frame, into ./sequence next to the app.");

  // ---- the scrub bar -------------------------------------------------------
  float t = a.graph.time;
  ImGui::SetNextItemWidth(-1);
  if (ImGui::SliderFloat("##scrub", &t, a.anim_start, a.anim_end, "%.2fs")) {
    a.graph.time = t;
    a.request_eval();
  }

  // ---- tracks of the selected node ----------------------------------------
  gpx::Node *n = a.graph.find_node(a.selected_node);
  if (!n) {
    ImGui::TextDisabled("Select a node to keyframe its parameters.");
    ImGui::End();
    return;
  }
  ImGui::SeparatorText(n->type.c_str());
  int shown = 0;
  for (gpx::Attribute &at : n->attrs.items) {
    if (at.type != gpx::AttrType::Float && at.type != gpx::AttrType::Int)
      continue;
    ImGui::PushID(at.key.c_str());
    bool keyed_here = at.anim.has_key_at(a.graph.time);
    // the key button: filled when a key sits at the playhead
    if (ImGui::SmallButton(keyed_here ? "[K]" : " K ")) {
      float v = at.type == gpx::AttrType::Float ? at.f : (float)at.i;
      if (keyed_here) at.anim.remove_key(a.graph.time);
      else at.anim.set_key(a.graph.time, v);
      n->dirty = true;
      a.request_eval();
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(keyed_here ? "Remove the key at the playhead"
                                   : "Key this value at the playhead");
    ImGui::SameLine();
    if (at.anim.empty()) {
      ImGui::TextDisabled("%s", at.label.c_str());
    } else {
      ImGui::Text("%s", at.label.c_str());
      ImGui::SameLine();
      ImGui::TextDisabled("(%d keys)", (int)at.anim.keys.size());
      ImGui::SameLine();
      int interp = (int)at.anim.interp;
      ImGui::SetNextItemWidth(90);
      if (ImGui::Combo("##interp", &interp, "Constant\0Linear\0Smooth\0")) {
        at.anim.interp = (gpx::Interp)interp;
        n->dirty = true;
        a.request_eval();
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("clear")) {
        at.anim.keys.clear();
        n->dirty = true;
        a.request_eval();
      }
      // key markers along a miniature track bar
      ImVec2 p0 = ImGui::GetCursorScreenPos();
      float bar_w = ImGui::GetContentRegionAvail().x;
      float bar_h = 8.f;
      ImDrawList *dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p0, ImVec2(p0.x + bar_w, p0.y + bar_h),
                        IM_COL32(45, 45, 48, 255), 2.f);
      float span = std::max(a.anim_end - a.anim_start, 1e-3f);
      for (const gpx::Key &k : at.anim.keys) {
        float fx = (k.time - a.anim_start) / span;
        if (fx < 0.f || fx > 1.f) continue;
        float mx = p0.x + fx * bar_w;
        dl->AddRectFilled(ImVec2(mx - 2, p0.y), ImVec2(mx + 2, p0.y + bar_h),
                          IM_COL32(0xc8, 0x78, 0x30, 255), 1.f);
      }
      float px = p0.x + (a.graph.time - a.anim_start) / span * bar_w;
      dl->AddLine(ImVec2(px, p0.y - 2), ImVec2(px, p0.y + bar_h + 2),
                  IM_COL32(230, 230, 230, 200), 1.f);
      ImGui::Dummy(ImVec2(bar_w, bar_h + 2));
    }
    ImGui::PopID();
    ++shown;
  }
  if (!shown)
    ImGui::TextDisabled("This node has no keyable number parameters.");
  ImGui::End();
}

} // namespace studio
