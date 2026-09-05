// Geekatplay TerraForge - applying the AI/scripting action documents:
// every op the assistant or the actions inbox may emit, one dispatcher.
// Split from ai_assist.cpp for the 500-line module rule; the assist UI
// stays there.
// Geekatplay TerraForge — natural-language assistant shared by every tab.
// The model returns a small JSON action document; ai_apply_actions executes
// it. The scripting API and the MCP server call the same function, so text,
// script and tool calls all take one code path.
#include "ai_assist.hpp"
#include "ai_actions_internal.hpp"
#include "app.hpp"
#include "console.hpp"
#include "ollama.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/serialization.hpp"
#include <imgui.h>
#include <json.hpp>
#include <atomic>
#include <fstream>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace studio {

std::string field_gpu_verify_all(App &a);

bool ai_apply_actions(App &a, const std::string &text, std::string &err) {
  renderer_invalidate_views();
  json j;
  try {
    size_t b = text.find('{'), e = text.rfind('}');
    if (b == std::string::npos || e == std::string::npos || e <= b) {
      err = "no JSON object in the reply";
      return false;
    }
    j = json::parse(text.substr(b, e - b + 1));
  } catch (const std::exception &ex) {
    err = std::string("JSON parse: ") + ex.what();
    return false;
  }
  json actions = j.contains("actions") ? j["actions"] : json::array({j});
  if (!actions.is_array() || actions.empty()) {
    err = "no actions";
    return false;
  }
  // Anything the assistant, a script or an MCP tool does is a single undo
  // step, so an AI change can be reverted like any other edit.
  {
    std::string what = "AI change";
    if (actions.size() == 1 && actions[0].contains("op"))
      what = "AI: " + actions[0]["op"].get<std::string>();
    else
      what = "AI: " + std::to_string(actions.size()) + " changes";
    bool only_history = true;
    for (const json &act : actions) {
      std::string op = act.value("op", "");
      if (op != "undo" && op != "redo") only_history = false;
    }
    if (!only_history) undo_push(a, what);
  }
  RenderSettings &rs = render_settings();
  SceneState &sc = scene();
  int applied = 0;
  for (const json &act : actions) {
    if (!act.is_object() || !act.contains("op")) continue;
    std::string op = act["op"].get<std::string>();

    if (op == "add_camera" || op == "set_camera") {
      int idx = -1;
      if (op == "set_camera") {
        if (act.contains("name")) {
          std::string want = act["name"].get<std::string>();
          for (int i = 0; i < (int)sc.objects.size(); ++i)
            if (sc.objects[i].type == SceneObject::Camera &&
                sc.objects[i].name == want)
              idx = i;
        }
        if (idx < 0 && sc.selected >= 0 && sc.selected < (int)sc.objects.size() &&
            sc.objects[sc.selected].type == SceneObject::Camera)
          idx = sc.selected;
        if (idx < 0) idx = scene_active_camera();
      }
      if (idx < 0) {
        idx = scene_add_camera(act.value("name", std::string()));
      }
      apply_camera_fields(sc.objects[idx].cam, act);
      if (act.value("activate", op == "add_camera")) {
        scene_active_camera() = idx;
        scene_last_used_camera() = idx;
      }
      sc.selected = idx;
      a.scene_selection_serial++;
      ++applied;
    } else if (op == "set_sun") {
      rs.sun_mode = 0;
      if (act.contains("azimuth_deg")) rs.sun_azimuth = act["azimuth_deg"].get<float>();
      // below the horizon is allowed: that is what night is
      if (act.contains("altitude_deg")) rs.sun_altitude = std::clamp(act["altitude_deg"].get<float>(), -35.f, 89.f);
      if (act.contains("intensity")) rs.sun_intensity = act["intensity"].get<float>();
      read_vec3(act, "color", rs.sun_color);
      ++applied;
    } else if (op == "set_sky") {
      if (act.contains("density")) rs.atmosphere_density = act["density"].get<float>();
      if (act.contains("ambient")) rs.ambient_intensity = act["ambient"].get<float>();
      read_vec3(act, "zenith", rs.sky_zenith);
      read_vec3(act, "horizon", rs.sky_horizon);
      ++applied;
    } else if (op == "set_fog") {
      if (act.contains("type") && act["type"].is_string()) {
        std::string t = act["type"].get<std::string>();
        rs.fog_type = t == "off" ? 0 : t == "haze" ? 1 : t == "pollution" ? 3 : 2;
      }
      if (act.contains("density")) rs.fog_density = act["density"].get<float>();
      if (act.contains("level")) rs.fog_level = act["level"].get<float>();
      read_vec3(act, "color", rs.fog_color);
      ++applied;
    } else if (op == "set_clouds") {
      if (act.contains("enabled")) rs.clouds_on = act["enabled"].get<bool>();
      if (act.contains("type") && act["type"].is_string()) {
        std::string t = act["type"].get<std::string>();
        rs.cloud_type = t == "stratus" ? 0 : t == "cumulonimbus" ? 2 : 1;
      }
      if (act.contains("coverage")) rs.cloud_coverage = std::clamp(act["coverage"].get<float>(), 0.f, 1.f);
      if (act.contains("density")) rs.cloud_density = act["density"].get<float>();
      if (act.contains("altitude")) rs.cloud_altitude = act["altitude"].get<float>();
      if (act.contains("thickness")) rs.cloud_thickness = act["thickness"].get<float>();
      if (act.contains("wind_speed")) rs.cloud_wind_speed = act["wind_speed"].get<float>();
      ++applied;
    } else if (op == "set_water") {
      if (act.contains("enabled")) rs.show_water = act["enabled"].get<bool>();
      if (act.contains("level")) rs.water_level = act["level"].get<float>();
      read_vec3(act, "deep", rs.water_deep_color);
      read_vec3(act, "shallow", rs.water_shallow_color);
      if (act.contains("foam")) rs.water_foam = act["foam"].get<bool>();
      ++applied;
    } else if (op == "set_render") {
      int cam = scene_active_camera();
      RenderAssign fallback;
      RenderAssign &r = (cam >= 0 && cam < (int)sc.objects.size() &&
                         sc.objects[cam].type == SceneObject::Camera)
                            ? sc.objects[cam].cam.render
                            : fallback;
      if (act.contains("engine") && act["engine"].is_string())
        r.engine = engine_index(act["engine"].get<std::string>());
      r.width = act.value("width", r.width);
      r.height = act.value("height", r.height);
      r.samples = act.value("samples", r.samples);
      if (act.contains("output")) r.output = act["output"].get<std::string>();
      if (act.contains("passes")) r.passes = act["passes"].get<bool>();
      if (act.contains("panorama")) r.panorama = act["panorama"].get<bool>();
      ++applied;
    } else if (op == "render") {
      a.request_camera_render = scene_active_camera();
      ++applied;
    } else if (op == "render_passes") {
      // The render editor from a script: the viewport engine draws the beauty
      // in the chosen format and one linear EXR per requested pass. `passes`
      // is a bit mask or a list of pass names; absent, the project's setting.
      RenderSettings &rsg = render_settings();
      std::string path = act.value("path", rsg.render_path);
      int w = std::clamp(act.value("width", rsg.render_width), 16, 8192);
      int h = std::clamp(act.value("height", rsg.render_height), 16, 8192);
      int mask = rsg.pass_mask;
      if (act.contains("passes")) {
        if (act["passes"].is_number()) mask = act["passes"].get<int>();
        else if (act["passes"].is_array()) {
          mask = 0;
          for (const auto &nm : act["passes"])
            for (int i = 0; i < RENDER_PASS_COUNT; ++i)
              if (nm.is_string() && nm.get<std::string>() == render_pass_name(i))
                mask |= 1 << i;
        }
      }
      int format = act.value("format", rsg.render_format);
      std::string report;
      bool ok = renderer_render_passes(path, w, h, mask, format, report);
      log_info("render", report);
      if (ok) ++applied;
      else err = "render_passes: " + report;
    } else if (op == "undo") {
      int n = act.value("steps", 1);
      for (int i = 0; i < n && undo_perform(a); ++i) ++applied;
      if (!applied) err = "nothing to undo";
    } else if (op == "redo") {
      int n = act.value("steps", 1);
      for (int i = 0; i < n && redo_perform(a); ++i) ++applied;
      if (!applied) err = "nothing to redo";
    } else if (ai_scene_object_op(a, op, act, applied, err)) {
      // scene-object ops live in ai_actions_scene.cpp; the branch bodies count
      // into `applied` and set `err` exactly as they did here
    } else if (op == "export_instances") {
      // the scattered copies of a mesh as CSV transforms, for any DCC that
      // instances its own assets: x, y, z, scale, yaw_radians per line
      std::string want = act.value("object", std::string());
      std::string path = act.value("path", std::string());
      if (path.empty()) {
        err = "export_instances needs a path";
        continue;
      }
      scene_rebuild_scatter_instances(a);
      // a scripted batch may export before the async evaluation has run the
      // scatter node; evaluate synchronously once and rebuild if so
      bool any = false;
      for (auto &o : sc.objects)
        any = any || (o.type == SceneObject::Mesh && !o.inst.empty());
      if (!any) {
        {
          std::lock_guard<std::mutex> lk(a.graph_mtx);
          a.graph.evaluate();
        }
        scene_rebuild_scatter_instances(a);
      }
      for (auto &o : sc.objects) {
        if (o.type != SceneObject::Mesh) continue;
        if (!want.empty() && o.name != want) continue;
        if (o.inst.empty()) continue;
        std::ofstream f(path);
        if (!f) {
          err = "cannot write " + path;
          break;
        }
        f << "x,y,z,scale,yaw\n";
        const size_t per = 8;
        for (size_t i = 0; i + per <= o.inst.size(); i += per) {
          const float *s = o.inst.data() + i;
          f << s[0] << ',' << s[1] << ',' << s[2] << ',' << s[3] << ','
            << std::atan2(s[5], s[4]) << '\n';
        }
        ++applied;
        if (!want.empty()) break;
      }
      if (!applied && err.empty())
        err = "no scattered mesh " +
              (want.empty() ? std::string("found") : "named '" + want + "'");
    } else if (op == "run_macro") {
      // a macro is simply a saved action document; running one applies its
      // actions through this same dispatcher (one level deep - a macro that
      // calls run_macro is refused rather than allowed to recurse)
      std::string path = act.value("path", std::string());
      std::ifstream mf(path);
      if (!mf) {
        err = "cannot read macro " + path;
        continue;
      }
      std::string text((std::istreambuf_iterator<char>(mf)),
                       std::istreambuf_iterator<char>());
      if (text.find("run_macro") != std::string::npos) {
        err = "macros cannot call run_macro";
        continue;
      }
      std::string merr;
      if (ai_apply_actions(a, text, merr)) ++applied;
      else err = "macro: " + merr;
    } else if (op == "save_project") {
      std::string path = act.value("path", std::string());
      if (path.empty()) {
        err = "save_project needs a path";
      } else if (project_save(a, path)) {
        ++applied;
      } else {
        err = "could not write " + path;
      }
    } else if (op == "open_project") {
      std::string path = act.value("path", std::string());
      if (path.empty()) {
        err = "open_project needs a path";
      } else if (project_load(a, path)) {
        ++applied;
      } else {
        err = "could not open " + path;
      }
    } else if (op == "verify_field_gpu") {

      // the load-bearing check of the dual-domain design: does the generated

      // shader compute the same numbers as the CPU evaluator?

      a.status = "field GPU check:\n" + field_gpu_verify_all(a);
      log_info("field-gpu", a.status);

      ++applied;

    } else if (op == "graph") {
      if (act.contains("spec")) {
        std::string spec = act["spec"].dump();
        std::string gerr;
        std::lock_guard<std::mutex> lk(a.graph_mtx);
        // replace:true clears first, so a script can author a whole scene
        // rather than only ever bolting more onto what is already there
        bool merge = !act.value("replace", false);
        if (gpx::graph_from_ai_spec(a.graph, spec, gerr, nullptr, merge)) {
          if (!merge) a.view_node = a.selected_node = 0;
          a.graph_layout_serial++;
          a.request_eval();
          ++applied;
        } else {
          err = gerr;
        }
      }
    } else {
      // Viewport settings touch no graph state, so they are tried first and
      // without the lock — waiting on a running evaluation to change the
      // exposure would be a stall for nothing.
      int r = ai_layout_op(a, op, act, err);
      if (r < 0) r = ai_mesh_op(a, op, act, err);
      if (r < 0) r = ai_material_op(a, op, act, err);
      if (r < 0) r = ai_asset_op(a, op, act, err);
      if (r < 0) r = ai_generate_op(a, op, act, err);
      if (r < 0) r = ai_view_op(a, op, act, err);
      if (r < 0) {
        // node-level graph editing lives in ai_ops_graph.cpp; -1 means it did
        // not recognise the op either, and it falls through to "unsupported"
        std::lock_guard<std::mutex> lk(a.graph_mtx);
        r = ai_graph_op(a, op, act, err);
      }
      if (r > 0) ++applied;
    }
  }
  if (!applied) {
    if (err.empty()) err = "no supported actions in the reply";
    return false;
  }
  return true;
}

} // namespace studio
