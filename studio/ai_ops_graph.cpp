// Geekatplay TerraForge — node-level graph operations for the API/MCP surface.
//
// Until now the only way a script could touch the graph was the `graph` op,
// which *merges* a whole spec: it can add nodes but never delete one, never
// rewire an existing link, and never change a parameter on a node it did not
// itself create. So an agent could write to the graph and never edit it — the
// single largest gap between what the UI can do and what scripting can.
//
// These are the ops that close it. They are deliberately small and orthogonal,
// one per thing the graph editor can do, because an agent composes them and a
// person reads them.
//
// Undo is handled by the caller: ai_apply_actions pushes one step for the
// whole action document, so a script's changes revert as one edit.
#include "ai_assist.hpp"
#include "app.hpp"
#include "scene.hpp"
#include "render_settings.hpp"
#include "gpx/metanode.hpp"
#include "gpx/node_graph.hpp"
#include <json.hpp>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>

using nlohmann::json;

namespace studio {

namespace {

// Nodes are addressed by id. Accepting a *type* name as well costs nothing and
// is how a person naturally refers to a graph they can see ("the Hydraulic"),
// so both work; the type form takes the last match, which is the one furthest
// down the chain and almost always the one meant.
// Aliases: add_node may carry "alias":"rock", and every later op can say
// "node":"rock". A macro that creates six FlatColors could not otherwise
// tell them apart, since the type form always resolves to the last one.
// Aliases outlive the document on purpose — an MCP session adds a node in
// one call and wires it in the next — and a stale alias simply fails to
// resolve once its node is gone.
std::map<std::string, uint64_t> &aliases() {
  static std::map<std::string, uint64_t> m;
  return m;
}

} // namespace

// Shared with ai_ops_scene.cpp (declared in ai_assist.hpp).
gpx::Node *find_node(App &a, const json &j, const char *key) {
  if (!j.contains(key)) return nullptr;
  const json &v = j[key];
  if (v.is_number()) return a.graph.find_node(v.get<uint64_t>());
  if (!v.is_string()) return nullptr;
  const std::string s = v.get<std::string>();
  {
    auto it = aliases().find(s);
    if (it != aliases().end())
      if (gpx::Node *n = a.graph.find_node(it->second)) return n;
  }
  // a numeric string is still an id
  try {
    size_t used = 0;
    uint64_t id = std::stoull(s, &used);
    if (used == s.size())
      if (gpx::Node *n = a.graph.find_node(id)) return n;
  } catch (const std::exception &) {
  }
  gpx::Node *last = nullptr;
  for (auto &n : a.graph.nodes)
    if (n->type == s) last = n.get();
  return last;
}

namespace {

// Set one attribute from JSON, whatever its type. Mirrors the tolerance of the
// AI spec path: a choice takes an index or a label, a colour takes a triple or
// a grey, and anything that does not fit is left alone rather than guessed at.
bool set_attr_value(gpx::Attribute &at, const json &v) {
  using gpx::AttrType;
  switch (at.type) {
    case AttrType::Float:
      if (!v.is_number()) return false;
      at.f = std::clamp(v.get<float>(), at.fmin, at.fmax);
      return true;
    case AttrType::Int:
      if (!v.is_number()) return false;
      at.i = std::clamp(v.get<int>(), at.imin, at.imax);
      return true;
    case AttrType::Seed:
      if (!v.is_number()) return false;
      at.seed = (uint32_t)std::max(v.get<long long>(), 0LL);
      return true;
    case AttrType::Bool:
      if (!v.is_boolean()) return false;
      at.b = v.get<bool>();
      return true;
    case AttrType::Choice: {
      if (v.is_number()) {
        at.i = std::clamp(v.get<int>(), 0, (int)at.labels.size() - 1);
        return true;
      }
      if (!v.is_string()) return false;
      std::string s = v.get<std::string>();
      for (auto &c : s) c = (char)tolower(c);
      for (size_t k = 0; k < at.labels.size(); ++k) {
        std::string l = at.labels[k];
        for (auto &c : l) c = (char)tolower(c);
        if (l.find(s) != std::string::npos || s.find(l) != std::string::npos) {
          at.i = (int)k;
          return true;
        }
      }
      return false;
    }
    case AttrType::Range:
    case AttrType::Vec2:
      if (!v.is_array() || v.size() < 2) return false;
      at.v2[0] = v[0].get<float>();
      at.v2[1] = v[1].get<float>();
      return true;
    case AttrType::Color:
      if (v.is_number()) {
        float g = std::clamp(v.get<float>(), 0.f, 1.f);
        at.col[0] = at.col[1] = at.col[2] = g;
        at.col[3] = 1.f;
        return true;
      }
      if (!v.is_array() || v.size() < 3) return false;
      for (size_t k = 0; k < 4 && k < v.size(); ++k)
        at.col[k] = std::clamp(v[k].get<float>(), 0.f, 1.f);
      if (v.size() < 4) at.col[3] = 1.f;
      return true;
    case AttrType::Filename:
    case AttrType::Text:
      if (!v.is_string()) return false;
      at.s = v.get<std::string>();
      return true;
    default:
      return false;
  }
}

} // namespace

// Returns 1 when the op was handled and applied, 0 when handled but nothing
// changed, and -1 when this module does not know the op at all — which is how
// the caller knows to keep looking.
int ai_graph_op(App &a, const std::string &op, const json &act,
                std::string &err) {
  gpx::Graph &g = a.graph;

  if (op == "add_node") {
    std::string type = act.value("type", "");
    if (type.empty()) {
      err = "add_node needs a type";
      return 0;
    }
    gpx::Node *n = g.add_node(type, act.value("x", 0.f), act.value("y", 0.f));
    if (!n) {
      err = "unknown node type '" + type + "'";
      return 0;
    }
    if (act.contains("attrs") && act["attrs"].is_object())
      for (auto &[key, val] : act["attrs"].items())
        if (gpx::Attribute *at = n->attrs.find(key)) set_attr_value(*at, val);
    if (act.contains("alias") && act["alias"].is_string())
      aliases()[act["alias"].get<std::string>()] = n->id;
    a.selected_node = n->id;
    a.graph_layout_serial++;
    a.request_eval();
    return 1;
  }

  if (op == "delete_node") {
    gpx::Node *n = find_node(a, act, "node");
    if (!n) {
      err = "delete_node: no such node";
      return 0;
    }
    uint64_t id = n->id;
    if (a.view_node == id) a.view_node = 0;
    if (a.selected_node == id) a.selected_node = 0;
    g.remove_node(id);
    a.graph_layout_serial++;
    a.request_eval();
    return 1;
  }

  if (op == "connect") {
    gpx::Node *from = find_node(a, act, "from");
    gpx::Node *to = find_node(a, act, "to");
    if (!from || !to) {
      err = "connect: 'from' or 'to' node not found";
      return 0;
    }
    std::string fp = act.value("from_port", ""), tp = act.value("to_port", "");
    // Default to the obvious ports. Most nodes have exactly one of each and
    // making the caller name them every time is friction with no payoff.
    if (fp.empty())
      for (const gpx::Port &p : from->ports)
        if (p.dir == gpx::PortDir::Out) { fp = p.name; break; }
    if (tp.empty())
      for (const gpx::Port &p : to->ports)
        if (p.dir == gpx::PortDir::In) { tp = p.name; break; }
    if (!g.add_link(from->id, fp, to->id, tp)) {
      err = "connect: no such port, or the link would make a cycle (" + fp +
            " -> " + tp + ")";
      return 0;
    }
    a.request_eval();
    return 1;
  }

  if (op == "disconnect") {
    // by link id, or by naming the input end — which is how a person thinks
    // about it, since an input takes only one link
    if (act.contains("link")) {
      g.remove_link(act["link"].get<uint64_t>());
      a.request_eval();
      return 1;
    }
    gpx::Node *to = find_node(a, act, "to");
    if (!to) {
      err = "disconnect needs 'link', or 'to' and 'to_port'";
      return 0;
    }
    std::string tp = act.value("to_port", "");
    int removed = 0;
    for (size_t i = g.links.size(); i-- > 0;) {
      if (g.links[i].to_node != to->id) continue;
      if (!tp.empty() && g.links[i].to_port != tp) continue;
      g.remove_link(g.links[i].id);
      ++removed;
    }
    if (!removed) {
      err = "disconnect: nothing connected there";
      return 0;
    }
    a.request_eval();
    return 1;
  }

  if (op == "set_attr") {
    gpx::Node *n = find_node(a, act, "node");
    if (!n) {
      err = "set_attr: no such node";
      return 0;
    }
    // one key, or a whole object of them
    int done = 0;
    auto apply_one = [&](const std::string &key, const json &val) {
      gpx::Attribute *at = n->attrs.find(key);
      if (!at) {
        err = "set_attr: '" + n->type + "' has no attribute '" + key + "'";
        return;
      }
      if (set_attr_value(*at, val)) ++done;
      else err = "set_attr: value does not fit attribute '" + key + "'";
    };
    if (act.contains("attrs") && act["attrs"].is_object())
      for (auto &[key, val] : act["attrs"].items()) apply_one(key, val);
    else if (act.contains("key"))
      apply_one(act["key"].get<std::string>(), act.value("value", json()));
    else {
      err = "set_attr needs 'key' and 'value', or an 'attrs' object";
      return 0;
    }
    if (!done) return 0;
    g.mark_dirty(n->id);
    a.request_eval();
    return 1;
  }

  if (op == "bypass") {
    gpx::Node *n = find_node(a, act, "node");
    if (!n) {
      err = "bypass: no such node";
      return 0;
    }
    n->enabled = !act.value("bypass", true);
    g.mark_all_dirty();
    a.request_eval();
    return 1;
  }

  if (op == "move_node") {
    gpx::Node *n = find_node(a, act, "node");
    if (!n) {
      err = "move_node: no such node";
      return 0;
    }
    n->pos_x = act.value("x", n->pos_x);
    n->pos_y = act.value("y", n->pos_y);
    a.graph_layout_serial++;
    return 1;
  }

  if (op == "clear_graph") {
    g.clear();
    a.view_node = a.selected_node = 0;
    a.graph_layout_serial++;
    a.request_eval();
    return 1;
  }

  if (op == "set_resolution") {
    int r = act.value("resolution", act.value("value", 0));
    if (r < 64 || r > 8192) {
      err = "set_resolution: 64..8192";
      return 0;
    }
    g.resolution = r;
    g.mark_all_dirty();
    a.request_eval();
    return 1;
  }

  if (op == "view_node") {
    // which node the 3D viewport shows. Without this a script can build a
    // graph and never see it, because the viewport stays pinned to whatever
    // it was looking at.
    if (act.value("node", json()).is_null() && !act.contains("node")) {
      a.view_node = 0; // back to automatic
      return 1;
    }
    gpx::Node *n = find_node(a, act, "node");
    if (!n) {
      err = "view_node: no such node";
      return 0;
    }
    a.view_node = n->id;
    a.request_eval();
    return 1;
  }

  return ai_scene_op(a, op, act, err); // the rest live in ai_ops_scene.cpp
}

} // namespace studio
