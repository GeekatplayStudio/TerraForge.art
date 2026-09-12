// Geekatplay TerraForge - the Growth part: build_growth.
//
// The walker hands a Growth node one instance and expects a mesh, the
// instance's length and radius, and the sockets its leaves go on. This is
// the glue: read the parameters that hold still (plant_growth.hpp's
// GrowthParams; the age-filtered ones the simulation reads itself), run
// the simulation (plant_growth_sim.cpp) from the instance's frame, mesh the
// shoots (plant_growth_mesh.cpp), and turn the young wood into sockets.
//
// Where it starts: "From the root" grows the trunk up from the instance's
// frame; "Over the parent segment" is the same simulation, only the frame
// is already at the parent's tip (the walker put it there) and the
// parent's radius caps the trunk's - a growth completing a modelled trunk
// must not come out thicker than the trunk. Leaves sit where a real tree
// has them: on the internodes grown in the last two iterations and on
// every internode of a shoot still thin enough to be a twig (radius under
// three bud radii). A socket's z points outward along the bud's azimuth,
// so a leaf with attachment angle 0 stands out from the twig and one at
// 90 lies along it, the same convention as a segment's skin sockets.
// Material: one material for the whole system, Random among the connected
// slots or the First, as the manual says.
#include "plant/plant_growth.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {
namespace plant {

void build_growth(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk) {
  ParamReader pr(ctx, n, inst);

  GrowthParams gp;
  gp.iterations = std::max(1, std::min(60, (int)std::lround(pr.random("iterations", 0.f, 0, 12.f))));
  gp.over_parent = pr.choice("growth_input") == 1 && inst.parent != nullptr;
  gp.internode = pr.f("internode", 0.2f);
  gp.bud_radius = pr.f("bud_radius", 0.006f);
  gp.shadow_strength = pr.f("shadow_strength", 0.5f);
  gp.shadow_size = pr.f("shadow_size", 0.3f);
  gp.vertical_trunk = pr.b("vertical_trunk", true);
  gp.bottom_cut_mode = pr.choice("bottom_cut_mode", 1);
  gp.bottom_cut_rank = pr.i("bottom_cut_rank", 0);
  gp.bottom_cut_height = pr.random("bottom_cut_height", 0.f, 0, 0.f);
  gp.profile_cut = pr.b("profile_cut_enable", false);
  gp.profile_cut_mode = pr.choice("profile_cut_mode", 1);
  gp.profile = &pr.curve("profile_cut");
  gp.profile_bottom = pr.random("profile_bottom", 0.f, 0, 0.2f);
  gp.profile_top = pr.random("profile_top", 0.f, 0, 1.f);
  gp.profile_radius = pr.random("profile_radius", 0.f, 0, 0.6f);
  gp.scale = ctx.scale * inst.scale * (inst.pruned ? inst.prune_ratio : 1.f);
  gp.parent_radius = gp.over_parent ? inst.parent_radius : 0.f;

  std::vector<Shoot> shoots;
  if (growth_simulate(ctx, n, inst, gp, shoots))
    ctx.warnings.push_back("Growth: 50000 shoots reached; raise the shedding threshold or lower the growth speed");

  // the material: one for every branch
  int slot = 0;
  {
    std::vector<int> slots;
    const std::vector<const Node *> mats = ctx.materials_of(n);
    for (size_t i = 0; i < mats.size(); ++i)
      if (mats[i]) slots.push_back((int)i);
    if (!slots.empty()) {
      if (pr.choice("mat_distribution", 0) == 0) {
        Draw d(ctx.seed, inst.id, hash_str("mat_distribution"));
        slot = slots[d.pick((uint32_t)slots.size())];
      } else {
        slot = slots[0];
      }
    }
  }

  GrowthMeshParams mp;
  mp.axial_per_m = pr.f("axial_subdiv", 6.f);
  mp.angular_per_m = pr.f("angular_subdiv", 12.f);
  mp.mesh_boost = pr.f("mesh_boost", 0.f);
  mp.flexibility = pr.random("flexibility", 0.f, 0, 1.f);
  mp.gravity = pr.random("gravity", 0.f, 0, 1.f);
  mp.wind = pr.random("wind", 0.f, 0, 1.f);
  mp.material = ctx.material_for(n, slot);
  growth_mesh(ctx, inst, shoots, mp, out);

  // what it turned out to be: the trunk is shoot 0
  const float inter = std::max(1e-4f, gp.internode * gp.scale);
  const float bud_r = std::max(1e-5f, gp.bud_radius * gp.scale);
  float top = inst.frame.o.y;
  inst.length = 0.f;
  inst.radius = 0.f;
  inst.axis.clear();
  if (!shoots.empty() && !shoots[0].removed && !shoots[0].nodes.empty()) {
    const Shoot &t = shoots[0];
    inst.radius = t.radius;
    inst.length = (float)t.nodes.size() * inter;
    Instance::AxisSample a0;
    a0.p = t.start;
    a0.t = t.tangent_at(0);
    a0.n = perpendicular(a0.t);
    a0.radius = t.radius;
    inst.axis.push_back(a0);
    for (size_t k = 0; k < t.nodes.size(); ++k) {
      Instance::AxisSample a;
      a.p = t.nodes[k];
      a.t = t.tangent_at((int)k);
      a.n = perpendicular(a.t);
      a.radius = t.radii[k];
      a.dist = (float)(k + 1) * inter;
      a.primal = a.dist / std::max(inst.length, 1e-6f);
      inst.axis.push_back(a);
    }
  }
  for (const Shoot &s : shoots)
    if (!s.removed)
      for (const V3 &q : s.nodes) top = std::max(top, q.y);
  ctx.height_est = std::max(ctx.height_est, top);

  // the sockets: young internodes and thin twigs of living shoots
  const int N = gp.iterations;
  const float thin = 3.f * bud_r;
  sk.along.clear();
  for (const Shoot &s : shoots) {
    if (s.removed || s.dead_since >= 0 || s.nodes.empty() || s.radii.size() != s.nodes.size()) continue;
    const bool twig = s.radius < thin;
    const size_t m = s.nodes.size();
    for (size_t k = 0; k < m; ++k) {
      if (!twig && s.node_iter[k] < N - 2) continue;
      const V3 t = s.tangent_at((int)k);
      const V3 outward = s.radial_at((int)k, s.bud_az[k]);
      Socket so;
      const Frame ref = Frame::along(s.nodes[k], t, nullptr);
      so.frame = Frame::along(s.nodes[k], outward, &ref);
      so.primal = (float)(k + 1) / (float)m;
      so.radius = s.radii[k];
      so.side_radius = s.radii[k];
      so.azimuth = s.bud_az[k];
      so.remaining = (float)(m - 1 - k) * inter;
      so.dist = s.dist_base + (float)(k + 1) * inter;
      so.scale = 1.f;
      sk.along.push_back(so);
    }
  }
  std::stable_sort(sk.along.begin(), sk.along.end(),
                   [](const Socket &a, const Socket &b) { return a.dist < b.dist; });
  if (!inst.axis.empty()) {
    sk.has_bottom = sk.has_tip = true;
    sk.bottom.frame = inst.frame;
    sk.bottom.radius = inst.radius;
    sk.bottom.dist = inst.dist_root;
    const Instance::AxisSample &last = inst.axis.back();
    sk.tip.frame = Frame::along(last.p, last.t, &inst.frame);
    sk.tip.primal = 1.f;
    sk.tip.radius = last.radius;
    sk.tip.dist = inst.dist_root + inst.length;
  }
}

} // namespace plant
} // namespace gpx
