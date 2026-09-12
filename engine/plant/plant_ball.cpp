// Geekatplay TerraForge - the Ball part: a berry, an apple, a bud.
//
// A sphere on the frame's z axis, squashed along it by `squash` (height over
// width: 1 a sphere, 0.5 a tomato, 2 an olive), hung by `pivot_offset` (0 the
// frame's origin is its lowest point, 0.5 its centre, 1 its top). It is the
// contract's lathe of a half circle, with lat-long UVs (u around, v from the
// bottom pole to the top), so a fruit picture wraps it the way a globe is
// printed.
//
// Where the design is silent: rings come from min_subdiv scaled by
// 2^mesh_boost through subdiv() (so LOD halves them but never below the
// minimum) and columns are twice the rings; wind_strength gives a small
// flutter weight (0.15 of it) so a berry trembles a little but mostly rides
// its stalk; the dry shrink of the Seasons block scales the radius; the
// material is the first connected "material" port (0-based as materials_of
// lists them). Its two child slots sit at its top (a stalk grows there) and
// its bottom (a calyx, a bloom end).
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

void build_ball(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk) {
  ParamReader pr(ctx, n, inst);
  const SeasonLook look = season_look(ctx, pr);
  V3 axis_scale;
  const float size = transform_block(ctx, pr, inst, axis_scale);
  tropism_block(pr, inst.frame);
  if (look.droop_deg > 0.f) {
    const float avail = std::acos(clampf(dot(inst.frame.z, V3(0, -1, 0)), -1.f, 1.f));
    if (avail > 1e-4f) lean(inst.frame, V3(0, -1, 0), std::min(1.f, look.droop_deg * DEG / avail));
  }

  const float R = std::max(1e-5f, pr.random("radius", 0.f, 0, 0.03f) * size * (1.f - look.shrink));
  const float squash = std::max(0.05f, pr.random("squash", 0.f, 0, 1.f));
  const float pivot = clampf(pr.random("pivot_offset"), 0.f, 1.f);
  const float H = 2.f * R * squash * axis_scale.z;
  inst.length = H;
  inst.radius = R * std::max(axis_scale.x, axis_scale.y);

  for (int c = 0; c < 3; ++c) inst.tint[c] *= look.tint[c];
  const float tint4[4] = {inst.tint[0], inst.tint[1], inst.tint[2], inst.tint[3]};
  // No flutter on a fruit. Flutter is the rippling of a card and it is baked
  // per vertex; a lathed ball takes one weight for all of it, so any flutter
  // at all moves the whole fruit - including the point it hangs from - and
  // the fruit drifts off its stalk. It swings with the branch through `bend`
  // instead, which is what a cherry actually does.
  const float wind4[4] = {inst.wind_phase, inst.wind_bend, 0.f, -1.f};

  // the half circle from the bottom pole (t 0) to the top (t 1), hung by the pivot
  const float z0 = -pivot * H;
  auto profile = [&](float t, float &r, float &h) {
    r = R * std::sin(t * PI);
    h = z0 + 0.5f * H * (1.f - std::cos(t * PI));
  };
  // the section: the per-axis scale as an ellipse
  auto section = [&](float a, float) {
    const float ca = std::cos(a * TAU), sa = std::sin(a * TAU);
    return std::sqrt(ca * ca * axis_scale.x * axis_scale.x + sa * sa * axis_scale.y * axis_scale.y);
  };
  LatheOpts o;
  const int mn = std::max(3, pr.i("min_subdiv", 6));
  o.rings = std::max(mn, subdiv(ctx, (float)mn * std::ldexp(1.f, (int)pr.f("mesh_boost")), mn));
  o.columns = std::max(6, o.rings * 2);
  out.begin(inst, PlantPartKind::Fruit, first_material(ctx, n), false, "ball");
  lathe(out, inst.frame, profile, section, o, wind4, tint4);
  out.end();

  // children: a stalk at the top, a calyx at the bottom
  sk.tip.frame = inst.frame;
  sk.tip.frame.o = inst.frame.world(V3(0.f, 0.f, z0 + H));
  sk.tip.primal = 1.f;
  sk.tip.radius = 0.f;
  sk.tip.dist = inst.dist_root + H;
  sk.has_tip = true;
  sk.bottom.frame = inst.frame;
  sk.bottom.frame.o = inst.frame.world(V3(0.f, 0.f, z0));
  sk.bottom.frame.z = -inst.frame.z;
  sk.bottom.frame.x = -inst.frame.x;
  sk.bottom.primal = 0.f;
  sk.bottom.dist = inst.dist_root;
  sk.has_bottom = true;
}

} // namespace plant
} // namespace gpx
