#include "scene.hpp"
#include "gpx/node_graph.hpp"
#include "render_settings.hpp"
#include "world_shape.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <cstdlib>
#include <sstream>

namespace studio {

SceneState &scene() {
  static SceneState s;
  return s;
}

std::array<float, 3> scene_display_color(const SceneObject &o) {
  const SceneState &s = scene();
  if (s.color_by_layer && o.layer >= 0 && o.layer < (int)s.layers.size()) {
    const float *c = s.layers[(size_t)o.layer].color;
    return {c[0] * 1.5f, c[1] * 1.5f, c[2] * 1.5f};
  }
  return {o.color[0], o.color[1], o.color[2]};
}

void scene_init_builtins() {
  SceneState &s = scene();
  if (!s.objects.empty()) return;
  auto add = [&](SceneObject::Type t, const char *name) {
    SceneObject o;
    o.type = t;
    o.name = name;
    o.builtin = true;
    s.objects.push_back(o);
  };
  // the world first, so everything on it can be its child
  {
    SceneObject h;
    h.type = SceneObject::Planet;
    h.name = "Home planet";
    h.builtin = true;
    h.planet.home = true;
    h.planet.radius = render_settings().planet_radius;
    h.pos[0] = h.pos[1] = h.pos[2] = 0.f;
    s.objects.push_back(h);
  }
  add(SceneObject::Terrain, "Terrain");
  {
    // the tile's transform starts at identity: no offset (its centre is the
    // world's), no turn, its natural size (terrain_xform.hpp)
    SceneObject &t = s.objects.back();
    t.pos[0] = t.pos[1] = t.pos[2] = 0.f;
    t.scale = 1.f;
  }
  add(SceneObject::Water, "Water");
  add(SceneObject::Sun, "Sun");
  add(SceneObject::Atmosphere, "Atmosphere");
  add(SceneObject::Group, "Cameras"); // parent for every camera
  scene_add_camera("Camera 1");
  // the terrain, the water and the atmosphere belong to the world; the sun
  // and the cameras are independent
  for (SceneObject &o : s.objects)
    if (o.type == SceneObject::Terrain || o.type == SceneObject::Water ||
        o.type == SceneObject::Atmosphere)
      o.parent = 0;

  // Terragen's central premise, and the thing we had all the machinery for but
  // never switched on: "every scene is built in the context of an entire
  // planet… Having the planet as a background ensures that the scene doesn't
  // just end, nor does it just go on forever as it would with an infinite
  // plane. Instead it always curves realistically down to the horizon."
  // (Terragen 2 guide, p5.)
  //
  // So a new scene starts on a planet: the tile continues past its own edges
  // to a curved horizon, and the ground is the planet's own surface - the
  // realistic layer: eroded ridges, hills, plateaus, valleys and lowland
  // lakes, with the sea at the water level. The tile is placed onto it
  // (studio/planet_place.cpp): flat where the graph says nothing, so the
  // landscape shows through, and levelled under whatever the graph builds.
  int gnd = scene_add_infinite_surface(-1, "Planet surface");
  s.objects.back().parent = 0; // the world's ground is the home planet's child
  if (gnd >= 0 && gnd < (int)s.objects.size()) {
    InfiniteSurfaceData &d = s.objects[gnd].surf;
    d.layer.type = 3;
    d.layer.amplitude = 1.f;
    d.layer.frequency = 1.5f;
    d.layer.octaves = 12;
    d.layer.coverage = 1.f;
    d.height_scale = 1.f;
  }
}

static int g_active_camera = -1;    // -1 = free viewport camera
static int g_last_used_camera = -1;

int &scene_active_camera() { return g_active_camera; }
int &scene_last_used_camera() { return g_last_used_camera; }

int scene_cameras_group() {
  SceneState &s = scene();
  for (int i = 0; i < (int)s.objects.size(); ++i)
    if (s.objects[i].type == SceneObject::Group && s.objects[i].name == "Cameras")
      return i;
  SceneObject g;
  g.type = SceneObject::Group;
  g.name = "Cameras";
  g.builtin = true;
  s.objects.push_back(g);
  return (int)s.objects.size() - 1;
}

std::vector<int> scene_camera_indices() {
  SceneState &s = scene();
  std::vector<int> out;
  for (int i = 0; i < (int)s.objects.size(); ++i)
    if (s.objects[i].type == SceneObject::Camera) out.push_back(i);
  return out;
}

// ---- planets & infinite surfaces ------------------------------------------
std::vector<int> scene_planet_indices() {
  SceneState &s = scene();
  std::vector<int> out;
  for (int i = 0; i < (int)s.objects.size(); ++i)
    if (s.objects[i].type == SceneObject::Planet && !s.objects[i].planet.home) out.push_back(i);
  return out;
}

int scene_home_planet() {
  SceneState &s = scene();
  for (int i = 0; i < (int)s.objects.size(); ++i)
    if (s.objects[i].type == SceneObject::Planet && s.objects[i].planet.home) return i;
  return -1;
}

int scene_planet_of(int object) {
  SceneState &s = scene();
  int guard = 0;
  while (object >= 0 && object < (int)s.objects.size() && guard++ < 64) {
    if (s.objects[(size_t)object].type == SceneObject::Planet) return object;
    object = s.objects[(size_t)object].parent;
  }
  return -1;
}

int scene_delete_subtree(int object) {
  SceneState &s = scene();
  if (object < 0 || object >= (int)s.objects.size()) return 0;
  std::vector<int> del;
  for (int i = 0; i < (int)s.objects.size(); ++i)
    if (scene_is_descendant(s, i, object)) del.push_back(i);
  std::sort(del.rbegin(), del.rend());
  for (int d : del) {
    auto fix = [&](int &v) {
      if (v > d) v--;
      else if (v == d) v = -1;
    };
    for (auto &o : s.objects) {
      if (o.parent > d) o.parent--;
      else if (o.parent == d) o.parent = -1;
    }
    fix(scene_active_camera());
    fix(scene_last_used_camera());
    s.objects.erase(s.objects.begin() + d);
  }
  if (s.selected >= (int)s.objects.size()) s.selected = s.objects.empty() ? -1 : 0;
  s.selection = {s.selected};
  return (int)del.size();
}

SceneDeleteResult scene_delete_with_drivers(SceneState &sc, gpx::Graph *graph,
                                            const std::vector<int> &objects, bool keep_builtin) {
  SceneDeleteResult out;
  std::vector<int> want;
  for (int w : objects) {
    if (w < 0 || w >= (int)sc.objects.size()) continue;
    if (keep_builtin && sc.objects[(size_t)w].builtin) {
      ++out.kept_builtin;
      continue;
    }
    want.push_back(w);
  }
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    for (int w : want)
      if (scene_is_descendant(sc, i, w)) {
        out.objects.push_back(i);
        break;
      }
  if (out.objects.empty()) return out;
  // the nodes that would put them back, found before the indices move
  static const char *const REBUILDERS[] = {"Primitive", "ImportObject", "PlantSpecies", "LightSource", "SceneCamera",
                                           "Planet",    "Nebula",       "InfiniteTerrain"};
  if (graph)
    for (int d : out.objects)
      if (const uint64_t id = sc.objects[(size_t)d].driver_node)
        if (const gpx::Node *n = graph->find_node(id))
          for (const char *t : REBUILDERS)
            if (n->type == t && std::find(out.nodes.begin(), out.nodes.end(), id) == out.nodes.end()) {
              out.nodes.push_back(id);
              break;
            }
  std::sort(out.objects.rbegin(), out.objects.rend());
  for (int d : out.objects) {
    auto fix = [&](int &v) {
      if (v > d) v--;
      else if (v == d) v = -1;
    };
    for (auto &o : sc.objects) {
      if (o.parent > d) o.parent--;
      else if (o.parent == d) o.parent = -1;
    }
    fix(scene_active_camera());
    fix(scene_last_used_camera());
    sc.objects.erase(sc.objects.begin() + d);
  }
  for (uint64_t id : out.nodes) graph->remove_node(id);
  // Nothing is selected: the index that was selected now names whatever
  // slid into its place, and a second Delete would have taken that too.
  sc.selected = -1;
  sc.selection.clear();
  return out;
}

void scene_ensure_home_planet() {
  SceneState &s = scene();
  if (scene_home_planet() >= 0) return;
  // only a scene with a world in it - a terrain, water, an atmosphere or a
  // surface layer standing at the root, which every project saved before
  // the home planet had; a scene of meshes and cameras alone (a test's, an
  // import's) keeps its count
  bool world = false;
  for (const SceneObject &o : s.objects)
    if (o.parent == -1 && (o.type == SceneObject::Terrain || o.type == SceneObject::Water ||
                           o.type == SceneObject::Atmosphere || o.type == SceneObject::InfiniteSurface))
      world = true;
  if (!world) return;
  SceneObject h;
  h.type = SceneObject::Planet;
  h.name = "Home planet";
  h.builtin = true;
  h.planet.home = true;
  h.planet.radius = render_settings().planet_radius;
  h.pos[0] = h.pos[1] = h.pos[2] = 0.f;
  s.objects.push_back(h);
  const int home = (int)s.objects.size() - 1;
  // the world's pieces that stood at the root move under it: terrain tiles,
  // the water, the atmosphere and the surface layers (the ones that were the
  // home ground by standing at the root)
  for (int i = 0; i < home; ++i) {
    SceneObject &o = s.objects[(size_t)i];
    if (o.parent != -1) continue;
    if (o.type == SceneObject::Terrain || o.type == SceneObject::Water ||
        o.type == SceneObject::Atmosphere || o.type == SceneObject::InfiniteSurface)
      o.parent = home;
  }
}

int scene_add_planet(const std::string &name) {
  SceneState &s = scene();
  SceneObject o;
  o.type = SceneObject::Planet;
  int count = (int)scene_planet_indices().size();
  o.name = name.empty() ? "Planet " + std::to_string(count + 1) : name;
  // spread new planets on a loose spiral in the sky so they never overlap;
  // the user repositions them from the properties
  float ang = 0.9f + count * 2.4f; // golden-ish steps
  float dist = 14.f + count * 9.f;
  o.planet.radius = 2.f + (count % 4) * 1.3f;
  o.planet.seed = 1u + (uint32_t)count * 7919u;
  o.pos[0] = 0.5f + std::cos(ang) * dist;
  o.pos[1] = 3.5f + (count % 3) * 3.f;
  o.pos[2] = 0.5f + std::sin(ang) * dist;
  s.objects.push_back(o);
  int idx = (int)s.objects.size() - 1;
  // every planet starts with one ridged infinite terrain so it has relief
  scene_add_infinite_surface(idx);
  return idx;
}

int view_active_camera() { return scene_active_camera(); }

int scene_add_moon(const std::string &name) {
  SceneState &s = scene();
  int count = 0;
  for (const SceneObject &o : s.objects)
    if (o.type == SceneObject::Planet && o.planet.atmo_density <= 0.f && o.planet.sea_level <= 0.f) ++count;
  const int idx = scene_add_planet(name.empty() ? "Moon " + std::to_string(count + 1) : name);
  if (idx < 0) return idx;
  SceneObject &o = s.objects[(size_t)idx];
  PlanetData &P = o.planet;
  P.radius = 1.2f + (count % 3) * 0.5f;
  P.relief = 0.012f;
  P.sea_level = 0.f;
  P.snow_line = 1.f;
  P.atmo_density = 0.f;
  P.clouds = 0.f; // no air, no weather
  const float lo[3] = {0.36f, 0.35f, 0.34f}, hi[3] = {0.62f, 0.61f, 0.59f};
  for (int k = 0; k < 3; ++k) { P.rock_low[k] = lo[k]; P.rock_high[k] = hi[k]; }
  // its one surface layer is craters (gpx::planet::Layer type 4)
  for (SceneObject &c : s.objects)
    if (c.type == SceneObject::InfiniteSurface && c.parent == idx) {
      c.surf.layer.type = 4;
      c.surf.layer.frequency = 3.f;
      c.surf.layer.amplitude = 1.f;
      c.name = "Craters";
    }
  return idx;
}

int scene_add_nebula(const std::string &name, int type) {
  SceneState &s = scene();
  SceneObject o;
  o.type = SceneObject::Nebula;
  const int count = (int)scene_nebula_indices().size();
  NebulaData &N = o.nebula;
  N.type = type < 0 ? 0 : (type > 4 ? 4 : type);
  static const char *const names[5] = {"Nebula", "Dark nebula", "Galaxy", "Elliptical galaxy",
                                        "Planetary nebula"};
  o.name = name.empty() ? std::string(names[N.type]) + " " + std::to_string(count + 1) : name;
  // round the sky in golden steps, well above the horizon
  N.azimuth = std::fmod(40.f + count * 137.5f, 360.f);
  N.elevation = 25.f + (count % 4) * 12.f;
  N.seed = 1u + (uint32_t)count * 7919u;
  if (N.type == 2 || N.type == 3) N.size_deg = 12.f;
  if (N.type == 4) N.size_deg = 6.f;
  if (N.type == 2) { N.color1[0] = 1.f; N.color1[1] = 0.88f; N.color1[2] = 0.72f;
                     N.color2[0] = 0.6f; N.color2[1] = 0.72f; N.color2[2] = 1.f; }
  if (N.type == 3) { N.color1[0] = 1.f; N.color1[1] = 0.92f; N.color1[2] = 0.8f;
                     N.color2[0] = 0.85f; N.color2[1] = 0.8f; N.color2[2] = 0.75f; }
  if (N.type == 4) { N.color1[0] = 0.4f; N.color1[1] = 0.95f; N.color1[2] = 0.7f;
                     N.color2[0] = 0.95f; N.color2[1] = 0.45f; N.color2[2] = 0.35f; }
  s.objects.push_back(o);
  return (int)s.objects.size() - 1;
}

std::vector<int> scene_nebula_indices() {
  std::vector<int> out;
  const SceneState &s = scene();
  for (int i = 0; i < (int)s.objects.size(); ++i)
    if (s.objects[(size_t)i].type == SceneObject::Nebula) out.push_back(i);
  return out;
}

int scene_add_infinite_surface(int parent, const std::string &name) {
  SceneState &s = scene();
  SceneObject o;
  o.type = SceneObject::InfiniteSurface;
  o.parent = (parent >= 0 && parent < (int)s.objects.size()) ? parent : -1;
  int siblings = (int)scene_surface_layers(o.parent).size();
  o.name = name.empty()
               ? (o.parent < 0 ? "Infinite terrain " : "Surface layer ") +
                     std::to_string(siblings + 1)
               : name;
  o.surf.layer.seed = 1u + (uint32_t)(s.objects.size() * 31 + siblings * 7);
  o.surf.layer.type = siblings == 0 ? 3 : (siblings % 3); // vary the stack
  o.surf.layer.frequency = 3.f + siblings * 2.5f;
  o.surf.layer.amplitude = siblings == 0 ? 1.f : 0.5f;
  o.surf.layer.coverage = siblings == 0 ? 1.f : 0.6f;
  s.objects.push_back(o);
  return (int)s.objects.size() - 1;
}

std::vector<int> scene_surface_layers(int planet_idx) {
  SceneState &s = scene();
  std::vector<int> out;
  for (int i = 0; i < (int)s.objects.size(); ++i) {
    const SceneObject &o = s.objects[i];
    if (o.type != SceneObject::InfiniteSurface) continue;
    bool root = o.parent < 0 || o.parent >= (int)s.objects.size() ||
                s.objects[o.parent].type != SceneObject::Planet;
    // the home planet's surfaces are the world's ground: asked for by its
    // index, or by -1 the way the root-level ones always were
    if (planet_idx < 0 && !root && s.objects[o.parent].planet.home) root = true;
    if ((planet_idx < 0 && root) || (planet_idx >= 0 && o.parent == planet_idx))
      if (s.object_visible(o)) out.push_back(i);
  }
  return out;
}

std::vector<gpx::planet::Layer> planet_home_layers(int side) {
  std::vector<gpx::planet::Layer> out;
  SceneState &sc = scene();
  const RenderSettings &rsw = render_settings();
  // the surface layers under the home planet; a scene with no home planet
  // (none should remain) reads the root-level ones as it used to
  // (-1 lists the root-level surfaces and the home planet's children alike;
  // an older project's surfaces stand at the root until scene_ensure_home_planet)
  for (int idx : scene_surface_layers(-1)) {
    if ((int)out.size() >= gpx::planet::MAX_LAYERS) break;
    if (side != 0 && object_side(rsw, sc.objects[idx]) != side) continue;
    gpx::planet::Layer L = sc.objects[idx].surf.layer;
    L.amplitude *= sc.objects[idx].surf.height_scale;
    out.push_back(L);
  }
  return out;
}

int scene_add_camera(const std::string &name) {
  SceneState &s = scene();
  int group = scene_cameras_group();
  SceneObject c;
  c.type = SceneObject::Camera;
  c.parent = group;
  // a new camera inherits everything from the last used camera
  if (g_last_used_camera >= 0 && g_last_used_camera < (int)s.objects.size() &&
      s.objects[g_last_used_camera].type == SceneObject::Camera) {
    c.cam = s.objects[g_last_used_camera].cam;
    // offset a little so the copy is visibly a new camera
    c.cam.eye[0] += 0.15f;
  }
  if (name.empty()) {
    int n = 1;
    for (const auto &o : s.objects)
      if (o.type == SceneObject::Camera) ++n;
    c.name = "Camera " + std::to_string(n);
  } else {
    c.name = name;
  }
  s.objects.push_back(std::move(c));
  int idx = (int)s.objects.size() - 1;
  g_last_used_camera = idx;
  return idx;
}

// The OBJ loader itself, separated from object creation so a saved scene can
// refill a reconstructed mesh from its recorded path. Fills `verts` with
// interleaved position(3) + flat normal(3), normalised into a unit box.
bool scene_load_obj_verts(const std::string &path, std::vector<float> &verts,
                          std::string &err) {
  verts.clear();
  std::ifstream f(path);
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  std::vector<float> pos;
  std::vector<unsigned> idx;
  std::string line;
  while (std::getline(f, line)) {
    if (line.size() < 2) continue;
    if (line[0] == 'v' && line[1] == ' ') {
      float x, y, z;
      if (sscanf(line.c_str() + 2, "%f %f %f", &x, &y, &z) == 3) {
        pos.push_back(x);
        pos.push_back(y);
        pos.push_back(z);
      }
    } else if (line[0] == 'f' && line[1] == ' ') {
      // faces may be "f a b c d" with formats a, a/t, a/t/n, a//n
      //
      // An OBJ is an untrusted file, and the index it names is used directly
      // as &pos[i*3] below. This used to be sscanf("%u") with no upper bound
      // and no handling of OBJ's negative (relative) indices, so "f -1 -2 -3"
      // parsed as 4294967295 and "f 99999" in a ten-vertex file read whatever
      // was past the end of the heap. Both are now rejected before they can
      // reach the vertex array.
      std::istringstream ss(line.substr(2));
      std::vector<unsigned> face;
      std::string tok;
      const long long nverts = (long long)(pos.size() / 3);
      bool bad_face = false;
      while (ss >> tok) {
        char *endp = nullptr;
        long long v = std::strtoll(tok.c_str(), &endp, 10);
        if (endp == tok.c_str()) { bad_face = true; break; } // no digits at all
        // OBJ indices are 1-based; negative counts back from the last vertex
        // defined so far, which is why this is resolved here and not later.
        if (v < 0) v = nverts + v; else v -= 1;
        if (v < 0 || v >= nverts) { bad_face = true; break; }
        face.push_back((unsigned)v);
      }
      if (bad_face) {
        err = "OBJ face references a vertex that does not exist (line: " +
              line.substr(0, 64) + ")";
        return false;
      }
      for (size_t k = 2; k < face.size(); ++k) { // fan-triangulate
        idx.push_back(face[0]);
        idx.push_back(face[k - 1]);
        idx.push_back(face[k]);
      }
    }
  }
  if (pos.empty() || idx.empty()) {
    err = "no geometry found in OBJ";
    return false;
  }
  // normalize into unit box centered at origin (placed via object transform)
  float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
  for (size_t i = 0; i < pos.size(); i += 3)
    for (int k = 0; k < 3; ++k) {
      mn[k] = std::fmin(mn[k], pos[i + k]);
      mx[k] = std::fmax(mx[k], pos[i + k]);
    }
  float ext = std::fmax(std::fmax(mx[0] - mn[0], mx[1] - mn[1]), mx[2] - mn[2]);
  if (ext < 1e-9f) ext = 1.f;
  for (size_t i = 0; i < pos.size(); i += 3) {
    pos[i] = (pos[i] - (mn[0] + mx[0]) * 0.5f) / ext;
    pos[i + 1] = (pos[i + 1] - mn[1]) / ext; // rest on its base
    pos[i + 2] = (pos[i + 2] - (mn[2] + mx[2]) * 0.5f) / ext;
  }

  // flat-shaded triangles: expand the index fan into interleaved
  // position(3) + normal(3), one normal per face
  verts.reserve(idx.size() * 6);
  for (size_t t = 0; t + 2 < idx.size(); t += 3) {
    const float *a = &pos[idx[t] * 3];
    const float *b = &pos[idx[t + 1] * 3];
    const float *c = &pos[idx[t + 2] * 3];
    float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz,
          nz = ux * vy - uy * vx;
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-12f) len = 1;
    nx /= len;
    ny /= len;
    nz /= len;
    for (const float *q : {a, b, c})
      verts.insert(verts.end(), {q[0], q[1], q[2], nx, ny, nz});
  }

