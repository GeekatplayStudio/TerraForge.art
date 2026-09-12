// Geekatplay TerraForge - the Hydra part: children around a circle.
//
// A clump of grass, a rosette of leaves, a tuft of reeds: many stems from
// one point, arranged around a ring rather than along an axis. The Hydra
// grows nothing itself; it returns one socket per child on a circle of
// `radius` in the frame's x-y plane (the plane across its growth axis z),
// and the walker attaches one child to each socket (design section 9),
// ignoring the child's own count and range.
//
// The angles: angle 1 tilts each child away from the hydra's axis toward
// its own radial direction (0 stands them all up like the axis, 90 lays
// them flat along the ground of the circle); angle 2 tilts them sideways
// along the circle (a swirl); angle 3 rolls each about its own axis. The
// distribution curve is the density around the circle, sampled by its
// cumulative sum over 64 bins so where the curve is high the children
// crowd. Soft insert on the fractional count: rounded, an extra child
// scaled by the fraction (Socket.scale), or an extra one with that
// probability. Where the design is silent: child_scale multiplies every
// socket's scale; the first child sits at the frame's x; a count of 0
// leaves the hydra empty and warns nothing (a rosette with no leaves is a
// choice, not a mistake).
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

// The position around the circle (0..1) of the i-th of n children, spaced
// by the density curve's cumulative sum.
float place_by_density(const Curve &density, int i, int n) {
  const int B = 64;
  float cdf[B + 1];
  cdf[0] = 0.f;
  for (int b = 0; b < B; ++b) cdf[b + 1] = cdf[b] + std::max(0.f, density.eval(((float)b + 0.5f) / (float)B));
  const float target = ((float)i + 0.5f) / (float)n;
  if (cdf[B] <= 1e-9f) return target;
  const float want = target * cdf[B];
  int b = 0;
  while (b < B - 1 && cdf[b + 1] < want) ++b;
  const float span = cdf[b + 1] - cdf[b];
  return ((float)b + (span > 1e-12f ? (want - cdf[b]) / span : 0.5f)) / (float)B;
}

} // namespace

void build_hydra(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk) {
  (void)out;
  ParamReader pr(ctx, n, inst);
  V3 axis_scale;
  const float size = transform_block(ctx, pr, inst, axis_scale);
  tropism_block(pr, inst.frame);

  const float R = std::max(0.f, pr.random("radius", 0.f, 0, 0.1f) * size);
  inst.length = 0.f;
  inst.radius = R;
  const float child_scale = pr.random("child_scale", 0.f, 0, 1.f);
  const float a1 = pr.random("angle1", 0.f, 0, 45.f) * DEG;
  const float a2 = pr.random("angle2") * DEG;
  const float a3 = pr.random("angle3") * DEG;
  const Curve &density = pr.curve("distribution");

  // the count and its fractional part
  float count = std::max(0.f, pr.random("child_count", 0.f, 0, 8.f) * inst.density);
  int nchild = (int)std::floor(count);
  const float frac = count - (float)nchild;
  float extra_scale = 1.f;
  bool extra = false;
  switch (pr.choice("child_soft")) {
    case 1: if (frac > 1e-3f) { extra = true; extra_scale = frac; } break;
    case 2: { Draw d(ctx.seed, inst.id, hash_str("child_soft")); extra = d.unit() < frac; } break;
    default: nchild = (int)std::lround(count); break;
  }
  if (extra) ++nchild;
  nchild = std::min(nchild, 4096);

  const Frame &f = inst.frame;
  for (int i = 0; i < nchild; ++i) {
    const float a = place_by_density(density, i, nchild) * TAU;
    const V3 radial = f.x * (std::cos(a) * axis_scale.x) + f.y * (std::sin(a) * axis_scale.y);
    const V3 rdir = normalize(radial, f.x);
    const V3 tdir = normalize(cross(f.z, rdir), f.y);
    Socket s;
    s.frame.o = f.o + radial * R;
    // start along the hydra's axis, tilt toward the radial by angle 1,
    // sideways along the circle by angle 2, roll about itself by angle 3
    V3 z = rotate(f.z, tdir, a1);
    z = rotate(z, rdir, a2);
    s.frame.z = normalize(z, f.z);
    V3 x = rdir - s.frame.z * dot(rdir, s.frame.z);
    if (length(x) < 1e-4f) x = perpendicular(s.frame.z);
    s.frame.x = normalize(x);
    s.frame.y = cross(s.frame.z, s.frame.x);
    if (a3 != 0.f) s.frame = s.frame.rotated(s.frame.z, a3);
    s.primal = 0.f;
    s.radius = 0.f;
    s.side_radius = 0.f;
    s.azimuth = a - PI;
    s.remaining = 0.f;
    s.dist = inst.dist_root;
    s.scale = child_scale * (extra && i == nchild - 1 ? extra_scale : 1.f);
    s.whorl_index = i;
    sk.along.push_back(s);
  }
  sk.tip.frame = inst.frame;
  sk.tip.primal = 1.f;
  sk.tip.dist = inst.dist_root;
  sk.has_tip = true;
  sk.bottom = sk.tip;
  sk.has_bottom = true;
}

} // namespace plant
} // namespace gpx
