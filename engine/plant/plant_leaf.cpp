// Geekatplay TerraForge - the Leaf part: a card of width by length.
//
// The cheapest leaf there is and the one a tree wears ten thousand of: a
// rectangle in the frame's x-z plane (length along z, the growth axis, width
// across x, the face toward y), meshed as a small grid so it can fold at its
// mid-rib and curl along and across. Two or three cards crossed through the
// same axis stand in for a whole cluster of foliage seen at a distance, and
// the Diamond variant is the same card meshed as a rotated square so a
// picture drawn at 45 degrees wastes no texels. Everything here is one
// instance; the walker decides whether the leaf is there at all (presence)
// and what it wears (the material per season); this file decides its shape,
// its tint shift, its wind weights and which of its material slots each
// plane takes.
//
// Where the design is silent, the choices made here: curvature at 90
// degrees "closes on itself", so the card bends through twice the curvature
// angle over its length (90 becomes a half turn); the global axial/radial
// offsets are in leaf lengths (a value of 1 moves the leaf its own length);
// breeze flexibility f gives a flutter weight f + (1 - f) * t^3 along the
// card from the hook (0 -> only the tip moves, 1 -> the whole card moves);
// the material slot passed to BuildCtx::material_for is the 0-based index
// of the "material", "material 2".. ports as materials_of() lists them.
#include "plant/plant_internal.hpp"
#include <cmath>
#include <algorithm>

