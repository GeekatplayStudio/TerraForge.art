// Geekatplay TerraForge — every component by name (see component_add.hpp).
#include "component_add.hpp"
#include "app.hpp"
#include "material_presets.hpp"
#include "mesh_object.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <chrono>
#include <mutex>

namespace studio {

namespace {

void free_spot(gpx::Graph &g, float &x, float &y) {
  x = 120.f;
  y = 120.f;
  for (auto &n : g.nodes) x = std::max(x, n->pos_x + 240.f);
}

// A node with no object of its own (the atmosphere, a cloud layer, the sun,
// the water): added, selected, evaluated.
uint64_t add_scene_node(App &a, const char *type, std::string &err) {
  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(500))) {
    err = "the graph is busy; try again";
    return 0;
  }
  undo_push_locked(a, std::string("Add ") + type);
  float x = 0, y = 0;
  free_spot(a.graph, x, y);
  gpx::Node *n = a.graph.add_node(type, x, y);
  if (!n) {
    err = std::string("no node type called ") + type;
    return 0;
  }
  a.selected_node = n->id;
  a.graph_layout_serial++;
  a.request_eval();
  return n->id;
}

} // namespace

const std::vector<ComponentKind> &component_kinds() {
  static const std::vector<ComponentKind> K = {
      {"terrain", "Heightfield terrain", "Terrain",
       "A further terrain tile with its own node chain, standing beside the\n"
       "ones there are. Move, turn and size it like any object; each tile\n"
       "has its own material and placement."},
      {"infinite_terrain", "Infinite terrain", "Terrain",
       "A procedural surface with no edge: a planet's ground, or a layer of\n"
       "relief on it."},
      {"planet", "Planet", "Terrain", "A whole world in the sky, with its own infinite terrain."},
      {"moon", "Moon", "Space", "A small airless cratered world in the sky."},
      {"nebula", "Nebula", "Space",
       "A cloud of glowing gas in deep space, seen where the atmosphere lets\n"
       "space through: at night, from high up, beyond a ring's rim."},
      {"dark_nebula", "Dark nebula", "Space", "A cloud of dust that hides the stars behind it."},
      {"galaxy", "Spiral galaxy", "Space", "A spiral galaxy: arms, a bulge, dust lanes, tilted as you like."},
      {"elliptical_galaxy", "Elliptical galaxy", "Space", "A smooth glow of old stars."},
      {"planetary_nebula", "Planetary nebula", "Space", "A ring of gas round a dying star."},
      {"atmosphere", "Atmosphere", "Atmosphere",
       "An Atmosphere settings node: sky colours, density, ambient, fog - the\n"
       "environment as a node you can wire cloud layers into."},
      {"cloud_layer", "Cloud layer", "Atmosphere",
       "One more layer of cloud - stratus, cumulus, cirrus - at its own\n"
       "altitude. Chain them into the Atmosphere node's clouds input."},
      {"sun", "Sun", "Atmosphere", "A SunLight node: position by angle or by place, date and time."},
      {"water", "Water", "Atmosphere", "A WaterLayer node: level, colours, waves and foam."},
      {"light", "Light", "Scene", "A point or spot light."},
      {"camera", "Camera", "Scene", "A camera with real optics."},
      {"cube", "Cube", "Objects", "A cube, with its Primitive node and a material."},
      {"sphere", "Sphere", "Objects", "A sphere, with its Primitive node and a material."},
      {"plane", "Plane", "Objects", "A plane, with its Primitive node and a material."},
      {"cylinder", "Cylinder", "Objects", "A cylinder, with its Primitive node and a material."},
      {"cone", "Cone", "Objects", "A cone, with its Primitive node and a material."},
      {"import_mesh", "Import a mesh...", "Objects", "An OBJ, STL, PLY, glTF or FBX from disk."},
      {"pine", "Pine", "Plants", "A conifer, about 22 m: tiers of ragged, drooping branches."},
      {"juniper", "Juniper", "Plants", "A desert juniper or pinyon, about 6 m: a twisted trunk under a lumpy crown."},
      {"palm", "Palm", "Plants", "A palm, about 16 m: a leaning ringed trunk under arching fronds."},
      {"fern", "Fern", "Plants", "A fern, about 1.6 m: a rosette of arching fronds."},
      {"grass", "Grass tuft", "Plants", "A tuft of dry grass, about 0.8 m."},
      {"bush", "Bush", "Plants", "A shrub, about 2.5 m."},
      {"boulder", "Boulder", "Plants", "A sandstone boulder, about 3 m."},
      {"scatter", "Scatter points", "Populations",
       "A ScatterPoints node: a population placed over the terrain by mask,\n"
       "slope and altitude."},
      {"ecosystem", "Ecosystem layer", "Populations",
       "An EcosystemLayer node: Vue's EcoSystem as a material layer whose\n"
       "presence places a population."},
      {"material", "Material", "Materials", "A base material (Basic colour) ready to change."},
  };
  return K;
}

