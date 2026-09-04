// Geekatplay TerraForge — scene-object operations of the AI action dispatcher:
// selection, placing and importing objects, lights, primitives, scatter,
// planets and infinite terrains. Split from ai_actions.cpp for the 500-line
// module rule.
#include "ai_actions_internal.hpp"
#include "ai_assist.hpp"
#include "app.hpp"
#include "scene.hpp"
#include <json.hpp>
#include <cmath>
#include <fstream>
#include <mutex>
#include <string>

using json = nlohmann::json;

namespace studio {

bool ai_scene_object_op(App &a, const std::string &op, const json &act,
                        int &applied, std::string &err) {
  SceneState &sc = scene();
    if (op == "select") {
      if (act.contains("name")) {
        std::string want = act["name"].get<std::string>();
        for (int i = 0; i < (int)sc.objects.size(); ++i)
          if (sc.objects[i].name == want) {
            sc.selected = i;
            a.scene_selection_serial++;
            ++applied;
          }
      }
    } else if (op == "place_object") {
      std::string want = act.value("name", std::string());
      for (auto &o : sc.objects) {
        if (o.type != SceneObject::Mesh) continue;
        if (!want.empty() && o.name != want) continue;
        read_vec3(act, "position", o.pos);
        if (act.contains("scale")) o.scale = act["scale"].get<float>();
        if (act.contains("rotation_deg")) o.yaw = act["rotation_deg"].get<float>();
        ++applied;
        break;
      }
    } else if (op == "add_light" || op == "set_light") {
      int idx = -1;
      if (op == "set_light") {
        std::string want = act.value("name", std::string());
        for (int i = 0; i < (int)sc.objects.size(); ++i)
          if (sc.objects[i].type == SceneObject::Light &&
              (want.empty() || sc.objects[i].name == want))
            idx = i;
        if (idx < 0) {
          err = "no light named '" + want + "'";
          return true;
        }
      } else {
        idx = scene_add_light(act.value("name", std::string()));
      }
      SceneObject &o = sc.objects[idx];
      read_vec3(act, "position", o.pos);
      read_vec3(act, "color", o.color);
      if (act.contains("intensity"))
        o.light_intensity = act["intensity"].get<float>();
      if (act.contains("reach")) o.light_radius = act["reach"].get<float>();
      if (act.contains("type") && act["type"].is_string())
        o.light_type = act["type"].get<std::string>() == "spot" ? 1 : 0;
      if (act.contains("cone")) {
        o.light_cone = act["cone"].get<float>();
        o.light_type = 1;
      }
      if (act.contains("heading_deg")) o.yaw = act["heading_deg"].get<float>();
      if (act.contains("pitch_deg")) o.pitch = act["pitch_deg"].get<float>();
      a.scene_selection_serial++;
      ++applied;
    } else if (op == "add_primitive") {
      std::string kind = act.value("kind", std::string("cube"));
      int idx = scene_add_primitive(kind, act.value("name", std::string()));
      if (idx < 0) {
        err = "unknown primitive '" + kind +
              "' (cube, sphere, plane, cylinder, cone)";
      } else {
        SceneObject &o = sc.objects[idx];
        read_vec3(act, "position", o.pos);
        if (act.contains("scale")) o.scale = act["scale"].get<float>();
        read_vec3(act, "color", o.color);
        a.scene_selection_serial++;
        ++applied;
      }
    } else if (op == "import_object") {
      std::string path = act.value("path", std::string());
      std::string ierr;
      int idx = scene_import_obj(path, ierr);
      if (idx < 0) {
        err = "import failed: " + ierr;
      } else {
        SceneObject &o = sc.objects[idx];
        if (act.contains("name")) o.name = act["name"].get<std::string>();
        read_vec3(act, "position", o.pos);
        if (act.contains("scale")) o.scale = act["scale"].get<float>();
        sc.selected = idx;
        a.scene_selection_serial++;
        ++applied;
      }
    } else if (op == "set_scatter") {
      // bind a mesh object to a Points node: copies of the mesh appear at
      // every point, standing on the terrain
      std::string want = act.value("object", std::string());
      uint64_t node_id = 0;
      if (act.contains("node")) {
        if (act["node"].is_number()) node_id = act["node"].get<uint64_t>();
        else {
          std::string t = act["node"].get<std::string>();
          for (auto &cand : a.graph.nodes)
            if (cand->type == t || std::to_string(cand->id) == t)
              node_id = cand->id;
        }
      }
      for (auto &o : sc.objects) {
        if (o.type != SceneObject::Mesh) continue;
        if (!want.empty() && o.name != want) continue;
        o.scatter_node = node_id;
        if (act.contains("size")) o.scatter_scale = act["size"].get<float>();
        if (act.contains("jitter")) o.scatter_jitter = act["jitter"].get<float>();
        if (act.contains("sway")) o.scatter_sway = act["sway"].get<float>();
        if (act.contains("size_from_value"))
          o.scatter_value_size = act["size_from_value"].get<float>();
        if (act.contains("seed")) o.scatter_seed = act["seed"].get<uint32_t>();
        if (!node_id) o.inst.clear();
        ++applied;
        if (!want.empty()) break;
      }
      if (!applied) err = "no mesh object named '" + want + "'";
      else a.request_eval();
    } else if (op == "add_planet") {
      int idx = scene_add_planet(act.value("name", std::string()));
      SceneObject &o = sc.objects[idx];
      PlanetData &P = o.planet;
      read_vec3(act, "position", o.pos);
      if (act.contains("radius")) P.radius = act["radius"].get<float>();
      if (act.contains("relief")) P.relief = act["relief"].get<float>();
      if (act.contains("seed")) P.seed = act["seed"].get<uint32_t>();
      if (act.contains("sea_level")) P.sea_level = act["sea_level"].get<float>();
      if (act.contains("snow_line")) P.snow_line = act["snow_line"].get<float>();
      if (act.contains("atmosphere")) P.atmo_density = act["atmosphere"].get<float>();
      read_vec3(act, "water_color", P.water_color);
      read_vec3(act, "rock_low", P.rock_low);
      read_vec3(act, "rock_high", P.rock_high);
      read_vec3(act, "atmo_color", P.atmo_color);
      if (act.contains("surface_node"))
        P.surface_node = planet_surface_node_of(a, act["surface_node"]);
      sc.selected = idx;
      a.scene_selection_serial++;
      ++applied;
    } else if (op == "set_planet") {
      std::string want = act.value("name", std::string());
      for (auto &o : sc.objects) {
        if (o.type != SceneObject::Planet) continue;
        if (!want.empty() && o.name != want) continue;
        PlanetData &P = o.planet;
        read_vec3(act, "position", o.pos);
        if (act.contains("radius")) P.radius = act["radius"].get<float>();
        if (act.contains("relief")) P.relief = act["relief"].get<float>();
        if (act.contains("seed")) P.seed = act["seed"].get<uint32_t>();
        if (act.contains("sea_level")) P.sea_level = act["sea_level"].get<float>();
        if (act.contains("snow_line")) P.snow_line = act["snow_line"].get<float>();
        if (act.contains("atmosphere")) P.atmo_density = act["atmosphere"].get<float>();
        read_vec3(act, "water_color", P.water_color);
        read_vec3(act, "rock_low", P.rock_low);
        read_vec3(act, "rock_high", P.rock_high);
        read_vec3(act, "atmo_color", P.atmo_color);
        if (act.contains("surface_node"))
          P.surface_node = planet_surface_node_of(a, act["surface_node"]);
        ++applied;
        if (!want.empty()) break;
      }
      if (!applied) err = "no planet named '" + want + "'";
    } else if (op == "add_infinite_terrain") {
      // "planet":"name" attaches to that planet; omitted = home ground plane
      int parent = -1;
      std::string pn = act.value("planet", std::string());
      if (!pn.empty()) {
        for (int i = 0; i < (int)sc.objects.size(); ++i)
          if (sc.objects[i].type == SceneObject::Planet &&
              sc.objects[i].name == pn)
            parent = i;
        if (parent < 0) {
          err = "no planet named '" + pn + "'";
          return true;
        }
      }
      int idx = scene_add_infinite_surface(parent, act.value("name", std::string()));
      gpx::planet::Layer &L = sc.objects[idx].surf.layer;
      std::string style = act.value("style", std::string());
      if (style == "hills") L.type = 0;
      else if (style == "mountains" || style == "ridged") L.type = 1;
      else if (style == "dunes" || style == "billow") L.type = 2;
      else if (style == "terrain" || style == "realistic" ||
               style == "landscape") L.type = 3;
      if (act.contains("scale")) L.frequency = act["scale"].get<float>();
      if (act.contains("amplitude")) L.amplitude = act["amplitude"].get<float>();
      if (act.contains("coverage")) L.coverage = act["coverage"].get<float>();
      if (act.contains("seed")) L.seed = act["seed"].get<uint32_t>();
      sc.selected = idx;
      a.scene_selection_serial++;
      ++applied;
    } else {
      return false;
    }
    return true;
}

} // namespace studio