  return true;
}

// Any model format goes through mesh_object.cpp now; the name is kept for
// the callers that predate FBX and glTF.
int scene_import_mesh(const std::string &path, std::string &err);
int scene_import_obj(const std::string &path, std::string &err) {
  return scene_import_mesh(path, err);
}

int scene_import_obj_legacy(const std::string &path, std::string &err) {
  std::vector<float> verts;
  if (!scene_load_obj_verts(path, verts, err)) return -1;
  SceneObject o;
  o.type = SceneObject::Mesh;
  o.path = path;
  size_t slash = path.find_last_of("/\\");
  o.name = slash == std::string::npos ? path : path.substr(slash + 1);
  o.vert_count = (int)(verts.size() / 6);
  o.verts = std::move(verts);
  o.gpu_dirty = true;
  scene().objects.push_back(std::move(o));
  return (int)scene().objects.size() - 1;
}

// ------------------------------------------------------------- transforms
// M = T * Ry(heading) * Rx(pitch) * Rz(bank) * S, column-major for OpenGL.
// The normal matrix is R * S^-1, which is the inverse transpose of R*S given
// that R is orthonormal - so squeezing an object no longer tilts its shading.
void scene_object_bounds(SceneObject &o) {
  if (o.vert_count <= 0 || o.verts.size() < 6) {
    for (int i = 0; i < 3; ++i) { o.bmin[i] = -0.5f; o.bmax[i] = 0.5f; }
    return;
  }
  for (int i = 0; i < 3; ++i) { o.bmin[i] = 1e30f; o.bmax[i] = -1e30f; }
  for (int v = 0; v < o.vert_count; ++v)
    for (int i = 0; i < 3; ++i) {
      float x = o.verts[(size_t)v * 6 + i];
      o.bmin[i] = std::min(o.bmin[i], x);
      o.bmax[i] = std::max(o.bmax[i], x);
    }
  for (int i = 0; i < 3; ++i)
    if (o.bmax[i] - o.bmin[i] < 1e-6f) { o.bmin[i] -= 0.5f; o.bmax[i] += 0.5f; }
}

