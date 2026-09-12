// Geekatplay TerraForge - the segment: the part almost every plant is
// mostly made of (plant_internal.hpp: build_segment).
//
// A trunk, a branch, a stem, a twig, a root, a petiole - one part with an
// axis, a radius that tapers along it, a cross-section, and children spread
// over it. The manual's Segment node is the most complicated thing in the
// plant tool and this is the same shape: the axis (plant_axis.cpp), the body
// meshed as rings round it here, the caps and the root flares
// (plant_segment_extra.cpp), the blades (plant_blades.cpp), and the sockets
// the walker hangs children on.
//
// Two things happen here that could not happen anywhere else. The children
// are planned before the body is meshed - they shrink the radius where they
// emerge and kink the axis into the zig-zag a twig has - and the base is
// blended into the parent, which is what stops a branch from looking like a
// stick pushed into a trunk. Both read the same attachment plan the walker
// will use, because both come from the same hashed draws.
//
// Skin "None" is not a missing body, it is a deliberate one: the segment
// becomes a pure distributor of its children (a whorl point, a stem that is
// only there to carry leaves), with an axis and sockets but no triangles.
#include "plant/plant_internal.hpp"
#include "gpx/noise_core.hpp"
#include <algorithm>

namespace gpx {
namespace plant {

namespace {

// The cross-section: a radius factor by the angle round the axis (0..1) and
// the position along it, from the section choice, its squash and its twist.
std::function<float(float, float)> make_section(const ParamReader &pr) {
  const int kind = pr.choice("section");
  const float squash = std::max(pr.random("section_squash", 0.f, 0, 1.f), 0.05f);
  const float twist = pr.random("section_twist");
  const Curve custom = pr.curve("section_custom");
  return [kind, squash, twist, custom](float a, float t) {
    a += twist * t;
    a -= std::floor(a);
    switch (kind) {
      case 1: { // an ellipse
        const float c = std::cos(a * TAU), s = std::sin(a * TAU);
        return squash / std::sqrt(squash * squash * c * c + s * s);
      }
      case 2: case 3: case 4: { // a triangle, a square, a star
        const int sides = kind == 2 ? 3 : kind == 3 ? 4 : 5;
        const float step = TAU / (float)sides;
        const float ang = a * TAU;
        const float k = std::fmod(ang, step) - step * 0.5f;
        const float poly = std::cos(step * 0.5f) / std::max(std::cos(k), 0.2f);
        if (kind != 4) return poly;
        // a star: every other corner pulled in
        const float lobe = 0.5f + 0.5f * std::cos(ang * (float)sides);
        return poly * (0.55f + 0.45f * lobe);
      }
      case 5: { // a flat blade
        const float c = std::cos(a * TAU), s = std::sin(a * TAU);
        const float flat = 0.15f;
        return flat / std::sqrt(flat * flat * c * c + s * s);
      }
      case 6: return std::max(custom.eval(a), 0.02f);
      default: return 1.f;
    }
  };
}

// The radial displacement: what makes bark bark rather than a smooth tube.
std::function<float(float, float, float)> make_displacement(const ParamReader &pr, uint32_t seed,
                                                            float radius) {
  const int source = pr.choice("rdisp_source");
  if (source == 0) return {};
  const bool relative = pr.b("rdisp_relative", true);
  const float amount = pr.random("rdisp_amount");
  const float offset = pr.f("rdisp_offset");
  const float scale = std::max(pr.f("rdisp_scale", 1.f), 0.01f);
  if (amount == 0.f && offset == 0.f) return {};
  const float unit = relative ? std::max(radius, 1e-4f) : 1.f;
  return [source, amount, offset, scale, unit, seed](float a, float t, float dist) {
    const float x = a * 6.f * scale, y = dist * 4.f * scale;
    float v = 0.f;
    noise::FbmParams p;
    p.octaves = 4;
    switch (source) {
      case 1: v = noise::fbm(x, y, seed, p) * 0.5f; break;                          // noise
      case 2: {                                                                     // bark ridges
        p.octaves = 3;
        v = noise::fbm_ridged(x * 1.7f, y * 0.35f, seed, p) - 0.55f;
      } break;
      case 3: {                                                                     // knots
        float f1 = 0.f, f2 = 0.f;
        noise::worley(x * 0.6f, y * 0.5f, seed, f1, f2, 1.f);
        v = std::max(0.f, 0.45f - f1) * 2.f;
      } break;
      default: v = 0.f; break;                                                      // a field: per vertex
    }
    (void)t;
    return (v * amount + offset) * unit;
  };
}

// A ring's vertex positions, before displacement.
V3 ring_point(const Instance::AxisSample &s, const V3 &nx, const V3 &ny, float a, float radius,
              const std::function<float(float, float)> &section) {
  const float k = section ? section(a, s.primal) : 1.f;
  const float ang = a * TAU;
  return s.p + (nx * std::cos(ang) + ny * std::sin(ang)) * (radius * k);
}

} // namespace

void build_segment(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk) {
  ParamReader pr(ctx, n, inst);
  const SeasonLook look = season_look(ctx, pr);
  for (int c = 0; c < 3; ++c) inst.tint[c] *= look.tint[c];

  // ---- the transform it was placed with
  float size = ctx.scale * pr.random("scale", 0.f, 0, 1.f);
  if (pr.b("scale_inherit", true)) size *= inst.scale;
  inst.scale = size;
  {
    Frame &f = inst.frame;
    f.o += f.dir(V3(pr.random("offset_x"), pr.random("offset_y"), pr.random("offset_z"))) * size;
    const float rx = pr.random("rot_x"), ry = pr.random("rot_y"), rz = pr.random("rot_z");
    if (rx != 0.f) f = f.rotated(f.x, rx * DEG);
    if (ry != 0.f) f = f.rotated(f.y, ry * DEG);
    if (rz != 0.f) f = f.rotated(f.z, rz * DEG);
  }

  // ---- how long and how thick
  float length = pr.random("length", 0.f, 0, 5.f) * size;
  length *= 0.25f + 0.75f * clampf(inst.sap, 0.f, 1.f); // sap shortens what it feeds
  length = std::max(length, pr.f("min_length") * size);
  if (inst.pruned && inst.cut_length > 0.f) {
    const float cut = std::min(length, inst.cut_length * size);
    inst.prune_ratio = cut / std::max(length, 1e-6f);
    length = cut;
  }
  length = std::max(length, 1e-4f);

  const int radius_mode = pr.choice("radius_mode", 1);
  const float inherit_ratio = pr.random("inherit_ratio", 0.f, 0, 0.45f);
  float radius = pr.random("radius", 0.f, 0, 0.2f) * size;
  if (radius_mode == 0 && inst.parent_radius > 0.f) radius = inst.parent_radius * inherit_ratio;
  else if (radius_mode == 2 && inst.parent_radius > 0.f)
    radius = std::min(radius, inst.parent_radius * inherit_ratio);
  radius *= 0.4f + 0.6f * clampf(inst.sap, 0.f, 1.f);
  if (inst.pruned) radius *= inst.cut_radius;
  radius = std::max(radius, std::max(pr.f("min_radius") * size, 1e-5f));
  inst.length = length;
  inst.radius = radius;

  // ---- what the children do to the body before it is meshed
  AxisParams ap;
  std::vector<std::pair<float, float>> shrinks; // primal, factor
  for (int slot = 1; slot <= 6; ++slot) {
    const Node *child = ctx.upstream_plant(n, "child " + std::to_string(slot));
    if (!child) continue;
    for (const Connection &c : attach_plan(ctx, *child, slot, inst, length)) {
      if (c.shrink > 0.f) shrinks.push_back({c.primal, clampf(1.f - c.shrink * 0.5f, 0.2f, 1.f)});
      if (c.bending > 0.f) {
        AxisParams::Kink k;
        k.primal = c.primal;
        k.angle = c.bending * 0.35f;
        ap.kinks.push_back(k);
      }
    }
  }
  std::stable_sort(shrinks.begin(), shrinks.end(),
                   [](const std::pair<float, float> &a, const std::pair<float, float> &b) { return a.first < b.first; });
  ap.kink_smooth = pr.random("bending_smoothness", 0.f, 0, 0.5f) + 0.05f;

  // ---- the axis
  ap.length = length;
  ap.radius = radius;
  ap.mode = pr.choice("axis_mode");
  ap.bend_deg = pr.random("axis_bend");
  const Curve axis_curve = pr.curve("axis_curve");
  ap.custom = &axis_curve;
  ap.tropism = pr.random("tropism");
  ap.gravity = ctx.gravity * pr.random("gust_gravity", 0.f, 0, 1.f);
  ap.perturb_strength = pr.random("perturb_strength");
  ap.perturb_planar = pr.f("perturb_planar");
  ap.perturb_frequency = pr.random("perturb_frequency", 0.f, 0, 2.f);
  ap.perturb_keep_tip = pr.b("perturb_keep_tip");
  ap.perturb_smooth_start = pr.b("perturb_smooth_start", true);
  ap.perturb_apply = pr.choice("perturb_apply", 1);
  ap.smoothing = pr.f("smoothing");
  ap.prevent_backfolds = pr.b("prevent_backfolds", true);
  ap.sampling_boost = pr.i("sampling_boost");
  ap.global_bias_strength = pr.random("global_bias_strength", 0.f, 0, 1.f);
  ap.orient_vertical = pr.random("orient_vertical");
  ap.orient_horizontal = pr.random("orient_horizontal");
  ap.bias_type = pr.choice("bias_type");
  ap.bias_strength = pr.random("bias_strength");
  ap.bias_dir = V3(pr.f("bias_dir_x"), pr.f("bias_dir_y", 1.f), pr.f("bias_dir_z"));
  ap.bias_origin = V3(pr.f("bias_origin_x"), pr.f("bias_origin_y"), pr.f("bias_origin_z"));
  ap.bias_local = pr.b("bias_local");
  ap.bias_length_agnostic = pr.b("bias_length_agnostic");
  ap.bias_repeller = pr.b("bias_repeller");
  ap.bias_cone_angle = pr.f("bias_cone_angle", 90.f);
  ap.bias_base_length = pr.f("bias_base_length", 1.f);
  ap.twist_planar = pr.b("twist_planar");
  ap.twist_symmetry = pr.i("twist_symmetry", 2);
  ap.twist_target = pr.f("twist_target");
  ap.noise_seed = hash_u32(ctx.seed, inst.id, hash_str("axis"));
  // the species' own biases, resolved for this instance
  for (const Node *b : ctx.biases) {
    if (!b) continue;
    const ParamReader bp(ctx, *b, inst);
    AxisParams::Bias g;
    g.type = bp.choice("bias_type");
    g.strength = bp.random("strength", 0.f, 0, 0.3f);
    g.dir = V3(bp.f("dir_x"), bp.f("dir_y", 1.f), bp.f("dir_z"));
    g.origin = V3(bp.f("origin_x"), bp.f("origin_y"), bp.f("origin_z"));
    g.local = bp.b("local");
    g.relative = bp.b("relative", true);
    g.repeller = bp.b("repeller");
    g.cone_angle = bp.f("cone_angle", 90.f);
    g.base_length = bp.f("base_length", 1.f);
    g.twist_planar = bp.b("twist_planar");
    g.twist_symmetry = bp.i("twist_symmetry", 2);
    g.twist_target = bp.f("twist_target");
    ap.global_biases.push_back(g);
  }

  // how finely: along the axis by its mode, then the detail and the level
  const int axial_mode = pr.choice("axial_mode", 1);
  float axial = pr.f("axial_number", 4.f);
  if (axial_mode == 1) axial *= length;
  else if (axial_mode == 2) axial = 6.f + 40.f * std::fabs(ap.bend_deg) / 180.f;
  const int samples = std::min(subdiv(ctx, axial, pr.i("axial_min", 3)) + 1 + ap.sampling_boost * 2, 400);
  std::vector<Instance::AxisSample> axis;
  float twist_total = 0.f;
  build_axis(ctx, ap, inst.frame, samples, axis, twist_total);
  inst.twist_total = twist_total;

  // ---- the radius along it: the profile, the children, the parent's blend
  const Curve profile = pr.curve("radius_profile");
  const float blend_child = pr.random("blend_child", 0.f, 0, 0.5f);
  const float blend_lower = pr.random("blend_lower", 0.f, 0, 0.3f);
  for (Instance::AxisSample &s : axis) {
    float r = radius * std::max(profile.eval(s.primal), 0.f);
    for (const std::pair<float, float> &sh : shrinks)
      if (s.primal >= sh.first) r *= sh.second;
    if (inst.parent_radius > 0.f && blend_child > 1e-4f && inst.positioning != 0) {
      // The foot flares out to meet the parent's skin - a fillet, and only a
      // fillet. `blend_child` is a width on the child's own surface (the
      // manual's Blending group) measured in the parent's radii, so the same
      // number is the same-looking join on a trunk and on a twig. Reading it
      // as a share of the child's LENGTH, which is what this did, flared a
      // nine-metre branch out to the trunk's girth over four and a half of
      // them: every attachment on the tree became a sail.
      const float reach = clampf(blend_child * inst.parent_radius / std::max(length, 1e-4f), 0.f, 0.5f);
      if (reach > 1e-4f) {
        const float w = 1.f - smoothstep(0.f, reach, s.primal);
        // it stops just inside the parent's skin: flush, not poking through
        r = r + (inst.parent_radius * (1.f - 0.2f * blend_lower) - r) * w * 0.8f;
      }
    }
    s.radius = std::max(r, 1e-5f);
  }
  inst.axis = axis;

  // ---- the body
  const int skin = pr.choice("skin");
  SegmentShape sh;
  sh.length = length;
  sh.radius = radius;
  sh.rings = axis;
  sh.twist_total = twist_total;
  sh.section = make_section(pr);
  sh.rdisp = make_displacement(pr, hash_u32(ctx.seed, inst.id, hash_str("bark")), radius);
  // the buttress roots at the foot of a big trunk, as a swelling of the body
  FlareParams flare;
  flare.number = pr.i("flare_number");
  flare.randomness = pr.random("flare_randomness");
  flare.height = pr.random("flare_height", 0.f, 0, 1.f) * size;
  flare.swell = pr.random("flare_swell", 0.f, 0, 0.5f);
  flare.depth = pr.random("flare_depth", 0.f, 0, 0.8f) * size;
  flare.width = pr.random("flare_width", 0.f, 0, 0.5f);
  const Curve flare_shape = pr.curve("flare_shape");
  flare.shape = &flare_shape;
  flare.seed = hash_u32(ctx.seed, inst.id, hash_str("flare"));
  sh.body_material = ctx.material_for(n, 0);
  sh.cap_material = ctx.material_for(n, 4);
  sh.wind_phase = inst.wind_phase;
  sh.wind_flex = pr.random("wind_flexibility", 0.f, 0, 1.f);
  // the wind along this axis, worked out once: the rings below and the
  // sockets at the end both read these, and a child copies whichever one it
  // lands on, so no two things in the same place can disagree about it
  for (Instance::AxisSample &s : axis) {
    s.wind_phase = wind_phase_at(ctx, inst.dist_root + s.dist);
    s.wind_bend = wind_bend_at(ctx, inst, s.primal, sh.wind_flex);
  }
  inst.axis = axis;
  for (int c = 0; c < 4; ++c) sh.tint[c] = inst.tint[c];
  sh.skinned = skin == 0;

  const int radial_mode = pr.choice("radial_mode", 1);
  float radial_n = pr.f("radial_number", 24.f);
  if (radial_mode == 1) radial_n = std::max(6.f, TAU * radius * pr.f("radial_number", 24.f) * 0.5f);
  else if (radial_mode == 2) radial_n = 8.f + 24.f * radius;
  int radial = std::min(subdiv(ctx, radial_n, pr.i("radial_min", 5)), 128);
  const int symmetry = std::max(pr.i("radial_symmetry", 1), 1);
  if (symmetry > 1) radial = std::max(symmetry, (radial / symmetry) * symmetry);
  sh.radial = radial;

  // the texture coordinates the manual describes: how many times the bark
  // goes round, and how far up it goes
  const int uv_mode = pr.choice("uv_mode");
  const float u_tile = pr.f("u_tile", 1.f), v_tile = pr.f("v_tile", 1.f);
  const float u_offset = pr.f("u_offset"), v_offset = pr.f("v_offset");
  const float uv_twist = pr.f("uv_twist");
  const bool keep_aspect = pr.b("keep_aspect", true);
  const bool v_from_end = pr.b("map_v_from_end");
  const float circumference = std::max(TAU * radius, 1e-4f);
  const float u_rep = uv_mode == 0 ? std::max(1.f, std::round(circumference * u_tile)) : u_tile;
  sh.u_rep = u_rep;

  if (sh.skinned && axis.size() >= 2) {
    out.begin(inst, PlantPartKind::Body, sh.body_material, pr.b("double_sided"), "segment");
    const size_t stride = (size_t)radial + 1;
    std::vector<uint32_t> ids(axis.size() * stride);
    const bool field_disp = pr.driven("rdisp_amount");
    const float disp3 = pr.random("disp3_amount");
    for (size_t i = 0; i < axis.size(); ++i) {
      const Instance::AxisSample &s = axis[i];
      const V3 nx = s.n, ny = normalize(cross(s.t, s.n), perpendicular(s.t));
      const float wind4[4] = {s.wind_phase, s.wind_bend, 0.f, -1.f};
      for (int j = 0; j <= radial; ++j) {
        const float a = (float)j / (float)radial;
        V3 p = ring_point(s, nx, ny, a, s.radius, sh.section);
        // the normal from the ring and the taper, before displacement
        const V3 da = ring_point(s, nx, ny, a + 0.02f, s.radius, sh.section) -
                      ring_point(s, nx, ny, a - 0.02f, s.radius, sh.section);
        V3 nrm = normalize(cross(da, s.t), nx * std::cos(a * TAU) + ny * std::sin(a * TAU));
        if (dot(nrm, p - s.p) < 0.f) nrm = -nrm;
        float d = 0.f;
        if (sh.rdisp) d = sh.rdisp(a, s.primal, s.dist);
        if (flare.number > 0) d += segment_flare(flare, s.radius, length, a, s.primal);
        if (field_disp) {
          const PlantVars v = pr.vars(s.primal, a, s.radius);
          d += pr.field_at("rdisp_amount", v, 0.f) * std::max(s.radius, 1e-4f);
        }
        if (d != 0.f) p += normalize(p - s.p, nrm) * d;
        if (disp3 != 0.f) p += nrm * (disp3 * 0.05f * std::max(s.radius, 1e-4f));
        float u = a * u_rep + u_offset + uv_twist * s.primal;
        float v = uv_mode == 2 ? s.primal * v_tile
                               : (keep_aspect ? s.dist / circumference : s.dist) * v_tile;
        v += v_offset;
        if (v_from_end) v = -v;
        ids[i * stride + (size_t)j] = out.vertex(p, nrm, u, v, wind4, sh.tint);
        if (i == 0) {
          sh.bottom_ring.push_back(p);
          sh.bottom_nrm.push_back(nrm);
          sh.bottom_v = v;
        } else if (i + 1 == axis.size()) {
          sh.top_ring.push_back(p);
          sh.top_nrm.push_back(nrm);
          sh.top_v = v;
        }
      }
    }
    for (size_t i = 0; i + 1 < axis.size(); ++i)
      for (int j = 0; j < radial; ++j)
        out.quad(ids[i * stride + (size_t)j], ids[i * stride + (size_t)j + 1],
                 ids[(i + 1) * stride + (size_t)j + 1], ids[(i + 1) * stride + (size_t)j]);
    if (pr.choice("normal_mode") == 1) out.geometric_normals();
    out.end();
    ctx.mesh->primitives++;
  }

  // the caps, the root flares and the blades
  if (sh.skinned) segment_caps(ctx, n, inst, out, sh);
  segment_blades(ctx, n, inst, out, sh);

  // ---- where children may sit
  sk.along.clear();
  sk.along.reserve(axis.size());
  for (const Instance::AxisSample &s : axis) {
    Socket so;
    so.frame = Frame::along(s.p, s.t, nullptr);
    so.primal = s.primal;
    so.radius = s.radius;
    so.side_radius = s.radius;
    so.remaining = length * (1.f - s.primal);
    so.dist = inst.dist_root + s.dist;
    // the same two numbers this sample's ring of wood was stamped with
    so.wind_phase = s.wind_phase;
    so.wind_bend = s.wind_bend;
    sk.along.push_back(so);
  }
  if (!axis.empty()) {
    sk.tip.frame = Frame::along(axis.back().p, axis.back().t, nullptr);
    sk.tip.primal = 1.f;
    sk.tip.radius = axis.back().radius;
    // the distance the axis actually walked, not the length it was asked for:
    // a perturbed axis wanders and comes out longer, and the last ring of wood
    // is stamped with the walked figure. A child on the tip must start at the
    // same one or it begins a little out of step with the wood under it.
    sk.tip.dist = inst.dist_root + axis.back().dist;
    sk.tip.wind_phase = axis.back().wind_phase;
    sk.tip.wind_bend = axis.back().wind_bend;
    sk.has_tip = true;
    sk.bottom.frame = Frame::along(axis.front().p, -axis.front().t, nullptr);
    sk.bottom.primal = 0.f;
    sk.bottom.radius = axis.front().radius;
    sk.bottom.dist = inst.dist_root;
    sk.bottom.wind_phase = axis.front().wind_phase;
    sk.bottom.wind_bend = axis.front().wind_bend;
    sk.has_bottom = true;
  }
  ctx.height_est = std::max(ctx.height_est, axis.empty() ? 0.f : axis.back().p.y);
}

} // namespace plant
} // namespace gpx
