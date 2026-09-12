// Geekatplay TerraForge - Properties: planets and infinite terrain layers.
// Split from panel_properties_object.cpp for the 500-line module rule; the
// object switch there dispatches here.
#include "app.hpp"
#include "wheel_widgets.hpp"
#include "prop_lengths.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <imgui.h>
#include <vector>
#include "world_shape.hpp"

namespace studio {

// The world's shape (world_shape.hpp): a globe, a ring world, a Dyson
// sphere - and which face its ground is on.
static void world_shape_ui(RenderSettings &rs) {
  ImGui::SeparatorText("World shape");
  int preset = rs.world_shape == WORLD_RING ? 1
             : rs.world_shape == WORLD_FLAT ? 3
             : (rs.world_inside ? 2 : 0);
  if (ImGui::Combo("Shape", &preset, "Globe\0Ring world\0Dyson sphere\0Flat world\0"))
    world_preset_apply(rs, preset == 1 ? "ring" : preset == 2 ? "dyson"
                         : preset == 3 ? "flat" : "globe");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Globe: the planet, its ground on the outside and the\n"
                      "horizon falling away.\n"
                      "Ring world: a cylinder of this radius, curving along\n"
                      "the tile's east-west only, with the ground on the\n"
                      "inside and the sun on its axis - the far side of the\n"
                      "ring arches overhead.\n"
                      "Dyson sphere: a globe with the ground on the inside\n"
                      "and the sun at its centre.\n"
                      "Flat world: a plane of the width below, cut to a disc\n"
                      "or a square, with nothing beyond its edge.\n"
                      "The atmosphere, the clouds and the water lie on the\n"
                      "surface whatever its shape: on a ring the clouds are\n"
                      "a band round the inside of the ring.\n"
                      "The settings below are what a preset sets.");
  if (rs.world_shape == WORLD_RING) {
    drag_length("Ring width", &rs.world_width, 1.f, 1.f, 1e9f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How far the ring reaches north and south of the\n"
                        "tile; beyond its rim there is only space. With a\n"
                        "thickness the rim is a wall the thickness tall.");
  }
  if (rs.world_shape == WORLD_FLAT) {
    int outline = rs.world_outline == OUTLINE_SQUARE ? 1 : 0;
    if (ImGui::Combo("Outline", &outline, "Disc\0Square\0"))
      rs.world_outline = outline ? OUTLINE_SQUARE : OUTLINE_DISC;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The flat world's edge: a disc of the width below\n"
                        "across, or a square with sides that long.");
    drag_length("Width", &rs.world_width, 1.f, 1.f, 1e9f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How far the flat world reaches, edge to edge, with the\n"
                        "tile at its centre. Beyond the edge there is nothing;\n"
                        "with a thickness the edge is a wall down to the\n"
                        "underside.");
  }
  drag_length("Thickness", &rs.world_thickness, 1.f, 0.f, 1e9f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How thick the world's shell is. 0 is a skin: the other\n"
                      "face is the same surface. Above 0 the other face lies\n"
                      "this far below the ground - a ring's outside, a flat\n"
                      "world's underside, a globe's inner crust - and a ring\n"
                      "or a flat world is drawn as a body: both faces and the\n"
                      "rim between them, so it is not a sheet seen from its\n"
                      "edge or from below.");
  if (rs.world_shape != WORLD_FLAT) {
    studio::Checkbox("Ground on the inside", &rs.world_inside);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The ground faces the centre: up is toward it, the\n"
                        "surface rises away from you instead of falling, and\n"
                        "the far side of the world is drawn overhead. Every\n"
                        "view curves an inside world.");
    if (rs.world_inside) {
      studio::Checkbox("Sun inside", &rs.world_sun_inside);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The sun is a body on the ring's axis or at the\n"
                          "sphere's centre rather than a direction: it stands\n"
                          "straight above the tile and lights the far side\n"
                          "toward itself. The Sun object's angles are not\n"
                          "used while this is on.");
    }
  }
  ImGui::TextDisabled("A tile or a surface layer can stand on the other\n"
                      "face of the same world: its Side, in its own tab.");
}

void object_side_ui(SceneObject &o) {
  int side = std::clamp(o.side, 0, 2);
  if (ImGui::Combo("Side of the world", &side, "The world's own\0Outside\0Inside\0")) o.side = side;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Which face of the world this stands on. One shell\n"
                      "can carry ground on both: on the other face it is\n"
                      "the same shell with the heights going the other\n"
                      "way - a globe's other face is the inside of its\n"
                      "crust, seen from within the hollow planet; a Dyson\n"
                      "sphere's is its dark outside. It is placed against\n"
                      "that face's own layers. The world's own face is the\n"
                      "home planet's setting (World shape).");
}

