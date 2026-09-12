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
  add_float(n.attrs, "x_m", "X (m)", 2500.f, -100000.f, 100000.f, "Transform")
      .tooltip = "Where the object stands, in metres from the middle of the\n"
                 "tile along east.";
  add_float(n.attrs, "y_m", "Height (m)", 0.f, -10000.f, 100000.f, "Transform")
      .tooltip = "How high the object stands, in metres. Objects placed on\n"
                 "the terrain read the ground height for themselves; this\n"
                 "offsets from it.";
  add_float(n.attrs, "z_m", "Z (m)", 2500.f, -100000.f, 100000.f, "Transform")
      .tooltip = "Where the object stands, in metres from the middle of the\n"
                 "tile along north.";
  add_float(n.attrs, "size_m", "Size (m)", size_m, 0.01f, 100000.f, "Transform", true)
      .tooltip = "Uniform size of the object's unit box.";
  add_float(n.attrs, "heading", "Heading °", 0.f, -180.f, 180.f, "Transform")
      .tooltip = "How far the object is turned about the vertical, in\n"
                 "degrees.";
  add_float(n.attrs, "pitch", "Pitch °", 0.f, -180.f, 180.f, "Transform")
      .tooltip = "How far the object is tipped forward or back, in degrees.";
  add_float(n.attrs, "bank", "Bank °", 0.f, -180.f, 180.f, "Transform")
      .tooltip = "How far the object is rolled about its own forward axis,\n"
                 "in degrees.";
  add_bool(n.attrs, "visible", "Visible", true, "Transform")
      .tooltip = "Whether the object is drawn. Hiding is not deleting - it\n"
                 "keeps its place in the scene and all of its settings.";
}
} // namespace

REGISTER_NODE(
    ImportObject, "Scene",
    "An imported 3D model (FBX, glTF/GLB, OBJ, STL, PLY, OFF) placed in the scene, with "
    "its textures, transform and colour",
    [](Node &n) {
      add_filename(n.attrs, "file", "Model file", "", "Object").tooltip =
          "FBX, glTF and GLB with their textures; OBJ with its MTL pictures;\n"
          "STL, PLY and OFF. The Load button on the node card opens the dialog.";
      add_text(n.attrs, "object", "Scene object", "", "Object").tooltip =
          "Name in the Objects tree. Empty: the file name.";
      add_color(n.attrs, "color", "Colour", 0.62f, 0.60f, 0.57f, 1.f, "Object")
          .tooltip = "The object's colour, where no material is assigned to it.";
      add_transform(n, 400.f);
    },
    [](Node &) {})

REGISTER_NODE(
    Primitive, "Scene",
    "A built-in primitive placed in the scene: a cube, sphere, plane, cylinder or cone, "
    "or a plant or rock built from its kind (pine, juniper, palm, fern, grass tuft, bush, "
    "boulder)",
    [](Node &n) {
      add_choice(n.attrs, "kind", "Shape",
                 {"Cube", "Sphere", "Plane", "Cylinder", "Cone", "Pine", "Juniper", "Palm",
                  "Fern", "Grass tuft", "Bush", "Boulder"},
                 1, "Object")
          .tooltip = "Which shape this is. The plants and the boulder are built\n"
                     "from their kind - bark and foliage in their own colours -\n"
                     "and come at their own size; scatter them over the terrain\n"
                     "with a Scatter points or Ecosystem layer node.";
      add_text(n.attrs, "object", "Scene object", "", "Object").tooltip =
          "Name in the Objects tree. Empty: the shape's name.";
      add_color(n.attrs, "color", "Colour", 0.62f, 0.60f, 0.57f, 1.f, "Object")
          .tooltip = "The object's colour, where no material is assigned to it.";
      add_seed(n.attrs, "seed", "Plant seed", 0, "Object").tooltip =
          "The plants and the boulder: another one of the same kind -\n"
          "its branches, fronds and lumps laid out afresh. 0 is the one\n"
          "it has always been. The other shapes ignore it.";
      add_transform(n, 400.f);
    },
    [](Node &) {})

