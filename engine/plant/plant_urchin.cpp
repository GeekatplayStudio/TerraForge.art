// Geekatplay TerraForge - the Urchin part: a body that carries children on
// its skin.
//
// A seed head, a thistle, a cactus body, a dandelion clock: a round thing
// whose real job is where it puts its children. The body is a surface of
// revolution about the frame's z: height t (0 bottom, 1 top) runs over
// 2 x radius, the profile curve gives the radius at each height and the
// section curve scales it by angle, both in the units of `radius`. The
// pivot hangs it on the frame (0 its lowest point, 0.5 its centre, 1 its
// top). Skin None grows no geometry and only the sockets remain, which is
// how a bare spray of stems is made.
//
// The children: `child_count` sockets between child_start and child_end
// (height fractions), turned by phi (the golden angle by default, so no
// two line up) and spaced along the height by the density curve's
// cumulative sum; each frame's z is the surface normal there (orthogonal)
// or the axis tilted by child_angle toward the tangent; scale is
// child_scale, and scale shift moves the smaller ones toward the top. The
// walker attaches one child per socket (design section 9). The lathe here
// is local rather than the contract's because the sockets, the radial
// displacement and the normals all need the same evaluated surface.
//
// Where the design is silent: the "By unit length/radius" meshing mode
// counts subdivisions per metre of height and per metre of circumference;
// Adaptiveness on spaces the rings evenly and off spaces them by the axial
// density curve; the Field displacement source reads the driven
// disp_amount at each ring's height (the field sees the primal, not the
// angle); non-parametric UVs are metres along and around times the tiles;
// the wind flutter is breeze_strength times the height fraction, small
// (0.2) because a body rides its stalk more than it quivers; the material
// is the first connected "material" port, 0-based as materials_of lists it.
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

// Smooth lattice noise on (a, t) with the angle wrapped, so a seam never shows.
float hash2(int x, int y, uint32_t seed) { return hash_unit(hash_u32((uint32_t)x, (uint32_t)y, seed)); }
float vnoise(float x, float y, int wrap, uint32_t seed) {
  const int ix = (int)std::floor(x), iy = (int)std::floor(y);
  float fx = x - (float)ix, fy = y - (float)iy;
  fx = fx * fx * (3.f - 2.f * fx);
  fy = fy * fy * (3.f - 2.f * fy);
  auto H = [&](int a, int b) { return hash2(((a % wrap) + wrap) % wrap, b, seed); };
  const float a = H(ix, iy) + (H(ix + 1, iy) - H(ix, iy)) * fx;
  const float b = H(ix, iy + 1) + (H(ix + 1, iy + 1) - H(ix, iy + 1)) * fx;
  return a + (b - a) * fy;
}
float fbm(float x, float y, int wrap, uint32_t seed) {
  float s = 0.f, amp = 0.5f, n = 0.f;
  for (int o = 0; o < 4; ++o) {
    s += (vnoise(x, y, wrap, seed + (uint32_t)o) * 2.f - 1.f) * amp;
    n += amp; amp *= 0.5f; x *= 2.f; y *= 2.f; wrap *= 2;
  }
  return s / n;
}
// Sparse round bumps: one per lattice cell at a random spot, cosine lobes.
float bumps(float x, float y, int wrap, uint32_t seed) {
  const int ix = (int)std::floor(x), iy = (int)std::floor(y);
  float best = 0.f;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      const int cx = ix + dx, cy = iy + dy;
      const int wx = ((cx % wrap) + wrap) % wrap;
      const float px = (float)cx + hash2(wx, cy, seed), py = (float)cy + hash2(wx, cy, seed + 7u);
      const float d = std::sqrt((x - px) * (x - px) + (y - py) * (y - py)) / 0.45f;
      if (d < 1.f) best = std::max(best, 0.5f + 0.5f * std::cos(d * PI));
    }
  return best;
}

float place_by_density(const Curve &density, float target, float lo, float hi) {
  const int B = 64;
  float cdf[B + 1];
  cdf[0] = 0.f;
  for (int b = 0; b < B; ++b) cdf[b + 1] = cdf[b] + std::max(0.f, density.eval(lo + (hi - lo) * ((float)b + 0.5f) / (float)B));
  if (cdf[B] <= 1e-9f) return lo + (hi - lo) * target;
  const float want = target * cdf[B];
  int b = 0;
  while (b < B - 1 && cdf[b + 1] < want) ++b;
  const float span = cdf[b + 1] - cdf[b];
  return lo + (hi - lo) * ((float)b + (span > 1e-12f ? (want - cdf[b]) / span : 0.5f)) / (float)B;
}

} // namespace

