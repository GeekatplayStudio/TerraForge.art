// Geekatplay TerraForge — built-in scene primitives: cube, sphere, plane,
// cylinder, cone. Each is generated as the same interleaved position+normal
// triangle soup the OBJ importer produces, sits on y=0 like an imported
// prop, and records a "primitive:<kind>" pseudo-path so a saved scene can
// regenerate it without any file on disk.
#include "scene.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace studio {

namespace {

void tri(std::vector<float> &v, const float *a, const float *b,
         const float *c) {
  float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
  float wx = c[0] - a[0], wy = c[1] - a[1], wz = c[2] - a[2];
  float nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
  float l = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (l > 1e-12f) { nx /= l; ny /= l; nz /= l; }
  for (const float *p : {a, b, c})
    v.insert(v.end(), {p[0], p[1], p[2], nx, ny, nz});
}

void quad(std::vector<float> &v, const float *a, const float *b,
          const float *c, const float *d) {
  tri(v, a, b, c);
  tri(v, a, c, d);
}

} // namespace

bool scene_primitive_verts(const std::string &kind, std::vector<float> &v) {
  v.clear();
  const int N = 24; // segments for the round ones
  if (kind == "cube") {
    float p[8][3];
    for (int i = 0; i < 8; ++i) {
      p[i][0] = (i & 1) ? 0.5f : -0.5f;
      p[i][1] = (i & 2) ? 1.f : 0.f;
      p[i][2] = (i & 4) ? 0.5f : -0.5f;
    }
    quad(v, p[0], p[1], p[3], p[2]); // -z
    quad(v, p[5], p[4], p[6], p[7]); // +z
    quad(v, p[4], p[0], p[2], p[6]); // -x
    quad(v, p[1], p[5], p[7], p[3]); // +x
    quad(v, p[2], p[3], p[7], p[6]); // top
    quad(v, p[4], p[5], p[1], p[0]); // bottom
    return true;
  }
  if (kind == "plane") {
    float a[3] = {-0.5f, 0, -0.5f}, b[3] = {0.5f, 0, -0.5f},
          c[3] = {0.5f, 0, 0.5f}, d[3] = {-0.5f, 0, 0.5f};
    quad(v, a, d, c, b);
    return true;
  }
  if (kind == "sphere") {
    const int R = 16;
    auto at = [&](int ring, int seg, float *out) {
      float th = 3.14159265f * ring / R;
      float ph = 6.2831853f * seg / N;
      out[0] = std::sin(th) * std::cos(ph) * 0.5f;
      out[1] = 0.5f - std::cos(th) * 0.5f; // 0..1, resting on the ground
      out[2] = std::sin(th) * std::sin(ph) * 0.5f;
    };
    for (int r = 0; r < R; ++r)
      for (int s = 0; s < N; ++s) {
        float a[3], b[3], c[3], d[3];
        at(r, s, a);
        at(r + 1, s, b);
        at(r + 1, s + 1, c);
        at(r, s + 1, d);
        quad(v, a, b, c, d);
      }
    return true;
  }
  if (kind == "cylinder" || kind == "cone") {
    bool cone = kind == "cone";
    float apex[3] = {0, 1.f, 0};
    for (int s = 0; s < N; ++s) {
      float a0 = 6.2831853f * s / N, a1 = 6.2831853f * (s + 1) / N;
      float b0[3] = {std::cos(a0) * 0.5f, 0, std::sin(a0) * 0.5f};
      float b1[3] = {std::cos(a1) * 0.5f, 0, std::sin(a1) * 0.5f};
      if (cone) {
        tri(v, b0, apex, b1);
      } else {
        float t0[3] = {b0[0], 1.f, b0[2]}, t1[3] = {b1[0], 1.f, b1[2]};
        quad(v, b0, t0, t1, b1);
        float ctr_top[3] = {0, 1.f, 0};
        tri(v, t0, ctr_top, t1);
      }
      float ctr[3] = {0, 0, 0};
      tri(v, b1, ctr, b0); // base
    }
    return true;
  }
  return false;
}

int scene_add_primitive(const std::string &kind, const std::string &name) {
  std::vector<float> verts;
  if (!scene_primitive_verts(kind, verts)) return -1;
  SceneState &s = scene();
  SceneObject o;
  o.type = SceneObject::Mesh;
  o.path = "primitive:" + kind;
  o.name = name.empty()
               ? (char)std::toupper((unsigned char)kind[0]) + kind.substr(1)
               : name;
  o.verts = std::move(verts);
  o.vert_count = (int)(o.verts.size() / 6);
  o.gpu_dirty = true;
  o.pos[0] = 0.5f;
  o.pos[1] = 0.f;
  o.pos[2] = 0.5f;
  o.scale = 0.08f;
  s.objects.push_back(std::move(o));
  s.selected = (int)s.objects.size() - 1;
  return s.selected;
}

} // namespace studio
