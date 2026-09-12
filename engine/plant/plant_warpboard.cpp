// Geekatplay TerraForge - the Warpboard part: a curled rectangle.
//
// The oldest leaf shape there is and still the quickest petal or grass
// blade: a strip of width by length in the frame's x-z plane, bent across by
// `curl` and along by `flexibility`, each an arc so a value of 1 folds the
// strip through a quarter turn and 2 through a half. `randomness` perturbs
// both per instance so a bed of them never lines up. It is a paraboloid in
// spirit and an arc pair in practice, because arcs keep their length and a
// paraboloid stretches.
//
// Where the design is silent: the strip hangs from its base (the frame's
// origin is the middle of the bottom edge); rows come from min_subdiv and
// the boosts, columns from the same count scaled by the aspect ratio and
// never below 2; the wind strength is the flutter weight, growing as the
// square of the distance up the strip so the root stays put; the material
// slot is the first connected "material" port, 0-based as materials_of lists
// them.
#include "plant/plant_internal.hpp"
#include <algorithm>

namespace gpx {
namespace plant {

namespace {

float transform_block(const BuildCtx &ctx, const ParamReader &pr, Instance &inst, V3 &axis_scale) {
  float size = ctx.scale * pr.random("scale", 0.f, 0, 1.f);
  if (pr.b("scale_inherit", true)) size *= inst.scale;
  axis_scale = V3(pr.random("scale_x", 0.f, 0, 1.f), pr.random("scale_y", 0.f, 0, 1.f), pr.random("scale_z", 0.f, 0, 1.f));
  Frame &f = inst.frame;
  f.o += f.dir(V3(pr.random("offset_x"), pr.random("offset_y"), pr.random("offset_z"))) * size;
  const float rx = pr.random("rot_x"), ry = pr.random("rot_y"), rz = pr.random("rot_z");
  if (rx != 0.f) f = f.rotated(f.x, rx * DEG);
  if (ry != 0.f) f = f.rotated(f.y, ry * DEG);
  if (rz != 0.f) f = f.rotated(f.z, rz * DEG);
  return size;
}

int first_material(BuildCtx &ctx, const Node &n) {
  const std::vector<const Node *> mats = ctx.materials_of(n);
  for (size_t i = 0; i < mats.size(); ++i) if (mats[i]) return ctx.material_for(n, (int)i);
  return ctx.material_for(n, 0);
}

} // namespace

void build_warpboard(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk) {
  ParamReader pr(ctx, n, inst);
  const SeasonLook look = season_look(ctx, pr);
  V3 axis_scale;
  const float size = transform_block(ctx, pr, inst, axis_scale);
  tropism_block(pr, inst.frame);
  if (look.droop_deg > 0.f) {
    const float avail = std::acos(clampf(dot(inst.frame.z, V3(0, -1, 0)), -1.f, 1.f));
    if (avail > 1e-4f) lean(inst.frame, V3(0, -1, 0), std::min(1.f, look.droop_deg * DEG / avail));
  }

  const float W = std::max(1e-4f, pr.random("width", 0.f, 0, 0.05f) * size * (1.f - look.shrink));
  const float L = std::max(1e-4f, pr.random("length", 0.f, 0, 0.1f) * size * (1.f - look.shrink));
  inst.length = L;
  inst.radius = W * 0.5f;
  const float rnd = clampf(pr.random("shape_randomness", 0.f, 0, 0.2f), 0.f, 1.f);
  Draw d(ctx.seed, inst.id, hash_str("warp"));
  const float curl = pr.random("curl", 0.f, 0, 0.2f) * (1.f + rnd * d.signed_unit()) * PI * 0.5f;
  const float flex = pr.random("flexibility", 0.f, 0, 0.3f) * (1.f + rnd * d.signed_unit()) * PI * 0.5f;

  for (int c = 0; c < 3; ++c) inst.tint[c] *= look.tint[c];
  const float tint4[4] = {inst.tint[0], inst.tint[1], inst.tint[2], inst.tint[3]};
  const float strength = pr.random("wind_strength", 0.f, 0, 1.f);
  const float phase = inst.wind_phase, bend = inst.wind_bend;
  auto wind = [&](float, float v, float w4[4]) {
    w4[0] = phase; w4[1] = bend; w4[2] = strength * v * v * L * 0.35f; w4[3] = -1.f;
  };

  // the strip: u across (0..1), v along (0..1); an arc across of angle
  // `curl` and one along of angle `flex`, the section riding the axis arc
  const Frame f = inst.frame;
  auto local = [&](float u, float v) {
    const float x0 = (u - 0.5f) * W, s = v * L;
    float x = x0, y = 0.f;
    if (std::fabs(curl) > 1e-4f) {
      const float R = W / curl, a = x0 / R;
      x = R * std::sin(a);
      y = R * (1.f - std::cos(a));
    }
    float z = s, yy = y;
    if (std::fabs(flex) > 1e-4f) {
      const float R = L / flex, a = s / R;
      z = R * std::sin(a) - y * std::sin(a);
      yy = R * (1.f - std::cos(a)) + y * std::cos(a);
    }
    return V3(x * axis_scale.x, yy * axis_scale.y, z * axis_scale.z);
  };
  auto at = [&](float u, float v, V3 &p, V3 &nn) {
    const float e = 1e-3f;
    const V3 du = local(u + e, v) - local(u - e, v), dv = local(u, v + e) - local(u, v - e);
    p = f.world(local(u, v));
    nn = normalize(f.dir(cross(dv, du)), f.y);
  };

  const int rows = std::max(1, subdiv(ctx, (float)pr.i("min_subdiv", 2) * std::ldexp(1.f, (int)pr.f("mesh_boost")), pr.i("min_subdiv", 2)));
  const int cols = std::max(2, (int)std::lround((float)rows * W / L));
  out.begin(inst, PlantPartKind::Leaf, first_material(ctx, n), true, "warpboard");
  grid(out, cols, rows, at, false, wind, tint4);
  out.end();

  sk.tip.frame = inst.frame;
  sk.tip.frame.o = f.world(local(0.5f, 1.f));
  sk.tip.primal = 1.f;
  sk.tip.dist = inst.dist_root + L;
  sk.has_tip = true;
  sk.bottom.frame = inst.frame;
  sk.has_bottom = true;
}

} // namespace plant
} // namespace gpx