void object_properties_planet_ui(App &a, SceneObject &o) {
  SceneState &sc = scene();

  PlanetData &P = o.planet;
  RenderSettings &rsn = render_settings();
  float km = rsn.terrain_size_m / 1000.f; // world unit -> km
  if (P.home) {
    // The world. Its curvature is the render setting every pass reads; its
    // ground is its surface-layer children; its terrain tiles, water and
    // atmosphere are its children too.
    ImGui::SeparatorText("The world");
    ImGui::TextDisabled("The planet the scene stands on. Terrain tiles, the\n"
                        "water, the atmosphere and the surface layers are\n"
                        "its children; add more with the + tile. A second\n"
                        "planet is a globe in the sky.");
    drag_length("Radius", &rsn.planet_radius, 1.f, 0.f, 1e12f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The world's curvature. 0 is a flat world. Free\n"
                        "perspective views draw it flat regardless; camera\n"
                        "views (and views with Planet curvature on) curve.");
    text_length("Circumference", rsn.planet_radius * 6.2831853f * rsn.terrain_size_m);
    P.radius = rsn.planet_radius;
    world_shape_ui(rsn);
    return;
  }
  ImGui::SeparatorText("Body");
  (void)km;
  drag_length("Radius", &P.radius, 1.f, 1e-4f, 1e12f);
  text_length("Circumference", P.radius * 6.2831853f *
                                   render_settings().terrain_size_m);
  studio::DragFloatW("Relief", &P.relief, 0.001f, 0.f, 1.f, "%.3f");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Maximum mountain height as a fraction of the\n"
                      "radius. Earth is about 0.0014; go higher for\n"
                      "dramatic fantasy worlds.");
  int seed = (int)P.seed;
  if (studio::DragIntW("Seed", &seed, 1, 1, 1 << 24)) P.seed = (uint32_t)seed;
  studio::SliderFloatW("Rotation", &P.spin_deg, -180.f, 180.f, "%.0f\xC2\xB0");
  ImGui::SeparatorText("Position");
  drag_length("X", &o.pos[0]);
  drag_length("Y", &o.pos[1]);
  drag_length("Z", &o.pos[2]);
  ImGui::TextDisabled("Distances between worlds, in real units.");
  ImGui::SeparatorText("Ocean & climate");
  studio::SliderFloatW("Sea level", &P.sea_level, 0.f, 1.f);
  studio::SliderFloatW("Snow line", &P.snow_line, 0.f, 1.2f);
  ImGui::ColorEdit3("Water", P.water_color);
  ImGui::ColorEdit3("Rock (low)", P.rock_low);
  ImGui::ColorEdit3("Rock (high)", P.rock_high);
  ImGui::SeparatorText("Atmosphere");
  studio::SliderFloatW("Density", &P.atmo_density, 0.f, 2.f);
  ImGui::ColorEdit3("Tint", P.atmo_color);
  studio::SliderFloatW("Cloud cover", &P.clouds, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How much of the planet a deck of cloud covers, seen\n"
                      "from space: 0 clear, 1 overcast. It drifts with the\n"
                      "scene's clouds. A world with no atmosphere has none.");
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
    world_shape_ui(rs);
  }
  object_side_ui(o);
  ImGui::SeparatorText("Relief");
  int type = L.type;
  if (ImGui::Combo("Style", &type,
                   "Rolling hills\0Ridged mountains\0Billow dunes\0"
                   "Realistic terrain\0Craters\0"))
    L.type = type;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Realistic terrain is a whole landscape in one\n"
                      "layer: eroded ridges where the uplands are, hills\n"
                      "elsewhere, terraced plateaus, carved valleys and\n"
                      "lowland lakes that fill from the water level.");
  studio::DragFloatW("Feature scale", &L.frequency, 0.1f, 0.2f, 200.f, "%.1f");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How many features fit across the world.\n"
                      "Low = continents, high = hills.");
  studio::SliderFloatW("Amplitude", &L.amplitude, 0.f, 2.f);
  int seed = (int)L.seed;
  if (studio::DragIntW("Seed", &seed, 1, 1, 1 << 24)) L.seed = (uint32_t)seed;
  ImGui::SeparatorText("Coverage");
  studio::SliderFloatW("Coverage", &L.coverage, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Fraction of the world this layer occupies.\n"
                      "1 covers everything; lower values confine it to\n"
                      "procedurally chosen regions (continents, ranges).");
  studio::DragFloatW("Region size", &L.mask_scale, 0.05f, 0.2f, 12.f, "%.2f");
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
    studio::SliderFloatW("Height scale", &o.surf.height_scale, 0.f, 3.f);
    ImGui::TextDisabled("Blends seamlessly out of the terrain tile's\n"
                        "edges and continues to the horizon.");
  }
}

} // namespace studio