void build_urchin(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk) {
  ParamReader pr(ctx, n, inst);
  const SeasonLook look = season_look(ctx, pr);
  V3 axis_scale;
  const float size = transform_block(ctx, pr, inst, axis_scale);
  tropism_block(pr, inst.frame);
  for (int c = 0; c < 3; ++c) inst.tint[c] *= look.tint[c];
  const float tint4[4] = {inst.tint[0], inst.tint[1], inst.tint[2], inst.tint[3]};

  const float R = std::max(1e-5f, pr.random("radius", 0.f, 0, 0.1f) * size * (1.f - look.shrink));
  const float H = 2.f * R * axis_scale.z;
  const float z0 = -clampf(pr.random("pivot_offset"), 0.f, 1.f) * H;
  inst.length = H;
  inst.radius = R * std::max(axis_scale.x, axis_scale.y);
  const Curve &profile = pr.curve("profile");
  const Curve &section = pr.curve("section");

  // displacement along the radial direction
  const int disp_src = pr.choice("disp_source");
  const float disp_amount = pr.random("disp_amount");
  const float disp_offset = pr.f("disp_offset");
  const float disp_unit = pr.b("disp_relative", true) ? R : 1.f;
  const uint32_t nseed = hash_u32(inst.id, hash_str("disp"));
  auto displacement = [&](float a, float t) -> float {
    if (disp_src == 0) return 0.f;
    float s = 0.f;
    if (disp_src == 1) s = fbm(a * 8.f, t * 8.f, 8, nseed);
    else if (disp_src == 2) s = bumps(a * 6.f, t * 6.f, 6, nseed);
    else s = pr.field("disp_amount", t, disp_amount);
    return (disp_src == 3 ? s : disp_amount * s + disp_offset) * disp_unit;
  };
  // the surface in the frame's local coordinates
  auto surface = [&](float a, float t) -> V3 {
    const float r = R * std::max(0.f, profile.eval(t)) * std::max(0.f, section.eval(a)) + displacement(a, t);
    return V3(std::cos(a * TAU) * r * axis_scale.x, std::sin(a * TAU) * r * axis_scale.y, z0 + t * H);
  };
  auto normal_at = [&](float a, float t) -> V3 {
    const float e = 1e-3f;
    const V3 da = surface(a + e, t) - surface(a - e, t);
    const V3 dt = surface(a, std::min(1.f, t + e)) - surface(a, std::max(0.f, t - e));
    V3 nn = cross(da, dt);
    if (length(nn) < 1e-12f) nn = V3(std::cos(a * TAU), std::sin(a * TAU), 0.f);
    return normalize(nn);
  };

  // the body
  const Frame &f = inst.frame;
  const float breeze = pr.random("breeze_strength", 0.f, 0, 1.f);
  if (pr.choice("skin") == 0) {
    const float boost = std::ldexp(1.f, (int)pr.f("mesh_boost"));
    const bool by_unit = pr.choice("subdiv_mode") == 1;
    const float ax = pr.f("axial_subdiv", 12.f) * (by_unit ? H : 1.f) * boost;
    const float an = pr.f("angular_subdiv", 16.f) * (by_unit ? TAU * R : 1.f) * boost;
    const int rings = std::min(400, std::max(2, subdiv(ctx, ax, pr.i("axial_min", 4))));
    const int cols = std::min(400, std::max(3, subdiv(ctx, an, pr.i("angular_min", 6))));
    const bool adaptive = pr.b("adaptive", true);
    const Curve &axial_density = pr.curve("axial_density");
    const bool parametric_uv = pr.b("uv_parametric", true);
    const float ut = pr.f("u_tile", 1.f), vt = pr.f("v_tile", 1.f), uo = pr.f("u_offset"), vo = pr.f("v_offset");
    const bool geometric = pr.choice("normal_mode") == 1;
    std::vector<uint32_t> ids((size_t)(rings + 1) * (cols + 1));
    out.begin(inst, PlantPartKind::Body, first_material(ctx, n), pr.b("double_sided"), "urchin");
    for (int i = 0; i <= rings; ++i) {
      const float ti = (float)i / (float)rings;
      const float t = adaptive ? ti : place_by_density(axial_density, ti, 0.f, 1.f);
      for (int j = 0; j <= cols; ++j) {
        const float a = (float)j / (float)cols;
        const V3 p = surface(a, t);
        const float u = (parametric_uv ? a : a * TAU * R) * ut + uo;
        const float v = (parametric_uv ? t : t * H) * vt + vo;
        // in metres, a fifth of the part's own height
        const float w4[4] = {inst.wind_phase, inst.wind_bend, 0.2f * breeze * t * H, -1.f};
        ids[(size_t)i * (cols + 1) + j] = out.vertex(f.world(p), f.dir(normal_at(a, t)), u, v, w4, tint4);
      }
    }
    for (int i = 0; i < rings; ++i)
      for (int j = 0; j < cols; ++j) {
        const uint32_t a = ids[(size_t)i * (cols + 1) + j], b = ids[(size_t)i * (cols + 1) + j + 1];
        const uint32_t c = ids[(size_t)(i + 1) * (cols + 1) + j + 1], d = ids[(size_t)(i + 1) * (cols + 1) + j];
        out.quad(a, b, c, d);
      }
    if (geometric) out.geometric_normals();
    out.end();
  }

  // the children on the skin
  float count = std::max(0.f, pr.random("child_count", 0.f, 0, 30.f) * inst.density);
  int nchild = (int)std::floor(count);
  const float frac = count - (float)nchild;
  float extra_scale = 1.f;
  bool extra = false;
  switch (pr.choice("child_soft")) {
    case 1: extra = frac > 1e-3f; break;
    case 2: if (frac > 1e-3f) { extra = true; extra_scale = frac; } break;
    case 3: { Draw d(ctx.seed, inst.id, hash_str("child_soft")); extra = d.unit() < frac; } break;
    default: nchild = (int)std::lround(count); break;
  }
  if (extra) ++nchild;
  nchild = std::min(nchild, 8192);
  const float t_lo = clampf(pr.random("child_start"), 0.f, 1.f), t_hi = clampf(pr.random("child_end", 0.f, 0, 1.f), 0.f, 1.f);
  const Curve &child_density = pr.curve("child_density");
  const float phi = pr.random("child_phi", 0.f, 0, 137.5f) * DEG;
  const bool orthogonal = pr.b("child_orthogonal", true);
  const float child_angle = pr.random("child_angle") * DEG;
  const float child_scale = pr.random("child_scale", 0.f, 0, 1.f);
  const float shift = clampf(pr.random("child_scale_shift"), 0.f, 1.f);
  for (int i = 0; i < nchild; ++i) {
    // scale shift: the small ones go up; the order along the height is the
    // order of scale, and every child's own scale is its share of that
    float sc = child_scale * (extra && i == nchild - 1 ? extra_scale : 1.f);
    const float rank = ((float)i + 0.5f) / (float)nchild;
    if (shift > 0.f) sc *= 1.f - shift * 0.5f * (2.f * rank - 1.f);
    const float t = place_by_density(child_density, rank, std::min(t_lo, t_hi), std::max(t_lo, t_hi));
    const float a = std::fmod((float)i * phi / TAU + 1e6f, 1.f);
    Socket s;
    const V3 p = surface(a, t), nrm = normal_at(a, t);
    s.frame.o = f.world(p);
    const V3 radial = normalize(f.dir(V3(nrm.x, nrm.y, 0.f)), f.x);
    V3 z = orthogonal ? f.dir(nrm) : rotate(f.z, normalize(cross(f.z, radial), f.y), child_angle);
    s.frame.z = normalize(z, f.z);
    V3 x = f.z - s.frame.z * dot(f.z, s.frame.z);
    if (length(x) < 1e-4f) x = perpendicular(s.frame.z);
    s.frame.x = normalize(x);
    s.frame.y = cross(s.frame.z, s.frame.x);
    s.primal = t;
    s.radius = length(V3(p.x, p.y, 0.f));
    s.side_radius = s.radius;
    s.azimuth = a * TAU - PI;
    s.remaining = (1.f - t) * H;
    s.dist = inst.dist_root + t * H;
    s.scale = sc;
    s.whorl_index = i;
    sk.along.push_back(s);
  }
  sk.tip.frame = f;
  sk.tip.frame.o = f.world(surface(0.f, 1.f));
  sk.tip.primal = 1.f;
  sk.tip.dist = inst.dist_root + H;
  sk.has_tip = true;
  sk.bottom.frame = f;
  sk.bottom.frame.o = f.world(surface(0.f, 0.f));
  sk.bottom.frame.z = -f.z;
  sk.bottom.frame.x = -f.x;
  sk.bottom.dist = inst.dist_root;
  sk.has_bottom = true;
}

} // namespace plant
} // namespace gpx
