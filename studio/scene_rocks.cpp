// Geekatplay TerraForge - a Rock node drives a stone in the scene.
//
// The same arrangement a PlantSpecies has: the node is the truth, the object
// is the handle, and editing either moves the other. The mesh is rebuilt only
// when the recipe changes, because rebuilding a five-thousand-face rock every
// evaluation would cost more than drawing it.
//
// Rocks land in the Rocks folder of whatever terrain they belong to
// (scene.hpp: scene_rocks_group), so a scene with two tiles keeps two sets of
// stone and moving the folder moves the lot.
#include "app.hpp"
#include "gpx/rock.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "terrain_relief.hpp"
#include <cmath>
#include <map>
#include <string>

namespace studio {

namespace {

// Everything the mesh depends on, as one number. A transform is not in it:
// moving a stone does not rebuild it.
uint64_t recipe_hash(const gpx::AttrSet &at) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](uint64_t v) {
    h ^= v;
    h *= 1099511628211ull;
  };
  auto mixf = [&](float f) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(f), "float is four bytes");
    __builtin_memcpy(&bits, &f, sizeof bits);
    mix(bits);
  };
  mix((uint64_t)at.get_choice("type"));
  mix((uint64_t)at.get_seed("seed"));
  mix((uint64_t)at.get_i("detail", 3));
  mixf(at.get_f("size_m", 1.f));
  mixf(at.get_f("fracture", 1.f));
  mixf(at.get_f("roundness", 1.f));
  mixf(at.get_f("weathering", 1.f));
  mixf(at.get_f("flatness", 1.f));
  mixf(at.get_f("elongation", 1.f));
  return h;
}

int find_driven_rock(const gpx::Node &n) {
  SceneState &sc = scene();
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    if (sc.objects[(size_t)i].driver_node == n.id && sc.objects[(size_t)i].type == SceneObject::Mesh)
      return i;
  return -1;
}

} // namespace

void apply_rock(App &a, gpx::Node &n, float size_m) {
  const gpx::AttrSet &at = n.attrs;
  std::string name = at.get_s("object");
  if (name.empty()) name = std::string(gpx::rock_type_name((gpx::RockType)at.get_choice("type"))) + " rock";

  int idx = find_driven_rock(n);
  if (idx < 0) {
    // the folder before the object: making it can move the objects vector
    const int grp = scene_rocks_group(scene_terrain_for_population());
    SceneObject o;
    o.type = SceneObject::Mesh;
    o.parent = grp;
    o.name = name;
    o.path = "rock:" + std::to_string(n.id);
    o.driver_node = n.id;
    scene().objects.push_back(std::move(o));
    idx = (int)scene().objects.size() - 1;
    a.scene_selection_serial++;
  }
  SceneObject &o = scene().objects[(size_t)idx];
  if (!at.get_s("object").empty() || o.name.empty()) o.name = name;

  // ---- the mesh, when the recipe moved ------------------------------------
  static std::map<uint64_t, uint64_t> built; // node id -> recipe hash
  const uint64_t want = recipe_hash(at);
  if (built[n.id] != want || o.verts.empty()) {
    built[n.id] = want;
    gpx::RockParams p;
    p.type = (gpx::RockType)std::min(at.get_choice("type"), (int)gpx::RockType::Count - 1);
    p.seed = at.get_seed("seed");
    p.size_m = at.get_f("size_m", 1.f);
    p.detail = at.get_i("detail", 3);
    p.fracture = at.get_f("fracture", 1.f);
    p.roundness = at.get_f("roundness", 1.f);
    p.weathering = at.get_f("weathering", 1.f);
    p.flatness = at.get_f("flatness", 1.f);
    p.elongation = at.get_f("elongation", 1.f);
    gpx::RockMesh m;
    gpx::rock_build(p, m);

    // Into the object's own frame, which is tile units: the rock is built in
    // metres because that is how anyone thinks about a stone.
    const float inv = 1.f / std::max(size_m, 1.f);
    o.verts.clear();
    o.uvs.clear();
    o.wind.clear();
    o.tint.clear();
    o.parts.clear();
    o.verts.reserve(m.idx.size() * 6);
    for (uint32_t i : m.idx) {
      o.verts.insert(o.verts.end(), {m.pos[i * 3] * inv, m.pos[i * 3 + 1] * inv,
                                     m.pos[i * 3 + 2] * inv, m.nrm[i * 3],
                                     m.nrm[i * 3 + 1], m.nrm[i * 3 + 2]});
      o.uvs.insert(o.uvs.end(), {m.uv[i * 2], m.uv[i * 2 + 1]});
      // a rock does not sway, but the streams have to be the same length as
      // the positions or the vertex arrays disagree about how many there are
      o.wind.insert(o.wind.end(), {0.f, 0.f, 0.f, 0.f});
      o.tint.insert(o.tint.end(), {1.f, 1.f, 1.f, 1.f});
    }
    o.vert_count = (int)(o.verts.size() / 6);
    for (int k = 0; k < 3; ++k) {
      o.bmin[k] = m.bmin[k] * inv;
      o.bmax[k] = m.bmax[k] * inv;
    }
    o.plant = false;
    o.gpu_dirty = true;
    o.lod_verts[0].clear();
    o.lod_verts[1].clear();
    o.lod_tried = false;
  }

  // ---- where it stands ----------------------------------------------------
  const float inv = 1.f / std::max(size_m, 1.f);
  o.pos[0] = at.get_f("x_m", 0.f) * inv;
  o.pos[2] = at.get_f("z_m", 0.f) * inv;
  if (at.get_b("sit", true)) {
    // Onto the ground the viewport actually draws, relief and all, so a stone
    // sits ON the hillside rather than through it.
    //
    // An object's height is kept in the HEIGHTMAP's units and the tile's
    // height scale is applied when it is drawn (scene_object_matrix); the
    // ground answers in world units. Taken as it came, every rock sat a
    // hundred metres under the hill it was meant to be on - the same trap the
    // plants fell into, with the same fix.
    const float hs = std::max(render_settings().height_scale, 1e-6f);
    o.pos[1] = renderer_ground_under(o.pos[0], o.pos[2], RELIEF_NEAR_OCTAVES) / hs;
  } else {
    o.pos[1] = at.get_f("y_m", 0.f) * inv / std::max(render_settings().height_scale, 1e-6f);
  }
  o.yaw = at.get_f("heading", 0.f);
  o.scale = 1.f; // the mesh is already the size it was asked for
  if (const gpx::Attribute *c = at.find("color"))
    for (int k = 0; k < 3; ++k) o.color[k] = c->col[k];
}

} // namespace studio
