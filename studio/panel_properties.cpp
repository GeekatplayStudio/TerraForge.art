// Geekatplay Studio â€” properties panel: auto-generated UI from attributes
#include "app.hpp"
#include "ai_assist.hpp"
#include "undo.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <imgui.h>
#include <cstring>
#include <map>
#include <string>

namespace studio {

// hover explanations for common attribute keys (per-node tooltips override)
static const char *attr_tooltip(const std::string &key) {
  static const std::map<std::string, const char *> tips = {
      {"seed", "Random seed: same seed reproduces the exact same result.\nUse the dice to try variations."},
      {"octaves", "Number of noise layers. More octaves = finer detail,\nslower compute."},
      {"lacunarity", "Frequency multiplier between octaves. 2.0 is standard;\nhigher packs detail tighter."},
      {"gain", "Amplitude falloff per octave. Lower = smoother,\nhigher = rougher surfaces."},
      {"kw", "Wavenumber: how many noise features fit across the terrain\nin X and Y."},
      {"offset", "Shifts the noise pattern across the terrain."},
      {"angle", "Rotation or direction in degrees."},
      {"type", "Algorithm variant. Each option changes which parameters matter."},
      {"method", "Simulation method. Droplets are fast and detailed;\nshallow water is more physical."},
      {"particles", "How many erosion droplets to simulate (thousands).\nMore = deeper carving, slower."},
      {"lifetime", "Steps each droplet lives. Longer = longer valleys."},
      {"inertia", "How much droplets keep their direction. Low = follows\nterrain tightly, high = straighter paths."},
      {"capacity", "How much sediment a droplet can carry. Higher = deeper cuts."},
      {"erode_rate", "Fraction of free capacity eroded per step."},
      {"deposit_rate", "Fraction of excess sediment dropped per step."},
      {"evaporation", "Water loss per step; ends droplet life sooner."},
      {"gravity", "Acceleration downhill; affects droplet speed and carving."},
      {"brush", "Radius of terrain affected by each erosion step."},
      {"iterations", "Simulation steps. More = stronger effect, slower."},
      {"talus", "Angle of repose: slopes steeper than this shed material."},
      {"rate", "Material transport speed per iteration."},
      {"k_erode", "Erodibility: how fast rivers cut into rock."},
      {"m_exp", "Drainage-area exponent in E = K*A^m*S^n."},
      {"n_exp", "Slope exponent in E = K*A^m*S^n."},
      {"dt", "Implicit solver timestep. Large values stay stable."},
      {"uplift_rate", "Tectonic uplift added each step (implicit method).\nGrows mountains against erosion."},
      {"smooth", "Hillslope diffusion; softens sharp edges between steps."},
      {"level", "Threshold height (normalized 0..1)."},
      {"softness", "Width of the soft transition edge."},
      {"radius", "Effect radius as a fraction of terrain size."},
      {"factor", "Blend amount between inputs."},
      {"mode", "How the two inputs are combined."},
      {"smoothing", "Softness of the selection edges."},
      {"invert", "Flips the result (selected becomes unselected)."},
      {"post_remap", "Rescale output into the range below after computing."},
      {"post_range", "Output range after remapping."},
      {"post_invert", "Flip the output upside down."},
      {"post_gain", "Gamma curve on the output; >1 darkens lows."},
      {"tiles", "How many times the texture repeats across the terrain."},
      {"mapping", "Stretch = one copy over the whole terrain.\nTile = repeat with the count below."},
      {"asset", "ambientCG asset ID, e.g. Rock035, Grass004, Snow010.\nBrowse ambientcg.com for the catalog (CC0)."},
      {"resolution", "Source texture resolution. 8K sets are ~400MB downloads."},
      {"amplitude", "Strength of the warp displacement."},
      {"strength", "Effect intensity."},
      {"path", "Output/input file path."},
      {"auto_export", "Write the file every time this node recomputes\n(enabled automatically by Bake)."},
  };
  auto it = tips.find(key);
  return it == tips.end() ? nullptr : it->second;
}

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
static bool scalar_float(const char *id, float *v, float mn, float mx,
                         bool log_scale = false) {
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

static bool draw_attribute(gpx::Attribute &at) {
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

// ---- scene object properties (Cinema-4D-style attribute manager) ----------
static void object_properties(App &a) {
  SceneState &sc = scene();
  RenderSettings &rs = render_settings();
  if (sc.selected < 0 || sc.selected >= (int)sc.objects.size()) {
    ImGui::TextDisabled("nothing selected");
    return;
  }
  SceneObject &o = sc.objects[sc.selected];
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.55f, 0.24f, 1.f));
  ImGui::Text("%s", o.name.c_str());
  ImGui::PopStyleColor();
  ImGui::SameLine();
  const char *kind = o.type == SceneObject::Terrain ? "Terrain object"
                   : o.type == SceneObject::Water   ? "Water object"
                   : o.type == SceneObject::Sun     ? "Light"
                   : o.type == SceneObject::Atmosphere ? "Environment"
                   : o.type == SceneObject::Camera ? "Camera"
                   : o.type == SceneObject::Group  ? "Group"
                                                   : "Mesh object";
  ImGui::TextDisabled("Â· %s", kind);
  studio::Checkbox("Visible", &o.visible);
  ImGui::Separator();

  if (o.type == SceneObject::Camera) {
    camera_properties_ui(a, o);
    return;
  }
  if (o.type == SceneObject::Group) {
    ImGui::TextDisabled("Group — expand it in the Outliner to reach its members.");
    int n = 0;
    for (const auto &c : sc.objects)
      if (&c != &o && c.parent >= 0 && &sc.objects[c.parent] == &o) ++n;
    ImGui::TextDisabled("%d child object%s", n, n == 1 ? "" : "s");
    return;
  }
  switch (o.type) {
    case SceneObject::Terrain:
      ImGui::SeparatorText("Shape");
      scalar_float("hs", &rs.height_scale, 0.02f, 0.8f);
      ImGui::SameLine();
      ImGui::TextUnformatted("Height scale");
      ImGui::TextDisabled("Terrain shape is built in the node graph\n"
                          "(Terrain workspace). Material lives in the\n"
                          "Material tab.");
      ImGui::SeparatorText("Surface");
      scalar_float("ro", &rs.mat_roughness, 0.02f, 1.f);
      ImGui::SameLine();
      ImGui::TextUnformatted("Roughness");
      scalar_float("rf", &rs.mat_reflection, 0.f, 1.f);
      ImGui::SameLine();
      ImGui::TextUnformatted("Reflection");
      break;
    case SceneObject::Water:
      ImGui::SeparatorText("Level");
      scalar_float("wl", &rs.water_level, 0.f, 1.f);
      ImGui::SameLine();
      ImGui::TextUnformatted("Water level");
      ImGui::TextDisabled("Colors, waves and foam: Material tab.");
      break;
    case SceneObject::Sun:
      ImGui::SeparatorText("Direction");
      ImGui::RadioButton("Manual", &rs.sun_mode, 0);
      ImGui::SameLine();
      ImGui::RadioButton("Location & time", &rs.sun_mode, 1);
      if (rs.sun_mode == 0) {
        scalar_float("az", &rs.sun_azimuth, 0.f, 360.f);
        ImGui::SameLine();
        ImGui::TextUnformatted("Azimuth");
        scalar_float("al", &rs.sun_altitude, 1.f, 89.f);
        ImGui::SameLine();
        ImGui::TextUnformatted("Altitude");
      } else {
        ImGui::DragFloat("Latitude", &rs.latitude, 0.1f, -89.f, 89.f, "%.2f");
        ImGui::DragFloat("Longitude", &rs.longitude, 0.1f, -180.f, 180.f, "%.2f");
        ImGui::DragFloat("UTC offset", &rs.utc_offset, 0.25f, -12.f, 14.f, "%.2f h");
        ImGui::SliderInt("Month", &rs.month, 1, 12);
        ImGui::SliderInt("Day", &rs.day, 1, 31);
        ImGui::SliderFloat("Local time", &rs.hour, 0.f, 24.f, "%.2f h");
      }
      ImGui::SeparatorText("Light");
      ImGui::ColorEdit3("Color", rs.sun_color);
      scalar_float("si", &rs.sun_intensity, 0.2f, 8.f);
      ImGui::SameLine();
      ImGui::TextUnformatted("Intensity");
      studio::Checkbox("Casts shadows", &rs.shadows);
      break;
    case SceneObject::Atmosphere:
      ImGui::SeparatorText("Sky");
      scalar_float("ad", &rs.atmosphere_density, 0.05f, 3.f);
      ImGui::SameLine();
      ImGui::TextUnformatted("Density");
      ImGui::ColorEdit3("Zenith", rs.sky_zenith);
      ImGui::ColorEdit3("Horizon", rs.sky_horizon);
      ImGui::SeparatorText("Fog");
      ImGui::Combo("Type", &rs.fog_type, "Off\0Haze\0Fog\0Pollution\0");
      scalar_float("fd", &rs.fog_density, 0.f, 6.f);
      ImGui::SameLine();
      ImGui::TextUnformatted("Density");
      ImGui::SeparatorText("Clouds");
      studio::Checkbox("Volumetric clouds", &rs.clouds_on);
      scalar_float("cc", &rs.cloud_coverage, 0.f, 1.f);
      ImGui::SameLine();
      ImGui::TextUnformatted("Coverage");
      ImGui::TextDisabled("Full atmosphere controls: Environment tab.");
      break;
    case SceneObject::Mesh:
      ImGui::SeparatorText("Transform");
      ImGui::DragFloat("Position X", &o.pos[0], 0.005f, -0.5f, 1.5f);
      ImGui::DragFloat("Position Z", &o.pos[2], 0.005f, -0.5f, 1.5f);
      ImGui::DragFloat("Height", &o.pos[1], 0.005f, -0.5f, 2.f);
      ImGui::DragFloat("Scale", &o.scale, 0.002f, 0.005f, 1.f);
      ImGui::SliderFloat("Rotation", &o.yaw, -180.f, 180.f, "%.0f\xC2\xB0");
      ImGui::SeparatorText("Info");
      ImGui::TextDisabled("%d triangles", o.vert_count / 3);
      ImGui::TextDisabled("%s", o.path.c_str());
      break;
  }
}

// ------------------------------------------------- Blender-style properties
static char g_prop_search[64] = "";

bool prop_filter_match(const char *text) {
  if (!g_prop_search[0]) return true;
  std::string hay = text ? text : "", needle = g_prop_search;
  for (auto &c : hay) c = (char)tolower(c);
  for (auto &c : needle) c = (char)tolower(c);
  return hay.find(needle) != std::string::npos;
}

// Scene tab: project-wide settings
static void scene_properties(App &a) {
  RenderSettings &rs = render_settings();
  if (prop_filter_match("Resolution")) {
    ImGui::SeparatorText("Terrain resolution");
    int res = a.graph.resolution;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputInt("##res", &res, 64, 256,
                        ImGuiInputTextFlags_EnterReturnsTrue)) {
      std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
      if (lk.owns_lock()) {
        a.graph.resolution = std::clamp(res, 64, 8192);
        a.graph.mark_all_dirty();
        a.request_eval();
      }
    }
    ImGui::TextDisabled("Preview resolution of every node (64..8192).");
  }
  if (prop_filter_match("World scale")) {
    ImGui::SeparatorText("World scale");
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat("##size", &rs.terrain_size_m, 50.f, 100.f, 100000.f,
                     "%.0f m across");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##units", &rs.units, "Metric\0Imperial\0");
  }
  if (prop_filter_match("Statistics")) {
    ImGui::SeparatorText("Statistics");
    std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
    if (lk.owns_lock()) {
      double ms = 0;
      for (auto &n : a.graph.nodes) ms += n->last_compute_ms;
      ImGui::TextDisabled("%zu nodes, %zu links", a.graph.nodes.size(),
                          a.graph.links.size());
      ImGui::TextDisabled("last evaluation: %.0f ms", ms);
    }
    ImGui::TextDisabled("%zu scene objects", scene().objects.size());
  }
}

