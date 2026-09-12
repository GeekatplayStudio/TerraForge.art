// Geekatplay TerraForge - how a part sits on the part it grows from
// (plant_internal.hpp: attach, attach_plan, attach_at).
//
// This is the manual's Add Children tab, and it is where most of what makes
// a plant look like that plant lives: how many children there are, where
// along the parent they start and stop, how they are spread around it, how
// far they lean out of it, and which of them were never there at all.
//
// The order matters and is fixed here once for every part: the count and the
// range give a list of connections along the parent's axis (attach_plan);
// each connection may carry a whorl of several children; each child gets an
// azimuth from the arrangement, the roll and the coil; each is placed on the
// axis or on the skin, tilted by its angle, turned by its rotation, leaned
// by its tropism; and each may be pruned away or cut short. Every one of
// those draws hashes the parent's instance id, the child's node id and the
// slot, so a species grows the same plant on every machine, and adding a
// second child node never moves the first one's leaves.
//
// attach_plan is public because a segment needs the connections before it
// meshes itself: children shrink the parent's radius where they emerge and
// kink it into a zig-zag. The plan is drawn from the same hashes as the
// instances, so the two always agree about where the branches are.
#include "plant/plant_internal.hpp"
#include <algorithm>

namespace gpx {
namespace plant {

namespace {

// A position in 0..1 drawn from a density curve, by inverting its running
// sum: where the curve is high, connections crowd.
float place_by_density(const Curve &c, float u) {
  const int B = 64;
  float cdf[B + 1];
  cdf[0] = 0.f;
  for (int i = 0; i < B; ++i) {
    const float t = ((float)i + 0.5f) / (float)B;
    cdf[i + 1] = cdf[i] + std::max(c.eval(t), 0.f);
  }
  if (cdf[B] <= 1e-9f) return u;
  const float want = clampf(u, 0.f, 1.f) * cdf[B];
  int i = 0;
  while (i < B && cdf[i + 1] < want) ++i;
  const float span = std::max(cdf[i + 1] - cdf[i], 1e-9f);
  return clampf(((float)i + (want - cdf[i]) / span) / (float)B, 0.f, 1.f);
}

// start / end by their mode: relative to the parent, or a distance from its
// base or its tip.
float range_end(int mode, float value, float parent_length) {
  const float L = std::max(parent_length, 1e-5f);
  switch (mode) {
    case 1: return value / L;        // absolute from the start
    case 2: return 1.f - value / L;  // absolute from the end
    default: return value;
  }
}

int rounded_count(float n, int soft, Draw &d, float &extra_scale) {
  const int whole = (int)std::floor(n);
  const float frac = n - (float)whole;
  extra_scale = 1.f;
  if (frac <= 1e-4f) return whole;
  switch (soft) {
    case 1: return whole + 1;                                   // offset the fractional part
    case 2: extra_scale = frac; return whole + 1;               // scale the extra one down
    case 3: return whole + (d.unit() < frac ? 1 : 0);           // or have it at random
    default: return (int)std::lround(n);
  }
}

} // namespace

Instance provisional_child(const BuildCtx &ctx, const Node &child, int slot, const Instance &parent) {
  Instance c;
  c.node = child.id;
  c.kind = kind_of(child);
  c.parent = &parent;
  c.slot = slot;
  c.depth = parent.depth + 1;
  c.id = hash_u32(parent.id, ctx.key_of(child), (uint32_t)slot);
  c.frame = parent.frame;
  c.scale = parent.scale;
  c.sap = parent.sap;
  c.density = parent.density;
  c.lod = parent.lod;
  c.iteration = parent.iteration;
  c.iterations = parent.iterations;
  c.parent_length = parent.length;
  c.parent_radius = parent.radius;
  c.parent_remaining = parent.length;
  c.dist_root = parent.dist_root;
  c.height_frac = parent.height_frac;
  c.rnd = hash_unit(hash_u32(ctx.seed, c.id));
  return c;
}

std::vector<Connection> attach_plan(BuildCtx &ctx, const Node &child, int slot, const Instance &parent,
                                    float length) {
  std::vector<Connection> out;
  const Instance prov = provisional_child(ctx, child, slot, parent);
  const ParamReader pr(ctx, child, prov);
  const uint32_t key = hash_u32(parent.id, ctx.key_of(child), (uint32_t)slot);
  Draw d(ctx.seed, key, hash_str("attach"));

  // how many
  float count = pr.random("count", 0.f, 0, 8.f) * clampf(parent.density, 0.f, 1.f);
  switch (pr.choice("count_mode")) {
    case 1: count *= std::max(length, 0.f); break;
    case 2: count *= std::max(length, 0.f) * 0.1f; break;
    default: break;
  }
  if (count <= 0.f) return out;
  float extra_scale = 1.f;
  int n = rounded_count(count, pr.choice("soft_insert"), d, extra_scale);
  n = std::min(n, 4096);
  if (n <= 0) return out;

  // where along the parent
  float s0 = range_end(pr.choice("start_mode"), pr.random("start", 0.f, 0, 0.2f), length);
  float s1 = range_end(pr.choice("end_mode"), pr.random("end", 0.f, 0, 1.f), length);
  // the manual allows values outside 0..1: they gather the connections at
  // that end rather than putting them off the plant
  const float gather_lo = std::max(0.f - s0, 0.f), gather_hi = std::max(s1 - 1.f, 0.f);
  s0 = clampf(s0, 0.f, 1.f);
  s1 = clampf(s1, 0.f, 1.f);
  if (s1 < s0) std::swap(s0, s1);
  const float margin = pr.f("margin_before_cut", 0.f);
  if (margin > 0.f && length > 1e-5f) s1 = std::min(s1, 1.f - margin / length);
  if (s1 <= s0) s1 = std::min(1.f, s0 + 1e-3f);

  const Curve &density = pr.curve("density");
  const float randomness = pr.random("randomness");
  const float pruning = pr.random("pruning");
  const int arrangement = pr.choice("arrangement");
  const bool paired = arrangement == 2 || arrangement == 3;
  const int pair_mode = pr.choice("pair_offset_mode");
  const float pair_offset = pr.random("pair_offset");
  const float spacing = (s1 - s0) / (float)std::max(n, 1);

  for (int i = 0; i < n; ++i) {
    const float u = n > 1 ? (float)i / (float)(n - 1) : 0.5f;
    float t = s0 + (s1 - s0) * place_by_density(density, u);
    // the ends gather what was asked for beyond them
    if (gather_lo > 0.f) t = s0 + (t - s0) * (1.f / (1.f + gather_lo));
    if (gather_hi > 0.f) t = s1 - (s1 - t) * (1.f / (1.f + gather_hi));
    if (randomness > 0.f) t += d.signed_unit() * randomness * spacing;
    t = clampf(t, 0.f, 1.f);
    if (pruning > 0.f && d.unit() < pruning) continue;
    Connection c;
    c.primal = t;
    c.index = i;
    c.pair = 0;
    c.scale = (i == n - 1) ? extra_scale : 1.f;
    c.shrink = pr.random("shrink_radius", t);
    c.bending = pr.random("bending", t);
    out.push_back(c);
    if (paired) {
      Connection p = c;
      p.pair = 1;
      p.primal = clampf(pair_mode == 1 ? t + pair_offset / std::max(length, 1e-5f) : t + pair_offset * spacing,
                        0.f, 1.f);
      out.push_back(p);
    }
  }
  std::stable_sort(out.begin(), out.end(),
                   [](const Connection &a, const Connection &b) { return a.primal < b.primal; });
  return out;
}

std::vector<float> attach_positions(BuildCtx &ctx, const Node &child, int slot, const Instance &parent,
                                    float length) {
  std::vector<float> out;
  for (const Connection &c : attach_plan(ctx, child, slot, parent, length)) out.push_back(c.primal);
  return out;
}

void apply_transform(const BuildCtx &ctx, const Node &n, Instance &inst) {
  const ParamReader pr(ctx, n, inst);
  float size = ctx.scale * pr.random("scale", 0.f, 0, 1.f);
  if (pr.b("scale_inherit", true)) size *= inst.scale;
  inst.scale = size;
  Frame &f = inst.frame;
  f.o += f.dir(V3(pr.random("offset_x"), pr.random("offset_y"), pr.random("offset_z"))) * size;
  const float rx = pr.random("rot_x"), ry = pr.random("rot_y"), rz = pr.random("rot_z");
  if (rx != 0.f) f = f.rotated(f.x, rx * DEG);
  if (ry != 0.f) f = f.rotated(f.y, ry * DEG);
  if (rz != 0.f) f = f.rotated(f.z, rz * DEG);
}

Instance attach_at(BuildCtx &ctx, const Node &child, int slot, const Instance &parent, const Socket &s,
                   const Node &parent_node, int index) {
  (void)parent_node;
  Instance c = provisional_child(ctx, child, slot, parent);
  c.index = index;
  c.id = hash_u32(parent.id, ctx.key_of(child), (uint32_t)(slot * 65536 + index));
  c.rnd = hash_unit(hash_u32(ctx.seed, c.id));
  const ParamReader pr(ctx, child, c);
  Draw d(ctx.seed, c.id, hash_str("place"));

  c.parent_primal = s.primal;
  c.parent_radius = s.radius;
  c.parent_length = parent.length;
  c.parent_remaining = s.remaining;
  c.azimuth = s.azimuth;
  c.dist_root = s.dist;
  c.positioning = pr.choice("positioning", 2);

  // the frame: out of the parent at the socket, then tilted by the angle
  Frame f = s.frame;
  const V3 outward = f.z;          // the socket's own direction (radial, or the tip's)
  const V3 along = f.x;            // the parent's tangent there
  const float move_out = pr.random("move_out", s.primal);
  if (move_out != 0.f) f.o += outward * (move_out * std::max(s.radius, 1e-4f));
  const float angle = pr.random("angle", s.primal, 0, 30.f) * DEG;
  V3 z = normalize(outward * std::cos(angle) + along * std::sin(angle), outward);
  f = Frame::along(f.o, z, &f);
  const float rotation = pr.random("rotation", s.primal);
  if (rotation != 0.f) f = f.rotated(f.z, rotation * DEG);

  // the tropism cone: the children turned toward a direction of their own
  const float tro_angle = pr.random("tropism_angle");
  const float tro_roll = pr.random("tropism_roll");
  const float tro_rot = pr.random("tropism_rotation");
  if (tro_angle != 0.f || tro_roll != 0.f || tro_rot != 0.f) {
    V3 dir(pr.f("tropism_x"), pr.f("tropism_y", 1.f), pr.f("tropism_z"));
    if (pr.b("tropism_local")) dir = parent.frame.dir(dir);
    dir = normalize(dir, V3(0, 1, 0));
    const float cone = pr.random("tropism_cone", 0.f, 0, 90.f) * DEG;
    // the direction on the cone nearest the way the child already grows
    V3 side = f.z - dir * dot(f.z, dir);
    if (length(side) < 1e-6f) side = perpendicular(dir);
    const V3 target = normalize(dir * std::cos(cone) + normalize(side) * std::sin(cone), dir);
    if (tro_angle != 0.f) {
      const float turn = std::acos(clampf(dot(f.z, target), -1.f, 1.f)) * clampf(tro_angle, 0.f, 1.f);
      V3 axis = cross(f.z, target);
      if (length(axis) > 1e-6f && turn > 1e-6f) f = f.rotated(normalize(axis), turn);
    }
    if (tro_roll != 0.f) {
      // roll it round the parent toward the target's side
      const float want = std::atan2(dot(cross(outward, target), along), dot(outward, target));
      f = f.rotated(along, want * clampf(tro_roll, 0.f, 1.f));
    }
    if (tro_rot != 0.f) f = f.rotated(f.z, TAU * 0.25f * clampf(tro_rot, -1.f, 1.f));
  }
  c.frame = f;

  // what it inherits: fewer children, a smaller size, less sap the further
  // it is from the root
  c.density = clampf(parent.density * pr.random("inherit_density", 0.f, 0, 1.f), 0.f, 1.f);
  c.sap = clampf(parent.sap * pr.random("inherit_sap", 0.f, 0, 1.f), 0.f, 1.f);
  c.scale = parent.scale * pr.random("inherit_scale", 0.f, 0, 1.f) * s.scale;

  // a cut: the manual's pruning of the children themselves
  const float cut_p = pr.random("cut_probability", s.primal);
  if (cut_p > 0.f && d.unit() < cut_p) {
    c.pruned = true;
    c.cut_length = std::max(pr.random("cut_length", s.primal, 0, 0.5f), 0.f);
    c.cut_radius = clampf(pr.random("cut_radius_reduction", s.primal, 0, 1.f), 0.05f, 1.f);
    ctx.mesh->cut++;
  }

  // where it stands in the plant, and how it moves in the wind
  c.height_frac = clampf(f.o.y / std::max(ctx.height_est, 1e-3f), 0.f, 1.f);
  // The wind the parent has at this point, taken from the socket exactly as
  // the parent stamped it - never worked out again here. Two vertices in the
  // same place whose wind numbers differ by even a hundredth are pulled
  // apart, and re-deriving the bend from the parent's flexibility read at a
  // different place along it did differ, by about a tenth: enough to shake
  // every leaf off its twig.
  // A builder that knows better - the segment, whose wind varies along its
  // axis - has filled the socket in; the rest hand on the part's own, which
  // is what their own vertices carry.
  c.wind_phase = s.wind_phase >= 0.f ? s.wind_phase : parent.wind_phase;
  c.wind_bend = s.wind_bend >= 0.f ? s.wind_bend : parent.wind_bend;
  for (int k = 0; k < 4; ++k) c.tint[k] = parent.tint[k];

  // how much of this kind of part exists at all, at this season and health
  const SeasonLook look = season_look(ctx, pr);
  c.presence = clampf(pr.random("presence", s.primal, 0, 1.f) * look.presence, 0.f, 1.f);
  return c;
}

std::vector<Instance> attach(BuildCtx &ctx, const Node &child_node, int slot, const Instance &parent,
                             const Sockets &sk, const Node &parent_node) {
  std::vector<Instance> out;
  const Instance prov = provisional_child(ctx, child_node, slot, parent);
  const ParamReader pr(ctx, child_node, prov);
  const int positioning = pr.choice("positioning", 2);
  if (positioning == 3) return out; // a dummy connection shapes the parent and grows nothing

  const Kind pk = kind_of(parent_node);
  const bool parent_places = pk == Kind::Urchin || pk == Kind::Hydra || pk == Kind::Growth;
  if (parent_places) {
    // the parent already said where its children go: one per socket
    int i = 0;
    for (const Socket &s : sk.along) out.push_back(attach_at(ctx, child_node, slot, parent, s, parent_node, i++));
    return out;
  }
  if (positioning == 0 || parent.axis.size() < 2) {
    if (sk.has_tip) out.push_back(attach_at(ctx, child_node, slot, parent, sk.tip, parent_node, 0));
    return out;
  }
  if (positioning == 6) {
    if (sk.has_bottom) out.push_back(attach_at(ctx, child_node, slot, parent, sk.bottom, parent_node, 0));
    return out;
  }

  // along the parent's own axis
  const std::vector<Connection> plan = attach_plan(ctx, child_node, slot, parent, parent.length);
  if (plan.empty()) return out;
  const float roll = pr.random("roll") * DEG;
  const float coil = pr.random("coil", 0.f, 0, 137.5f) * DEG;
  const int arrangement = pr.choice("arrangement");
  const bool influenced_by_twist = pr.b("influenced_by_twist", true);
  const float whorl_spread = pr.random("whorl_spread", 0.f, 0, 360.f) * DEG;
  const float whorl_rand = pr.random("whorl_randomness");
  const float whorl_angle_rand = pr.random("whorl_angle_randomness") * DEG;
  const float whorl_pos_rand = pr.random("whorl_position_randomness");
  const V3 up(0.f, 1.f, 0.f);
  int index = 0;

  for (const Connection &conn : plan) {
    Draw d(ctx.seed, hash_u32(prov.id, (uint32_t)conn.index, (uint32_t)conn.pair), hash_str("whorl"));
    float per = pr.random("per_whorl", conn.primal, 0, 1.f);
    float whorl_extra = 1.f;
    int members = rounded_count(std::max(per, 0.f), pr.choice("whorl_soft") == 0 ? 0 : pr.choice("whorl_soft") + 1,
                                d, whorl_extra);
    members = std::clamp(members, 0, 64);
    for (int w = 0; w < members; ++w) {
      // where on the axis, and which way round
      float primal = conn.primal;
      if (whorl_pos_rand > 0.f && parent.length > 1e-5f)
        primal = clampf(primal + d.signed_unit() * whorl_pos_rand * 0.1f, 0.f, 1.f);
      float az = roll + coil * (float)conn.index;
      switch (arrangement) {
        case 1: az += PI * (float)conn.index; break;                     // alternate
        case 2: az += PI * (float)conn.pair; break;                      // opposite
        case 3: az += PI * (float)conn.pair + PI * 0.5f * (float)conn.index; break; // decussate
        case 4: az = roll; break;                                        // simple
        default: break;                                                  // spiral
      }
      if (members > 1) az += whorl_spread * ((float)w / (float)members);
      if (whorl_rand > 0.f) az += d.signed_unit() * whorl_rand * PI;
      if (influenced_by_twist) az += parent.twist_total * TAU * primal;

      // the socket: interpolate the parent's axis there
      const float ft = clampf(primal, 0.f, 1.f) * (float)(parent.axis.size() - 1);
      const size_t i0 = std::min(parent.axis.size() - 2, (size_t)ft);
      const float fu = ft - (float)i0;
      const Instance::AxisSample &a0 = parent.axis[i0], &a1 = parent.axis[i0 + 1];
      const V3 p = lerp(a0.p, a1.p, fu);
      const V3 t = normalize(lerp(a0.t, a1.t, fu), a0.t);
      const V3 nx = normalize(lerp(a0.n, a1.n, fu) - t * dot(lerp(a0.n, a1.n, fu), t), perpendicular(t));
      const V3 ny = normalize(cross(t, nx), perpendicular(t));
      const float radius = std::max(a0.radius + (a1.radius - a0.radius) * fu, 1e-5f);
      const V3 r = normalize(nx * std::cos(az) + ny * std::sin(az), nx);

      Socket s;
      s.primal = primal;
      s.radius = radius;
      s.side_radius = radius;
      s.remaining = parent.length * (1.f - primal);
      // the walked distance and the wind, read off the parent's own axis the
      // same way its position and radius were: a child between two samples
      // starts exactly in step with the wood it comes out of
      s.dist = parent.dist_root + a0.dist + (a1.dist - a0.dist) * fu;
      s.wind_phase = a0.wind_phase + (a1.wind_phase - a0.wind_phase) * fu;
      s.wind_bend = a0.wind_bend + (a1.wind_bend - a0.wind_bend) * fu;
      s.scale = conn.scale * whorl_extra;
      s.whorl_index = w;
      // the azimuth the plant's own variables report: from the way up
      V3 up_r = up - t * dot(up, t);
      if (length(up_r) < 1e-5f) up_r = nx;
      up_r = normalize(up_r);
      s.azimuth = std::atan2(dot(cross(up_r, r), t), clampf(dot(up_r, r), -1.f, 1.f));
      // on the axis, on the skin, or just under it
      V3 origin = p;
      if (positioning == 2 || positioning == 4) origin = p + r * radius;
      else if (positioning == 5) origin = p + r * (radius * 0.7f);
      s.frame.o = origin;
      s.frame.z = r;
      s.frame.x = t;
      s.frame.y = normalize(cross(r, t), ny);
      if (positioning == 4) {
        // orthogonal to the surface: the skin's normal, which leans with the
        // radius changing along the axis
        const float dr = (a1.radius - a0.radius);
        const float dl = std::max(a1.dist - a0.dist, 1e-6f);
        s.frame.z = normalize(r - t * (dr / dl), r);
      }
      Instance c = attach_at(ctx, child_node, slot, parent, s, parent_node, index++);
      if (whorl_angle_rand > 0.f) {
        const float jitter = d.signed_unit() * whorl_angle_rand;
        c.frame = c.frame.rotated(c.frame.x, jitter);
      }
      out.push_back(c);
    }
  }
  return out;
}

} // namespace plant
} // namespace gpx