namespace gpx {
namespace plant {

namespace {

// The Transform block: scale (own, per axis, inherited), offsets along the
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

// The orientation modes: the card's face (frame y) is turned toward what the
// mode asks while its length keeps as much of the attachment's direction as
// it can. Outwards stands the card up along the plant's vertical.
void orient(Frame &f, int mode, const BuildCtx &ctx) {
  V3 face;
  switch (mode) {
    case 1: case 4: face = normalize(V3(ctx.opt.facing), V3(0, 0, 1)); break;
    case 2: {
      f.z = V3(0, 1, 0);
      face = normalize(V3(f.o.x, 0.f, f.o.z), V3(0, 0, 1));
    } break;
    case 3: face = V3(1, 0, 0); break;
    default: return;
  }
  // keep z, put y as near `face` as the plane across z allows
  V3 y = face - f.z * dot(face, f.z);
  if (length(y) < 1e-4f) y = perpendicular(f.z);
  f.y = normalize(y);
  f.x = normalize(cross(f.y, f.z));
  f.z = normalize(cross(f.x, f.y));
}

struct LeafShape {
  float L = 0.08f, W = 0.05f;
  float hook_u = 0.5f, hook_v = 0.f;
  float rib = 0.f;       // radians, the fold at the mid-rib (each half tilts by half)
  float curv_w = 0.f, curv_h = 0.f; // radians of total bend across / along
  V3 axis_scale{1, 1, 1};
  // The card in its local frame: u across (0..1), v along (0..1).
  V3 local(float u, float v) const {
    const float x0 = (u - hook_u) * W, s = (v - hook_v) * L;
    // across: an arc of angle curv_w over the width, then the rib fold
    float x = x0, y = 0.f;
    if (std::fabs(curv_w) > 1e-4f) {
      const float R = W / curv_w;
      const float a = x0 / R;
      x = R * std::sin(a);
      y = R * (1.f - std::cos(a));
    }
    y += std::fabs(x0) * std::tan(rib * 0.5f);
    // along: an arc of angle curv_h over the length, the section riding it
    float z = s, yy = y;
    if (std::fabs(curv_h) > 1e-4f) {
      const float R = L / curv_h;
      const float a = s / R;
      z = R * std::sin(a) - y * std::sin(a);
      yy = R * (1.f - std::cos(a)) + y * std::cos(a);
    }
    return V3(x * axis_scale.x, yy * axis_scale.y, z * axis_scale.z);
  }
};

} // namespace

void build_leaf(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk) {
  ParamReader pr(ctx, n, inst);
  const SeasonLook look = season_look(ctx, pr);
  V3 axis_scale;
  const float size = transform_block(ctx, pr, inst, axis_scale);

  LeafShape sh;
  sh.L = std::max(1e-4f, pr.random("length", 0.f, 0, 0.08f) * size * (1.f - look.shrink));
  sh.W = std::max(1e-4f, pr.random("width", 0.f, 0, 0.05f) * size * (1.f - look.shrink));
  {
    const Attribute *h = n.attrs.find("hook");
    if (h) { sh.hook_u = h->v2[0]; sh.hook_v = h->v2[1]; }
  }
  sh.rib = pr.random("midrib_angle") * DEG;
  sh.curv_w = pr.random("curvature_w") * DEG * 2.f;
  sh.curv_h = pr.random("curvature_h") * DEG * 2.f;
  sh.axis_scale = axis_scale;
  inst.length = sh.L;
  inst.radius = sh.W * 0.5f;

  // orientation, the global offsets and the dry droop, all on the frame
  const int orientation = pr.choice("orientation");
  orient(inst.frame, orientation, ctx);
  {
    const float ga = pr.random("global_axial"), gr = pr.random("global_radial");
    if (ga != 0.f) inst.frame.o.y += ga * sh.L;
    if (gr != 0.f) {
      const V3 r = normalize(V3(inst.frame.o.x, 0.f, inst.frame.o.z), V3(1, 0, 0));
      inst.frame.o += r * (gr * sh.L);
    }
  }
  if (look.droop_deg > 0.f) {
    const float avail = std::acos(clampf(dot(inst.frame.z, V3(0, -1, 0)), -1.f, 1.f));
    if (avail > 1e-4f) lean(inst.frame, V3(0, -1, 0), std::min(1.f, look.droop_deg * DEG / avail));
  }

  // colour: the shift, then the season and health tint
  hls_shift(inst.tint, pr.random("shift_hue"), pr.random("shift_lum"), pr.random("shift_sat"));
  for (int c = 0; c < 3; ++c) inst.tint[c] *= look.tint[c];
  const float tint4[4] = {inst.tint[0], inst.tint[1], inst.tint[2], inst.tint[3]};

  // wind: the card flutters from its hook to its tip
  const float breeze = pr.random("breeze_strength", 0.f, 0, 1.f);
  const float flex = clampf(pr.random("breeze_flexibility", 0.f, 0, 0.5f), 0.f, 1.f);
  const float phase = inst.wind_phase, bend = inst.wind_bend;
  auto wind = [&](float, float v, float w4[4]) {
    const float t = clampf((v - sh.hook_v) / std::max(1e-3f, 1.f - sh.hook_v), 0.f, 1.f);
    w4[0] = phase;
    w4[1] = bend;
    // The flutter is nothing at the hook and everything at the tip. It has
    // to be nothing at the hook: the hook is nailed to the twig, and a leaf
    // whose anchor flutters shakes free of the wood it grows on - which is
    // what `flex` used to do, sitting under the whole leaf as a floor and
    // moving its hook by a tenth of a metre on a 20 m tree. Flexibility
    // shapes how quickly the flutter builds along the blade instead: a limp
    // leaf ripples from near the stalk, a stiff one only at its tip.
    const float build = 1.f + 3.f * (1.f - flex);
    // in metres: a leaf tip ripples by about a third of the leaf's length.
    // The weight used to be a bare 0..1 and the wind scaled it by the whole
    // PLANT's height, so every leaf on a 22 m oak was smeared over 22 cm -
    // twice its own length - and the crown drew as green streaks.
    w4[2] = breeze * std::pow(t, build) * sh.L * 0.35f;
    w4[3] = -1.f;
  };

  // materials: which slot each plane takes
  const int mesh_kind = pr.choice("mesh_kind");
  const int planes = mesh_kind == 1 ? 2 : mesh_kind == 2 ? 3 : 1;
  std::vector<int> slots;
  {
    const std::vector<const Node *> mats = ctx.materials_of(n);
    for (size_t i = 0; i < mats.size(); ++i) if (mats[i]) slots.push_back((int)i);
  }
  std::vector<int> plane_mat(planes, 0);
  if (!slots.empty()) {
    Draw d(ctx.seed, inst.id, hash_str("mat_distribution"));
    const int mode = pr.choice("mat_distribution", 1);
    const int m = (int)slots.size();
    if (mode == 2 && m % planes == 0 && m >= planes) {
      const int g = (int)d.pick((uint32_t)(m / planes));
      for (int k = 0; k < planes; ++k) plane_mat[k] = slots[(size_t)(g * planes + k)];
    } else if (mode == 0) {
      for (int k = 0; k < planes; ++k) plane_mat[k] = slots[d.pick((uint32_t)m)];
    } else {
      const int one = slots[d.pick((uint32_t)m)];
      for (int k = 0; k < planes; ++k) plane_mat[k] = one;
    }
  }

  // resolution: 2^boost each way, never above 8, thinned by LOD and boost
  const int gw = std::min(8, std::max(1, subdiv(ctx, (float)(1 << std::max(0, pr.i("subdiv_w"))), 1)));
  const int gh = std::min(8, std::max(1, subdiv(ctx, (float)(1 << std::max(0, pr.i("subdiv_h", 1))), 1)));
  const int normals_mode = planes > 1 ? pr.choice("normals") : 0;
  const float hazard = pr.f("normal_hazard");
  const V3 plant_centre(0.f, ctx.height_est * 0.6f, 0.f);
  const PlantPartKind kind = orientation == 4 ? PlantPartKind::Card : PlantPartKind::Leaf;

  for (int k = 0; k < planes; ++k) {
    // the k-th plane is the card turned about its length axis
    Frame f = inst.frame;
    if (k > 0) f = f.rotated(f.z, PI / (float)planes * (float)k);
    V3 haz_axis(0, 1, 0);
    float haz_angle = 0.f;
    if (hazard > 0.f) {
      Draw d(ctx.seed, inst.id, hash_str("normal_hazard") + (uint32_t)k);
      haz_axis = normalize(V3(d.signed_unit(), d.signed_unit(), d.signed_unit()), V3(0, 1, 0));
      haz_angle = d.unit() * hazard * PI * 0.5f;
    }
    auto normal_of = [&](V3 p, V3 geometric) {
      V3 nn = geometric;
      if (normals_mode == 1) nn = f.z;
      else if (normals_mode == 2) nn = normalize(p - plant_centre, geometric);
      else if (normals_mode == 3) nn = normalize(p - f.o, geometric);
      if (haz_angle > 0.f) nn = rotate(nn, haz_axis, haz_angle);
      return nn;
    };
    auto at = [&](float u, float v, V3 &p, V3 &nn) {
      const float e = 1e-3f;
      const V3 c = sh.local(u, v);
      const V3 du = sh.local(u + e, v) - sh.local(u - e, v);
      const V3 dv = sh.local(u, v + e) - sh.local(u, v - e);
      p = f.world(c);
      nn = normal_of(p, normalize(f.dir(cross(dv, du)), f.y));
    };
    const int material = ctx.material_for(n, plane_mat[(size_t)k]);
    out.begin(inst, kind, material, true, "leaf");
    if (mesh_kind != 3) {
      grid(out, gw, gh, at, false, wind, tint4);
    } else {
      // the diamond: the same card in a coordinate system turned 45 degrees,
      // so the four corners are the hook side, the tip and the two flanks;
      // (a, b) walk the two diagonals. Diamond UV mode maps (a, b) straight.
      const bool diamond_uv = pr.choice("uv_mode") == 1;
      const bool rib_vertical = pr.choice("rib_orientation", 1) == 1;
      const int g = gw;
      std::vector<uint32_t> ids((size_t)(g + 1) * (g + 1));
      for (int j = 0; j <= g; ++j)
        for (int i = 0; i <= g; ++i) {
          const float a = (float)i / (float)g, b = (float)j / (float)g;
          const float u = 0.5f + (a - b) * 0.5f, v = (a + b) * 0.5f;
          V3 p, nn;
          at(u, v, p, nn);
          float w4[4];
          wind(u, v, w4);
          ids[(size_t)j * (g + 1) + i] = out.vertex(p, nn, diamond_uv ? a : u, diamond_uv ? b : v, w4, tint4);
        }
      for (int j = 0; j < g; ++j)
        for (int i = 0; i < g; ++i) {
          const uint32_t q00 = ids[(size_t)j * (g + 1) + i], q10 = ids[(size_t)j * (g + 1) + i + 1];
          const uint32_t q01 = ids[(size_t)(j + 1) * (g + 1) + i], q11 = ids[(size_t)(j + 1) * (g + 1) + i + 1];
          // a diagonal from q00 to q11 runs base->tip (vertical rib); q10-q01 runs flank to flank
          if (rib_vertical) { out.tri(q00, q10, q11); out.tri(q00, q11, q01); }
          else { out.tri(q00, q10, q01); out.tri(q10, q11, q01); }
        }
    }
    out.end();
  }

  // a leaf carries nothing, but its tip is a place nonetheless
  sk.tip.frame = inst.frame;
  sk.tip.frame.o = inst.frame.world(sh.local(sh.hook_u, 1.f));
  sk.tip.primal = 1.f;
  sk.tip.dist = inst.dist_root + sh.L;
  sk.has_tip = true;
  sk.bottom.frame = inst.frame;
  sk.has_bottom = true;
}

} // namespace plant
} // namespace gpx
