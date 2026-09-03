// Geekatplay TerraForge — lighting nodes (the Lighting workspace).
//
// Terragen keeps its lights in a Lighting network beside the terrain and
// shader networks; Vue gives every light an editor with shadow, lighting and
// influence tabs. TerraForge already draws point and spot lights that live as
// scene objects — these nodes are the network view of the same lights, so a
// light can be placed and tuned from the graph, saved with it, driven by the
// AI, and undone like any other node.
//
// LightSource is live: studio/scene_nodes_objects.cpp keeps one scene light
// per node in step with its attributes. The rest of the family is declared
// here as planned nodes so the roadmap (P3, Lighting) is visible in the
// editor itself rather than only in a document: each says what it will do
// and what it is waiting on, evaluates to nothing, and is exempt from the
// output requirement of the contract battery as a configuration node.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"

namespace gpx {

namespace {
// The shape every planned node shares. The text attribute carries the plan
// into the Properties editor; the description carries it into the library
// tooltip and the AI catalogue.
void planned(Node &n, const char *what, const char *phase) {
  add_text(n.attrs, "plan", "Planned", what, "Roadmap").tooltip =
      "This node is a placeholder: it documents a capability on the roadmap\n"
      "so the module is not forgotten. It has no effect on the scene yet.";
  add_text(n.attrs, "phase", "Roadmap phase", phase, "Roadmap");
}
} // namespace

REGISTER_NODE(
    LightSource, "Light",
    "A point or spot light in the scene: position, colour, intensity, reach, cone",
    [](Node &n) {
      add_text(n.attrs, "object", "Scene object", "Light", "Light").tooltip =
          "Name of the scene light this node drives. Created when missing;\n"
          "an existing light of that name is adopted.";
      add_bool(n.attrs, "enabled", "Enabled", true, "Light");
      add_choice(n.attrs, "type", "Type", {"Point", "Spot"}, 0, "Light");
      add_color(n.attrs, "color", "Colour", 1.f, 0.9f, 0.75f, 1.f, "Light");
      add_float(n.attrs, "intensity", "Intensity", 1.f, 0.f, 50.f, "Light");
      add_float(n.attrs, "radius_m", "Reach (m)", 1750.f, 1.f, 100000.f, "Light", true)
          .tooltip = "Distance at which the light has faded to nothing.";
      add_float(n.attrs, "x_m", "X (m)", 2500.f, -100000.f, 100000.f, "Position");
      add_float(n.attrs, "y_m", "Height (m)", 1500.f, -10000.f, 100000.f, "Position");
      add_float(n.attrs, "z_m", "Z (m)", 2500.f, -100000.f, 100000.f, "Position");
      add_float(n.attrs, "heading", "Heading °", 0.f, -180.f, 180.f, "Spot");
      add_float(n.attrs, "pitch", "Pitch °", -60.f, -90.f, 90.f, "Spot");
      add_float(n.attrs, "cone", "Cone angle °", 40.f, 1.f, 179.f, "Spot");
      add_bool(n.attrs, "shadows", "Cast shadows", false, "Shadow").tooltip =
          "Recorded now, honoured by the offline engines; the viewport's\n"
          "point lights do not cast shadows yet (roadmap P3).";
    },
    [](Node &) {})

REGISTER_NODE(
    AreaLight, "Light",
    "[Planned] Rectangular and disc area lights with soft shadows",
    [](Node &n) {
      planned(n,
              "Area lights (rectangle, disc, sphere) with physically sized soft\n"
              "shadows, for the offline engines first and a shadow-mapped\n"
              "approximation in the viewport.",
              "P3 Lighting (Vue p288, p673-678)");
    },
    [](Node &) {})

REGISTER_NODE(
    Skylight, "Light",
    "[Planned] Global illumination model: ambient, hemispherical sky light, GI",
    [](Node &n) {
      planned(n,
              "Chooses the global lighting model - plain ambient, hemispherical\n"
              "sky light from the atmosphere, or full GI in the offline engines -\n"
              "and its strength. Today the sky-light term lives on\n"
              "AtmosphereSettings.ambient.",
              "P3 Lighting (Vue p587 global lighting models)");
    },
    [](Node &) {})

REGISTER_NODE(
    LightGel, "Light",
    "[Planned] Projected texture (gel / gobo) on a spot light",
    [](Node &n) {
      planned(n,
              "A texture or field graph projected through a spot light, for\n"
              "cloud shadows, window patterns and stage gobos.",
              "P3 Lighting (Vue p670)");
    },
    [](Node &) {})

REGISTER_NODE(
    VolumetricLight, "Light",
    "[Planned] Visible light shafts through haze and cloud",
    [](Node &n) {
      planned(n,
              "Light that is visible in the air: god rays through cloud gaps and\n"
              "spot beams in fog, ray-marched with the existing height fog.",
              "P3 Lighting (Vue p671-672)");
    },
    [](Node &) {})

REGISTER_NODE(
    LensFlare, "Light",
    "[Planned] Lens flare and reflections editor per light",
    [](Node &n) {
      planned(n,
              "Per-light flare: glow, streaks, rings and polygonal reflections\n"
              "composited after tone mapping.",
              "P3 Lighting (Vue p662-669)");
    },
    [](Node &) {})

} // namespace gpx