void scene_object_matrix(const SceneObject &o, float height_scale, float *m16,
                         float *n9) {
  const float D2R = 0.017453292519943295f;
  float ch = std::cos(o.yaw * D2R), sh = std::sin(o.yaw * D2R);
  float cp = std::cos(o.pitch * D2R), sp = std::sin(o.pitch * D2R);
  float cb = std::cos(o.roll * D2R), sb = std::sin(o.roll * D2R);
  // R = Ry * Rx * Rz, rows written out
  float r[9] = {
      ch * cb + sh * sp * sb, -ch * sb + sh * sp * cb, sh * cp,
      cp * sb,                 cp * cb,                -sp,
      -sh * cb + ch * sp * sb, sh * sb + ch * sp * cb, ch * cp};
  float sx = o.scale * o.scl[0], sy = o.scale * o.scl[1],
        sz = o.scale * o.scl[2];
  const float s3[3] = {sx, sy, sz};
  for (int c = 0; c < 3; ++c)
    for (int rr = 0; rr < 3; ++rr) m16[c * 4 + rr] = r[rr * 3 + c] * s3[c];
  m16[3] = m16[7] = m16[11] = 0.f;
  m16[12] = o.pos[0];
  m16[13] = o.pos[1] * height_scale;
  m16[14] = o.pos[2];
  m16[15] = 1.f;
  if (n9) {
    for (int c = 0; c < 3; ++c) {
      float inv = std::fabs(s3[c]) > 1e-9f ? 1.f / s3[c] : 0.f;
      for (int rr = 0; rr < 3; ++rr) n9[c * 3 + rr] = r[rr * 3 + c] * inv;
    }
  }
}