// The node's attributes are mirrored here so the panel keeps drawing (and
// stays editable) while evaluation holds the graph lock. Edits land in the
// mirror and are flushed to the real node as soon as the lock is free —
// that is what stops the panel blinking out while you scroll a value.
struct NodeMirror {
  uint64_t id = 0;
  std::string type, category, error;
  gpx::AttrSet attrs;
  bool valid = false;
  bool pending = false; // mirror holds edits not yet written to the node
};
static NodeMirror g_mirror;

static void node_properties(App &a) {
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (lk.owns_lock()) {
    gpx::Node *live = a.graph.find_node(a.selected_node);
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
      g_mirror.attrs = live->attrs;
      g_mirror.valid = true;
    } else {
      g_mirror.valid = false;
      g_mirror.pending = false;
    }
  }
  lk.unlock(); // everything below works on the mirror

  if (!g_mirror.valid || g_mirror.id != a.selected_node) {
    ImGui::TextDisabled("No node selected.");
    ImGui::TextDisabled("Click a node in the graph below.");
    return;
  }
  NodeMirror *n = &g_mirror;
  // never show a node that belongs to a different workspace â€” that was the
  // source of "terrain texture showing under Terrain"
  if (domain_of_category(n->category) != a.workspace) {
    const char *ws[4] = {"Terrain", "Materials", "Atmosphere", "Render"};
    ImGui::TextDisabled("%s belongs to the %s workspace.", n->type.c_str(),
                        ws[domain_of_category(n->category) & 3]);
    if (ImGui::Button("Go to that workspace"))
      a.workspace = domain_of_category(n->category);
    ImGui::SameLine();
    if (ImGui::Button("Inspect object instead")) a.prop_tab = TAB_OBJECT;
    return;
  }
  ImGui::Text("%s", n->type.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("Â· %s", n->category.c_str());
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
  bool searching = g_prop_search[0] != 0;
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
  static bool was_active = false;
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

// Blender's Properties editor: a vertical tab column on the left, a
// breadcrumb and search at the top, collapsible panels in the body. Tabs
// appear only when they apply to the current selection, and the active tab
// is sticky across selections.
void draw_panel_properties(App &a) {
  SceneState &sc = scene();
  bool have_obj = sc.selected >= 0 && sc.selected < (int)sc.objects.size();
  SceneObject::Type otype = have_obj ? sc.objects[sc.selected].type
                                     : SceneObject::Mesh;
  bool has_material = have_obj && (otype == SceneObject::Terrain ||
                                   otype == SceneObject::Water ||
                                   otype == SceneObject::Mesh);
  bool is_camera = have_obj && otype == SceneObject::Camera;
  bool has_node = a.selected_node != 0;

  struct TabDef { int id; const char *label; const char *tip; bool shown; };
  const TabDef tabs[] = {
      {TAB_RENDER, "Render", "Output engine, resolution, samples", true},
      {TAB_SCENE, "Scene", "Resolution, world scale, statistics", true},
      {TAB_WORLD, "World", "Sun, sky, clouds, fog, water", true},
      {TAB_OBJECT, is_camera ? "Camera" : "Object",
       is_camera ? "Lens, exposure, film and render for this camera"
                 : "The selected object's properties",
       have_obj},
      {TAB_MATERIAL, "Material", "Surface of the selected object", has_material},
      {TAB_NODE, "Node", "The selected node's parameters", has_node},
  };
  const int TAB_N = 6;

  // only fall back when the current tab genuinely cannot be shown
  bool valid = false;
  for (const TabDef &t : tabs)
    if (t.id == a.prop_tab && t.shown) valid = true;
  if (!valid) a.prop_tab = have_obj ? TAB_OBJECT : TAB_SCENE;

  // The active tab is entirely sticky: selecting objects or nodes never
  // switches it and never steals focus, so working in the node editor is
  // not interrupted by clicking something in a viewport.
  if (!ImGui::Begin("Properties", &a.show_properties)) {
    ImGui::End();
    return;
  }

  // breadcrumb + search
  const char *tab_name = "Scene";
  for (const TabDef &t : tabs)
    if (t.id == a.prop_tab) tab_name = t.label;
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.53f, 0.50f, 1.f));
  if (have_obj)
    ImGui::Text("Scene  >  %s  >  %s", sc.objects[sc.selected].name.c_str(),
                tab_name);
  else
    ImGui::Text("Scene  >  %s", tab_name);
  ImGui::PopStyleColor();
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##search", "search properties...", g_prop_search,
                           sizeof g_prop_search);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Filters the parameters of the active tab.");
  ImGui::Separator();

  // vertical tab column + content
  const float col_w = ImGui::CalcTextSize("Material").x + 22.f;
  ImGui::BeginChild("##tabs", ImVec2(col_w, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar);
  for (int i = 0; i < TAB_N; ++i) {
    const TabDef &t = tabs[i];
    if (!t.shown) continue;
    bool active = a.prop_tab == t.id;
    if (active) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.97f, 0.94f, 0.88f, 1.f));
    }
    if (ImGui::Button(t.label, ImVec2(col_w - 6, 0))) a.prop_tab = t.id;
    if (active) ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.tip);
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##body", ImVec2(0, 0), ImGuiChildFlags_Borders);
  switch (a.prop_tab) {
    case TAB_RENDER:
      render_properties_ui(a);
      ai_assist_bar(a, AiDomain::Render,
                    "render 4k with mitsuba, 512 samples");
      break;
    case TAB_SCENE:
      scene_properties(a);
      ai_assist_bar(a, AiDomain::Object,
                    "put the rock in front of the terrain");
      break;
    case TAB_WORLD:
      world_properties_ui(a);
      ai_assist_bar(a, AiDomain::World,
                    "low golden sunset, heavy haze, towering cumulonimbus");
      break;
    case TAB_OBJECT:
      object_properties(a);
      ai_assist_bar(a, is_camera
                           ? AiDomain::Camera
                           : AiDomain::Object,
                    is_camera ? "35mm camera, 50mm lens, cinematic, Kodak film"
                              : "place this object on the ridge");
      break;
    case TAB_MATERIAL:
      material_properties_ui(a);
      break; // the Material tab has its own material-graph AI
    default:
      node_properties(a);
      ai_assist_bar(a, AiDomain::Terrain,
                    "add ridged mountains with river erosion");
      break;
  }
  ImGui::EndChild();
  ImGui::End();
}

} // namespace studio


