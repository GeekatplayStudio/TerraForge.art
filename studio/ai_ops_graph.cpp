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
#include "app.hpp"
#include "scene.hpp"
#include "gpx/metanode.hpp"
#include "gpx/node_graph.hpp"
#include <json.hpp>
#include <filesystem>
#include <string>

using nlohmann::json;

namespace studio {

namespace {

// Nodes are addressed by id. Accepting a *type* name as well costs nothing and
// is how a person naturally refers to a graph they can see ("the Hydraulic"),
// so both work; the type form takes the last match, which is the one furthest
// down the chain and almost always the one meant.
gpx::Node *find_node(App &a, const json &j, const char *key) {
  if (!j.contains(key)) return nullptr;
  const json &v = j[key];
  if (v.is_number()) return a.graph.find_node(v.get<uint64_t>());
  if (!v.is_string()) return nullptr;
  const std::string s = v.get<std::string>();
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

  if (op == "set_time") {
    a.graph.time = act.value("time", 0.f);
    a.request_eval();
    return 1;
  }

  if (op == "render_sequence") {
    a.seq_dir = act.value("dir", std::string("sequence"));
    std::error_code ec;
    std::filesystem::create_directories(a.seq_dir, ec);
    a.seq_fps = act.value("fps", 30.f);
    a.seq_w = act.value("width", 1280);
    a.seq_h = act.value("height", 720);
    if (act.contains("start")) a.anim_start = act["start"].get<float>();
    if (act.contains("end")) a.anim_end = act["end"].get<float>();
    a.seq_cam_path = 0;
    if (act.contains("camera_path")) {
      std::string t = act["camera_path"].get<std::string>();
      for (auto &cand : a.graph.nodes)
        if (cand->type == t || std::to_string(cand->id) == t)
          a.seq_cam_path = cand->id;
    }
    a.seq_cam_height = act.value("camera_height", 0.08f);
    a.seq_sun_sweep = act.contains("sun_from") || act.contains("sun_to");
    if (act.contains("sun_from") && act["sun_from"].is_array() &&
        act["sun_from"].size() >= 2) {
      a.seq_sun[0] = act["sun_from"][0].get<float>();
      a.seq_sun[1] = act["sun_from"][1].get<float>();
    }
    if (act.contains("sun_to") && act["sun_to"].is_array() &&
        act["sun_to"].size() >= 2) {
      a.seq_sun[2] = act["sun_to"][0].get<float>();
      a.seq_sun[3] = act["sun_to"][1].get<float>();
    }
    a.seq_total = std::max(
        (int)((a.anim_end - a.anim_start) * a.seq_fps + 0.5f), 1);
    a.seq_frame = 0;
    a.anim_playing = false;
    a.graph.time = a.anim_start;
    a.seq_active = true;
    a.request_eval();
    return 1;
  }

  if (op == "set_camera_key") {
    // key the named camera's whole pose (eye + target) at the given time
    SceneState &sc = scene();
    std::string want = act.value("camera", act.value("name", std::string()));
    int idx = -1;
    for (int i = 0; i < (int)sc.objects.size(); ++i)
      if (sc.objects[i].type == SceneObject::Camera &&
          (want.empty() || sc.objects[i].name == want))
        idx = i;
    if (idx < 0) {
      err = "no camera named '" + want + "'";
      return 0;
    }
    CameraData &cd = sc.objects[idx].cam;
    float t = act.value("time", a.graph.time);
    bool remove = act.value("remove", false);
    for (int k = 0; k < 3; ++k) {
      if (remove) {
        cd.anim_eye[k].remove_key(t);
        cd.anim_target[k].remove_key(t);
      } else {
        cd.anim_eye[k].set_key(t, cd.eye[k]);
        cd.anim_target[k].set_key(t, cd.target[k]);
      }
    }
    return 1;
  }

  if (op == "set_key") {
    gpx::Node *n = find_node(a, act, "node");
    if (!n) {
      err = "set_key: no such node";
      return 0;
    }
    std::string key = act.value("attr", "");
    gpx::Attribute *at = n->attrs.find(key);
    if (!at) {
      err = "set_key: no attribute '" + key + "' on " + n->type;
      return 0;
    }
    float t = act.value("time", a.graph.time);
    float v = act.contains("value")
                  ? act["value"].get<float>()
                  : (at->type == gpx::AttrType::Float ? at->f : (float)at->i);
    if (act.value("remove", false)) at->anim.remove_key(t);
    else at->anim.set_key(t, v);
    if (act.contains("interp") && act["interp"].is_string()) {
      std::string s = act["interp"].get<std::string>();
      at->anim.interp = s == "constant" ? gpx::Interp::Constant
                        : s == "linear" ? gpx::Interp::Linear
                                        : gpx::Interp::Smooth;
    }
    n->dirty = true;
    a.request_eval();
    return 1;
  }

  if (op == "select_node") {
    gpx::Node *n = find_node(a, act, "node");
    if (!n) {
      err = "select_node: no such node";
      return 0;
    }
    a.selected_node = n->id;
    return 1;
  }

  if (op == "set_workspace") {
    // Which workflow the second bar has selected, and therefore which tools
    // the third bar offers: 0 terrain, 1 materials, 2 atmosphere, 3 render.
    const json &v = act.contains("workspace") ? act["workspace"] : act["value"];
    int w = -1;
    if (v.is_number()) w = v.get<int>();
    else if (v.is_string()) {
      std::string s = v.get<std::string>();
      for (auto &c : s) c = (char)tolower(c);
      const char *names[4] = {"terrain", "material", "atmosphere", "render"};
      for (int i = 0; i < 4; ++i)
        if (s.find(names[i]) != std::string::npos) w = i;
    }
    if (w < 0 || w > 3) {
      err = "set_workspace: 0..3, or terrain/materials/atmosphere/render";
      return 0;
    }
    a.workspace = w;
    return 1;
  }

  if (op == "evaluate") {
    g.mark_all_dirty();
    a.request_eval();
    return 1;
  }

  return -1; // not ours
}

} // namespace studio