REGISTER_NODE(
    Planet, "Scene",
    "A procedural planet: radius, relief, seas, snow, atmosphere and its surface layers",
    [](Node &n) {
      add_text(n.attrs, "object", "Scene object", "Planet", "Planet")
          .tooltip = "Which scene object this node drives.";
      add_float(n.attrs, "radius_m", "Radius (m)", 15000.f, 10.f, 1e8f, "Planet", true)
          .tooltip = "The planet's radius, in metres. It decides how quickly the\n"
                     "horizon curves away, which is the whole difference between\n"
                     "a small moon and an earth.";
      add_float(n.attrs, "relief", "Relief (fraction of radius)", 0.02f, 0.f, 0.3f, "Planet")
          .tooltip = "How much height the surface has, as a fraction of the\n"
                     "radius.";
      add_seed(n.attrs, "seed", "Seed", 1, "Planet");
      add_float(n.attrs, "sea_level", "Sea level", 0.35f, 0.f, 1.f, "Surface").tooltip =
          "Within the relief range; 0 = no ocean.";
      add_float(n.attrs, "snow_line", "Snow line", 0.75f, 0.f, 1.f, "Surface").tooltip =
          "Altitude where snow begins; 1 = none.";
      add_color(n.attrs, "rock_low", "Lowland rock", 0.38f, 0.34f, 0.30f, 1.f, "Surface")
          .tooltip = "The colour of the low ground.";
      add_color(n.attrs, "rock_high", "Highland rock", 0.55f, 0.51f, 0.47f, 1.f, "Surface")
          .tooltip = "The colour of the high ground.";
      add_color(n.attrs, "water_color", "Ocean", 0.06f, 0.16f, 0.28f, 1.f, "Surface")
          .tooltip = "The colour of the seas.";
      add_color(n.attrs, "atmo_color", "Atmosphere", 0.45f, 0.62f, 0.90f, 1.f, "Atmosphere")
          .tooltip = "The colour of the air, seen from outside.";
      add_float(n.attrs, "atmo_density", "Atmosphere density", 0.6f, 0.f, 2.f, "Atmosphere")
          .tooltip = "0 = airless rim.";
      add_float(n.attrs, "clouds", "Cloud cover", 0.4f, 0.f, 1.f, "Atmosphere")
          .tooltip = "How much of the planet a deck of cloud covers, seen from\n"
                     "space: 0 clear, 1 overcast. It drifts with the scene's\n"
                     "clouds. A planet with no atmosphere has no clouds.";
      add_float(n.attrs, "spin", "Spin °", 0.f, -180.f, 180.f, "Atmosphere")
          .tooltip = "How far the planet is turned about its axis, which chooses\n"
                     "which face is toward the camera.";
      add_float(n.attrs, "x_m", "X (m)", 70000.f, -1e7f, 1e7f, "Position")
          .tooltip = "Where the planet sits, east-west, in metres.";
      add_float(n.attrs, "y_m", "Height (m)", 17500.f, -1e7f, 1e7f, "Position")
          .tooltip = "How high the planet sits, in metres.";
      add_float(n.attrs, "z_m", "Z (m)", 2500.f, -1e7f, 1e7f, "Position")
          .tooltip = "Where the planet sits, north-south, in metres.";
      add_bool(n.attrs, "visible", "Visible", true, "Position")
          .tooltip = "Whether the planet is drawn.";
    },
    [](Node &) {})

REGISTER_NODE(
    InfiniteTerrain, "Scene",
    "An endless procedural terrain layer: on the ground plane or shaping a planet",
    [](Node &n) {
      add_text(n.attrs, "object", "Scene object", "Infinite terrain", "Layer")
          .tooltip = "Which scene object this node drives.";
      add_text(n.attrs, "planet", "Parent planet", "", "Layer").tooltip =
          "Name of the Planet object this layer shapes. Empty: extends the\n"
          "home ground plane to the horizon.";
      add_seed(n.attrs, "seed", "Seed", 1, "Layer");
      add_choice(n.attrs, "type", "Landscape",
                 {"Rolling hills", "Ridged mountains", "Billow dunes",
                  "Realistic terrain"}, 1, "Layer")
          .tooltip = "The kind of ground that runs to the horizon past the tile.";
      add_float(n.attrs, "frequency", "Feature scale", 3.f, 0.1f, 64.f, "Layer", true)
          .tooltip = "How large the surround's features are.";
      add_float(n.attrs, "amplitude", "Amplitude", 1.f, 0.f, 4.f, "Layer")
          .tooltip = "How much relief the surround has. Matching it to the\n"
                     "tile's own is what makes the join invisible.";
      add_float(n.attrs, "coverage", "Coverage", 1.f, 0.f, 1.f, "Layer").tooltip =
          "Fraction of the surface the layer occupies.";
      add_float(n.attrs, "mask_scale", "Region size", 1.5f, 0.1f, 10.f, "Layer")
          .tooltip = "How large the patches are where the surround changes\n"
                     "character.";
      add_float(n.attrs, "height_scale", "Height scale", 1.f, 0.f, 4.f, "Layer").tooltip =
          "Extra multiplier for ground-plane layers.";
      add_bool(n.attrs, "visible", "Visible", true, "Layer")
          .tooltip = "Whether the surround is drawn.";
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
