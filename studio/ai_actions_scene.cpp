// Geekatplay TerraForge — scene-object operations of the AI action dispatcher:
// selection, placing and importing objects, lights, primitives, scatter,
// planets and infinite terrains. Split from ai_actions.cpp for the 500-line
// module rule.
#include "ai_actions_internal.hpp"
#include "ai_assist.hpp"
#include "app.hpp"
#include "component_new.hpp"
#include "render_settings.hpp"
#include "imprint.hpp"
#include "scene.hpp"
#include "space_presets.hpp"
#include "world_shape.hpp"
#include <algorithm>
#include <json.hpp>
#include <cmath>
#include <fstream>
#include <mutex>
#include <string>

using json = nlohmann::json;

namespace studio {

// A nebula's kind by name or number (scene.hpp NebulaData::type).
static int nebula_type_of(const json &act, int fallback) {
  if (!act.contains("type")) return fallback;
  const json &t = act["type"];
  if (t.is_number()) return std::clamp(t.get<int>(), 0, 4);
  const std::string s = t.is_string() ? t.get<std::string>() : std::string();
  if (s == "nebula" || s == "emission" || s == "cloud") return 0;
  if (s == "dark" || s == "dark_nebula" || s == "dust") return 1;
  if (s == "galaxy" || s == "spiral") return 2;
  if (s == "elliptical" || s == "elliptical_galaxy") return 3;
  if (s == "planetary" || s == "ring") return 4;
  return fallback;
}
static void apply_nebula_fields(NebulaData &N, const json &act) {
  auto f = [&](const char *k, float &dst, float lo, float hi) {
    if (act.contains(k) && act[k].is_number()) dst = std::clamp(act[k].get<float>(), lo, hi);
  };
  f("azimuth", N.azimuth, -360.f, 360.f);
  f("elevation", N.elevation, -90.f, 90.f);
  f("size_deg", N.size_deg, 0.2f, 180.f);
  f("tilt_deg", N.tilt_deg, 0.f, 85.f);
  f("rotation_deg", N.rotation_deg, -360.f, 360.f);
  f("brightness", N.brightness, 0.f, 10.f);
  f("density", N.density, 0.f, 1.f);
  f("detail", N.detail, 0.f, 1.f);
  f("dust", N.dust, 0.f, 1.f);
  f("warp", N.warp, 0.f, 1.5f);
  f("glow", N.glow, 0.f, 2.f);
  if (act.contains("sources") && act["sources"].is_number())
    N.sources = std::clamp(act["sources"].get<int>(), 1, 4);
  f("turbulence", N.turbulence, 0.f, 1.f);
  f("lanes", N.lanes, 0.f, 1.f);
  f("core_glow", N.core_glow, 0.f, 2.f);
  f("source_stars", N.source_stars, 0.f, 2.f);
  if (act.contains("seed") && act["seed"].is_number()) N.seed = act["seed"].get<uint32_t>();
  // "palette":"auto" takes both colours from the realism dial
  if (act.value("palette", std::string()) == "auto")
    space_nebula_colors(N.type, render_settings().space.realism, N.seed, N.color1, N.color2);
  if (act.contains("arms") && act["arms"].is_number()) N.arms = std::clamp(act["arms"].get<int>(), 1, 6);
  read_vec3(act, "color1", N.color1);
  read_vec3(act, "color2", N.color2);
  read_vec3(act, "color3", N.color3);
}

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
      // which face of the world a tile or a surface layer stands on
      // (world_shape.hpp): "world", "outside", "inside", or 0..2
      auto read_side = [&](SceneObject &o) {
        if (!act.contains("side")) return false;
        const json &v = act["side"];
        int side = -1;
        if (v.is_number()) side = v.get<int>();
        else {
          const std::string s = v.get<std::string>();
          side = s == "world" ? SIDE_WORLD : s == "outside" ? SIDE_OUTSIDE : s == "inside" ? SIDE_INSIDE : -1;
        }
        if (side < 0 || side > 2) {
          err = "side is world, outside or inside";
          return false;
        }
        o.side = side;
        return true;
      };
      for (auto &o : sc.objects) {
        if (o.type == SceneObject::InfiniteSurface && (want.empty() || o.name == want)) {
          if (read_side(o)) ++applied;
          continue;
        }
        // meshes, and the terrain tile - which is placed, turned, sized and
        // deformed through the same fields (terrain_xform.hpp)
        if (o.type != SceneObject::Mesh && o.type != SceneObject::Terrain) continue;
        if (!want.empty() && o.name != want) continue;
        read_side(o);
        read_vec3(act, "position", o.pos);
        if (act.contains("scale")) o.scale = act["scale"].get<float>();
        if (act.contains("rotation_deg")) o.yaw = act["rotation_deg"].get<float>();
        if (act.contains("heading_deg")) o.yaw = act["heading_deg"].get<float>();
        if (act.contains("pitch_deg")) o.pitch = act["pitch_deg"].get<float>();
        if (act.contains("bank_deg")) o.roll = act["bank_deg"].get<float>();
        read_vec3(act, "squeeze", o.scl);
        read_vec3(act, "twist", o.deform.twist);
        if (act.contains("bend")) o.deform.bend = act["bend"].get<float>();
        if (act.contains("bend_axis")) o.deform.bend_axis = std::clamp(act["bend_axis"].get<int>(), 0, 2);
        read_vec3(act, "skew", o.deform.shear);
        if (act.contains("taper")) o.deform.taper = act["taper"].get<float>();
        if (act.contains("show_gizmo")) o.show_gizmo = act["show_gizmo"].get<bool>();
        ++applied;
        break;
      }
    } else if (op == "place_on_terrain") {
      std::string want = act.value("object", act.value("name", std::string()));
      int idx = -1;
      for (int i = 0; i < (int)sc.objects.size(); ++i)
        if (sc.objects[i].type == SceneObject::Mesh && (want.empty() || sc.objects[i].name == want)) { idx = i; break; }
      if (idx < 0) err = "place_on_terrain: no mesh object '" + want + "'";
      else if (imprint_place_on_terrain(a, idx) < 0) err = "place_on_terrain: no terrain object";
      else ++applied;
    } else if (op == "set_ground") {
      // the grounded object's settings, in metres
      std::string want = act.value("object", act.value("name", std::string()));
      const RenderSettings &rs = render_settings();
      const float tile_m = std::max(rs.terrain_size_m, 1e-3f);
      const float hm_m = std::max(rs.height_scale, 1e-6f) * tile_m;
      bool found = false;
      for (SceneObject &o : sc.objects) {
        if (o.type != SceneObject::Mesh || (!want.empty() && o.name != want)) continue;
        found = true;
        if (act.contains("lock")) o.ground_lock = act["lock"].get<bool>();
        if (act.contains("offset_m")) o.ground_offset = act["offset_m"].get<float>() / hm_m;
        if (act.contains("margin_m")) o.ground_margin = std::max(act["margin_m"].get<float>(), 0.f) / tile_m;
        if (act.contains("blend_m")) o.ground_blend = std::max(act["blend_m"].get<float>(), 0.f) / tile_m;
        if (act.contains("sink_m")) o.ground_sink = std::max(act["sink_m"].get<float>(), 0.f) / hm_m;
        // How far the ground may travel to meet it. Absent leaves the
        // default, which is unlimited and is how it behaved before.
        // A displacement in metres the terrain does not react to: positive
        // sinks the object in, negative lifts it out.
        if (act.contains("sunk_m")) o.ground_sunk = act["sunk_m"].get<float>() / hm_m;
        if (act.contains("lift_m")) o.ground_lift = std::max(act["lift_m"].get<float>(), 0.f) / hm_m;
        if (act.contains("dig_m")) o.ground_dig = std::max(act["dig_m"].get<float>(), 0.f) / hm_m;
        ++applied;
        if (!want.empty()) break;
      }
      if (!found) err = "set_ground: no mesh object '" + want + "'";
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
    } else if (op == "combine_objects" || op == "metaball") {
      // Both consume the selection and leave one object. `objects` names
      // them explicitly, because a script has no pointer to click with.
      SceneState &sc = scene();
      if (act.contains("objects") && act["objects"].is_array()) {
        std::vector<int> pick;
        for (const auto &e : act["objects"]) {
          const std::string want = e.is_string() ? e.get<std::string>() : "";
          for (int i = 0; i < (int)sc.objects.size(); ++i)
            if (e.is_number() ? i == e.get<int>() : sc.objects[i].name == want)
              pick.push_back(i);
        }
        if (pick.size()) {
          sc.selected = pick[0];
          sc.selection = pick;
        }
      }
      std::string why;
      bool ok;
      if (op == "metaball") {
        ok = scene_metaball_from_selection(act.value("smoothness", 1.f),
                                           act.value("detail", 64), why);
      } else {
        const std::string mode = act.value("mode", std::string("union"));
        const int code = mode == "intersect" || mode == "intersection" ? 1
                         : mode == "difference" || mode == "subtract"  ? 2
                                                                       : 0;
        ok = scene_boolean_selection(code, why);
      }
      if (!ok) {
        err = why;
        return 0;
      }
      a.scene_selection_serial++;
      a.status = sc.objects[sc.selected].name;
      ++applied;
    } else if (op == "add_primitive") {
      std::string kind = act.value("kind", std::string("cube"));
      // The same complete component the toolbar makes: object, the node that
      // drives it, and a material. A script adding a cube should get what a
      // person adding a cube gets.
      const NewComponent nc = component_add_primitive(
          a, kind, act.value("name", std::string()));
      int idx = nc.object;
      if (idx >= 0 && act.contains("detail"))
        if (gpx::Node *pn = a.graph.find_node(nc.node))
          if (gpx::Attribute *d = pn->attrs.find("detail"))
            d->i = act.value("detail", 24);
      // another plant of the same kind (the Primitive node's Plant seed)
      if (idx >= 0 && act.contains("seed") && act["seed"].is_number())
        if (gpx::Node *pn = a.graph.find_node(nc.node))
          if (gpx::Attribute *s = pn->attrs.find("seed"))
            s->seed = act["seed"].get<uint32_t>();
      if (idx < 0) {
        err = "unknown primitive '" + kind +
              "' (cube, sphere, plane, cylinder, cone, pine, juniper, palm, fern, grass, bush, boulder)";
      } else {
        SceneObject &o = sc.objects[idx];
        read_vec3(act, "position", o.pos);
        if (act.contains("scale")) o.scale = act["scale"].get<float>();
        read_vec3(act, "color", o.color);
        // The Primitive node drives the object: whatever is written to the
        // object is overwritten from the node's own metres every frame. So
        // the position and size go into the node, or the script's values
        // are silently replaced by the defaults and the cube ends up buried
        // in the mountain at 0.08 - which is exactly what happened.
        if (gpx::Node *pn = a.graph.find_node(nc.node)) {
          const float size_m = render_settings().terrain_size_m;
          auto put = [&](const char *key, float v) {
            if (gpx::Attribute *at = pn->attrs.find(key)) at->f = v;
          };
          put("x_m", o.pos[0] * size_m);
          put("y_m", o.pos[1] * size_m);
          put("z_m", o.pos[2] * size_m);
          put("size_m", o.scale * size_m);
          if (gpx::Attribute *c = pn->attrs.find("color"))
            for (int k = 0; k < 3; ++k) c->col[k] = o.color[k];
          a.graph.mark_dirty(pn->id);
          a.request_eval();
        }
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
      // by id, by a macro's alias, by a numeric string or by type - the
      // same resolution every graph op uses
      uint64_t node_id = 0;
      if (gpx::Node *sn = find_node(a, act, "node")) node_id = sn->id;
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
        if (act.contains("species")) o.scatter_species = std::max(act["species"].get<int>() - 1, -1);
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
      if (act.contains("clouds")) P.clouds = std::clamp(act["clouds"].get<float>(), 0.f, 1.f);
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
        if (act.contains("clouds")) P.clouds = std::clamp(act["clouds"].get<float>(), 0.f, 1.f);
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
    } else if (op == "add_moon") {
      int idx = scene_add_moon(act.value("name", std::string()));
      SceneObject &o = sc.objects[idx];
      read_vec3(act, "position", o.pos);
      if (act.contains("radius")) o.planet.radius = act["radius"].get<float>();
      if (act.contains("seed")) o.planet.seed = act["seed"].get<uint32_t>();
      sc.selected = idx;
      a.scene_selection_serial++;
      ++applied;
    } else if (op == "add_nebula") {
      int idx = scene_add_nebula(act.value("name", std::string()), nebula_type_of(act, 0));
      apply_nebula_fields(sc.objects[idx].nebula, act);
      sc.selected = idx;
      a.scene_selection_serial++;
      ++applied;
    } else if (op == "set_nebula") {
      std::string want = act.value("name", std::string());
      for (auto &o : sc.objects) {
        if (o.type != SceneObject::Nebula) continue;
        if (!want.empty() && o.name != want) continue;
        o.nebula.type = nebula_type_of(act, o.nebula.type);
        apply_nebula_fields(o.nebula, act);
        ++applied;
        if (!want.empty()) break;
      }
      if (!applied) err = "no nebula named '" + want + "'";
    } else if (op == "set_space") {
      // deep space behind the air (render_settings.hpp, shaders_space.cpp)
      RenderSettings &rs = render_settings();
      auto f = [&](const char *k, float &dst, float lo, float hi) {
        if (act.contains(k) && act[k].is_number()) dst = std::clamp(act[k].get<float>(), lo, hi);
      };
      auto b = [&](const char *k, bool &dst) {
        if (act.contains(k) && act[k].is_boolean()) dst = act[k].get<bool>();
      };
      auto i = [&](const char *k, int &dst, int lo, int hi) {
        if (act.contains(k) && act[k].is_number()) dst = std::clamp(act[k].get<int>(), lo, hi);
      };
      b("on", rs.space.on);
      f("brightness", rs.space.brightness, 0.f, 8.f);
      f("realism", rs.space.realism, 0.f, 1.f);
      f("glow", rs.space.glow, 0.f, 4.f);
      i("quality", rs.space.quality, 0, 3);
      b("stars", rs.space.stars);
      f("star_density", rs.space.star_density, 0.f, 1.f);
      f("star_brightness", rs.space.star_brightness, 0.f, 10.f);
      f("star_size", rs.space.star_size, 0.f, 6.f);
      f("star_temperature", rs.space.star_temperature, 0.f, 1.f);
      f("star_spikes", rs.space.star_spikes, 0.f, 4.f);
      f("star_halo", rs.space.star_halo, 0.f, 4.f);
      f("star_clump", rs.space.star_clump, 0.f, 1.f);
      i("star_seed", rs.space.star_seed, 1, 1 << 24);
      i("star_spike_points", rs.space.star_spike_points, 4, 8);
      f("star_spike_angle", rs.space.star_spike_angle, -180.f, 180.f);
      f("star_spike_chroma", rs.space.star_spike_chroma, 0.f, 1.f);
      f("star_saturation", rs.space.star_saturation, 0.f, 2.f);
      f("star_glow", rs.space.star_glow, 0.f, 2.f);
      f("star_bright_share", rs.space.star_bright_share, 0.f, 1.f);
      f("star_clusters", rs.space.star_clusters, 0.f, 1.f);
      f("star_cluster_size", rs.space.star_cluster_size, 0.2f, 5.f);
      b("galaxy", rs.space.galaxy_on);
      f("galaxy_intensity", rs.space.galaxy_intensity, 0.f, 10.f);
      f("galaxy_width", rs.space.galaxy_width, 0.5f, 90.f);
      f("galaxy_yaw", rs.space.galaxy_yaw, -360.f, 360.f);
      f("galaxy_pitch", rs.space.galaxy_pitch, -90.f, 90.f);
      f("galaxy_core", rs.space.galaxy_core, -360.f, 360.f);
      f("galaxy_dust", rs.space.galaxy_dust, 0.f, 1.f);
      f("galaxy_grain", rs.space.galaxy_grain, 0.f, 1.f);
      read_vec3(act, "galaxy_color", rs.space.galaxy_color);
      i("galaxy_seed", rs.space.galaxy_seed, 1, 1 << 24);
      ++applied;
    } else if (op == "space_preset") {
      // a whole sky at once (space_presets.hpp)
      const std::string key = act.value("name", act.value("preset", std::string()));
      if (!space_preset_apply(key, err)) return true;
      a.scene_selection_serial++;
      a.status = "space preset " + key;
      ++applied;
    } else if (op == "space_populate") {
      const int n = space_populate(act.value("count", 4), act.value("seed", 1),
                                   act.value("style", std::string("mixed")), err);
      if (n < 0) return true;
      a.scene_selection_serial++;
      a.status = std::to_string(n) + " scattered over the sky";
      applied += n > 0 ? n : 1;
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
      else if (style == "craters" || style == "moon") L.type = 4;
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
