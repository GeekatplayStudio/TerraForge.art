#include "sculpt.hpp"
#include "app.hpp"
#include "undo.hpp"
#include "gpx/noise_core.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace studio {

SculptState &sculpt_state() {
  static SculptState s;
  return s;
}

// ---------------------------------------------------------------- target node
unsigned long long sculpt_target_node(App &a) {
  // an existing sculpt layer is reused; strokes accumulate into it
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainSculpt") return n->id;
  if (a.graph.nodes.empty()) return 0;

  // Create one and splice it into the terrain chain. If a TerrainOutput
  // exists, the sculpt goes right before its heightmap input; otherwise it
  // hangs off whatever node currently is the terrain.
  gpx::Node *out_node = nullptr;
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainOutput") out_node = n.get();

  gpx::Node *feed = nullptr; // node whose output the sculpt should receive
  std::string feed_port = "output";
  if (out_node) {
    for (const gpx::Link &l : a.graph.links)
      if (l.to_node == out_node->id && l.to_port == "heightmap") {
        feed = a.graph.find_node(l.from_node);
        feed_port = l.from_port;
      }
  }
  if (!feed) {
    uint64_t view = a.view_node ? a.view_node : a.selected_node;
    feed = a.graph.find_node(view);
    if (feed && !feed->first_out(gpx::DataType::Heightmap)) feed = nullptr;
    if (!feed)
      for (auto &cand : a.graph.nodes)
        if (cand->first_out(gpx::DataType::Heightmap) &&
            cand->type != "TerrainOutput")
          feed = cand.get();
    if (feed) feed_port = feed->first_out(gpx::DataType::Heightmap)->name;
  }
  if (!feed) return 0;

  gpx::Node *sc = a.graph.add_node("TerrainSculpt", feed->pos_x + 190,
                                   feed->pos_y + 40);
  if (!sc) return 0;
  a.graph.add_link(feed->id, feed_port, sc->id, "input");
  if (out_node) {
    // re-route the output to run through the sculpt layer
    for (const gpx::Link &l : a.graph.links)
      if (l.to_node == out_node->id && l.to_port == "heightmap" &&
          l.from_node == feed->id) {
        a.graph.remove_link(l.id);
        break;
      }
    a.graph.add_link(sc->id, "output", out_node->id, "heightmap");
  }
  a.graph_layout_serial++;
  a.status = "sculpt layer added to the graph";
  return sc->id;
}

// ------------------------------------------------------------------- brushing
// The brush profile: 1 at the center falling to 0 at the rim, with `falloff`
// shaping how hard the edge is.
static inline float brush_w(float d_norm, float falloff) {
  float t = std::clamp(1.f - d_norm, 0.f, 1.f);
  return std::pow(t * t * (3.f - 2.f * t), 1.f / std::max(falloff, 0.25f));
}

