// Geekatplay TerraForge - the walk that grows a species, part by part
// (plant_internal.hpp: grow).
//
// One function decides everything about *whether and where* a part grows,
// and the builders decide only what it looks like. That is the whole reason
// this file exists: levels of detail, presence over the seasons, the
// selectors, the loops and the caps all have to behave the same on every
// kind of part, and they do because none of them is written twice.
//
// The order for one instance: is it switched off or outside the level being
// built (stop); build its geometry and ask it where children may sit; then,
// for each of its child inputs, work out the instances that input asks for
// (plant_attach.cpp) and grow each of them in turn. Parts that place their
// own children - a growth simulation, an urchin's skin, a hydra's ring -
// hand back one socket per child and the choice of *which* child node fills
// each socket is made here by the children's presence weights, so a weight
// sum below one leaves bare sockets, which is how the manual thins foliage.
//
// A loop (PlantRepeat) grows its body into itself by recursion rather than
// iteration: each level's instances live in the stack frame that grows the
// next, which is what keeps a child's pointer to its parent valid for as
// long as anything reads it.
#include "plant/plant_internal.hpp"
#include <algorithm>

namespace gpx {
namespace plant {

namespace {

// Does this one exist at all? Presence is a probability per instance, so a
// season, a health or a thinned foliage removes the same parts every time
// for a given seed.
bool keep(const BuildCtx &ctx, const Instance &c) {
  if (c.presence >= 0.999f) return true;
  if (c.presence <= 0.f) return false;
  return hash_unit(hash_u32(ctx.seed, c.id, hash_str("presence"))) < c.presence;
}

void warn_once(BuildCtx &ctx, const std::string &what) {
  for (const std::string &w : ctx.warnings)
    if (w == what) return;
  ctx.warnings.push_back(what);
}

bool room_left(BuildCtx &ctx) {
  if (ctx.instances >= ctx.instance_cap) {
    warn_once(ctx, "the plant stopped at " + std::to_string(ctx.instance_cap) +
                       " parts: lower a count, a level or the age");
    return false;
  }
  if (ctx.mesh && ctx.mesh->vertex_count() >= ctx.vertex_cap) {
    warn_once(ctx, "the plant stopped at " + std::to_string(ctx.vertex_cap / 1000) +
                       "k vertices: lower the detail or a subdivision");
    return false;
  }
  return true;
}

// The socket a part with no geometry of its own offers: itself.
Sockets own_socket(const Instance &inst) {
  Sockets sk;
  sk.tip.frame = inst.frame;
  sk.tip.primal = 0.f;
  sk.tip.radius = std::max(inst.parent_radius, 1e-4f);
  sk.tip.side_radius = sk.tip.radius;
  sk.tip.dist = inst.dist_root;
  sk.has_tip = true;
  sk.bottom = sk.tip;
  sk.has_bottom = true;
  return sk;
}

// Build one instance's geometry and ask it where children may sit. False
// when it does not grow at all (switched off, outside this level of detail,
// or the plant has hit a cap).
bool grow_one(BuildCtx &ctx, Instance &inst, MeshOut &out, Sockets &sk, const Node &n) {
  if (!n.enabled || !room_left(ctx)) return false;
  const ParamReader pr(ctx, n, inst);
  const int lod_min = pr.i("lod_min", 0), lod_max = pr.i("lod_max", 8);
  if (ctx.lod < lod_min || ctx.lod > lod_max) return false;
  ++ctx.instances;
  inst.kind = kind_of(n);
  switch (inst.kind) {
    case Kind::Segment: build_segment(ctx, n, inst, out, sk); break;
    case Kind::Leaf: build_leaf(ctx, n, inst, out, sk); ctx.mesh->leaves++; break;
    case Kind::CutoutLeaf: build_cutout_leaf(ctx, n, inst, out, sk); ctx.mesh->leaves++; break;
    case Kind::Warpboard: build_warpboard(ctx, n, inst, out, sk); ctx.mesh->leaves++; break;
    case Kind::Object: build_object(ctx, n, inst, out, sk); ctx.mesh->primitives++; break;
    case Kind::Urchin: build_urchin(ctx, n, inst, out, sk); ctx.mesh->primitives++; break;
    case Kind::Hydra: build_hydra(ctx, n, inst, out, sk); break;
    case Kind::Ball: build_ball(ctx, n, inst, out, sk); ctx.mesh->primitives++; break;
    case Kind::Flower: build_flower(ctx, n, inst, out, sk); ctx.mesh->primitives++; break;
    case Kind::Growth: build_growth(ctx, n, inst, out, sk); ctx.mesh->primitives++; break;
    default: break; // a selector or a loop: no geometry of its own
  }
  if (!sk.has_tip && sk.along.empty()) sk = own_socket(inst);
  if (inst.kind == Kind::Leaf || inst.kind == Kind::CutoutLeaf || inst.kind == Kind::Warpboard)
    ctx.crown_samples.push_back(inst.frame.o);
  return true;
}

// The child nodes on a part's child inputs, with the presence weight each
// one asks for (the manual's children distribution).
struct ChildSlot {
  const Node *node = nullptr;
  int slot = 0;
  float weight = 1.f;
};
std::vector<ChildSlot> child_slots(BuildCtx &ctx, const Node &n, const Instance &inst) {
  std::vector<ChildSlot> out;
  for (int slot = 1; slot <= 6; ++slot) {
    const Node *c = ctx.upstream_plant(n, "child " + std::to_string(slot));
    if (!c || !c->enabled) continue;
    ChildSlot cs;
    cs.node = c;
    cs.slot = slot;
    const Instance prov = provisional_child(ctx, *c, slot, inst);
    cs.weight = std::max(ParamReader(ctx, *c, prov).random("presence", 0.f, 0, 1.f), 0.f);
    out.push_back(cs);
  }
  return out;
}

void grow_children(BuildCtx &ctx, Instance &inst, MeshOut &out, const Sockets &sk, const Node &n);

// A loop: the body grown into itself, iteration after iteration, and the
// tail on the last one (or on every one, when asked).
void repeat_level(BuildCtx &ctx, const Node &body, const Node *tail, bool tail_every, Instance &host,
                  const Sockets &host_sockets, MeshOut &out, int iteration, int iterations) {
  if (iteration >= iterations || !room_left(ctx)) return;
  const Node *host_node = ctx.graph->find_node(host.node);
  if (!host_node) return;
  std::vector<Instance> kids = attach(ctx, body, 1, host, host_sockets, *host_node);
  for (Instance &b : kids) {
    if (!keep(ctx, b)) continue;
    b.iteration = iteration;
    b.iterations = iterations;
    Sockets bs;
    if (!grow_one(ctx, b, out, bs, body)) continue;
    grow_children(ctx, b, out, bs, body);
    repeat_level(ctx, body, tail, tail_every, b, bs, out, iteration + 1, iterations);
    if (tail && (tail_every || iteration + 1 >= iterations))
      for (Instance &t : attach(ctx, *tail, 1, b, bs, body))
        if (keep(ctx, t)) grow(ctx, t, out);
  }
}

void grow_children(BuildCtx &ctx, Instance &inst, MeshOut &out, const Sockets &sk, const Node &n) {
  if (!room_left(ctx)) return;
  const Kind k = kind_of(n);

  // a loop: its body, its tail, and how many times round
  if (k == Kind::Repeat) {
    const Node *body = ctx.upstream_plant(n, "child 1");
    const Node *tail = ctx.upstream_plant(n, "tail");
    if (!body || !body->enabled) return;
    const ParamReader pr(ctx, n, inst);
    const int iterations = std::clamp((int)std::lround(pr.random("iterations", 0.f, 0, 3.f)), 1, 12);
    repeat_level(ctx, *body, tail && tail->enabled ? tail : nullptr, pr.b("tail_every"), inst, sk, out, 0,
                 iterations);
    return;
  }

  const std::vector<ChildSlot> slots = child_slots(ctx, n, inst);
  if (slots.empty()) return;

  // a selector: one of its children grows here, chosen by the mode
  if (k == Kind::ChildSelect) {
    const ParamReader pr(ctx, n, inst);
    const int mode = pr.choice("mode");
    int pick = -1;
    if (mode == 1) { // spread: they take turns
      pick = inst.index % (int)slots.size();
    } else if (mode == 2) { // a sequence of letters: A, B, C...
      const std::string seq = pr.s("sequence").empty() ? "ABC" : pr.s("sequence");
      const char c = seq[(size_t)(inst.index % (int)seq.size())];
      const int want = (int)(c >= 'a' ? c - 'a' : c - 'A');
      pick = std::clamp(want, 0, (int)slots.size() - 1);
    } else if (mode == 3) { // by a threshold
      pick = pr.random("value") < pr.random("level", 0.f, 0, 0.5f) ? 0 : std::min(1, (int)slots.size() - 1);
    } else if (mode == 4) { // by the level of detail
      pick = std::min(ctx.lod, (int)slots.size() - 1);
    } else { // at random, by the children's own presence weights
      float total = 0.f;
      for (const ChildSlot &c : slots) total += c.weight;
      if (total <= 0.f) return;
      float r = hash_unit(hash_u32(ctx.seed, inst.id, hash_str("select"))) * std::max(total, 1.f);
      for (size_t i = 0; i < slots.size(); ++i) {
        if (r < slots[i].weight) {
          pick = (int)i;
          break;
        }
        r -= slots[i].weight;
      }
      if (pick < 0) return; // the weights did not add up to one: nothing here
    }
    if (pick < 0 || pick >= (int)slots.size()) return;
    const ChildSlot &c = slots[(size_t)pick];
    for (Instance &child : attach(ctx, *c.node, c.slot, inst, sk, n))
      if (keep(ctx, child)) grow(ctx, child, out);
    return;
  }

  // a part that placed its own children: one child per socket, which one
  // decided by the weights
  if (k == Kind::Growth || k == Kind::Urchin || k == Kind::Hydra) {
    float total = 0.f;
    for (const ChildSlot &c : slots) total += c.weight;
    if (total <= 0.f) return;
    const float span = std::max(total, 1.f); // a sum below one leaves sockets bare
    for (size_t i = 0; i < sk.along.size(); ++i) {
      if (!room_left(ctx)) return;
      float r = hash_unit(hash_u32(ctx.seed, inst.id, (uint32_t)i, hash_str("socket"))) * span;
      const ChildSlot *chosen = nullptr;
      for (const ChildSlot &c : slots) {
        if (r < c.weight) {
          chosen = &c;
          break;
        }
        r -= c.weight;
      }
      if (!chosen) continue;
      Instance child = attach_at(ctx, *chosen->node, chosen->slot, inst, sk.along[i], n, (int)i);
      child.presence = 1.f; // the socket already decided
      grow(ctx, child, out);
    }
    return;
  }

  // the ordinary case: every child input asks for its own connections
  for (const ChildSlot &c : slots) {
    if (!room_left(ctx)) return;
    for (Instance &child : attach(ctx, *c.node, c.slot, inst, sk, n))
      if (keep(ctx, child)) grow(ctx, child, out);
  }
}

} // namespace

void grow(BuildCtx &ctx, Instance &inst, MeshOut &out) {
  const Node *n = ctx.graph ? ctx.graph->find_node(inst.node) : nullptr;
  if (!n) return;
  if (inst.depth > 24) {
    warn_once(ctx, "the species is more than 24 parts deep: a loop feeding itself?");
    return;
  }
  Sockets sk;
  if (!grow_one(ctx, inst, out, sk, *n)) return;
  grow_children(ctx, inst, out, sk, *n);
}

} // namespace plant
} // namespace gpx
