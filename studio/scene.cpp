#include "scene.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace studio {

SceneState &scene() {
  static SceneState s;
  return s;
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
  add(SceneObject::Terrain, "Terrain");
  add(SceneObject::Water, "Water");
  add(SceneObject::Sun, "Sun");
  add(SceneObject::Atmosphere, "Atmosphere");
  add(SceneObject::Group, "Cameras"); // parent for every camera
  scene_add_camera("Camera 1");
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

int scene_import_obj(const std::string &path, std::string &err) {
  std::ifstream f(path);
  if (!f) {
    err = "cannot open " + path;
    return -1;
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
      std::istringstream ss(line.substr(2));
      std::vector<unsigned> face;
      std::string tok;
      while (ss >> tok) {
        unsigned vi = 0;
        sscanf(tok.c_str(), "%u", &vi);
        if (vi > 0) face.push_back(vi - 1);
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
    return -1;
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

  SceneObject o;
  o.type = SceneObject::Mesh;
  o.path = path;
  size_t slash = path.find_last_of("/\\");
  o.name = slash == std::string::npos ? path : path.substr(slash + 1);
  o.verts.reserve(idx.size() * 6);
  for (size_t t = 0; t + 2 < idx.size(); t += 3) {
    const float *a = &pos[idx[t] * 3];
    const float *b = &pos[idx[t + 1] * 3];
    const float *c = &pos[idx[t + 2] * 3];
    // flat normal
    float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-12f) len = 1;
    nx /= len; ny /= len; nz /= len;
    for (const float *p : {a, b, c}) {
      o.verts.insert(o.verts.end(), {p[0], p[1], p[2], nx, ny, nz});
    }
  }
  o.vert_count = (int)(o.verts.size() / 6);
  o.gpu_dirty = true;
  scene().objects.push_back(std::move(o));
  return (int)scene().objects.size() - 1;
}

} // namespace studio
