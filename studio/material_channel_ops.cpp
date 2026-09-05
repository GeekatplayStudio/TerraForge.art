// Geekatplay TerraForge - channel modes, hierarchy, randomize, snapshots.
// See material_channel_ops.hpp.
#include "material_channel_ops.hpp"
#include "gpx/serialization.hpp"
#include "material_stack_ops.hpp"
#include <algorithm>
#include <functional>
#include <unordered_set>

namespace studio {

const char *channel_mode_name(int mode) {
  switch (mode) {
    case CH_NONE: return "None";
    case CH_PICTURE: return "Mapped picture";
    case CH_PROCEDURAL: return "Procedural";
    case CH_GRAIN: return "Natural grain";
    default: return "Connected";
  }
}

gpx::Node *channel_source(gpx::Graph &g, gpx::Node *owner, const char *port) {
  return owner ? g.upstream_node(*owner, port) : nullptr;
}

int channel_mode_of(gpx::Graph &g, gpx::Node *owner, const char *port) {
  gpx::Node *src = channel_source(g, owner, port);
  if (!src) return CH_NONE;
  if (src->type == "TextureFile") return CH_PICTURE;
  if (src->type == "NaturalGrain") return CH_GRAIN;
  if (src->type == "MaterialLayer" || src->type == "MaterialStack" ||
      src->type == "PBRMaterial" || src->type == "DistributionLayer" ||
      src->type == "EffectorLayer")
    return CH_OTHER;
  return CH_PROCEDURAL;
}

gpx::Node *channel_set_mode(gpx::Graph &g, gpx::Node *owner, const char *port,
                            int mode, const std::string &path) {
  if (!owner) return nullptr;
  gpx::Node *src = channel_source(g, owner, port);
  const int cur = channel_mode_of(g, owner, port);
  if (mode == cur && mode != CH_PICTURE) return src;
  if (mode == CH_PICTURE && cur == CH_PICTURE && src) {
    if (!path.empty())
      if (gpx::Attribute *pa = src->attrs.find("path")) pa->s = path;
    g.mark_dirty(src->id);
    return src;
  }
  if (gpx::Link *lk = layer_incoming(g, owner->id, port)) g.remove_link(lk->id);
  if (mode == CH_NONE) return nullptr;
  const char *type = mode == CH_PICTURE ? "TextureFile"
                     : mode == CH_GRAIN ? "NaturalGrain"
                                        : "FractalColor";
  gpx::Node *n = g.add_node(type, owner->pos_x - 280.f, owner->pos_y + 40.f);
  if (!n) return nullptr;
  if (mode == CH_PICTURE && !path.empty())
    if (gpx::Attribute *pa = n->attrs.find("path")) pa->s = path;
  g.add_link(n->id, "texture", owner->id, port);
  g.mark_dirty(owner->id);
  return n;
}

std::vector<uint64_t> material_upstream(gpx::Graph &g, uint64_t mat_id) {
  std::vector<uint64_t> out;
  std::unordered_set<uint64_t> seen{mat_id};
  std::vector<uint64_t> stack{mat_id};
  while (!stack.empty()) {
    uint64_t id = stack.back();
    stack.pop_back();
    for (const gpx::Link &l : g.links)
      if (l.to_node == id && !seen.count(l.from_node)) {
        seen.insert(l.from_node);
        out.push_back(l.from_node);
        stack.push_back(l.from_node);
      }
  }
  return out;
}

int material_randomize(gpx::Graph &g, gpx::Node *mat, uint32_t salt) {
  if (!mat) return 0;
  int changed = 0;
  for (uint64_t id : material_upstream(g, mat->id)) {
    gpx::Node *n = g.find_node(id);
    if (!n) continue;
    bool any = false;
    for (gpx::Attribute &at : n->attrs.items)
      if (at.type == gpx::AttrType::Seed) {
        at.seed = at.seed * 1664525u + 1013904223u + salt;
        any = true;
      }
    if (any) {
      g.mark_dirty(n->id);
      ++changed;
    }
  }
  return changed;
}

bool material_replace_from_json(gpx::Graph &g, uint64_t mat_id,
                                const std::string &json, std::string &err) {
  gpx::Node *old = g.find_node(mat_id);
  if (!old || old->type != "MaterialOutput") {
    err = "no such material";
    return false;
  }
  std::vector<uint64_t> old_up = material_upstream(g, mat_id);
  uint64_t fresh_id = gpx::material_from_json(g, json, err, old->pos_x, old->pos_y);
  gpx::Node *fresh = g.find_node(fresh_id);
  if (!fresh) return false;
  // the old material's inputs go away; the snapshot's sources take their
  // place on the same node, so anything assigned to it is still assigned
  std::vector<uint64_t> drop;
  for (const gpx::Link &l : g.links)
    if (l.to_node == mat_id) drop.push_back(l.id);
  for (uint64_t id : drop) g.remove_link(id);
  std::vector<gpx::Link> moved;
  for (const gpx::Link &l : g.links)
    if (l.to_node == fresh_id) moved.push_back(l);
  for (const gpx::Link &l : moved) {
    g.remove_link(l.id);
    g.add_link(l.from_node, l.from_port, mat_id, l.to_port);
  }
  old->attrs = fresh->attrs;
  g.remove_node(fresh_id);
  // the old upstream that nothing reads any more is gone too
  for (uint64_t id : old_up) {
    bool used = false;
    for (const gpx::Link &l : g.links)
      if (l.from_node == id) { used = true; break; }
    if (!used) g.remove_node(id);
  }
  g.mark_dirty(mat_id);
  return true;
}

std::vector<MatHierItem> material_hierarchy(gpx::Graph &g, gpx::Node *mat) {
  std::vector<MatHierItem> out;
  if (!mat) return out;
  out.push_back({mat->id, 0, 0, mat->attrs.get_s("name")});
  std::vector<gpx::Node *> layers = collect_layers(g, mat);
  for (size_t i = 0; i < layers.size(); ++i)
    out.push_back({layers[i]->id, 1, 1, layer_display_name(layers[i], i)});
  gpx::Node *src = g.upstream_node(*mat, "base color");
  if (src && src->type == "MaterialStack") {
    out.push_back({src->id, 1, 2, "Mix"});
    const char *names[2] = {"Material 1", "Material 2"};
    for (int k = 0; k < 2; ++k) {
      gpx::Node *sub = g.upstream_node(*src, k == 0 ? "albedo 1" : "albedo 2");
      if (sub) out.push_back({sub->id, 2, 3, std::string(names[k]) + ": " + sub->type});
    }
  } else if (src && src->type == "DistributionLayer") {
    out.push_back({src->id, 1, 4, "Distribution"});
  } else if (src && src->type == "EffectorLayer") {
    out.push_back({src->id, 1, 5, "Effector"});
  }
  return out;
}

int hier_visibility(gpx::Node *n) {
  if (!n) return 0;
  if (!n->enabled) return 1;
  if (n->type == "MaterialLayer" && n->attrs.get_b("highlight", false)) return 2;
  return 0;
}

void hier_set_visibility(gpx::Node *n, int state) {
  if (!n) return;
  n->enabled = state != 1;
  if (gpx::Attribute *h = n->attrs.find("highlight")) h->b = state == 2;
  if (gpx::Attribute *e = n->attrs.find("enabled")) e->b = state != 1;
}

} // namespace studio