bool sculpt_apply(App &a, float tx, float tz, float dt) {
  SculptState &S = sculpt_state();
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (!lk.owns_lock()) return false; // busy frame: skip, the stroke continues

  if (!S.stroking) {
    // one undo step per stroke, named for the tool
    static const char *names[] = {"Sculpt raise", "Sculpt flatten",
                                  "Sculpt smooth", "Sculpt terrace",
                                  "Sculpt noise",  "Sculpt erase"};
    undo_push_locked(a, names[(int)S.tool]);
    S.stroking = true;
    S.have_target = false;
  }

  uint64_t id = sculpt_target_node(a);
  gpx::Node *n = a.graph.find_node(id);
  if (!n) return false;
  gpx::Attribute *fa = n->attrs.find("delta");
  if (!fa || fa->fw <= 0 || fa->fh <= 0) return false;
  if (fa->field.empty()) fa->field.assign((size_t)fa->fw * fa->fh, 0.f);

  // the terrain shape under the brush, for tools that reference it
  const gpx::Heightmap *in = n->in_hmap("input");
  float in_mn = 0.f, in_amp = 1.f;
  if (in && !in->empty()) {
    float mx;
    in->minmax(in_mn, mx);
    in_amp = (mx - in_mn) > 1e-9f ? (mx - in_mn) : 1.f;
  }
  float strength = std::max(n->attrs.get_f("strength", 1.f), 0.05f);
  // convert: field units are fractions of the input amplitude
  auto surface01 = [&](float u, float v) -> float {
    float base = (in && !in->empty()) ? (in->sample(u, v) - in_mn) / in_amp : 0.f;
    float fx = std::clamp(u * (fa->fw - 1), 0.f, (float)(fa->fw - 1));
    float fy = std::clamp(v * (fa->fh - 1), 0.f, (float)(fa->fh - 1));
    int x0 = (int)fx, y0 = (int)fy;
    float d = fa->field[(size_t)y0 * fa->fw + x0];
    return base + d * strength;
  };

  if ((S.tool == SculptTool::Flatten) && !S.have_target) {
    S.flatten_target = surface01(tx, tz);
    S.have_target = true;
  }

  int fw = fa->fw, fh = fa->fh;
  int cx = (int)(tx * (fw - 1)), cy = (int)(tz * (fh - 1));
  int r = std::max(1, (int)(S.radius * fw));
  float amount = S.flow * std::min(dt, 0.05f) * 4.f;
  bool inv = S.invert;
  float lo = fa->fmin, hi = fa->fmax;

  int x0 = std::max(0, cx - r), x1 = std::min(fw - 1, cx + r);
  int y0 = std::max(0, cy - r), y1 = std::min(fh - 1, cy + r);

  // Smooth needs the neighborhood before this frame's writes
  std::vector<float> before;
  if (S.tool == SculptTool::Smooth) before = fa->field;

  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      float dxp = (x - cx) / (float)r, dyp = (y - cy) / (float)r;
      float dist = std::sqrt(dxp * dxp + dyp * dyp);
      if (dist > 1.f) continue;
      float w = brush_w(dist, S.falloff) * amount;
      if (w <= 0.f) continue;
      size_t i = (size_t)y * fw + x;
      float &d = fa->field[i];
      float u = x / (float)(fw - 1), v = y / (float)(fh - 1);
      switch (S.tool) {
        case SculptTool::Raise:
          d += (inv ? -0.35f : 0.35f) * w;
          break;
        case SculptTool::Flatten: {
          float cur = surface01(u, v);
          d += (S.flatten_target - cur) * w / strength;
        } break;
        case SculptTool::Smooth: {
          // relax the sculpted layer toward its neighborhood average, and pull
          // the combined surface toward its local blur
          float acc = 0;
          int cnt = 0;
          for (int oy = -2; oy <= 2; ++oy)
            for (int ox = -2; ox <= 2; ++ox) {
              int sx = std::clamp(x + ox, 0, fw - 1);
              int sy = std::clamp(y + oy, 0, fh - 1);
              acc += before[(size_t)sy * fw + sx];
              ++cnt;
            }
          float avg = acc / cnt;
          float surf_avg = 0;
          if (in && !in->empty()) {
            float e = 2.f / fw;
            surf_avg = ((in->sample(u - e, v) + in->sample(u + e, v) +
                         in->sample(u, v - e) + in->sample(u, v + e)) *
                            0.25f -
                        in_mn) / in_amp;
            float here = (in->sample(u, v) - in_mn) / in_amp;
            avg += (surf_avg - here) / strength * 0.5f;
          }
          d += (avg - d) * std::min(w * 2.f, 1.f);
        } break;
        case SculptTool::Terrace: {
          float steps = 12.f;
          float cur = surface01(u, v);
          float q = std::round(cur * steps) / steps;
          d += (q - cur) * w / strength;
        } break;
        case SculptTool::Noise: {
          gpx::noise::FbmParams fp;
          fp.octaves = 5;
          float nse = gpx::noise::fbm(u * 24.f, v * 24.f, 1337u, fp) - 0.5f;
          d += nse * (inv ? -0.8f : 0.8f) * w;
        } break;
        case SculptTool::Erase:
          d *= 1.f - std::min(w * 2.f, 1.f);
          break;
      }
      d = std::clamp(d, lo, hi);
    }

  a.graph.mark_dirty(n->id);
  a.eval_interactive.store(true);
  a.request_eval();
  return true;
}

void sculpt_end_stroke(App &a) {
  SculptState &S = sculpt_state();
  if (!S.stroking) return;
  S.stroking = false;
  S.have_target = false;
  a.eval_interactive.store(false);
  a.request_eval(); // one full-resolution pass now that the stroke is done
}

// ------------------------------------------------------------------- toolbar
void sculpt_toolbar(App &a) {
  SculptState &S = sculpt_state();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 3));
  bool on = S.active;
  if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
  if (ImGui::SmallButton("sculpt")) {
    S.active = !S.active;
    if (S.active) {
      // make sure the layer exists so the first stroke lands instantly
      std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
      if (lk.owns_lock() && !a.graph.nodes.empty()) {
        bool had = false;
        for (auto &n : a.graph.nodes)
          if (n->type == "TerrainSculpt") had = true;
        if (!had) undo_push_locked(a, "Add sculpt layer");
        sculpt_target_node(a);
        a.request_eval();
      }
    }
  }
  if (on) ImGui::PopStyleColor();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Sculpt the terrain with brushes.\n"
                      "Strokes live in a TerrainSculpt node in the graph, so\n"
                      "the procedural terrain underneath stays editable.");
  if (S.active) {
    ImGui::SameLine();
    static const char *tools[] = {"raise", "flatten", "smooth",
                                  "terrace", "noise", "erase"};
    static const char *tips[] = {
        "Raise the ground (hold Alt or tick invert to dig).",
        "Pull the surface toward the height under your first click.",
        "Relax bumps and stroke marks.",
        "Cut the slope under the brush into steps.",
        "Stamp fractal detail (Alt inverts).",
        "Remove sculpted strokes, revealing the procedural terrain."};
    for (int i = 0; i < 6; ++i) {
      ImGui::SameLine(0, 2);
      bool sel = (int)S.tool == i;
      if (sel)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.40f, 0.26f, 0.12f, 1.f));
      if (ImGui::SmallButton(tools[i])) S.tool = (SculptTool)i;
      if (sel) ImGui::PopStyleColor();
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tips[i]);
    }
    ImGui::SameLine(0, 8);
    ImGui::SetNextItemWidth(90);
    ImGui::SliderFloat("##radius", &S.radius, 0.01f, 0.3f, "radius %.2f");
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(90);
    ImGui::SliderFloat("##flow", &S.flow, 0.05f, 2.f, "flow %.2f");
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(90);
    ImGui::SliderFloat("##falloff", &S.falloff, 0.25f, 6.f, "falloff %.1f");
    ImGui::SameLine(0, 6);
    studio::Checkbox("invert", &S.invert);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Reverses the brush (raise digs...). Holding Alt\n"
                        "while brushing does the same.");
  }
  ImGui::PopStyleVar();
}

} // namespace studio
