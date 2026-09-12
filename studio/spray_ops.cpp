// Geekatplay TerraForge - the spray brush's operations (spray.hpp).
//
// Four: make one, add a kind to it, read back what is in it with each kind's
// share as a percentage, and change a kind. Mirrored as MCP tools and shown
// to the assistant in ai_schema.cpp, like every other operation.
#include "app.hpp"
#include "ai_assist.hpp"
#include "scene.hpp"
#include "gpx/ecology.hpp"
#include "spray.hpp"
#include <algorithm>
#include <string>

using nlohmann::json;

namespace studio {

namespace {

// The spray a request names: its layer node's id, or the only one there is.
uint64_t spray_resolve(App &a, const json &act, std::string &err) {
  if (act.contains("spray")) {
    const json &v = act["spray"];
    if (v.is_number_unsigned() || v.is_number_integer()) {
      const uint64_t id = (uint64_t)v.get<long long>();
      const gpx::Node *n = a.graph.find_node(id);
      if (n && n->type == "EcosystemLayer") return id;
      err = "no spray " + std::to_string(id);
      return 0;
    }
  }
  uint64_t found = 0;
  int n = 0;
  for (const auto &np : a.graph.nodes)
    if (np->type == "EcosystemLayer") {
      found = np->id;
      ++n;
    }
  if (!n) err = "there is no spray in the scene yet - make one with spray_new";
  else if (n > 1) err = "there are several sprays: name one with \"spray\": <node id>";
  return n == 1 ? found : 0;
}

SprayComponent component_from(const json &act) {
  SprayComponent c;
  c.plant_words = act.value("plant", std::string());
  c.object = act.value("object", std::string());
  c.name = act.value("name", std::string());
  // a share given as a percentage is a weight like any other: the layer
  // normalises them, so 40 against 60 and 0.4 against 0.6 place the same mix
  if (act.contains("percent")) c.share = std::clamp(act["percent"].get<float>() / 100.f, 0.f, 1.f);
  else if (act.contains("share")) c.share = std::clamp(act["share"].get<float>(), 0.f, 1.f);
  c.scale = act.value("scale", 1.f);
  c.size_variation = act.value("size_variation", 0.f);
  c.lean = act.value("lean", -1.f);
  return c;
}

json component_json(const SprayComponent &c) {
  return json{{"slot", c.slot},         {"object", c.object},
              {"share", c.share},       {"percent", c.percent},
              {"scale", c.scale},       {"size_variation", c.size_variation},
              {"lean", c.lean}};
}

} // namespace

// -1 when the op is not one of ours, 0 on failure with `err`, 1 on success.
int spray_op(App &a, const std::string &op, const json &act, std::string &err) {
  if (op == "spray_new") {
    SprayPlan p;
    p.area_m = std::clamp(act.value("area_m", 1200.f), 20.f, 100000.f);
    p.unbounded = act.value("unbounded", false);
    p.density_ha = std::max(0.01f, act.value("density_ha", 60.f));
    p.spacing_m = std::max(0.05f, act.value("spacing_m", 6.f));
    p.clumping = std::clamp(act.value("clumping", 0.4f), 0.f, 1.f);
    p.size_variation = std::clamp(act.value("size_variation", 0.35f), 0.f, 1.f);
    const uint64_t id = spray_new(a, p, err);
    if (!id) return 0;
    // components may be given with it, so one call makes a whole brush
    json added = json::array();
    if (act.contains("components") && act["components"].is_array())
      for (const json &cj : act["components"]) {
        std::string e;
        const int slot = spray_add(a, id, component_from(cj), e);
        if (slot < 0 && err.empty()) err = e;
        if (slot >= 0) added.push_back(slot);
      }
    a.api_reply = json{{"spray", id}, {"added", added}}.dump();
    return 1;
  }

  if (op == "spray_biome") {
    SprayPlan p;
    p.area_m = std::clamp(act.value("area_m", 1200.f), 20.f, 100000.f);
    p.unbounded = act.value("unbounded", false);
    std::vector<BiomeLayer> made;
    const uint64_t first = spray_biome(a, act.value("biome", std::string()), p, made, err);
    if (!first) return 0;
    json layers = json::array();
    for (const BiomeLayer &L : made)
      layers.push_back(json{{"layer", L.node}, {"group", L.group}, {"is", L.label},
                            {"placed_against", L.below}, {"share", L.share}});
    a.api_reply = json{{"first", first}, {"layers", layers}}.dump();
    return 1;
  }

  if (op == "spray_biomes") {
    const std::string family = act.value("family", std::string());
    json out = json::array();
    for (const gpx::eco::Biome *b : gpx::eco::biomes_in(family)) {
      json groups = json::array();
      for (const gpx::eco::BiomeMember &m : gpx::eco::in_order(*b)) groups.push_back(m.group);
      out.push_back(json{{"id", b->id}, {"name", b->name}, {"family", b->family},
                         {"note", b->note}, {"groups", groups}});
    }
    json fams = json::array();
    for (const char *f : {"forest", "field", "farm", "desert", "water", "underwater", "alien", "built"})
      fams.push_back(f);
    a.api_reply = json{{"biomes", out}, {"families", fams}}.dump();
    return 1;
  }

  if (op == "spray_groups") {
    json out = json::array();
    for (const gpx::eco::Group &g : gpx::eco::groups()) {
      json rules = json::array();
      for (const gpx::eco::Relation *r : gpx::eco::relations_of(g.name))
        rules.push_back(json{{"near", r->near}, {"affinity", r->affinity},
                             {"repulsion", r->repulsion}, {"why", r->why}});
      out.push_back(json{{"group", g.name}, {"is", g.plural}, {"tier", g.tier},
                         {"footprint_m", g.footprint_m}, {"rules", rules}});
    }
    a.api_reply = json{{"groups", out}}.dump();
    return 1;
  }

  if (op == "spray_add") {
    // A group says where the thing belongs, and the layer for it is already
    // standing in the right place in the stack with the right rules on it.
    // Failing that the name is guessed from - "mossy_boulder_02" is a
    // boulder - and failing THAT the request must name a spray, because
    // putting moss where trees go is worse than asking.
    std::string group = act.value("group", std::string());
    if (group.empty())
      group = gpx::eco::group_from_name(act.value("plant", act.value("object", std::string())));
    uint64_t id = 0;
    if (!group.empty() && !act.contains("spray")) id = spray_layer_for_group(a, group);
    if (!id) id = spray_resolve(a, act, err);
    if (!id) return 0;
    err.clear();
    const int slot = spray_add(a, id, component_from(act), err);
    if (slot < 0) return 0;
    json out = json::array();
    for (const SprayComponent &c : spray_components(a, id)) out.push_back(component_json(c));
    a.api_reply = json{{"spray", id}, {"slot", slot}, {"group", group}, {"components", out}}.dump();
    return 1;
  }

  if (op == "spray_list") {
    json sprays = json::array();
    for (const auto &np : a.graph.nodes) {
      if (np->type != "EcosystemLayer") continue;
      json out = json::array();
      for (const SprayComponent &c : spray_components(a, np->id)) out.push_back(component_json(c));
      bool painted = false;
      for (const gpx::Link &l : a.graph.links)
        if (l.to_node == np->id && l.to_port == "mask") painted = true;
      sprays.push_back(json{{"spray", np->id},
                            {"group", np->attrs.get_s("eco_group")},
                            {"painted", painted},
                            {"density_ha", np->attrs.get_f("density", 0.f)},
                            {"components", out}});
    }
    a.api_reply = json{{"sprays", sprays}}.dump();
    return 1;
  }

  if (op == "spray_set") {
    const uint64_t id = spray_resolve(a, act, err);
    if (!id) return 0;
    if (act.contains("slot")) {
      SprayComponent c = component_from(act);
      if (!act.contains("percent") && !act.contains("share")) c.share = -1.f;
      if (!act.contains("scale")) c.scale = -1.f;
      if (!act.contains("size_variation")) c.size_variation = -1.f;
      if (!spray_set(a, id, act["slot"].get<int>(), c, err)) return 0;
    }
    // the brush's own settings, which belong to the whole spray
    if (gpx::Node *layer = a.graph.find_node(id)) {
      auto setf = [&](const char *key, float v) {
        if (gpx::Attribute *at = layer->attrs.find(key))
          if (at->type == gpx::AttrType::Float) at->f = std::clamp(v, at->fmin, at->fmax);
      };
      if (act.contains("density_ha")) setf("density", act["density_ha"].get<float>());
      if (act.contains("spacing_m")) setf("spacing_m", act["spacing_m"].get<float>());
      if (act.contains("clumping")) setf("clump_amount", act["clumping"].get<float>());
      if (act.contains("size_variation") && !act.contains("slot"))
        setf("variation", act["size_variation"].get<float>());
      a.graph.mark_dirty(id);
      a.request_eval();
    }
    json out = json::array();
    for (const SprayComponent &c : spray_components(a, id)) out.push_back(component_json(c));
    a.api_reply = json{{"spray", id}, {"components", out}}.dump();
    return 1;
  }

  return -1;
}

} // namespace studio
