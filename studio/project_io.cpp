// Geekatplay Studio — project save/load + default startup graph.
//
// A project file is the graph JSON plus a "scene_bodies" section carrying the
// planets and infinite terrain layers (the loader ignores keys it does not
// know, so older files load unchanged).
#include "app.hpp"
#include "scene.hpp"
#include "scene_io.hpp"
#include "undo.hpp"
#include "gpx/serialization.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <map>

using json = nlohmann::json;

namespace studio {

void project_new(App &a) {
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  a.graph.clear();
  a.selected_node = a.view_node = 0;
  a.project_path.clear();
  a.status = "new project";
}

static json bodies_to_json() {
  SceneState &sc = scene();
  json arr = json::array();
  for (int i = 0; i < (int)sc.objects.size(); ++i) {
    const SceneObject &o = sc.objects[i];
    if (o.type == SceneObject::Planet) {
      const PlanetData &P = o.planet;
      arr.push_back({{"kind", "planet"},
                     {"index", i},
                     {"name", o.name},
                     {"visible", o.visible},
                     {"pos", {o.pos[0], o.pos[1], o.pos[2]}},
                     {"radius", P.radius},
                     {"relief", P.relief},
                     {"seed", P.seed},
                     {"sea", P.sea_level},
                     {"snow", P.snow_line},
                     {"spin", P.spin_deg},
                     {"rock_lo", {P.rock_low[0], P.rock_low[1], P.rock_low[2]}},
                     {"rock_hi", {P.rock_high[0], P.rock_high[1], P.rock_high[2]}},
                     {"water", {P.water_color[0], P.water_color[1], P.water_color[2]}},
                     {"atmo", {P.atmo_color[0], P.atmo_color[1], P.atmo_color[2]}},
                     {"atmo_d", P.atmo_density}});
    } else if (o.type == SceneObject::InfiniteSurface) {
      const gpx::planet::Layer &L = o.surf.layer;
      arr.push_back({{"kind", "surface"},
                     {"name", o.name},
                     {"visible", o.visible},
                     {"parent", o.parent},
                     {"seed", L.seed},
                     {"type", L.type},
                     {"freq", L.frequency},
                     {"amp", L.amplitude},
                     {"coverage", L.coverage},
                     {"mask_scale", L.mask_scale},
                     {"hscale", o.surf.height_scale}});
    }
  }
  return arr;
}

static void bodies_from_json(const json &arr) {
  SceneState &sc = scene();
  // clear any bodies from the previous session, keep everything else
  for (int i = (int)sc.objects.size() - 1; i >= 0; --i)
    if (sc.objects[i].type == SceneObject::Planet ||
        sc.objects[i].type == SceneObject::InfiniteSurface)
      sc.objects.erase(sc.objects.begin() + i);
  if (!arr.is_array()) return;
  std::map<int, int> planet_remap; // saved index -> live index
  for (const json &j : arr) {
    if (j.value("kind", "") != "planet") continue;
    SceneObject o;
    o.type = SceneObject::Planet;
    o.name = j.value("name", "Planet");
    o.visible = j.value("visible", true);
    if (j.contains("pos"))
      for (int k = 0; k < 3; ++k) o.pos[k] = j["pos"][k].get<float>();
    PlanetData &P = o.planet;
    P.radius = j.value("radius", 3.f);
    P.relief = j.value("relief", 0.02f);
    P.seed = j.value("seed", 1u);
    P.sea_level = j.value("sea", 0.35f);
    P.snow_line = j.value("snow", 0.75f);
    P.spin_deg = j.value("spin", 0.f);
    auto col = [&](const char *key, float *dst) {
      if (j.contains(key))
        for (int k = 0; k < 3; ++k) dst[k] = j[key][k].get<float>();
    };
    col("rock_lo", P.rock_low);
    col("rock_hi", P.rock_high);
    col("water", P.water_color);
    col("atmo", P.atmo_color);
    P.atmo_density = j.value("atmo_d", 0.6f);
    sc.objects.push_back(o);
    planet_remap[j.value("index", -1)] = (int)sc.objects.size() - 1;
  }
  for (const json &j : arr) {
    if (j.value("kind", "") != "surface") continue;
    SceneObject o;
    o.type = SceneObject::InfiniteSurface;
    o.name = j.value("name", "Surface layer");
    o.visible = j.value("visible", true);
    int saved_parent = j.value("parent", -1);
    auto it = planet_remap.find(saved_parent);
    o.parent = it != planet_remap.end() ? it->second : -1;
    gpx::planet::Layer &L = o.surf.layer;
    L.seed = j.value("seed", 1u);
    L.type = j.value("type", 1);
    L.frequency = j.value("freq", 3.f);
    L.amplitude = j.value("amp", 1.f);
    L.coverage = j.value("coverage", 1.f);
    L.mask_scale = j.value("mask_scale", 1.5f);
    o.surf.height_scale = j.value("hscale", 1.f);
    sc.objects.push_back(o);
  }
}

bool project_save(App &a, const std::string &path) {
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  bool ok = false;
  try {
    json j = json::parse(gpx::graph_to_json(a.graph));
    // kept for anything older that still reads it; the full scene follows
    j["scene_bodies"] = bodies_to_json();
    j["scene"] = scene_to_json();
    j["environment"] = environment_to_json();
    std::ofstream f(path, std::ios::binary);
    if (f) {
      f << j.dump(1);
      ok = (bool)f;
    }
  } catch (const std::exception &) {
    ok = false;
  }
  a.status = ok ? ("saved " + path) : ("SAVE FAILED: " + path);
  if (ok) a.project_path = path;
  return ok;
}

bool project_load(App &a, const std::string &path) {
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    a.status = "LOAD FAILED: cannot open " + path;
    return false;
  }
  std::string text((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
  std::string err;
  GraphIdMap idmap;
  bool ok = gpx::graph_from_json(a.graph, text, err, &idmap);
  a.status = ok ? ("loaded " + path) : ("LOAD FAILED: " + err);
  if (ok) {
    try {
      json j = json::parse(text);
      if (j.contains("scene")) {
        // the full scene: objects, cameras, meshes, layers, selection
        std::string warnings;
        scene_from_json(j["scene"], idmap, warnings);
        if (!warnings.empty())
          a.status = "loaded " + path + " (with warnings: " + warnings + ")";
      } else {
        // an older file: only planets and infinite surfaces were recorded
        bodies_from_json(j.value("scene_bodies", json::array()));
      }
      if (j.contains("environment"))
        environment_from_json(j["environment"], idmap);
    } catch (const std::exception &) {
      // graph loaded fine; a mangled scene section should not kill the load
    }
    undo_clear(); // a loaded project starts a fresh history
    a.project_path = path;
    a.selected_node = a.view_node = 0;
    a.graph_layout_serial++;
    a.request_eval();
  }
  return ok;
}

// startup demo: ridged mountains -> warp -> hydraulic erosion -> texture
// A new scene is a planet with flat ground on it.
//
// Terragen starts the same way and for the same reason: the planet is the
// context, and terrain is what you displace its surface *with*. Starting with
// a mountain already built hides that — it makes the terrain look like the
// scene rather than like one shader in it, and it means the first thing a user
// does is delete something.
//
// Flat does not mean empty: the ground curves to a horizon, the sky and water
// are there, and the Terrain menu drops a complete editable chain — Mountain,
// Ridged peaks, Eroded mountain, Canyon, Dunes, Iceberg, Lunar — in one click.
void project_default_graph(App &a) {
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  a.graph.clear();
  a.graph.resolution = 512;
  gpx::Node *base = a.graph.add_node("Constant", 0, 120);
  gpx::Node *out = a.graph.add_node("TerrainOutput", 300, 120);
  gpx::Node *tex = a.graph.add_node("TerrainTexture", 300, 320);
  if (base && out && tex) {
    // sea level sits at 0.08, so the ground starts just above the water
    if (gpx::Attribute *v = base->attrs.find("value")) v->f = 0.12f;
    // Remapping a *constant* buffer is degenerate — min equals max, so the
    // whole tile collapses to one end of the range and sinks under the sea.
    // Flat ground has to pass through untouched.
    if (gpx::Attribute *r = out->attrs.find("remap")) r->b = false;
    // the tile is already flat; fading its edges to zero would dig a moat
    // around it, and the planet surface it blends into is flat too
    if (gpx::Attribute *z = out->attrs.find("zero_edges")) z->f = 0.f;
    a.graph.add_link(base->id, "output", out->id, "heightmap");
    a.graph.add_link(out->id, "heightmap", tex->id, "input");
    a.view_node = out->id;
    a.selected_node = out->id;
  }
}

// ------------------------------------------------------- graph view state
// The node editor remembers pan, zoom and node positions in its own settings
// file. If those values ever go non-finite — a NaN position, a zoom that
// underflows to nothing — it tries to lay out a canvas billions of units
// across and never finishes a frame. The application then spins at full CPU
// behind a black window, on every subsequent launch, with nothing to say why:
// the bad file outlives the process that wrote it, so the failure is
// permanent and looks like the application itself is broken.
//
// That happened. So the file is inspected before the editor is allowed near
// it, and a nonsensical one is discarded instead of loaded. Losing the graph's
// pan and zoom is a trivial cost; an application that will not start is not.
bool graph_view_is_sane(const std::string &text) {
  if (text.empty()) return true; // nothing saved yet is perfectly fine
  try {
    nlohmann::json j = nlohmann::json::parse(text);
    // A usable zoom sits around 1. Far outside that is a collapsed view, and
    // it is what makes the canvas enormous.
    if (j.contains("view") && j["view"].contains("zoom")) {
      double z = j["view"]["zoom"].get<double>();
      if (!std::isfinite(z) || z < 1e-3 || z > 1e3) return false;
    }
    // A NaN position that has been through an int cast comes back as INT_MIN,
    // which is how this showed up in practice.
    if (j.contains("nodes"))
      for (auto &[key, n] : j["nodes"].items()) {
        (void)key;
        if (!n.contains("location")) continue;
        for (const char *ax : {"x", "y"}) {
          if (!n["location"].contains(ax)) continue;
          double v = n["location"][ax].get<double>();
          if (!std::isfinite(v) || std::fabs(v) > 1e7) return false;
        }
      }
  } catch (const std::exception &) {
    return false; // unparseable is just as unusable
  }
  return true;
}

void discard_insane_graph_view(const std::string &path) {
  std::ifstream f(path);
  if (!f) return;
  std::string text((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
  f.close();
  if (graph_view_is_sane(text)) return;
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

} // namespace studio

