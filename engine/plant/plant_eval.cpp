// Geekatplay TerraForge - growing one plant from a species
// (gpx/plant.hpp: plant_build; plant_internal.hpp: the context).
//
// The root of a species is a sink: every part links into the part it grows
// on and the trunk links into the root, so the root evaluates last and any
// edit anywhere in the species reaches it. Its compute calls this, and this
// walks the subtree once and hands back a mesh.
//
// What the root decides, and nothing else does: the seed, the age and how
// mature that makes the plant, its health, the season, the wind, the
// meshing detail and the level being built. Age is not a slider on the
// geometry - it scales the whole plant and thins its children, which is the
// difference between a sapling and the same species at eighty.
//
// Two things are finished here because no builder can know them while it
// works: the wind weight's height (a vertex knows its own height in metres,
// not the plant's, until the last branch is grown) and the ambient occlusion
// in the tint's alpha, which needs the finished crown to say what is buried
// in it. Both are one pass over the vertices at the end.
#include "plant/plant_internal.hpp"
#include <algorithm>
#include <cstring>

namespace gpx {

bool plant_is_part(const std::string &type) {
  bool ok = false;
  const plant::Kind k = plant::kind_of_type(type, &ok);
  if (!ok) return false;
  switch (k) {
    case plant::Kind::Species:
    case plant::Kind::Bias:
    case plant::Kind::Material:
    case plant::Kind::Variable:
    case plant::Kind::Vector:
    case plant::Kind::COUNT: return false;
    default: return true;
  }
}

float plant_maturity(float age_years, float max_age_years) {
  const float full = std::max(max_age_years, 1e-3f) * 0.45f;
  const float m = age_years / full;
  return m < 0.f ? 0.f : m > 1.f ? 1.f : m;
}

float plant_size_at(float maturity) {
  const float m = maturity < 0.f ? 0.f : maturity > 1.f ? 1.f : maturity;
  return 0.15f + 0.85f * std::pow(m, 0.6f);
}

const Node *plant_root_of(const Graph &g, const Node &any) {
  if (any.type == "PlantSpecies") return &any;
  std::vector<uint64_t> stack{any.id}, seen;
  while (!stack.empty() && seen.size() < 4096) {
    const uint64_t id = stack.back();
    stack.pop_back();
    if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
    seen.push_back(id);
    for (const Link &l : g.links) {
      if (l.from_node != id) continue;
      const Node *to = g.find_node(l.to_node);
      if (!to || to->category != "Plant") continue;
      if (to->type == "PlantSpecies") return to;
      stack.push_back(to->id);
    }
  }
  return nullptr;
}

std::vector<const Node *> plant_subtree(const Graph &g, const Node &root) {
  std::vector<const Node *> out{&root};
  std::vector<uint64_t> seen{root.id};
  for (size_t i = 0; i < out.size() && out.size() < 8192; ++i) {
    const Node *n = out[i];
    for (const Port &p : n->ports) {
      if (p.dir != PortDir::In || p.type != DataType::Plant) continue;
      for (const Link &l : g.links) {
        if (l.to_node != n->id || l.to_port != p.name) continue;
        if (std::find(seen.begin(), seen.end(), l.from_node) != seen.end()) continue;
        const Node *src = g.find_node(l.from_node);
        if (!src || src->category != "Plant") continue;
        seen.push_back(src->id);
        out.push_back(src);
      }
    }
  }
  return out;
}

namespace plant {

const Node *BuildCtx::upstream_plant(const Node &n, const std::string &port) const {
  if (!graph) return nullptr;
  for (const Link &l : graph->links) {
    if (l.to_node != n.id || l.to_port != port) continue;
    const Node *src = graph->find_node(l.from_node);
    if (src && src->category == "Plant") return src;
  }
  return nullptr;
}

// The species numbered part by part, the same way every time: the walk from
// the root through the Plant ports in port order, and within a port in the
// order the links were made. Node ids never enter it, so the numbering
// survives a save and a load - which is what makes a loaded species grow the
// individual it grew before.
uint32_t BuildCtx::key_of(uint64_t node_id) const {
  const auto it = part_key.find(node_id);
  return it == part_key.end() ? 0u : it->second;
}

uint32_t BuildCtx::key_of(const Node &n) const { return key_of(n.id); }

std::vector<const Node *> BuildCtx::materials_of(const Node &part) const {
  static const char *const PORTS[7] = {"material", "material 2", "material 3", "material 4",
                                       "cap material", "blade material", "blade material 2"};
  std::vector<const Node *> out(7, nullptr);
  for (int i = 0; i < 7; ++i) {
    const Node *m = upstream_plant(part, PORTS[i]);
    if (m && m->type == "PlantMaterial") out[(size_t)i] = m;
  }
  // a cap or a second blade material falls back to the ones before it
  if (!out[4]) out[4] = out[0];
  if (!out[5]) out[5] = out[0];
  if (!out[6]) out[6] = out[5];
  return out;
}

} // namespace plant

bool plant_build(const Graph &g, const Node &root, const PlantBuildOptions &o, PlantMesh &out,
                 std::string &err) {
  using namespace plant;
  if (root.type != "PlantSpecies") {
    err = "a plant is grown from a PlantSpecies node";
    return false;
  }
  out.clear();
  BuildCtx ctx;
  ctx.graph = &g;
  ctx.root = &root;
  ctx.opt = o;
  ctx.mesh = &out;
  const AttrSet &at = root.attrs;
  ctx.seed = o.seed ? o.seed : at.get_seed("seed");
  if (ctx.seed == 0) ctx.seed = 1;
  ctx.max_age = std::max(at.get_f("max_age", 80.f), 0.1f);
  ctx.age = clampf(o.age >= 0.f ? o.age : at.get_f("age", 30.f), 0.f, ctx.max_age);
  ctx.maturity = plant_maturity(ctx.age, ctx.max_age);
  ctx.health = clampf(o.health >= 0.f ? o.health : at.get_f("health", 1.f), 0.f, 1.f);
  const float season = o.season >= 0.f ? o.season : at.get_f("season", 0.5f);
  ctx.season = season - std::floor(season);
  ctx.time = o.time >= 0.f ? o.time : (at.get_b("time_from_scene", true) ? g.time : 0.f);
  ctx.gravity = at.get_f("gravity", 1.f);
  // a young plant is a small plant: the whole thing scales with maturity and
  // carries fewer children, which is what a sapling is
  ctx.scale = std::max(at.get_f("scale", 1.f), 1e-4f) * plant_size_at(ctx.maturity);
  ctx.lod = std::max(o.lod, 0);
  ctx.lod_max = std::max(at.get_i("lod_levels", 2), 0);
  ctx.detail = o.detail + at.get_f("mesh_boost", 0.f);
  ctx.manual_meshing = at.get_choice("meshing_type") == 4;
  ctx.geometry_target = at.get_choice("geometry_target");
  for (int i = 1; i <= 4; ++i)
    if (const Node *b = ctx.upstream_plant(root, "bias " + std::to_string(i)))
      if (b->type == "PlantBias" && b->enabled) ctx.biases.push_back(b);

  {
    const std::vector<const Node *> parts = plant_subtree(g, root);
    for (size_t i = 0; i < parts.size(); ++i) ctx.part_key[parts[i]->id] = (uint32_t)(i + 1);
  }

  const Node *trunk = ctx.upstream_plant(root, "trunk");
  if (!trunk) {
    err = "no trunk: the species root has nothing growing on it";
    return false;
  }
  if (!trunk->enabled) {
    err.clear();
    return true; // a species switched off grows nothing, and that is not an error
  }

  MeshOut mo(out, ctx);
  Instance ri;
  ri.node = trunk->id;
  ri.kind = kind_of(*trunk);
  ri.id = hash_u32(ctx.seed, ctx.key_of(*trunk), 0u);
  ri.frame = Frame::along(V3(0.f, 0.f, 0.f), V3(0.f, 1.f, 0.f), nullptr);
  ri.lod = ctx.lod;
  ri.density = 0.3f + 0.7f * ctx.maturity;
  ri.rnd = hash_unit(hash_u32(ctx.seed, ri.id));
  ri.wind_phase = wind_phase_at(ctx, 0.f);
  ctx.wind_phase_base = hash_unit(hash_u32(ctx.seed, hash_str("wind phase")));
  ctx.height_est = std::max(1.f, ctx.scale * 5.f);
  grow(ctx, ri, mo);

  out.compute_bounds();
  out.height_m = std::max(out.bmax[1], 1e-3f);

  // The two weights no builder could finish, because neither knew how tall
  // the plant would turn out: the height, which is wanted as a fraction of
  // the plant rather than metres, and the flutter, which builders bake as
  // the amplitude they want in metres. The wind function multiplies flutter
  // by 0.02 * height, so dividing by that here leaves the metres the builder
  // asked for - a leaf ripples by a third of a leaf, not a third of a tree.
  const size_t verts = out.vertex_count();
  const float flut = std::max(0.02f * out.height_m, 1e-6f);
  for (size_t i = 0; i < verts && out.wind.size() >= (i + 1) * 4; ++i) {
    out.wind[i * 4 + 2] /= flut;
    out.wind[i * 4 + 3] = clampf(out.wind[i * 4 + 3] / out.height_m, 0.f, 1.f);
  }

  // ambient occlusion into the tint's alpha: open at the top and the
  // outside, closed deep in the crown and near the ground, shaped by the
  // root's post-processing settings
  if (o.tints) {
    const float ao_min = at.get_f("ao_min", 0.25f), ao_max = at.get_f("ao_max", 1.f);
    const float bright = at.get_f("ao_brightness", 0.f), falloff = std::max(at.get_f("ao_falloff", 1.f), 0.05f);
    const float g_str = at.get_f("ao_ground_strength", 0.4f);
    const float g_h = std::max(at.get_f("ao_ground_height", 15.f) * 0.01f, 1e-3f);
    const float g_tr = std::max(at.get_f("ao_ground_transition", 0.5f), 0.01f);
    float rmax = 1e-4f;
    for (size_t i = 0; i < verts; ++i)
      rmax = std::max(rmax, std::hypot(out.pos[i * 3], out.pos[i * 3 + 2]));
    for (size_t i = 0; i < verts && out.tint.size() >= (i + 1) * 4; ++i) {
      const float h = clampf(out.pos[i * 3 + 1] / out.height_m, 0.f, 1.f);
      const float r = clampf(std::hypot(out.pos[i * 3], out.pos[i * 3 + 2]) / rmax, 0.f, 1.f);
      float open = clampf(0.35f + 0.4f * h + 0.35f * r, 0.f, 1.f);
      open = std::pow(open, falloff);
      float ao = ao_min + (ao_max - ao_min) * open + bright;
      if (h < g_h) {
        const float deep = 1.f - smoothstep(g_h * (1.f - g_tr), g_h, h);
        ao *= 1.f - g_str * deep;
      }
      out.tint[i * 4 + 3] = clampf(ao, 0.f, 1.f);
    }
  }

  for (const std::string &w : ctx.warnings) out.warnings += (out.warnings.empty() ? "" : "\n") + w;
  err.clear();
  return true;
}

} // namespace gpx