float scene_object_radius(const SceneObject &o) {
  float m = std::fabs(o.scl[0]);
  m = std::fmax(m, std::fabs(o.scl[1]));
  m = std::fmax(m, std::fabs(o.scl[2]));
  return o.scale * m;
}

} // namespace studio

// ------------------------------------------------------- tree bookkeeping
namespace studio {

void scene_layer_default_color(int index, float *rgb) {
  // a small palette that stays legible on a dark ground; Default is grey
  static const float P[][3] = {
      {0.55f, 0.55f, 0.55f}, {0.85f, 0.55f, 0.20f}, {0.35f, 0.65f, 0.85f},
      {0.45f, 0.75f, 0.40f}, {0.80f, 0.40f, 0.45f}, {0.70f, 0.55f, 0.85f},
      {0.85f, 0.80f, 0.35f}, {0.35f, 0.75f, 0.70f}};
  const int n = (int)(sizeof P / sizeof P[0]);
  int k = index < 0 ? 0 : index % n;
  for (int i = 0; i < 3; ++i) rgb[i] = P[k][i];
}

bool scene_is_descendant(const SceneState &sc, int node, int root) {
  int guard = (int)sc.objects.size() + 1;
  for (int i = node; i >= 0 && i < (int)sc.objects.size() && guard-- > 0;
       i = sc.objects[i].parent)
    if (i == root) return true;
  return false;
}

// Walks up from `o`: the first non-grey ancestor (or the object itself)
// decides; all grey up to the root means visible.
static bool three_state_visible(const SceneState &sc, const SceneObject &o,
                                bool render) {
  const SceneObject *cur = &o;
  int guard = (int)sc.objects.size() + 1;
  while (cur && guard-- > 0) {
    int st = render ? cur->vis_render : cur->vis_viewport;
    if (st == 1) return true;
    if (st == 2) return false;
    cur = cur->parent >= 0 && cur->parent < (int)sc.objects.size()
              ? &sc.objects[cur->parent]
              : nullptr;
  }
  return true;
}

bool SceneState::object_visible(const SceneObject &o) const {
  bool lv = o.layer >= 0 && o.layer < (int)layers.size()
                ? layers[o.layer].visible
                : true;
  return o.visible && o.enabled && lv && three_state_visible(*this, o, false);
}

bool SceneState::object_render_visible(const SceneObject &o) const {
  bool lv = o.layer >= 0 && o.layer < (int)layers.size()
                ? layers[o.layer].visible
                : true;
  return o.visible && o.enabled && lv && three_state_visible(*this, o, true);
}

int scene_move_object(int from, int to, int new_parent) {
  SceneState &sc = scene();
  const int n = (int)sc.objects.size();
  if (from < 0 || from >= n) return -1;
  if (to < 0) to = 0;
  if (to >= n) to = n - 1;
  if (new_parent >= n) return -1;
  if (new_parent >= 0 && scene_is_descendant(sc, new_parent, from)) return -1;

  // the new order, as a list of old indices
  std::vector<int> order;
  order.reserve(n);
  for (int i = 0; i < n; ++i)
    if (i != from) order.push_back(i);
  order.insert(order.begin() + to, from);
  std::vector<int> map(n); // old index -> new index
  for (int i = 0; i < n; ++i) map[order[i]] = i;

  std::vector<SceneObject> next;
  next.reserve(n);
  for (int i = 0; i < n; ++i) next.push_back(std::move(sc.objects[order[i]]));
  sc.objects = std::move(next);

  auto fix = [&](int &v) {
    if (v >= 0 && v < n) v = map[v];
  };
  for (SceneObject &o : sc.objects) fix(o.parent);
  sc.objects[to].parent = new_parent >= 0 ? map[new_parent] : -1;
  fix(sc.selected);
  for (int &s : sc.selection) fix(s);
  fix(scene_active_camera());
  fix(scene_last_used_camera());
  return to;
}

} // namespace studio
