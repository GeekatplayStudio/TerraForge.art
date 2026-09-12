// Geekatplay TerraForge - the Flower part: a lathe of a lobed profile.
//
// A bloom is a tube whose radius is a function of two things rather than
// one: how far up it you are, and where round it you stand. Two profile
// curves give the first - an inner one through the sinus between the
// petals, an outer one through the petal's middle - and the In/Out filter
// blends between them `lobes` times round the circumference, so five lobes
// and a filter that rises and falls once give five petals with a notch
// between each pair. The other profile modes are the same lathe with a
// built-in radius curve: a cylinder for a spike, a disc for a floret, a
// cup for a tulip, a bell for a foxglove, a trumpet for a daffodil, or a
// curve the user draws. With the schema's defaults the node is a five
// lobed flower 3 cm tall, its petal tips one radius (3 cm) out.
//
// The base is plugged: over the first `plug_influence` metres the section
// blends to a circle of `plug_radius`, so the bloom meets its stalk in a
// stem rather than in a ring of petal edges. The axis bends by `axis_bend`
// over its length and by gravitropism toward the ground (a value of 1 is a
// right angle over the whole length), it twists by `twist` turns, and
// `axis_influence` decides how much the rings tilt with it: 0 keeps every
// ring parallel to the base, 1 stands each one square to the axis. The
// length is measured along that curve, so a bent flower stands lower than
// a straight one of the same length, as the manual says.
//
// Geometric twist is about sampling, not shape. The surface twists either
// way; with it on the mesh columns spiral with the lobes, so every column
// keeps its own place on the profile and the petal tips come out sharp;
// with it off the columns run straight and the lobes drift through them.
// The same distinction is offered to the UVs, which come in the manual's
// four modes: a disc (the flower as a circle in texture space, native by
// ring or projected from the vertices - what a painted petal picture
// wants) or a cylinder (u round, v along).
//
// Where the design is silent, the choices made here: the angular count is
// rounded up to a multiple of the symmetry factor times the lobe count, so
// every lobe is meshed alike and no petal tip lands between two columns;
// the built-in profiles are normalised to reach 1 at the mouth, so
// `radius` is the flower's own half-width whatever the mode; displacement
// is a share of the radius along the surface normal, wrapped in the angle
// so the seam never shows, and the plug's "modify normals and
// displacement" fades it out over the plug; front is the top of the
// flower (the inside of a cup) unless Invert front/back is set, which is
// the manual's default and the reason a petal reads lit from above; the
// wind flutter grows with the height fraction, because a bloom shakes at
// its rim and not at its stalk; and when several material ports are
// connected one is drawn per flower, so a bed of them comes out mixed.
#include "plant/plant_internal.hpp"
#include <algorithm>
#include <vector>

namespace gpx {
namespace plant {

namespace {

// The Transform group: scale (own, per axis, inherited), offsets along the
// frame, rotations about its axes. Returns the uniform size factor; the
// per-axis factors come back in `axis_scale`.
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

// Smooth lattice noise on (angle, height) with the angle wrapped, so the
// displacement meets itself round the back of the flower.
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
    n += amp;
    amp *= 0.5f;
    x *= 2.f;
    y *= 2.f;
    wrap *= 2;
  }
  return s / n;
}

// One place on the axis: where it is, which way it goes, and the basis the
// section is drawn in there.
struct Ring {
  V3 p, x, y, z;
};

float frac(float x) { return x - std::floor(x); }

// The built-in radius-by-height curves, every one reaching 1 at the mouth.
float builtin_profile(int mode, float t) {
  switch (mode) {
    case 1: return 1.f;                                                         // cylinder
    case 2: return std::pow(t, 0.22f);                                          // disc
    case 3: return 0.15f + 0.85f * std::sin(t * PI * 0.5f);                     // cup
    case 4: return 0.10f + 0.72f * std::pow(t, 1.9f) + 0.18f * smoothstep(0.78f, 1.f, t); // bell
    default: return 0.10f + 0.90f * std::pow(smoothstep(0.35f, 1.f, t), 2.2f);  // trumpet
  }
}

int first_material(BuildCtx &ctx, const Node &n, const Instance &inst) {
  const std::vector<const Node *> mats = ctx.materials_of(n);
  std::vector<int> slots;
  for (size_t i = 0; i < mats.size(); ++i)
    if (mats[i]) slots.push_back((int)i);
  if (slots.empty()) return ctx.material_for(n, 0);
  Draw d(ctx.seed, inst.id, hash_str("material"));
  return ctx.material_for(n, slots[d.pick((uint32_t)slots.size())]);
}

} // namespace

