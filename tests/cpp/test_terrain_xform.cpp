// Geekatplay TerraForge - the terrain tile's transform, without a window:
// identity is a no-op and reports itself off; offset, heading and scale
// invert exactly on the ground plane; a deformer and a pitch move points the
// way the mesh deformers do; the grounding query returns the transformed
// height at a world point.
#include "terrain_xform.hpp"
#include "scene.hpp"
#include <cmath>
#include <cstdio>

using namespace studio;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

int main() {
  SceneObject o;
  o.type = SceneObject::Terrain;
  o.pos[0] = o.pos[1] = o.pos[2] = 0.f;

  std::printf("identity...\n");
  TerrainXform id = terrain_xform_of(o, 0.22f);
  check(!id.on, "every field at its default is off");
  float p[3] = {0.3f, 0.1f, 0.7f};
  terrain_xform_apply(id, p);
  check(near(p[0], 0.3f) && near(p[1], 0.1f) && near(p[2], 0.7f), "identity leaves a point alone");

  std::printf("offset, heading, scale...\n");
  o.pos[0] = 0.25f;
  o.pos[2] = -0.1f;
  o.yaw = 90.f;
  o.scl[0] = 2.f;
  o.scl[2] = 0.5f;
  TerrainXform t = terrain_xform_of(o, 0.22f);
  check(t.on, "a moved tile is on");
  // the centre stays at the centre plus the offset
  float c[3] = {0.5f, 0.f, 0.5f};
  terrain_xform_apply(t, c);
  check(near(c[0], 0.75f) && near(c[1], 0.f) && near(c[2], 0.4f), "the centre moves by the offset only");
  // a point east of the centre, scaled x2 then turned 90 degrees about Y
  float e[3] = {0.6f, 0.f, 0.5f};
  terrain_xform_apply(t, e);
  // (0.1, 0, 0) * (2, 1, 0.5) = (0.2, 0, 0); H(90): x' = ch*x + sh*z = 0, z' = -sh*x + ch*z = -0.2
  check(near(e[0], 0.75f) && near(e[2], 0.4f - 0.2f), "scale then heading, HPB order");
  // and the ground-plane inverse takes it back
  float u = 0, v = 0;
  check(terrain_xform_unapply_xz(t, e[0], e[2], u, v), "inverse exists");
  check(near(u, 0.6f) && near(v, 0.5f), "unapply inverts offset, heading and scale");
  for (int i = 0; i < 20; ++i) {
    float q[3] = {0.05f * i, 0.f, 1.f - 0.04f * i};
    float w[3] = {q[0], q[1], q[2]};
    terrain_xform_apply(t, w);
    terrain_xform_unapply_xz(t, w[0], w[2], u, v);
    check(near(u, q[0], 1e-3f) && near(v, q[2], 1e-3f), "round trip over the tile");
  }

  std::printf("pitch and the deformers...\n");
  SceneObject o2 = o;
  o2.pos[0] = o2.pos[2] = 0.f;
  o2.yaw = 0.f;
  o2.scl[0] = o2.scl[2] = 1.f;
  o2.pitch = 90.f; // nose up: +Z of the tile turns to -Y? P(90): y' = -z, z' = y
  TerrainXform tp = terrain_xform_of(o2, 0.22f);
  float n[3] = {0.5f, 0.f, 0.9f}; // 0.4 north of the centre
  terrain_xform_apply(tp, n);
  check(near(n[0], 0.5f) && near(n[1], -0.4f) && near(n[2], 0.5f), "pitch tips the far edge down (HPB, about X)");
  SceneObject o3 = o2;
  o3.pitch = 0.f;
  o3.deform.taper = -1.f; // the top comes to a point: at h = height_scale the width is 0
  TerrainXform td = terrain_xform_of(o3, 0.5f);
  check(td.on, "a deformer alone switches it on");
  float top[3] = {0.9f, 0.5f, 0.5f};
  terrain_xform_apply(td, top);
  check(near(top[0], 0.5f, 1e-3f), "taper -1 pulls the top in to the centre");
  float base[3] = {0.9f, 0.f, 0.5f};
  terrain_xform_apply(td, base);
  check(near(base[0], 0.9f), "and leaves the base alone");

  std::printf("grounding...\n");
  auto sample = [](float x, float z) { return 0.1f + 0.2f * x + 0.05f * z; };
  const float g_id = terrain_xform_ground(id, 0.3f, 0.7f, sample);
  check(near(g_id, sample(0.3f, 0.7f)), "identity grounds at the tile's own height");
  SceneObject o4 = o;
  o4.pos[0] = o4.pos[2] = 0.f;
  o4.yaw = 0.f;
  o4.scl[0] = o4.scl[2] = 1.f;
  o4.pos[1] = 0.05f;
  o4.scl[1] = 2.f;
  TerrainXform tg = terrain_xform_of(o4, 0.22f);
  const float g = terrain_xform_ground(tg, 0.3f, 0.7f, sample);
  check(near(g, sample(0.3f, 0.7f) * 2.f + 0.05f * 0.22f), "a raised (in height units), stretched tile grounds higher");

  check(TERRAIN_XFORM_GLSL && TERRAIN_XFORM_FS_GLSL, "the GLSL exists");
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("terrain transform tests OK\n");
  return 0;
}
