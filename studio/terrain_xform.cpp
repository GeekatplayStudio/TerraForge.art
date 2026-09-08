// Geekatplay TerraForge — the terrain tile's transform (see terrain_xform.hpp).
#include "terrain_xform.hpp"
#include "scene.hpp"
#include <cmath>

namespace studio {

namespace {

constexpr float DEG = 3.14159265358979f / 180.f;

// HPB the way the mesh vertex shader composes it: heading about Y, then
// pitch about X, then bank about Z, applied to a point as R = H * P * B.
void hpb_matrix(float yaw, float pitch, float roll, float m[9]) {
  const float ch = std::cos(yaw * DEG), sh = std::sin(yaw * DEG);
  const float cp = std::cos(pitch * DEG), sp = std::sin(pitch * DEG);
  const float cb = std::cos(roll * DEG), sb = std::sin(roll * DEG);
  // H (about Y)
  const float H[9] = {ch, 0, sh, 0, 1, 0, -sh, 0, ch};
  // P (about X)
  const float P[9] = {1, 0, 0, 0, cp, -sp, 0, sp, cp};
  // B (about Z)
  const float B[9] = {cb, -sb, 0, sb, cb, 0, 0, 0, 1};
  float HP[9];
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      HP[r * 3 + c] = H[r * 3] * P[c] + H[r * 3 + 1] * P[3 + c] + H[r * 3 + 2] * P[6 + c];
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      m[r * 3 + c] = HP[r * 3] * B[c] + HP[r * 3 + 1] * B[3 + c] + HP[r * 3 + 2] * B[6 + c];
}

} // namespace

TerrainXform terrain_xform_of(const SceneObject &o, float height_scale) {
  TerrainXform t;
  for (int i = 0; i < 3; ++i) {
    t.pos[i] = o.pos[i];
    t.scl[i] = o.scl[i];
  }
  // pos[1] is in height units like every object's, so the panel's Altitude
  // row and the gizmo's Y handle read the same number a mesh would
  t.pos[1] = o.pos[1] * height_scale;
  t.yaw = o.yaw;
  t.pitch = o.pitch;
  t.roll = o.roll;
  t.deform = o.deform;
  t.bmax[1] = std::fmax(height_scale, 1e-4f);
  hpb_matrix(o.yaw, o.pitch, o.roll, t.rot);
  t.on = o.pos[0] != 0.f || o.pos[1] != 0.f || o.pos[2] != 0.f ||
         o.yaw != 0.f || o.pitch != 0.f || o.roll != 0.f ||
         o.scl[0] != 1.f || o.scl[1] != 1.f || o.scl[2] != 1.f ||
         !o.deform.identity();
  return t;
}

void terrain_xform_apply(const TerrainXform &t, float p[3]) {
  if (!t.on) return;
  if (!t.deform.identity()) gpx::deform_point(t.deform, t.bmin, t.bmax, p);
  const float c[3] = {0.5f, 0.f, 0.5f};
  float q[3];
  for (int i = 0; i < 3; ++i) q[i] = (p[i] - c[i]) * t.scl[i];
  for (int r = 0; r < 3; ++r)
    p[r] = t.rot[r * 3] * q[0] + t.rot[r * 3 + 1] * q[1] + t.rot[r * 3 + 2] * q[2] +
           c[r] + t.pos[r];
}

bool terrain_xform_unapply_xz(const TerrainXform &t, float x, float z, float &u, float &v) {
  if (!t.on) {
    u = x;
    v = z;
    return true;
  }
  if (std::fabs(t.scl[0]) < 1e-6f || std::fabs(t.scl[2]) < 1e-6f) return false;
  // undo the offset and the centre, then the heading (about Y), then the
  // scale; pitch, bank and the deformers are left out on purpose
  const float qx = x - 0.5f - t.pos[0], qz = z - 0.5f - t.pos[2];
  const float ch = std::cos(t.yaw * DEG), sh = std::sin(t.yaw * DEG);
  // H rotates (x, z) -> (ch*x + sh*z, -sh*x + ch*z); its inverse is the transpose
  const float lx = ch * qx - sh * qz;
  const float lz = sh * qx + ch * qz;
  u = lx / t.scl[0] + 0.5f;
  v = lz / t.scl[2] + 0.5f;
  return true;
}

// The vertex-stage twin. DEFORM_FN_GLSL (u_def_*, deform()) precedes it.
const char *const TERRAIN_XFORM_GLSL = R"GLSL(
uniform int u_tx_on;
uniform vec3 u_tx_pos, u_tx_scl;
uniform mat3 u_tx_rot;
vec3 tile_xform(vec3 p){
  if (u_tx_on == 0) return p;
  if (u_def_on == 1) p = deform(p);
  vec3 c = vec3(0.5, 0.0, 0.5);
  return u_tx_rot * ((p - c) * u_tx_scl) + c + u_tx_pos;
}
)GLSL";

// The fragment stage: the normal's share of the rotation and scale (the
// deformers are left to the geometry), and the outline cut when the tile
// is not placed on a planet - placed, the planet shows through outside
// the outline instead (planet_place.cpp), which is the same picture.
const char *const TERRAIN_XFORM_FS_GLSL = R"GLSL(
uniform int u_tx_on, u_tx_cut, u_tx_shape;
uniform vec3 u_tx_scl;
uniform mat3 u_tx_rot;
uniform float u_tx_aspect;
vec3 tile_xform_normal(vec3 n){
  if (u_tx_on == 0) return n;
  return normalize(u_tx_rot * (n / max(abs(u_tx_scl), vec3(1e-4))));
}
bool tile_cut(vec2 uv){
  if (u_tx_cut == 0) return false;
  if (u_tx_shape == 1) return length(uv - 0.5) > 0.5;
  if (u_tx_shape == 2){
    float hw = 0.5 * min(1.0 / max(u_tx_aspect, 0.05), 1.0);
    float hd = 0.5 * min(u_tx_aspect, 1.0);
    return abs(uv.x - 0.5) > hw || abs(uv.y - 0.5) > hd;
  }
  return false;
}
)GLSL";

} // namespace studio
