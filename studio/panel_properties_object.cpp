// Geekatplay TerraForge - Properties: the selected object, and the scene.
//
// Every length here is a real one. A world unit is the home terrain tile,
// which is render_settings().terrain_size_m metres across, and nothing in
// this file shows a bare fraction of it: positions, sizes and altitudes are
// converted to metres (or feet) and given a unit that reads well at the size
// in question, so a 30 cm rock is "30 cm" and not "0.0003".
#include "app.hpp"
#include "ai_assist.hpp"
#include "icons.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <mutex>
#include <vector>
#include <string>

namespace studio {

namespace {

// ---------------------------------------------------------- metric lengths
// The unit a length is shown in follows its magnitude, the way a person would
// say it: centimetres for a pebble, metres for a building, kilometres for a
// mountain range. Imperial does the same with inches, feet and miles.
struct LenUnit {
  const char *suffix;
  float per_m;   // display value = metres * per_m
  int decimals;
};

LenUnit pick_unit(float metres) {
  float m = std::fabs(metres);
  if (render_settings().units == 1) { // imperial
    if (m < 0.3048f) return {"in", 39.37008f, 2};
    if (m < 1609.344f) return {"ft", 3.280840f, m < 30.f ? 2 : 1};
    return {"mi", 1.f / 1609.344f, 3};
  }
  if (m < 1.f) return {"cm", 100.f, 1};
  if (m < 1000.f) return {"m", 1.f, m < 10.f ? 2 : 1};
  return {"km", 0.001f, 3};
}

// `v` is in world units; `world_scale` converts it to world units of length
// (height_scale for altitudes, 1 for everything else).
bool drag_length(const char *label, float *v, float world_scale = 1.f,
                 float lo_m = -1e7f, float hi_m = 1e7f) {
  const float tile = render_settings().terrain_size_m;
  const float k = tile * (world_scale != 0.f ? world_scale : 1.f);
  float metres = *v * k;
  LenUnit u = pick_unit(metres);
  float shown = metres * u.per_m;
  char fmt[24];
  std::snprintf(fmt, sizeof fmt, "%%.%df %s", u.decimals, u.suffix);
  float speed = std::fmax(std::fabs(shown) * 0.01f, 0.01f);
  bool ch = ImGui::DragFloat(label, &shown, speed, lo_m * u.per_m,
                             hi_m * u.per_m, fmt);
  if (ch && k != 0.f) *v = (shown / u.per_m) / k;
  return ch;
}

// A read-only length, for things the user cannot drag here.
void text_length(const char *label, float metres) {
  LenUnit u = pick_unit(metres);
  ImGui::TextDisabled("%s: %.*f %s", label, u.decimals, metres * u.per_m,
                      u.suffix);
}

// A slider with its name above it rather than beside it. A trailing label is
// the first thing to be clipped when the panel is narrow or the font is
// large, and this panel is often both.
void labeled_scalar(const char *label, const char *id, float *v, float mn,
                    float mx) {
  ImGui::TextUnformatted(label);
  scalar_float(id, v, mn, mx);
}

// ------------------------------------------------- the surface node graph
// Planets and the endless ground plane have no heightmap: they are evaluated
// on the GPU from parameters, at whatever detail the camera has earned. That
// is why they cost no memory, and it is also why their shape cannot be painted
// or baked. The way to author a function is to author a function - so this
// finds (or builds) the SurfaceDisplacement sink and shows it in the editor,
// with a FieldNoise already wired into it to start from.
// `assign` (may be null) receives the node's id, so the planet or surround
// that asked owns that graph from now on; `create_new` forces a fresh one
// even when the graph already has a SurfaceDisplacement.
void open_surface_graph(App &a, unsigned long long *assign, bool create_new) {
  uint64_t focus = 0;
  {
    std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
    if (!lk.owns_lock()) {
      a.status = "the graph is evaluating - try again in a moment";
      return;
    }
    gpx::Node *sink = nullptr;
    if (assign && *assign) sink = a.graph.find_node(*assign);
    if (!sink && !create_new)
      for (auto &n : a.graph.nodes)
        if (n->type == "SurfaceDisplacement") sink = n.get();
    if (!sink) {
      undo_push_locked(a, "Add surface displacement");
      float x = 0.f, y = 260.f;
      for (auto &n : a.graph.nodes) x = std::max(x, n->pos_x);
      gpx::Node *src = a.graph.add_node("FieldNoise", x, y);
      sink = a.graph.add_node("SurfaceDisplacement", x + 260.f, y);
      if (src && sink) a.graph.add_link(src->id, "out", sink->id, "field");
      a.graph_layout_serial++;
      a.request_eval();
      a.status = "added a surface displacement graph";
    }
    focus = sink ? sink->id : 0;
    if (assign && sink) *assign = sink->id;
  }
  if (focus) graph_focus_node(a, focus); // takes the lock itself
}

// The picker: which SurfaceDisplacement node shapes this surface. Listed by
// id with what feeds them, so two graphs can be told apart.
static void surface_graph_picker(App &a, unsigned long long *node) {
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  std::vector<std::pair<unsigned long long, std::string>> sinks;
  if (lk.owns_lock())
    for (auto &n : a.graph.nodes)
      if (n->type == "SurfaceDisplacement") {
        gpx::Node *src = a.graph.upstream_node(*n, "field");
        sinks.push_back({n->id, "#" + std::to_string(n->id) + "  " +
                                    (src ? src->type : std::string("(unwired)"))});
      }
  if (lk.owns_lock()) lk.unlock();
  std::string label = "the graph's first";
  for (auto &s : sinks)
    if (s.first == *node) label = s.second;
  if (*node && label == "the graph's first") label = "(missing) built-in layers";
  ImGui::SetNextItemWidth(-1);
  if (ImGui::BeginCombo("##surfgraph", label.c_str())) {
    if (ImGui::Selectable("the graph's first SurfaceDisplacement", *node == 0))
      *node = 0;
    for (auto &s : sinks)
      if (ImGui::Selectable(s.second.c_str(), s.first == *node)) *node = s.first;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The SurfaceDisplacement node whose field graph\n"
                      "displaces this surface - each world can have its own,\n"
                      "as a Terragen planet has its own terrain network.");
  if (ImGui::Button("Edit graph")) open_surface_graph(a, node, false);
  ImGui::SameLine();
  if (ImGui::Button("New graph for this world")) open_surface_graph(a, node, true);
}

// ------------------------------------------------------------- transform
// Position, rotation and size, in the same block for every object that has
// one. Rotation is HPB (heading about Y, pitch about X, bank about Z) - the
// Cinema 4D convention, and the reason "tilt" is a number here rather than
// something you can only do by dragging in a viewport.
void transform_ui(App &a, SceneObject &o) {
  (void)a;
  const float hs = render_settings().height_scale;
  // the lock, and then everything below reads but does not write
  {
    bool locked = o.locked;
    if (studio::Checkbox("Locked", &locked)) o.locked = locked;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("No gizmo, no dragging, and these fields are read-only\n"
                        "until unlocked. Also in the Objects tree.");
  }
  ImGui::BeginDisabled(o.locked);
  struct EndDisabledAtExit {
    ~EndDisabledAtExit() { ImGui::EndDisabled(); }
  } end_disabled_at_exit;
  if (prop_filter_match("Position")) {
    ImGui::SeparatorText("Position");
    drag_length("X", &o.pos[0]);
    drag_length("Altitude", &o.pos[1], hs);
    drag_length("Z", &o.pos[2]);
  }
  if (prop_filter_match("Rotation")) {
    ImGui::SeparatorText("Rotation");
    ImGui::DragFloat("Heading", &o.yaw, 0.5f, -180.f, 180.f, "%.1f°");
    ImGui::DragFloat("Pitch", &o.pitch, 0.5f, -180.f, 180.f, "%.1f°");
    ImGui::DragFloat("Bank", &o.roll, 0.5f, -180.f, 180.f, "%.1f°");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Heading turns about the vertical axis, pitch tips the\n"
                        "nose up and down, bank rolls it - applied in that\n"
                        "order (HPB).");
  }
  if (prop_filter_match("Size")) {
    ImGui::SeparatorText("Size");
    drag_length("Size", &o.scale, 1.f, 0.0005f, 1e6f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The object's largest dimension, as a real length.");
    ImGui::DragFloat3("Squeeze", o.scl, 0.005f, 0.01f, 20.f, "%.3f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Scales X, Y and Z independently around the size\n"
                        "above: 1, 0.4, 1 flattens without shrinking.");
    float w = o.scale * o.scl[0] * render_settings().terrain_size_m;
    float ht = o.scale * o.scl[1] * render_settings().terrain_size_m;
    float d = o.scale * o.scl[2] * render_settings().terrain_size_m;
    LenUnit u = pick_unit(std::fmax(w, std::fmax(ht, d)));
    ImGui::TextDisabled("%.*f x %.*f x %.*f %s", u.decimals, w * u.per_m,
                        u.decimals, ht * u.per_m, u.decimals, d * u.per_m,
                        u.suffix);
  }
}

} // namespace

// ---- scene object properties (Cinema-4D-style attribute manager) ----------
void object_properties_ui(App &a) {
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
                   : o.type == SceneObject::Planet ? "Planet"
                   : o.type == SceneObject::InfiniteSurface ? "Infinite terrain"
                                                   : "Mesh object";
  ImGui::TextDisabled("· %s", kind);
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
      if (prop_filter_match("Size")) {
        ImGui::SeparatorText("Size");
        ImGui::TextUnformatted("Across");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##across", &rs.terrain_size_m, 50.f, 10.f, 1e7f,
                         "%.0f m");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("How much ground the terrain tile covers. Every\n"
                            "other length in the interface is measured\n"
                            "against this.");
        text_length("Highest possible point",
                    rs.height_scale * rs.terrain_size_m);
      }
      ImGui::SeparatorText("Shape");
      labeled_scalar("Height scale", "hs", &rs.height_scale, 0.02f, 0.8f);
      ImGui::TextDisabled("Terrain shape is built in the node graph\n"
                          "(Terrain workspace). Material lives in the\n"
                          "Material tab.");
      ImGui::SeparatorText("Surface");
      labeled_scalar("Roughness", "ro", &rs.mat_roughness, 0.02f, 1.f);
      labeled_scalar("Reflection", "rf", &rs.mat_reflection, 0.f, 1.f);
      break;
    case SceneObject::Water:
      ImGui::SeparatorText("Level");
      labeled_scalar("Water level", "wl", &rs.water_level, 0.f, 1.f);
      ImGui::TextDisabled("Colors, waves and foam: Material tab.");
      break;
    case SceneObject::Sun:
      ImGui::SeparatorText("Direction");
      ImGui::RadioButton("Manual", &rs.sun_mode, 0);
      ImGui::SameLine();
      ImGui::RadioButton("Location & time", &rs.sun_mode, 1);
      if (rs.sun_mode == 0) {
        labeled_scalar("Azimuth", "az", &rs.sun_azimuth, 0.f, 360.f);
        labeled_scalar("Altitude", "al", &rs.sun_altitude, 1.f, 89.f);
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
      labeled_scalar("Intensity", "si", &rs.sun_intensity, 0.2f, 8.f);
      studio::Checkbox("Casts shadows", &rs.shadows);
      break;
    case SceneObject::Atmosphere:
      ImGui::SeparatorText("Sky");
      labeled_scalar("Density", "ad", &rs.atmosphere_density, 0.05f, 3.f);
      ImGui::ColorEdit3("Zenith", rs.sky_zenith);
      ImGui::ColorEdit3("Horizon", rs.sky_horizon);
      ImGui::SeparatorText("Fog");
      ImGui::Combo("Type", &rs.fog_type, "Off\0Haze\0Fog\0Pollution\0");
      labeled_scalar("Density", "fd", &rs.fog_density, 0.f, 6.f);
      ImGui::SeparatorText("Clouds");
      studio::Checkbox("Volumetric clouds", &rs.clouds_on);
      labeled_scalar("Coverage", "cc", &rs.cloud_coverage, 0.f, 1.f);
      ImGui::TextDisabled("Full atmosphere controls: Environment tab.");
      break;
    case SceneObject::Light:
      ImGui::SeparatorText("Point light");
      drag_length("X", &o.pos[0]);
      drag_length("Y", &o.pos[1]);
      drag_length("Z", &o.pos[2]);
      ImGui::ColorEdit3("Color", o.color);
      ImGui::SliderFloat("Intensity", &o.light_intensity, 0.f, 10.f);
      ImGui::SliderFloat("Reach", &o.light_radius, 0.01f, 2.f);
      ImGui::Combo("Type", &o.light_type, "Point\0Spot\0");
      if (o.light_type == 1) {
        ImGui::SliderFloat("Cone", &o.light_cone, 5.f, 160.f, "%.0f\xC2\xB0");
        ImGui::SliderFloat("Heading", &o.yaw, -180.f, 180.f, "%.0f\xC2\xB0");
        ImGui::SliderFloat("Pitch", &o.pitch, -90.f, 90.f, "%.0f\xC2\xB0");
        ImGui::TextDisabled("Pitch -90 aims straight down.");
      }
      ImGui::TextDisabled("Lights the terrain and every mesh within reach.");
      break;
    case SceneObject::Mesh:
      transform_ui(a, o);
      if (prop_filter_match("Color")) {
        ImGui::SeparatorText("Surface");
        ImGui::ColorEdit3("Color", o.color);
      }
      if (prop_filter_match("Scatter")) {
        ImGui::SeparatorText("Scatter");
        // pick any node with a point-cloud output; None turns it off
        {
          const char *cur = "None";
          std::string cur_label;
          gpx::Node *curn = a.graph.find_node(o.scatter_node);
          if (curn) {
            cur_label = curn->type + " #" + std::to_string(curn->id);
            cur = cur_label.c_str();
          }
          if (ImGui::BeginCombo("Points node", cur)) {
            if (ImGui::Selectable("None", o.scatter_node == 0)) {
              o.scatter_node = 0;
              o.inst.clear();
            }
            for (auto &cand : a.graph.nodes) {
              bool has_pts = false;
              for (const gpx::Port &p : cand->ports)
                has_pts = has_pts || (p.dir == gpx::PortDir::Out &&
                                      p.type == gpx::DataType::Points);
              if (!has_pts) continue;
              std::string label =
                  cand->type + " #" + std::to_string(cand->id);
              if (ImGui::Selectable(label.c_str(),
                                    o.scatter_node == cand->id)) {
                o.scatter_node = cand->id;
                a.request_eval();
              }
            }
            ImGui::EndCombo();
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies of this mesh appear at every point of\n"
                              "the chosen cloud, standing on the terrain.");
        }
        if (o.scatter_node) {
          bool ch = false;
          ch |= ImGui::SliderFloat("Size", &o.scatter_scale, 0.05f, 4.f);
          ch |= ImGui::SliderFloat("Size jitter", &o.scatter_jitter, 0.f, 1.f);
          ImGui::SliderFloat("Wind sway", &o.scatter_sway, 0.f, 0.3f);
          ch |= ImGui::SliderFloat("Size from value", &o.scatter_value_size,
                                   0.f, 1.f);
          int sd = (int)o.scatter_seed;
          if (ImGui::DragInt("Seed", &sd, 1, 0, 1 << 24)) {
            o.scatter_seed = (unsigned)sd;
            ch = true;
          }
          if (ch) a.request_eval();
          ImGui::TextDisabled("%d copies", (int)(o.inst.size() / 8));
        }
      }
      if (prop_filter_match("Info")) {
        ImGui::SeparatorText("Info");
        ImGui::TextDisabled("%d triangles", o.vert_count / 3);
        ImGui::TextDisabled("%s", o.path.c_str());
      }
      break;
    case SceneObject::Planet: {
      PlanetData &P = o.planet;
      RenderSettings &rsn = render_settings();
      float km = rsn.terrain_size_m / 1000.f; // world unit -> km
      ImGui::SeparatorText("Body");
      (void)km;
      drag_length("Radius", &P.radius, 1.f, 1.f, 1e9f);
      text_length("Circumference", P.radius * 6.2831853f *
                                       render_settings().terrain_size_m);
      ImGui::DragFloat("Relief", &P.relief, 0.001f, 0.f, 1.f, "%.3f");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Maximum mountain height as a fraction of the\n"
                          "radius. Earth is about 0.0014; go higher for\n"
                          "dramatic fantasy worlds.");
      int seed = (int)P.seed;
      if (ImGui::DragInt("Seed", &seed, 1, 1, 1 << 24)) P.seed = (uint32_t)seed;
      ImGui::SliderFloat("Rotation", &P.spin_deg, -180.f, 180.f, "%.0f\xC2\xB0");
      ImGui::SeparatorText("Position");
      drag_length("X", &o.pos[0]);
      drag_length("Y", &o.pos[1]);
      drag_length("Z", &o.pos[2]);
      ImGui::TextDisabled("Distances between worlds, in real units.");
      ImGui::SeparatorText("Ocean & climate");
      ImGui::SliderFloat("Sea level", &P.sea_level, 0.f, 1.f);
      ImGui::SliderFloat("Snow line", &P.snow_line, 0.f, 1.2f);
      ImGui::ColorEdit3("Water", P.water_color);
      ImGui::ColorEdit3("Rock (low)", P.rock_low);
      ImGui::ColorEdit3("Rock (high)", P.rock_high);
      ImGui::SeparatorText("Atmosphere");
      ImGui::SliderFloat("Density", &P.atmo_density, 0.f, 2.f);
      ImGui::ColorEdit3("Tint", P.atmo_color);
      // The shape of a planet's surface is its stack of displacement layers,
      // so they are edited here rather than only in the Objects tree: this is
      // the panel you are already in when you decide the world is too smooth.
      ImGui::SeparatorText("Surface displacement");
      const char *STYLE[3] = {"rolling hills", "ridged mountains",
                              "billow dunes"};
      std::vector<int> ls = scene_surface_layers(sc.selected);
      for (int idx : ls) {
        SceneObject &SL = sc.objects[idx];
        ImGui::PushID(idx);
        if (ImGui::Selectable(SL.name.c_str())) {
          sc.selected = idx;
          a.scene_selection_serial++;
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Click to edit this layer's relief and coverage.");
        ImGui::SameLine();
        ImGui::TextDisabled("%s, x%.2f",
                            STYLE[std::clamp(SL.surf.layer.type, 0, 2)],
                            SL.surf.layer.amplitude);
        ImGui::PopID();
      }
      if (ls.empty())
        ImGui::TextDisabled("No displacement: a smooth ball.");
      if (ImGui::Button("+ add displacement layer", ImVec2(-1, 0))) {
        undo_push(a, "Add displacement layer");
        int idx = scene_add_infinite_surface(sc.selected);
        sc.selected = idx;
        a.scene_selection_serial++;
        return; // `o` and `P` are references into a vector that just grew
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Layers stack: a broad ridged layer for the\n"
                          "continents, a finer one for the foothills, a\n"
                          "third at low coverage for dune fields.");
      ImGui::SeparatorText("Surface graph");
      ImGui::TextDisabled("A field graph, transpiled to GPU code and evaluated\n"
                          "on the sphere itself - it holds up all the way down\n"
                          "to a walk on the surface.");
      surface_graph_picker(a, &P.surface_node);
      ImGui::TextDisabled("Surface colour is the rock and water above; the\n"
                          "snow line and sea level decide where each shows.");
      ImGui::TextDisabled("It is all generated on the GPU from these numbers\n"
                          "alone - a planet costs no memory or textures, so\n"
                          "add as many worlds and layers as you like.");
    } break;
    case SceneObject::InfiniteSurface: {
      gpx::planet::Layer &L = o.surf.layer;
      bool on_planet = o.parent >= 0 && o.parent < (int)sc.objects.size() &&
                       sc.objects[o.parent].type == SceneObject::Planet;
      ImGui::TextDisabled(on_planet
                              ? "Shapes the surface of %s."
                              : "Extends the home terrain to the horizon.%s",
                          on_planet ? sc.objects[o.parent].name.c_str() : "");
      if (!on_planet) {
        // The home planet: the sphere the terrain tile lies on. Its radius
        // is a real length from one metre up; small enough and the tile
        // wraps the whole globe, so this is also how a globe is made from
        // a heightmap.
        RenderSettings &rs = render_settings();
        ImGui::SeparatorText("Home planet");
        bool flat = rs.planet_radius <= 0.f;
        if (studio::Checkbox("Flat world", &flat))
          rs.planet_radius = flat ? 0.f : 1275.f;
        if (!flat) {
          drag_length("Radius", &rs.planet_radius, 1.f, 1.f, 1e9f);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Earth is about 6 371 km. Below the tile's own\n"
                              "circumference the tile wraps the globe\n"
                              "completely - a 1 m planet from the heightmap.");
          float circ_m = rs.planet_radius * 6.2831853f * rs.terrain_size_m;
          text_length("Circumference", circ_m);
          if (rs.planet_radius * 6.2831853f < 1.f)
            ImGui::TextDisabled("The tile wraps the whole planet; the\n"
                                "surround below is not drawn.");
        }
      }
      ImGui::SeparatorText("Relief");
      int type = L.type;
      if (ImGui::Combo("Style", &type,
                       "Rolling hills\0Ridged mountains\0Billow dunes\0"))
        L.type = type;
      ImGui::DragFloat("Feature scale", &L.frequency, 0.1f, 0.2f, 200.f, "%.1f");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How many features fit across the world.\n"
                          "Low = continents, high = hills.");
      ImGui::SliderFloat("Amplitude", &L.amplitude, 0.f, 2.f);
      int seed = (int)L.seed;
      if (ImGui::DragInt("Seed", &seed, 1, 1, 1 << 24)) L.seed = (uint32_t)seed;
      ImGui::SeparatorText("Coverage");
      ImGui::SliderFloat("Coverage", &L.coverage, 0.f, 1.f);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fraction of the world this layer occupies.\n"
                          "1 covers everything; lower values confine it to\n"
                          "procedurally chosen regions (continents, ranges).");
      ImGui::DragFloat("Region size", &L.mask_scale, 0.05f, 0.2f, 12.f, "%.2f");
      ImGui::SeparatorText("Surface graph");
      ImGui::TextDisabled("These layers are parameters, and there are shapes\n"
                          "no parameter can make. A field graph can: it is\n"
                          "transpiled to GPU code and evaluated on the\n"
                          "surface at every scale.");
      if (on_planet) {
        // the graph belongs to the planet the layer shapes
        surface_graph_picker(a, &sc.objects[o.parent].planet.surface_node);
      } else {
        // the home planet's surround: named on its first root layer
        std::vector<int> roots = scene_surface_layers(-1);
        int owner = roots.empty() ? sc.selected : roots[0];
        surface_graph_picker(a, &sc.objects[owner].surf.surface_node);
      }
      if (!on_planet) {
        ImGui::SeparatorText("Ground plane");
        ImGui::SliderFloat("Height scale", &o.surf.height_scale, 0.f, 3.f);
        ImGui::TextDisabled("Blends seamlessly out of the terrain tile's\n"
                            "edges and continues to the horizon.");
      }
    } break;
  }
}

// Scene tab: project-wide settings
void scene_properties_ui(App &a) {
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
  if (prop_filter_match("Layers")) {
    ImGui::SeparatorText("Layers");
    scene_layers_ui(a);
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

} // namespace studio
