// Geekatplay TerraForge — applies the object-family nodes to the scene.
//
// LightSource, SceneCamera, ImportObject, Primitive, Planet and
// InfiniteTerrain each describe one scene object; CameraPath and
// AnimationSequence describe the shot. After every evaluation this walks the
// graph and makes the scene agree with it (scene_nodes.cpp calls it): the
// object a node drives is found by `driver_node`, then adopted by name, and
// created when neither exists. Nothing is ever deleted here - removing the
// node leaves the object behind as an ordinary hand-edited one, so a graph
// experiment cannot destroy a scene.
//
// Lengths on the nodes are metres; the scene stores tile units
// (1 = terrain_size_m). The conversion happens here and nowhere else.
#include "app.hpp"
#include "console.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace studio {

namespace {

// The object `n` drives: bound by id first, then adopted by name and type.
int find_driven(const gpx::Node &n, SceneObject::Type type,
                const std::string &name) {
  SceneState &sc = scene();
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    if (sc.objects[i].driver_node == n.id && sc.objects[i].type == type) return i;
  if (!name.empty())
    for (int i = 0; i < (int)sc.objects.size(); ++i)
      if (sc.objects[i].type == type && sc.objects[i].name == name &&
          sc.objects[i].driver_node == 0)
        return i;
  return -1;
}

float m_to_tile(float m, float size_m) { return m / std::max(size_m, 1.f); }

void apply_transform(SceneObject &o, const gpx::AttrSet &at, float size_m) {
  o.pos[0] = m_to_tile(at.get_f("x_m", 2500.f), size_m);
  o.pos[1] = m_to_tile(at.get_f("y_m", 0.f), size_m);
  o.pos[2] = m_to_tile(at.get_f("z_m", 2500.f), size_m);
  o.scale = m_to_tile(at.get_f("size_m", 400.f), size_m);
  o.yaw = at.get_f("heading", 0.f);
  o.pitch = at.get_f("pitch", 0.f);
  o.roll = at.get_f("bank", 0.f);
  o.visible = at.get_b("visible", true);
  if (const gpx::Attribute *c = at.find("color"))
    for (int k = 0; k < 3; ++k) o.color[k] = c->col[k];
}

void apply_light(App &a, gpx::Node &n, float size_m) {
  (void)a;
  const gpx::AttrSet &at = n.attrs;
  std::string name = at.get_s("object");
  if (name.empty()) name = "Light";
  int idx = find_driven(n, SceneObject::Light, name);
  if (idx < 0) idx = scene_add_light(name);
  if (idx < 0) return;
  SceneObject &o = scene().objects[idx];
  o.driver_node = n.id;
  o.name = name;
  o.visible = at.get_b("enabled", true);
  o.light_type = at.get_choice("type");
  if (const gpx::Attribute *c = at.find("color"))
    for (int k = 0; k < 3; ++k) o.color[k] = c->col[k];
  o.light_intensity = at.get_f("intensity", 1.f);
  o.light_radius = m_to_tile(at.get_f("radius_m", 1750.f), size_m);
  o.pos[0] = m_to_tile(at.get_f("x_m", 2500.f), size_m);
  o.pos[1] = m_to_tile(at.get_f("y_m", 1500.f), size_m);
  o.pos[2] = m_to_tile(at.get_f("z_m", 2500.f), size_m);
  o.yaw = at.get_f("heading", 0.f);
  o.pitch = at.get_f("pitch", -60.f);
  o.light_cone = at.get_f("cone", 40.f);
}

void apply_camera(App &a, gpx::Node &n, float size_m) {
  const gpx::AttrSet &at = n.attrs;
  std::string name = at.get_s("object");
  if (name.empty()) name = "Camera";
  int idx = find_driven(n, SceneObject::Camera, name);
  if (idx < 0) idx = scene_add_camera(name);
  if (idx < 0) return;
  SceneObject &o = scene().objects[idx];
  o.driver_node = n.id;
  o.name = name;
  CameraData &c = o.cam;
  c.eye[0] = m_to_tile(at.get_f("eye_x_m", 2500.f), size_m);
  c.eye[1] = m_to_tile(at.get_f("eye_y_m", 2250.f), size_m);
  c.eye[2] = m_to_tile(at.get_f("eye_z_m", 8500.f), size_m);
  c.target[0] = m_to_tile(at.get_f("target_x_m", 2500.f), size_m);
  c.target[1] = m_to_tile(at.get_f("target_y_m", 500.f), size_m);
  c.target[2] = m_to_tile(at.get_f("target_z_m", 2500.f), size_m);
  c.focal_mm = at.get_f("focal_mm", 35.f);
  c.aperture = at.get_f("aperture", 8.f);
  c.shutter = 1.f / std::max(at.get_f("shutter_inv", 125.f), 0.01f);
  c.iso = at.get_f("iso", 100.f);
  c.film = at.get_i("film", 0);
  if (at.get_b("active", false) && scene_active_camera() != idx) {
    scene_active_camera() = idx;
    scene_last_used_camera() = idx;
    a.scene_selection_serial++;
  }
}

void apply_mesh(App &a, gpx::Node &n, float size_m, bool primitive) {
  (void)a;
  const gpx::AttrSet &at = n.attrs;
  static const char *KINDS[5] = {"cube", "sphere", "plane", "cylinder", "cone"};
  std::string source = primitive
                           ? std::string("primitive:") + KINDS[std::clamp(at.get_choice("kind"), 0, 4)]
                           : at.get_s("file");
  if (source.empty()) return; // an ImportObject with no file yet drives nothing
  std::string name = at.get_s("object");
  if (name.empty()) {
    if (primitive) {
      name = KINDS[std::clamp(at.get_choice("kind"), 0, 4)];
      name[0] = (char)std::toupper((unsigned char)name[0]);
    } else {
      size_t slash = source.find_last_of("/\\");
      name = slash == std::string::npos ? source : source.substr(slash + 1);
    }
  }
  int idx = find_driven(n, SceneObject::Mesh, name);
  if (idx >= 0 && scene().objects[idx].path != source) {
    // the file or shape changed: reload geometry in place, keep the object
    SceneObject &o = scene().objects[idx];
    std::vector<float> verts;
    std::string err;
    bool ok = primitive ? scene_primitive_verts(source.substr(10), verts)
                        : scene_load_obj_verts(source, verts, err);
    if (!ok) {
      n.error = err.empty() ? "could not load " + source : err;
      return;
    }
    o.path = source;
    o.verts = std::move(verts);
    o.vert_count = (int)(o.verts.size() / 6);
    o.gpu_dirty = true;
  }
  if (idx < 0) {
    std::string err;
    idx = primitive ? scene_add_primitive(source.substr(10), name)
                    : scene_import_obj(source, err);
    if (idx < 0) {
      n.error = err.empty() ? "could not load " + source : err;
      return;
    }
  }
  SceneObject &o = scene().objects[idx];
  o.driver_node = n.id;
  o.name = name;
  apply_transform(o, at, size_m);
}

void apply_planet(App &a, gpx::Node &n, float size_m) {
  (void)a;
  const gpx::AttrSet &at = n.attrs;
  std::string name = at.get_s("object");
  if (name.empty()) name = "Planet";
  int idx = find_driven(n, SceneObject::Planet, name);
  if (idx < 0) idx = scene_add_planet(name);
  if (idx < 0) return;
  SceneObject &o = scene().objects[idx];
  o.driver_node = n.id;
  o.name = name;
  PlanetData &P = o.planet;
  P.radius = m_to_tile(at.get_f("radius_m", 15000.f), size_m);
  P.relief = at.get_f("relief", 0.02f);
  P.seed = at.get_seed("seed");
  P.sea_level = at.get_f("sea_level", 0.35f);
  P.snow_line = at.get_f("snow_line", 0.75f);
  P.atmo_density = at.get_f("atmo_density", 0.6f);
  P.spin_deg = at.get_f("spin", 0.f);
  auto col = [&](const char *key, float *dst) {
    if (const gpx::Attribute *c = at.find(key))
      for (int k = 0; k < 3; ++k) dst[k] = c->col[k];
  };
  col("rock_low", P.rock_low);
  col("rock_high", P.rock_high);
  col("water_color", P.water_color);
  col("atmo_color", P.atmo_color);
  o.pos[0] = m_to_tile(at.get_f("x_m", 70000.f), size_m);
  o.pos[1] = m_to_tile(at.get_f("y_m", 17500.f), size_m);
  o.pos[2] = m_to_tile(at.get_f("z_m", 2500.f), size_m);
  o.visible = at.get_b("visible", true);
}

void apply_surface(App &a, gpx::Node &n) {
  (void)a;
  const gpx::AttrSet &at = n.attrs;
  std::string name = at.get_s("object");
  if (name.empty()) name = "Infinite terrain";
  // the parent planet, by name; empty means the home ground plane
  int parent = -1;
  std::string planet = at.get_s("planet");
  if (!planet.empty())
    for (int p : scene_planet_indices())
      if (scene().objects[p].name == planet) parent = p;
  int idx = find_driven(n, SceneObject::InfiniteSurface, name);
  if (idx < 0) idx = scene_add_infinite_surface(parent, name);
  if (idx < 0) return;
  SceneObject &o = scene().objects[idx];
  o.driver_node = n.id;
  o.name = name;
  o.parent = parent;
  gpx::planet::Layer &L = o.surf.layer;
  L.seed = at.get_seed("seed");
  L.type = at.get_choice("type");
  L.frequency = at.get_f("frequency", 3.f);
  L.amplitude = at.get_f("amplitude", 1.f);
  L.coverage = at.get_f("coverage", 1.f);
  L.mask_scale = at.get_f("mask_scale", 1.5f);
  o.surf.height_scale = at.get_f("height_scale", 1.f);
  o.visible = at.get_b("visible", true);
}

void apply_sequence(App &a, gpx::Node &n) {
  const gpx::AttrSet &at = n.attrs;
  a.anim_start = at.get_f("start", 0.f);
  a.anim_end = std::max(at.get_f("end", 10.f), a.anim_start + 0.01f);
  a.seq_fps = at.get_f("fps", 30.f);
  a.seq_w = at.get_i("width", 1280);
  a.seq_h = at.get_i("height", 720);
  std::string dir = at.get_s("dir");
  if (!dir.empty()) a.seq_dir = dir;
  a.seq_sun_sweep = at.get_b("sun_sweep", false);
  a.seq_sun[0] = at.get_f("sun_from_az", 90.f);
  a.seq_sun[1] = at.get_f("sun_from_alt", 10.f);
  a.seq_sun[2] = at.get_f("sun_to_az", 270.f);
  a.seq_sun[3] = at.get_f("sun_to_alt", 10.f);
}

void apply_camera_path(App &a, gpx::Node &n, float size_m) {
  if (!n.attrs.get_b("enabled", true)) {
    if (a.seq_cam_path != 0) {
      // only clear a path this node set; a path chosen in the Timeline stays
      const gpx::Node *src = a.graph.upstream_node(n, "path");
      if (src && a.seq_cam_path == src->id) a.seq_cam_path = 0;
    }
    return;
  }
  const gpx::Node *src = a.graph.upstream_node(n, "path");
  if (!src) return;
  a.seq_cam_path = src->id;
  a.seq_cam_height = m_to_tile(n.attrs.get_f("height_m", 400.f), size_m);
}

} // namespace

void apply_object_nodes(App &a) {
  const float size_m = render_settings().terrain_size_m;
  for (auto &np : a.graph.nodes) {
    gpx::Node &n = *np;
    if (!n.enabled) continue; // a bypassed node drives nothing
    if (n.type == "LightSource") apply_light(a, n, size_m);
    else if (n.type == "SceneCamera") apply_camera(a, n, size_m);
    else if (n.type == "ImportObject") apply_mesh(a, n, size_m, false);
    else if (n.type == "Primitive") apply_mesh(a, n, size_m, true);
    else if (n.type == "Planet") apply_planet(a, n, size_m);
    else if (n.type == "InfiniteTerrain") apply_surface(a, n);
    else if (n.type == "AnimationSequence") apply_sequence(a, n);
    else if (n.type == "CameraPath") apply_camera_path(a, n, size_m);
  }
}

} // namespace studio