int scene_add_terrain_tile(App &a, const std::string &name) {
  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(500))) return -1;
  undo_push_locked(a, "Add terrain");
  SceneState &sc = scene();
  int count = 0;
  for (const SceneObject &o : sc.objects)
    if (o.type == SceneObject::Terrain) ++count;
  SceneObject o;
  o.type = SceneObject::Terrain;
  o.name = name.empty() ? "Terrain " + std::to_string(count + 1) : name;
  // beside the tiles already there, one tile to the east each, so a new
  // tile never lands on top of an old one
  o.pos[0] = 1.05f * (float)count;
  o.pos[1] = 0.f;
  o.pos[2] = 0.f;
  o.scale = 1.f;
  // its chain: a fractal into its own Terrain Output, which is what makes
  // it a tile of its own (terrain_tiles.hpp)
  float x = 0, y = 0;
  free_spot(a.graph, x, y);
  gpx::Node *noise = a.graph.add_node("Noise", x, y);
  gpx::Node *out = a.graph.add_node("TerrainOutput", x + 240.f, y);
  if (!noise || !out) return -1;
  if (gpx::Attribute *at = noise->attrs.find("seed")) at->seed = 1000u + (uint32_t)count * 7919u;
  if (gpx::Attribute *at = noise->attrs.find("octaves")) at->i = 9;
  a.graph.add_link(noise->id, noise->first_out(gpx::DataType::Heightmap)->name, out->id, "heightmap");
  o.driver_node = out->id;
  o.material_node = component_material(a);
  o.parent = scene_home_planet(); // a tile stands on the world
  sc.objects.push_back(o);
  const int idx = (int)sc.objects.size() - 1;
  a.selected_node = out->id;
  a.graph_layout_serial++;
  a.request_eval();
  return idx;
}

bool component_add(App &a, const std::string &kind, const std::string &name,
                   const std::string &path, NewComponent &out, std::string &err) {
  SceneState &sc = scene();
  auto select = [&](int idx) {
    out.object = idx;
    if (idx >= 0) {
      sc.selected = idx;
      a.scene_selection_serial++;
    }
    return idx >= 0;
  };
  if (kind == "terrain") {
    const int idx = scene_add_terrain_tile(a, name);
    if (idx < 0) {
      err = "add terrain: the graph is busy";
      return false;
    }
    out.node = sc.objects[(size_t)idx].driver_node;
    out.material = sc.objects[(size_t)idx].material_node;
    return select(idx);
  }
  if (kind == "infinite_terrain") {
    // a surface layer of the world (scene_ensure_home_planet puts the
    // root-level ones there too)
    undo_push(a, "Add infinite terrain");
    return select(scene_add_infinite_surface(scene_home_planet(), name));
  }
  if (kind == "planet") {
    undo_push(a, "Add planet");
    return select(scene_add_planet(name));
  }
  if (kind == "moon") {
    undo_push(a, "Add moon");
    return select(scene_add_moon(name));
  }
  {
    // deep space (scene.hpp NebulaData): the kind names the type
    static const struct { const char *kind; int type; } NEB[] = {
        {"nebula", 0}, {"dark_nebula", 1}, {"galaxy", 2}, {"elliptical_galaxy", 3}, {"planetary_nebula", 4}};
    for (const auto &k : NEB)
      if (kind == k.kind) {
        undo_push(a, "Add nebula");
        return select(scene_add_nebula(name, k.type));
      }
  }
  if (kind == "light") {
    undo_push(a, "Add light");
    return select(scene_add_light(name));
  }
  if (kind == "camera") {
    undo_push(a, "Add camera");
    return select(scene_add_camera(name));
  }
  if (kind == "cube" || kind == "sphere" || kind == "plane" || kind == "cylinder" || kind == "cone" ||
      scene_is_plant_kind(kind)) {
    out = component_add_primitive(a, kind, name);
    if (out.object < 0) {
      err = "add " + kind + ": the graph is busy";
      return false;
    }
    return select(out.object);
  }
  if (kind == "import_mesh") {
    if (path.empty()) {
      err = "import_mesh needs a path";
      return false;
    }
    undo_push(a, "Import mesh");
    const int idx = scene_import_mesh(path, err);
    if (idx < 0) {
      if (err.empty()) err = "could not import " + path;
      return false;
    }
    return select(idx);
  }
  static const struct { const char *kind, *type; } NODES[] = {
      {"atmosphere", "AtmosphereSettings"}, {"cloud_layer", "CloudLayer"}, {"sun", "SunLight"},
      {"water", "WaterLayer"},              {"scatter", "ScatterPoints"},  {"ecosystem", "EcosystemLayer"}};
  for (const auto &n : NODES)
    if (kind == n.kind) {
      out.node = add_scene_node(a, n.type, err);
      return out.node != 0;
    }
  if (kind == "material") {
    out.material = material_preset_create(a, name.empty() ? "Basic Color" : name, err);
    return out.material != 0;
  }
  err = "add_component: no component called '" + kind + "'";
  return false;
}

} // namespace studio
