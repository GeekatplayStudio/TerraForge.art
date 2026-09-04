// Geekatplay TerraForge - Properties: planets and infinite terrain layers.
// Split from panel_properties_object.cpp for the 500-line module rule; the
// object switch there dispatches here.
#include "app.hpp"
#include "prop_lengths.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <imgui.h>
#include <vector>

namespace studio {

void object_properties_planet_ui(App &a, SceneObject &o) {
  SceneState &sc = scene();

  PlanetData &P = o.planet;
  RenderSettings &rsn = render_settings();
  float km = rsn.terrain_size_m / 1000.f; // world unit -> km
  ImGui::SeparatorText("Body");
  (void)km;
  drag_length("Radius", &P.radius, 1.f, 1e-4f, 1e12f);
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
  const char *STYLE[4] = {"rolling hills", "ridged mountains",
                          "billow dunes", "realistic terrain"};
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
                        STYLE[std::clamp(SL.surf.layer.type, 0, 3)],
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
}

void object_properties_surface_ui(App &a, SceneObject &o) {
  SceneState &sc = scene();

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
      drag_length("Radius", &rs.planet_radius, 1.f, 1e-4f, 1e12f);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Earth is about 6 371 km; anything from a tenth\n"
                          "of a millimetre to a billion kilometres works.\n"
                          "Below the tile's own circumference the tile\n"
                          "wraps the globe completely, heights shrinking\n"
                          "with it - a 1 m planet from the heightmap.\n"
                          "Below a millionth of the tile width (1 cm at\n"
                          "5 km) shrink the tile too, or precision blurs\n"
                          "the globe.");
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
                   "Rolling hills\0Ridged mountains\0Billow dunes\0"
                   "Realistic terrain\0"))
    L.type = type;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Realistic terrain is a whole landscape in one\n"
                      "layer: eroded ridges where the uplands are, hills\n"
                      "elsewhere, terraced plateaus, carved valleys and\n"
                      "lowland lakes that fill from the water level.");
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
}

} // namespace studio
