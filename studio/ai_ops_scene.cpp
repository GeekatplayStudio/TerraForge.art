// Geekatplay TerraForge — scene, animation and session operations for the
// API/MCP surface: time, sequence render, camera and attribute keys, node
// selection, object lock, node editors, workspace, evaluate, assign_material.
// Split from ai_ops_graph.cpp for the 500-line module rule.
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
#include <stdexcept>
#include <string>

using nlohmann::json;

namespace studio {

// Second half of the graph-op dispatch: ai_graph_op falls through to this
// when none of the node-editing ops matched. Same return convention.
int ai_scene_op(App &a, const std::string &op, const json &act,
                std::string &err) {
  gpx::Graph &g = a.graph;

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
    // "properties": true does what clicking the node does — the Properties
    // panel switches to the Node tab. Scripts could not reach that tab
    // before, which is why the Properties-while-evaluating crash was never
    // reproducible from automation.
    if (act.value("properties", false)) a.prop_tab = TAB_NODE;
    return 1;
  }

  if (op == "set_locked") {
    // Lock an object in place (no gizmo, no dragging, transform read-only)
    // or free it. Object by name; omitted = the selected one.
    std::string want = act.value("object", std::string());
    bool locked = act.value("locked", true);
    int hits = 0;
    SceneState &sc = scene();
    for (int i = 0; i < (int)sc.objects.size(); ++i) {
      SceneObject &o = sc.objects[i];
      if (want.empty() ? i != sc.selected : o.name != want) continue;
      o.locked = locked;
      ++hits;
    }
    if (!hits) {
      err = "set_locked: no object named '" + want + "'";
      return 0;
    }
    return 1;
  }

  if (op == "open_node_editor") {
    // Another node editor window, pinned to a domain: terrain, materials,
    // atmosphere, render, or all. It opens as a tab beside the main graph;
    // its corner button floats it out to the second monitor.
    const json &v = act.contains("domain") ? act["domain"] : act["value"];
    int d = -1;
    if (v.is_number()) d = v.get<int>();
    else if (v.is_string()) {
      std::string s = v.get<std::string>();
      for (auto &c : s) c = (char)tolower(c);
      const char *names[5] = {"terrain", "material", "atmosphere", "render", "all"};
      for (int i = 0; i < 5; ++i)
        if (s.find(names[i]) != std::string::npos) d = i;
    }
    if (d < 0 || d > 4) {
      err = "open_node_editor: domain 0..4 or terrain/materials/atmosphere/render/all";
      return 0;
    }
    graph_editor_add(a, d);
    return 1;
  }

  if (op == "debug_crash") {
    // Throws on the UI thread on purpose, so the crash pipeline (terminate
    // handler -> logs/crash_<stamp>.txt with what() and a stack) can be
    // exercised without waiting for a real one.
    throw std::runtime_error("debug_crash requested through the actions API");
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

  if (op == "assign_material") {
    // Bind a MaterialOutput to a scene object (the terrain unless named), the
    // same thing the Materials panel's Assign button does. "node" 0 or ""
    // unbinds. This is the last wire of the erosion → material pipeline:
    // ErosionLayers → MaterialStack → MaterialOutput → the ground.
    gpx::Node *m = find_node(a, act, "node");
    bool unbind = act.contains("node") &&
                  ((act["node"].is_number() && act["node"].get<uint64_t>() == 0) ||
                   (act["node"].is_string() && act["node"].get<std::string>().empty()));
    if (!m && !unbind) {
      err = "assign_material: no such node";
      return 0;
    }
    if (m && m->type != "MaterialOutput") {
      err = "assign_material: '" + m->type + "' is not a MaterialOutput";
      return 0;
    }
    std::string want = act.value("object", std::string());
    int hits = 0;
    for (SceneObject &o : scene().objects) {
      if (want.empty() ? o.type != SceneObject::Terrain : o.name != want) continue;
      o.material_node = m ? m->id : 0;
      ++hits;
    }
    if (!hits) {
      err = "assign_material: no object named '" + want + "'";
      return 0;
    }
    // assigning a material means wanting to see it: the viewport's
    // "textured" switch is what shows an albedo on the ground at all
    if (m) render_settings().use_albedo = true;
    a.request_eval();
    return 1;
  }
  return -1; // not ours
}

} // namespace studio
