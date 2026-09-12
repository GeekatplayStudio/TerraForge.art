// Geekatplay TerraForge — built-in scene primitives: cube, sphere, plane,
// cylinder, cone. Each is generated as the same interleaved position+normal
// triangle soup the OBJ importer produces, sits on y=0 like an imported
// prop, and records a "primitive:<kind>" pseudo-path so a saved scene can
// regenerate it without any file on disk.
#include "scene.hpp"
#include "render_settings.hpp"
#include <algorithm>
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

// A round surface's own normal at each corner instead of the face's. Flat
// faces made every sphere, cylinder and cone a faceted solid whatever its
// detail - and a glossy one a disco ball, each facet mirroring one direction.
void tri_n(std::vector<float> &v, const float *a, const float *b, const float *c,
           const float *na, const float *nb, const float *nc) {
  const float *p[3] = {a, b, c}, *n[3] = {na, nb, nc};
  for (int i = 0; i < 3; ++i) {
    float l = std::sqrt(n[i][0] * n[i][0] + n[i][1] * n[i][1] + n[i][2] * n[i][2]);
    l = l > 1e-12f ? 1.f / l : 0.f;
    v.insert(v.end(), {p[i][0], p[i][1], p[i][2], n[i][0] * l, n[i][1] * l, n[i][2] * l});
  }
}

} // namespace

bool scene_primitive_verts(const std::string &kind, std::vector<float> &v,
                           int detail) {
  v.clear();
  // `detail` is the segment count round the equator. It used to be a fixed
  // 24 with 16 rings, which is a sphere you can count the facets on the
  // moment it fills any part of the frame - and the reason to have it low
  // was never measured, it was just never raised. A sphere at 256 is 130k
  // triangles, which the renderer draws without noticing.
  // 2048 round is eight million triangles on a sphere - about 200 MB of
  // vertices - and is the one cap in this path. It exists so a typo does not
  // ask for a gigabyte, not because anything smaller is unreasonable.
  const int N = std::clamp(detail, 3, 2048);
  const int R = std::max(2, N / 2); // rings: half the segments reads round
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
    // Subdivided, not a single quad. A plane is the natural thing to put a
    // displacement material on, and a displacement needs vertices to move.
    const int G = std::max(1, N / 2);
    for (int j = 0; j < G; ++j)
      for (int i = 0; i < G; ++i) {
        const float x0 = -0.5f + (float)i / G, x1 = -0.5f + (float)(i + 1) / G;
        const float z0 = -0.5f + (float)j / G, z1 = -0.5f + (float)(j + 1) / G;
        float a[3] = {x0, 0, z0}, b[3] = {x1, 0, z0},
              c[3] = {x1, 0, z1}, d[3] = {x0, 0, z1};
        quad(v, a, d, c, b);
      }
    return true;
  }
  if (kind == "sphere") {
    auto at = [&](int ring, int seg, float *out) {
      float th = 3.14159265f * ring / R;
      float ph = 6.2831853f * seg / N;
      out[0] = std::sin(th) * std::cos(ph) * 0.5f;
      out[1] = 0.5f - std::cos(th) * 0.5f; // 0..1, resting on the ground
      out[2] = std::sin(th) * std::sin(ph) * 0.5f;
    };
    // the normal is the direction from the middle, (0, 0.5, 0)
    auto nrm = [](const float *p, float *out) {
      out[0] = p[0];
      out[1] = p[1] - 0.5f;
      out[2] = p[2];
    };
    for (int r = 0; r < R; ++r)
      for (int s = 0; s < N; ++s) {
        float a[3], b[3], c[3], d[3], na[3], nb[3], nc[3], nd[3];
        at(r, s, a);
        at(r + 1, s, b);
        at(r + 1, s + 1, c);
        at(r, s + 1, d);
        nrm(a, na);
        nrm(b, nb);
        nrm(c, nc);
        nrm(d, nd);
        tri_n(v, a, b, c, na, nb, nc);
        tri_n(v, a, c, d, na, nc, nd);
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
        // the side leans in by the slope: radius 0.5 over height 1; the apex
        // takes the normal half way between the edge's two
        const float am = (a0 + a1) * 0.5f;
        float n0[3] = {std::cos(a0), 0.5f, std::sin(a0)};
        float n1[3] = {std::cos(a1), 0.5f, std::sin(a1)};
        float nm[3] = {std::cos(am), 0.5f, std::sin(am)};
        tri_n(v, b0, apex, b1, n0, nm, n1);
      } else {
        float t0[3] = {b0[0], 1.f, b0[2]}, t1[3] = {b1[0], 1.f, b1[2]};
        float n0[3] = {b0[0], 0.f, b0[2]}, n1[3] = {b1[0], 0.f, b1[2]};
        tri_n(v, b0, t0, t1, n0, n0, n1);
        tri_n(v, b0, t1, b1, n0, n1, n1);
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

bool scene_primitive_build(const std::string &kind, int detail, SceneObject &o) {
  if (scene_plant_build(kind, detail, o)) return true;
  std::vector<float> verts;
  if (!scene_primitive_verts(kind, verts, detail)) return false;
  o.verts = std::move(verts);
  o.uvs.clear();
  o.parts.clear();
  o.vert_count = (int)(o.verts.size() / 6);
  return true;
}

int scene_add_primitive(const std::string &kind, const std::string &name,
                        int detail) {
  SceneObject o;
  if (!scene_primitive_build(kind, detail, o)) return -1;
  SceneState &s = scene();
  o.type = SceneObject::Mesh;
  o.path = "primitive:" + kind;
  o.primitive_detail = detail;
  // a seeded plant ("pine#12") is still named for its kind
  const std::string base = scene_is_plant_kind(kind) ? scene_plant_kind(kind, nullptr) : kind;
  o.name = name.empty()
               ? (char)std::toupper((unsigned char)base[0]) + base.substr(1)
               : name;
  o.gpu_dirty = true;
  o.pos[0] = 0.5f;
  o.pos[1] = 0.f;
  o.pos[2] = 0.5f;
  o.scale = 0.08f;
  if (scene_is_plant_kind(kind)) {
    // a plant comes at its own size and in its own colours (its parts')
    o.scale = scene_plant_size_m(kind) / std::max(render_settings().terrain_size_m, 1e-3f);
    o.color[0] = o.color[1] = o.color[2] = 1.f;
  }
  s.objects.push_back(std::move(o));
  s.selected = (int)s.objects.size() - 1;
  return s.selected;
}

int scene_add_light(const std::string &name) {
  SceneState &s = scene();
  SceneObject o;
  o.type = SceneObject::Light;
  int n = 1;
  for (const auto &e : s.objects)
    if (e.type == SceneObject::Light) ++n;
  o.name = name.empty() ? "Light " + std::to_string(n) : name;
  o.pos[0] = 0.5f;
  o.pos[1] = 0.3f;
  o.pos[2] = 0.5f;
  o.color[0] = 1.f;
  o.color[1] = 0.9f;
  o.color[2] = 0.75f;
  s.objects.push_back(std::move(o));
  s.selected = (int)s.objects.size() - 1;
  return s.selected;
}

} // namespace studio
