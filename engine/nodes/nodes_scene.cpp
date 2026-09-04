// Geekatplay TerraForge — scene object nodes (the Objects workspace).
//
// Imported meshes, primitives, planets and infinite terrain layers are scene
// objects; the Objects tree and the viewport gizmos edit them directly. These
// nodes are the network view of the same objects - Terragen's Objects
// network - so a scene can be assembled in the graph, saved with it, driven
// by the AI and undone. studio/scene_nodes_objects.cpp keeps one scene object
// per node in step with its attributes; the object is found by the node that
// drives it, then by name, and created when neither exists.
//
// Every length here is in metres. The scene stores tile units (1 = the home
// terrain tile = terrain_size_m metres); the conversion happens where the
// node is applied, so a scene rescaled later keeps the object where it was
// asked to be in the real world.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"

namespace gpx {

namespace {
void planned(Node &n, const char *what, const char *phase) {
  add_text(n.attrs, "plan", "Planned", what, "Roadmap").tooltip =
      "This node is a placeholder: it documents a capability on the roadmap\n"
      "so the module is not forgotten. It has no effect on the scene yet.";
  add_text(n.attrs, "phase", "Roadmap phase", phase, "Roadmap");
}

// The transform block shared by anything that sits in the world.
void add_transform(Node &n, float size_m) {
  add_float(n.attrs, "x_m", "X (m)", 2500.f, -100000.f, 100000.f, "Transform");
  add_float(n.attrs, "y_m", "Height (m)", 0.f, -10000.f, 100000.f, "Transform");
  add_float(n.attrs, "z_m", "Z (m)", 2500.f, -100000.f, 100000.f, "Transform");
  add_float(n.attrs, "size_m", "Size (m)", size_m, 0.01f, 100000.f, "Transform", true)
      .tooltip = "Uniform size of the object's unit box.";
  add_float(n.attrs, "heading", "Heading °", 0.f, -180.f, 180.f, "Transform");
  add_float(n.attrs, "pitch", "Pitch °", 0.f, -180.f, 180.f, "Transform");
  add_float(n.attrs, "bank", "Bank °", 0.f, -180.f, 180.f, "Transform");
  add_bool(n.attrs, "visible", "Visible", true, "Transform");
}
} // namespace

REGISTER_NODE(
    ImportObject, "Scene",
    "An imported 3D object (OBJ) placed in the scene, with its transform and colour",
    [](Node &n) {
      add_filename(n.attrs, "file", "OBJ file", "", "Object");
      add_text(n.attrs, "object", "Scene object", "", "Object").tooltip =
          "Name in the Objects tree. Empty: the file name.";
      add_color(n.attrs, "color", "Colour", 0.62f, 0.60f, 0.57f, 1.f, "Object");
      add_transform(n, 400.f);
    },
    [](Node &) {})

REGISTER_NODE(
    Primitive, "Scene",
    "A built-in primitive (cube, sphere, plane, cylinder, cone) placed in the scene",
    [](Node &n) {
      add_choice(n.attrs, "kind", "Shape", {"Cube", "Sphere", "Plane", "Cylinder", "Cone"},
                 1, "Object");
      add_text(n.attrs, "object", "Scene object", "", "Object").tooltip =
          "Name in the Objects tree. Empty: the shape's name.";
      add_color(n.attrs, "color", "Colour", 0.62f, 0.60f, 0.57f, 1.f, "Object");
      add_transform(n, 400.f);
    },
    [](Node &) {})

REGISTER_NODE(
    Planet, "Scene",
    "A procedural planet: radius, relief, seas, snow, atmosphere and its surface layers",
    [](Node &n) {
      add_text(n.attrs, "object", "Scene object", "Planet", "Planet");
      add_float(n.attrs, "radius_m", "Radius (m)", 15000.f, 10.f, 1e8f, "Planet", true);
      add_float(n.attrs, "relief", "Relief (fraction of radius)", 0.02f, 0.f, 0.3f, "Planet");
      add_seed(n.attrs, "seed", "Seed", 1, "Planet");
      add_float(n.attrs, "sea_level", "Sea level", 0.35f, 0.f, 1.f, "Surface").tooltip =
          "Within the relief range; 0 = no ocean.";
      add_float(n.attrs, "snow_line", "Snow line", 0.75f, 0.f, 1.f, "Surface").tooltip =
          "Altitude where snow begins; 1 = none.";
      add_color(n.attrs, "rock_low", "Lowland rock", 0.38f, 0.34f, 0.30f, 1.f, "Surface");
      add_color(n.attrs, "rock_high", "Highland rock", 0.55f, 0.51f, 0.47f, 1.f, "Surface");
      add_color(n.attrs, "water_color", "Ocean", 0.06f, 0.16f, 0.28f, 1.f, "Surface");
      add_color(n.attrs, "atmo_color", "Atmosphere", 0.45f, 0.62f, 0.90f, 1.f, "Atmosphere");
      add_float(n.attrs, "atmo_density", "Atmosphere density", 0.6f, 0.f, 2.f, "Atmosphere")
          .tooltip = "0 = airless rim.";
      add_float(n.attrs, "spin", "Spin °", 0.f, -180.f, 180.f, "Atmosphere");
      add_float(n.attrs, "x_m", "X (m)", 70000.f, -1e7f, 1e7f, "Position");
      add_float(n.attrs, "y_m", "Height (m)", 17500.f, -1e7f, 1e7f, "Position");
      add_float(n.attrs, "z_m", "Z (m)", 2500.f, -1e7f, 1e7f, "Position");
      add_bool(n.attrs, "visible", "Visible", true, "Position");
    },
    [](Node &) {})

REGISTER_NODE(
    InfiniteTerrain, "Scene",
    "An endless procedural terrain layer: on the ground plane or shaping a planet",
    [](Node &n) {
      add_text(n.attrs, "object", "Scene object", "Infinite terrain", "Layer");
      add_text(n.attrs, "planet", "Parent planet", "", "Layer").tooltip =
          "Name of the Planet object this layer shapes. Empty: extends the\n"
          "home ground plane to the horizon.";
      add_seed(n.attrs, "seed", "Seed", 1, "Layer");
      add_choice(n.attrs, "type", "Landscape",
                 {"Rolling hills", "Ridged mountains", "Billow dunes",
                  "Realistic terrain"}, 1, "Layer");
      add_float(n.attrs, "frequency", "Feature scale", 3.f, 0.1f, 64.f, "Layer", true);
      add_float(n.attrs, "amplitude", "Amplitude", 1.f, 0.f, 4.f, "Layer");
      add_float(n.attrs, "coverage", "Coverage", 1.f, 0.f, 1.f, "Layer").tooltip =
          "Fraction of the surface the layer occupies.";
      add_float(n.attrs, "mask_scale", "Region size", 1.5f, 0.1f, 10.f, "Layer");
      add_float(n.attrs, "height_scale", "Height scale", 1.f, 0.f, 4.f, "Layer").tooltip =
          "Extra multiplier for ground-plane layers.";
      add_bool(n.attrs, "visible", "Visible", true, "Layer");
    },
    [](Node &) {})

REGISTER_NODE(
    ObjectGroup, "Scene",
    "[Planned] Group objects under one transform, with instancing",
    [](Node &n) {
      planned(n,
              "A group node: children follow one transform, can be instanced\n"
              "and hidden together. Groups exist in the Objects tree already;\n"
              "this brings them into the network.",
              "P9 Objects & scene");
    },
    [](Node &) {})

REGISTER_NODE(
    BooleanObject, "Scene",
    "[Planned] Union, intersection and difference of meshes",
    [](Node &n) {
      planned(n,
              "Mesh booleans (union / intersection / difference) and metablobs,\n"
              "evaluated on import or as a live modifier.",
              "P9 Objects & scene (Vue p316-323)");
    },
    [](Node &n) { (void)n; })

REGISTER_NODE(
    TerrainObject, "Scene",
    "[Planned] Several independent heightfield terrains in one scene",
    [](Node &n) {
      planned(n,
              "More than one heightfield terrain object, each with its own graph,\n"
              "resolution and placement - islands, a distant range, a quarry.\n"
              "Today there is one home tile plus infinite procedural layers.",
              "P9 Objects & scene (Terragen: multiple terrain objects)");
    },
    [](Node &) {})

} // namespace gpx