void build_flower(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk) {
  ParamReader pr(ctx, n, inst);
  const SeasonLook look = season_look(ctx, pr);
  V3 axis_scale;
  const float size = transform_block(ctx, pr, inst, axis_scale);
  tropism_block(pr, inst.frame);
  if (look.droop_deg > 0.f) {
    const float avail = std::acos(clampf(dot(inst.frame.z, V3(0, -1, 0)), -1.f, 1.f));
    if (avail > 1e-4f) lean(inst.frame, V3(0, -1, 0), std::min(1.f, look.droop_deg * DEG / avail));
  }
  for (int c = 0; c < 3; ++c) inst.tint[c] *= look.tint[c];
  const float tint4[4] = {inst.tint[0], inst.tint[1], inst.tint[2], inst.tint[3]};
  const Frame f = inst.frame;

  const float dry = 1.f - look.shrink;
  const float L = std::max(1e-5f, pr.random("length", 0.f, 0, 0.03f) * size * dry * std::max(0.01f, axis_scale.z));
  const float R = std::max(1e-6f, pr.random("radius", 0.f, 0, 0.03f) * size * dry);
  inst.length = L;
  inst.radius = R * std::max(axis_scale.x, axis_scale.y);

  // ---- the profile: two curves blended round the circumference, or one
  // of the built-in shapes, or the curve the user drew
  const int mode = pr.choice("profile_mode");
  const int lobes = std::max(1, pr.i("lobes", 5));
  const Curve &inner = pr.curve("inner_profile");
  const Curve &outer = pr.curve("outer_profile");
  const Curve &filter = pr.curve("inout_filter");
  const Curve &custom = pr.curve("custom_profile");
  auto profile = [&](float pa, float t) -> float {
    if (mode == 6) return std::max(0.f, custom.eval(t));
    if (mode != 0) return std::max(0.f, builtin_profile(mode, t));
    const float k = filter.eval(frac(pa * (float)lobes));
    const float a = inner.eval(t), b = outer.eval(t);
    return std::max(0.f, a + (b - a) * k);
  };
  const float plug_r = std::max(0.f, pr.random("plug_radius", 0.f, 0, 0.003f)) * size * dry;
  const float plug_inf = std::max(0.f, pr.random("plug_influence", 0.f, 0, 0.01f)) * size * dry;
  const bool plug_modify = pr.b("plug_modify", true);
  auto plug_blend = [&](float t) { return plug_inf > 1e-6f ? smoothstep(0.f, plug_inf, t * L) : 1.f; };

  // ---- meshing counts: per metre when the mode asks, then the root's
  // detail and LOD, then rounded up so every lobe is meshed alike
  const bool by_unit = pr.choice("mesh_mode") == 1;
  const float axial = pr.f("axial_subdiv", 8.f) * (by_unit ? L : 1.f);
  const float angular = pr.f("angular_subdiv", 32.f) * (by_unit ? TAU * R : 1.f);
  const int rings = std::min(400, std::max(1, subdiv(ctx, axial, std::max(1, pr.i("axial_min", 3)))));
  int cols = std::min(1024, std::max(3, subdiv(ctx, angular, std::max(3, pr.i("angular_min", 8)))));
  const int step = std::max(1, pr.i("symmetry", 1)) * (mode == 0 ? lobes : 1);
  if (step > 1 && step <= 512) cols = ((std::max(cols, step) + step - 1) / step) * step;
  cols = std::min(cols, 2048);

  // ---- the axis: bend, gravitropism and the twist it carries to children
  const float bend = pr.random("axis_bend") * DEG;
  const float grav = pr.random("gravitropism");
  const float twist = pr.random("twist");
  const float influence = clampf(pr.f("axis_influence", 0.5f), 0.f, 1.f);
  inst.twist_total = twist;
  const int AX = std::min(512, std::max(32, rings * 4));
  std::vector<Ring> axis((size_t)AX + 1);
  {
    Ring r;
    r.p = f.o;
    r.x = f.x;
    r.y = f.y;
    r.z = f.z;
    axis[0] = r;
    const float dl = L / (float)AX;
    const float dbend = bend / (float)AX;
    const float dgrav = std::fabs(grav) * (PI * 0.5f) / (float)AX;
    const V3 down = grav >= 0.f ? V3(0, -1, 0) : V3(0, 1, 0);
    for (int i = 1; i <= AX; ++i) {
      if (dbend != 0.f) {
        r.z = rotate(r.z, r.x, dbend);
        r.y = rotate(r.y, r.x, dbend);
      }
      if (dgrav > 1e-9f && dot(r.z, down) < 0.9999f) {
        // straight up and asked to bow: any axis across the flower will do
        const V3 k = cross(r.z, down);
        const V3 kk = length(k) > 1e-6f ? normalize(k) : r.x;
        r.z = rotate(r.z, kk, dgrav);
        r.x = rotate(r.x, kk, dgrav);
        r.y = rotate(r.y, kk, dgrav);
      }
      r.z = normalize(r.z, f.z);
      r.p += r.z * dl;
      axis[(size_t)i] = r;
    }
  }
  // The section's basis at `t`: the axis' own basis when the rings follow
  // the axis, the flower's base plane when they do not.
  auto ring_at = [&](float t) {
    const float x = clampf(t, 0.f, 1.f) * (float)AX;
    const size_t i = std::min((size_t)AX - 1, (size_t)x);
    const float fr = x - (float)i;
    Ring r;
    r.p = lerp(axis[i].p, axis[i + 1].p, fr);
    const V3 tangent = normalize(lerp(axis[i].z, axis[i + 1].z, fr), axis[i].z);
    const V3 ref = normalize(lerp(axis[i].x, axis[i + 1].x, fr), axis[i].x);
    r.z = normalize(lerp(f.z, tangent, influence), tangent);
    V3 xx = ref - r.z * dot(ref, r.z);
    r.x = length(xx) > 1e-6f ? normalize(xx) : perpendicular(r.z);
    r.y = cross(r.z, r.x);
    return r;
  };

  // ---- the surface. `a` is the sampling parameter round the flower; the
  // geometric twist decides whether the columns spiral with the lobes (so
  // each keeps its own place on the profile) or run straight up.
  const bool geo_twist = pr.b("geometric_twist", true);
  const float disp_amount = pr.random("disp_amount");
  const bool disp_driven = pr.driven("disp_amount");
  const uint32_t nseed = hash_u32(inst.id, hash_str("disp_amount"));
  auto base_at = [&](float a, float t) {
    const Ring r = ring_at(t);
    const float pa = geo_twist ? a : a - twist * t;
    const float psi = (geo_twist ? a + twist * t : a) * TAU;
    float rad = profile(pa, clampf(t, 0.f, 1.f)) * R;
    const float b = plug_blend(t);
    rad = plug_r + (rad - plug_r) * b;
    return r.p + (r.x * (std::cos(psi) * axis_scale.x) + r.y * (std::sin(psi) * axis_scale.y)) * rad;
  };
  auto base_normal = [&](float a, float t) {
    const float da = 0.25f / (float)cols, dt = 0.25f / (float)rings;
    const V3 pa = base_at(a + da, t) - base_at(a - da, t);
    const V3 pt = base_at(a, std::min(t + dt, 1.f)) - base_at(a, std::max(t - dt, 0.f));
    V3 nn = cross(pa, pt);
    if (dot(nn, nn) < 1e-24f) {
      const Ring r = ring_at(t);
      nn = r.x * std::cos(a * TAU) + r.y * std::sin(a * TAU);
    }
    return normalize(nn, f.z);
  };
  auto displacement = [&](float a, float t) -> float {
    if (!disp_driven && disp_amount == 0.f) return 0.f;
    float amount = disp_amount;
    if (disp_driven) {
      // the field sees where it is: along the flower, round it, and how far
      // out from the axis in metres
      const float rad = profile(geo_twist ? a : a - twist * t, clampf(t, 0.f, 1.f)) * R;
      amount = pr.field_at("disp_amount", pr.vars(t, frac(a), rad), disp_amount);
    }
    const float b = plug_modify ? plug_blend(t) : 1.f;
    return amount * R * 0.5f * fbm(a * 6.f, t * 6.f, 6, nseed) * b;
  };
  const bool displaced = disp_driven || disp_amount != 0.f;
  auto surface = [&](float a, float t) {
    const V3 p = base_at(a, t);
    return displaced ? p + base_normal(a, t) * displacement(a, t) : p;
  };
  auto normal_at = [&](float a, float t) {
    if (!displaced) return base_normal(a, t);
    const float da = 0.25f / (float)cols, dt = 0.25f / (float)rings;
    const V3 pa = surface(a + da, t) - surface(a - da, t);
    const V3 pt = surface(a, std::min(t + dt, 1.f)) - surface(a, std::max(t - dt, 0.f));
    V3 nn = cross(pa, pt);
    if (dot(nn, nn) < 1e-24f) return base_normal(a, t);
    return normalize(nn, f.z);
  };

  // the widest the flower gets, for the projected UVs
  float rmax = 1e-6f;
  {
    const int probe = std::min(cols, 72);
    for (int i = 0; i <= rings; ++i)
      for (int j = 0; j < probe; ++j) {
        const V3 d = base_at((float)j / (float)probe, (float)i / (float)rings) - f.o;
        const float lx = dot(d, f.x), ly = dot(d, f.y);
        rmax = std::max(rmax, std::sqrt(lx * lx + ly * ly));
      }
  }

  // ---- the axis samples children and driven fields read
  inst.axis.clear();
  inst.axis.reserve((size_t)rings + 1);
  for (int i = 0; i <= rings; ++i) {
    const float t = (float)i / (float)rings;
    const Ring r = ring_at(t);
    Instance::AxisSample s;
    s.p = r.p;
    s.t = normalize(axis[std::min((size_t)AX, (size_t)((float)t * (float)AX))].z, f.z);
    s.n = r.x;
    float mean = 0.f;
    for (int k = 0; k < 8; ++k) mean += profile((float)k / 8.f, t);
    s.radius = mean / 8.f * R * plug_blend(t) + plug_r * (1.f - plug_blend(t));
    s.primal = t;
    s.dist = t * L;
    inst.axis.push_back(s);
  }

  // ---- UVs
  const int uv_mode = pr.choice("uv_mode", 1);
  const float uv_twist = pr.b("uv_geometric_twist", true) ? twist : pr.random("uv_twist");
  const float mapping = (float)std::max(1, pr.i("angular_mapping", 1));
  const bool axial_custom = pr.choice("axial_mapping") == 1;
  const Curve &axial_map = pr.curve("axial_map_curve");
  auto uv_of = [&](float a, float t, V3 p, float &u, float &v) {
    const float turn = a + uv_twist * t;
    const float vmap = axial_custom ? axial_map.eval(clampf(t, 0.f, 1.f)) : t;
    const V3 d = p - f.o;
    const float lx = dot(d, f.x), ly = dot(d, f.y);
    switch (uv_mode) {
      case 0: { // disc, native: the ring is a circle of the mapped radius
        const float rho = 0.5f * vmap, ang = turn * mapping * TAU;
        u = 0.5f + rho * std::cos(ang);
        v = 0.5f + rho * std::sin(ang);
      } break;
      case 2: // cylinder, native
        u = turn * mapping;
        v = vmap;
        break;
      case 3: { // cylinder, projected: the vertex's own polar coordinates
        float pol = std::atan2(ly, lx) / TAU;
        pol += std::round(turn - pol); // the branch nearest the sample, so no seam
        u = pol * mapping;
        v = dot(d, f.z) / L;
      } break;
      default: { // disc, projected: the flower seen from above, turned by the UV twist
        const float c = std::cos(uv_twist * t * TAU), s = std::sin(uv_twist * t * TAU);
        u = 0.5f + 0.5f * (lx * c - ly * s) / rmax;
        v = 0.5f + 0.5f * (lx * s + ly * c) / rmax;
      } break;
    }
  };

  // ---- the mesh. Front is the top of the flower unless the user swaps
  // it; an open radial profile leaves the last wedge out.
  const bool closed = pr.b("radial_continuity", true);
  const bool flip = !pr.b("invert_faces", false);
  const bool geometric_normals = pr.choice("normal_mode", 1) == 1;
  const int last = closed ? cols : cols - 1;
  const float breeze = pr.random("wind_strength", 0.f, 0, 1.f);
  const size_t stride = (size_t)last + 1;
  std::vector<uint32_t> ids((size_t)(rings + 1) * stride);
  out.begin(inst, PlantPartKind::Petal, first_material(ctx, n, inst), true, "flower");
  for (int i = 0; i <= rings; ++i) {
    const float t = (float)i / (float)rings;
    // in metres: the rim of a bloom ripples by about half its radius
    const float w4[4] = {inst.wind_phase, inst.wind_bend, breeze * t * R * 0.5f, -1.f};
    for (int j = 0; j <= last; ++j) {
      const float a = (float)j / (float)cols;
      const V3 p = surface(a, t);
      V3 nn = normal_at(a, t);
      if (flip) nn = -nn;
      float u = 0.f, v = 0.f;
      uv_of(a, t, p, u, v);
      ids[(size_t)i * stride + (size_t)j] = out.vertex(p, nn, u, v, w4, tint4);
    }
  }
  for (int i = 0; i < rings; ++i)
    for (int j = 0; j < last; ++j) {
      const uint32_t a = ids[(size_t)i * stride + (size_t)j];
      const uint32_t b = ids[(size_t)i * stride + (size_t)j + 1];
      const uint32_t c = ids[(size_t)(i + 1) * stride + (size_t)j + 1];
      const uint32_t d = ids[(size_t)(i + 1) * stride + (size_t)j];
      if (flip) out.quad(a, d, c, b);
      else out.quad(a, b, c, d);
    }
  if (geometric_normals) out.geometric_normals();
  out.end();

  // ---- where children go: the mouth and the foot
  const Ring top = ring_at(1.f);
  sk.tip.frame.o = top.p;
  sk.tip.frame.z = normalize(axis[(size_t)AX].z, f.z);
  sk.tip.frame.x = normalize(top.x - sk.tip.frame.z * dot(top.x, sk.tip.frame.z), perpendicular(sk.tip.frame.z));
  sk.tip.frame.y = cross(sk.tip.frame.z, sk.tip.frame.x);
  sk.tip.primal = 1.f;
  sk.tip.radius = inst.axis.empty() ? 0.f : inst.axis.back().radius;
  sk.tip.side_radius = sk.tip.radius;
  sk.tip.dist = inst.dist_root + L;
  sk.has_tip = true;
  sk.bottom.frame = f;
  sk.bottom.frame.z = -f.z;
  sk.bottom.frame.x = -f.x;
  sk.bottom.frame.y = cross(sk.bottom.frame.z, sk.bottom.frame.x);
  sk.bottom.primal = 0.f;
  sk.bottom.radius = plug_inf > 1e-6f ? plug_r : (inst.axis.empty() ? 0.f : inst.axis.front().radius);
  sk.bottom.side_radius = sk.bottom.radius;
  sk.bottom.dist = inst.dist_root;
  sk.has_bottom = true;
}

} // namespace plant
} // namespace gpx
