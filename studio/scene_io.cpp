// Geekatplay TerraForge — scene and environment persistence. See scene_io.hpp.
#include "scene_io.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <cstring>

using json = nlohmann::json;

namespace studio {

namespace {

// ------------------------------------------------------------ field table
// One table, both directions. A field listed here round-trips; a field not
// listed here visibly resets on load, which is how an omission gets noticed.
// ViewConfig (per-view camera/shading toggles) is deliberately absent: those
// are how you happened to be looking, not what the project is.
struct EnvField {
  const char *key;
  char kind; // 'f' float, 'i' int, 'b' bool, 'u' node id, 'c' float[3]
  void *p;
};

std::vector<EnvField> env_fields(RenderSettings &rs) {
  return {
      // sun
      {"sun_mode", 'i', &rs.sun_mode},
      {"sun_azimuth", 'f', &rs.sun_azimuth},
      {"sun_altitude", 'f', &rs.sun_altitude},
      {"latitude", 'f', &rs.latitude},
      {"longitude", 'f', &rs.longitude},
      {"utc_offset", 'f', &rs.utc_offset},
      {"month", 'i', &rs.month},
      {"day", 'i', &rs.day},
      {"hour", 'f', &rs.hour},
      {"sun_color", 'c', rs.sun_color},
      {"sun_intensity", 'f', &rs.sun_intensity},
      // atmosphere
      {"atmosphere_density", 'f', &rs.atmosphere_density},
      {"sky_zenith", 'c', rs.sky_zenith},
      {"sky_horizon", 'c', rs.sky_horizon},
      {"ambient_intensity", 'f', &rs.ambient_intensity},
      // fog
      {"fog_type", 'i', &rs.fog_type},
      {"fog_density", 'f', &rs.fog_density},
      {"fog_level", 'f', &rs.fog_level},
      {"fog_falloff", 'f', &rs.fog_falloff},
      {"fog_color", 'c', rs.fog_color},
      {"absorption_color", 'c', rs.absorption_color},
      {"fog_sun_scatter", 'f', &rs.fog_sun_scatter},
      // water
      {"show_water", 'b', &rs.show_water},
      {"water_level", 'f', &rs.water_level},
      {"water_deep_color", 'c', rs.water_deep_color},
      {"water_shallow_color", 'c', rs.water_shallow_color},
      {"water_wave_amp", 'f', &rs.water_wave_amp},
      {"water_wave_scale", 'f', &rs.water_wave_scale},
      {"water_wave_speed", 'f', &rs.water_wave_speed},
      {"water_clarity", 'f', &rs.water_clarity},
      {"water_opacity", 'f', &rs.water_opacity},
      {"water_foam", 'b', &rs.water_foam},
      {"foam_color", 'c', rs.foam_color},
      {"foam_amount", 'f', &rs.foam_amount},
      {"foam_scale", 'f', &rs.foam_scale},
      {"foam_crests", 'f', &rs.foam_crests},
      // material routing
      {"terrain_material_mode", 'i', &rs.terrain_material_mode},
      {"terrain_material_node", 'u', &rs.terrain_material_node},
      // background
      {"background_mode", 'i', &rs.background_mode},
      {"bg_color", 'c', rs.bg_color},
      {"bg_color2", 'c', rs.bg_color2},
      // units & scale
      {"units", 'i', &rs.units},
      {"terrain_size_m", 'f', &rs.terrain_size_m},
      // clouds
      {"clouds_on", 'b', &rs.clouds_on},
      {"cloud_coverage", 'f', &rs.cloud_coverage},
      {"cloud_density", 'f', &rs.cloud_density},
      {"cloud_altitude", 'f', &rs.cloud_altitude},
      {"cloud_thickness", 'f', &rs.cloud_thickness},
      {"cloud_type", 'i', &rs.cloud_type},
      {"cloud_detail", 'f', &rs.cloud_detail},
      {"cloud_wind_speed", 'f', &rs.cloud_wind_speed},
      {"cloud_wind_dir", 'f', &rs.cloud_wind_dir},
      {"cloud_color", 'c', rs.cloud_color},
      {"cloud_ambient", 'f', &rs.cloud_ambient},
      {"cloud_quality", 'i', &rs.cloud_quality},
      {"cloud_anvil", 'f', &rs.cloud_anvil},
      {"cloud_scatter_octaves", 'i', &rs.cloud_scatter_octaves},
      {"cloud_scatter_depth", 'f', &rs.cloud_scatter_depth},
      // terrain surface material
      {"mat_roughness", 'f', &rs.mat_roughness},
      {"mat_metallic", 'f', &rs.mat_metallic},
      {"mat_specular", 'f', &rs.mat_specular},
      {"mat_reflection", 'f', &rs.mat_reflection},
      {"mat_translucency", 'f', &rs.mat_translucency},
      {"mat_transparency", 'f', &rs.mat_transparency},
      {"mat_displacement", 'f', &rs.mat_displacement},
      {"mat_normal_strength", 'f', &rs.mat_normal_strength},
      {"map_normal_node", 'u', &rs.map_normal_node},
      {"map_roughness_node", 'u', &rs.map_roughness_node},
      {"map_displacement_node", 'u', &rs.map_displacement_node},
      // detail & displacement
      {"fractal_detail", 'f', &rs.fractal_detail},
      {"fractal_scale", 'f', &rs.fractal_scale},
      {"field_displacement", 'f', &rs.field_displacement},
      // tessellation & world shape
      {"tessellation", 'b', &rs.tessellation},
      {"tess_pixels", 'f', &rs.tess_pixels},
      {"tess_min", 'f', &rs.tess_min},
      {"tess_max", 'f', &rs.tess_max},
      {"frustum_cull", 'b', &rs.frustum_cull},
      {"planet_radius", 'f', &rs.planet_radius},
      // global
      {"height_scale", 'f', &rs.height_scale},
      {"exposure", 'f', &rs.exposure},
      {"use_albedo", 'b', &rs.use_albedo},
      {"shadows", 'b', &rs.shadows},
      {"shadow_softness", 'f', &rs.shadow_softness},
  };
}

uint64_t remap_id(uint64_t file_id, const GraphIdMap &idmap) {
  if (!file_id) return 0;
  auto it = idmap.find(file_id);
  return it == idmap.end() ? 0 : it->second; // a stale binding unbinds
}

json vec3_to_json(const float *v) { return json::array({v[0], v[1], v[2]}); }
void vec3_from_json(const json &j, float *v) {
  if (j.is_array() && j.size() >= 3)
    for (int k = 0; k < 3; ++k) v[k] = j[k].get<float>();
}

const char *kind_name(SceneObject::Type t) {
  switch (t) {
    case SceneObject::Terrain: return "terrain";
    case SceneObject::Water: return "water";
    case SceneObject::Sun: return "sun";
    case SceneObject::Atmosphere: return "atmosphere";
    case SceneObject::Mesh: return "mesh";
    case SceneObject::Group: return "group";
    case SceneObject::Camera: return "camera";
    case SceneObject::Planet: return "planet";
    case SceneObject::InfiniteSurface: return "surface";
  }
  return "mesh";
}

bool kind_from_name(const std::string &s, SceneObject::Type &t) {
  if (s == "terrain") t = SceneObject::Terrain;
  else if (s == "water") t = SceneObject::Water;
  else if (s == "sun") t = SceneObject::Sun;
  else if (s == "atmosphere") t = SceneObject::Atmosphere;
  else if (s == "mesh") t = SceneObject::Mesh;
  else if (s == "group") t = SceneObject::Group;
  else if (s == "camera") t = SceneObject::Camera;
  else if (s == "planet") t = SceneObject::Planet;
  else if (s == "surface") t = SceneObject::InfiniteSurface;
  else return false;
  return true;
}

} // namespace

// ---------------------------------------------------------------- the scene
json scene_to_json() {
  SceneState &sc = scene();
  json j;

  json layers = json::array();
  for (const SceneLayer &l : sc.layers)
    layers.push_back({{"name", l.name}, {"visible", l.visible}});
  j["layers"] = layers;

  json objs = json::array();
  for (const SceneObject &o : sc.objects) {
    json jo = {
        {"kind", kind_name(o.type)},
        {"name", o.name},
        {"visible", o.visible},
        {"builtin", o.builtin},
        {"layer", o.layer},
        {"parent", o.parent},
        {"expanded", o.expanded},
        {"pos", vec3_to_json(o.pos)},
        {"scale", o.scale},
        {"scl", vec3_to_json(o.scl)},
        {"yaw", o.yaw},
        {"pitch", o.pitch},
        {"roll", o.roll},
        {"color", vec3_to_json(o.color)},
    };
    if (o.type == SceneObject::Mesh) {
      jo["path"] = o.path;
      jo["material_node"] = o.material_node;
      jo["scatter_node"] = o.scatter_node;
      jo["scatter_scale"] = o.scatter_scale;
      jo["scatter_jitter"] = o.scatter_jitter;
      jo["scatter_seed"] = o.scatter_seed;
      jo["scatter_sway"] = o.scatter_sway;
    } else if (o.type == SceneObject::Camera) {
      const CameraData &c = o.cam;
      jo["camera"] = {
          {"eye", json::array({c.eye[0], c.eye[1], c.eye[2]})},
          {"target", json::array({c.target[0], c.target[1], c.target[2]})},
          {"focal_mm", c.focal_mm},
          {"format", c.format},
          {"aperture", c.aperture},
          {"shutter", c.shutter},
          {"iso", c.iso},
          {"film", c.film},
          {"render",
           {{"engine", c.render.engine},
            {"width", c.render.width},
            {"height", c.render.height},
            {"samples", c.render.samples},
            {"output", c.render.output}}},
      };
    } else if (o.type == SceneObject::Planet) {
      const PlanetData &P = o.planet;
      jo["planet"] = {
          {"radius", P.radius},       {"relief", P.relief},
          {"seed", P.seed},           {"sea_level", P.sea_level},
          {"snow_line", P.snow_line}, {"spin_deg", P.spin_deg},
          {"atmo_density", P.atmo_density},
          {"rock_low", vec3_to_json(P.rock_low)},
          {"rock_high", vec3_to_json(P.rock_high)},
          {"water_color", vec3_to_json(P.water_color)},
          {"atmo_color", vec3_to_json(P.atmo_color)},
      };
    } else if (o.type == SceneObject::InfiniteSurface) {
      const gpx::planet::Layer &L = o.surf.layer;
      jo["surface"] = {
          {"seed", L.seed},         {"type", L.type},
          {"frequency", L.frequency}, {"amplitude", L.amplitude},
          {"octaves", L.octaves},   {"coverage", L.coverage},
          {"mask_scale", L.mask_scale},
          {"height_scale", o.surf.height_scale},
      };
    }
    objs.push_back(std::move(jo));
  }
  j["objects"] = objs;
  j["selected"] = sc.selected;
  j["active_camera"] = scene_active_camera();
  j["last_used_camera"] = scene_last_used_camera();
  return j;
}

void scene_from_json(const json &j, const GraphIdMap &idmap,
                     std::string &warnings) {
  SceneState &sc = scene();

  sc.layers.clear();
  for (const json &jl : j.value("layers", json::array()))
    sc.layers.push_back({jl.value("name", std::string("Layer")),
                         jl.value("visible", true)});
  if (sc.layers.empty()) sc.layers.push_back({"Default", true});

  // The array is rebuilt verbatim, in file order, so every parent and layer
  // index in it still means what it meant when it was written.
  sc.objects.clear();
  for (const json &jo : j.value("objects", json::array())) {
    SceneObject o;
    if (!kind_from_name(jo.value("kind", std::string()), o.type)) continue;
    o.name = jo.value("name", std::string("object"));
    o.visible = jo.value("visible", true);
    o.builtin = jo.value("builtin", false);
    o.layer = jo.value("layer", 0);
    o.parent = jo.value("parent", -1);
    o.expanded = jo.value("expanded", true);
    if (jo.contains("pos")) vec3_from_json(jo["pos"], o.pos);
    o.scale = jo.value("scale", o.scale);
    if (jo.contains("scl")) vec3_from_json(jo["scl"], o.scl);
    o.yaw = jo.value("yaw", 0.f);
    o.pitch = jo.value("pitch", 0.f);
    o.roll = jo.value("roll", 0.f);
    if (jo.contains("color")) vec3_from_json(jo["color"], o.color);

    if (o.type == SceneObject::Mesh) {
      o.path = jo.value("path", std::string());
      o.material_node = remap_id(jo.value("material_node", 0ull), idmap);
      o.scatter_node = remap_id(jo.value("scatter_node", 0ull), idmap);
      o.scatter_scale = jo.value("scatter_scale", 1.f);
      o.scatter_jitter = jo.value("scatter_jitter", 0.4f);
      o.scatter_seed = jo.value("scatter_seed", 0u);
      o.scatter_sway = jo.value("scatter_sway", 0.f);
      std::string err;
      if (o.path.empty() || !scene_load_obj_verts(o.path, o.verts, err)) {
        // The geometry is gone but the object is not: its place in the scene,
        // its transform and its material binding are still worth keeping, and
        // an empty mesh draws nothing rather than crashing anything.
        o.verts.clear();
        warnings += "mesh '" + o.name + "': " +
                    (err.empty() ? "no path recorded" : err) + "\n";
      }
      o.vert_count = (int)(o.verts.size() / 6);
      o.gpu_dirty = true;
    } else if (o.type == SceneObject::Camera && jo.contains("camera")) {
      const json &jc = jo["camera"];
      CameraData &c = o.cam;
      if (jc.contains("eye"))
        for (int k = 0; k < 3; ++k) c.eye[k] = jc["eye"][k].get<float>();
      if (jc.contains("target"))
        for (int k = 0; k < 3; ++k) c.target[k] = jc["target"][k].get<float>();
      c.focal_mm = jc.value("focal_mm", c.focal_mm);
      c.format = jc.value("format", c.format);
      c.aperture = jc.value("aperture", c.aperture);
      c.shutter = jc.value("shutter", c.shutter);
      c.iso = jc.value("iso", c.iso);
      c.film = jc.value("film", c.film);
      if (jc.contains("render")) {
        const json &jr = jc["render"];
        c.render.engine = jr.value("engine", c.render.engine);
        c.render.width = jr.value("width", c.render.width);
        c.render.height = jr.value("height", c.render.height);
        c.render.samples = jr.value("samples", c.render.samples);
        c.render.output = jr.value("output", c.render.output);
      }
    } else if (o.type == SceneObject::Planet && jo.contains("planet")) {
      const json &jp = jo["planet"];
      PlanetData &P = o.planet;
      P.radius = jp.value("radius", P.radius);
      P.relief = jp.value("relief", P.relief);
      P.seed = jp.value("seed", P.seed);
      P.sea_level = jp.value("sea_level", P.sea_level);
      P.snow_line = jp.value("snow_line", P.snow_line);
      P.spin_deg = jp.value("spin_deg", P.spin_deg);
      P.atmo_density = jp.value("atmo_density", P.atmo_density);
      if (jp.contains("rock_low")) vec3_from_json(jp["rock_low"], P.rock_low);
      if (jp.contains("rock_high")) vec3_from_json(jp["rock_high"], P.rock_high);
      if (jp.contains("water_color"))
        vec3_from_json(jp["water_color"], P.water_color);
      if (jp.contains("atmo_color"))
        vec3_from_json(jp["atmo_color"], P.atmo_color);
    } else if (o.type == SceneObject::InfiniteSurface && jo.contains("surface")) {
      const json &js = jo["surface"];
      gpx::planet::Layer &L = o.surf.layer;
      L.seed = js.value("seed", L.seed);
      L.type = js.value("type", L.type);
      L.frequency = js.value("frequency", L.frequency);
      L.amplitude = js.value("amplitude", L.amplitude);
      L.octaves = js.value("octaves", L.octaves);
      L.coverage = js.value("coverage", L.coverage);
      L.mask_scale = js.value("mask_scale", L.mask_scale);
      o.surf.height_scale = js.value("height_scale", o.surf.height_scale);
    }
    sc.objects.push_back(std::move(o));
  }

  // If the file somehow carried nothing, fall back to a working scene rather
  // than an empty one nothing can be done with.
  if (sc.objects.empty()) scene_init_builtins();

  auto clamp_idx = [&](int v) {
    return (v >= 0 && v < (int)sc.objects.size()) ? v : -1;
  };
  sc.selected = clamp_idx(j.value("selected", 0));
  if (sc.selected < 0) sc.selected = 0;
  scene_active_camera() = clamp_idx(j.value("active_camera", -1));
  scene_last_used_camera() = clamp_idx(j.value("last_used_camera", -1));
}

// ---------------------------------------------------------- the environment
json environment_to_json() {
  RenderSettings &rs = render_settings();
  json j;
  for (const EnvField &f : env_fields(rs)) {
    switch (f.kind) {
      case 'f': j[f.key] = *(float *)f.p; break;
      case 'i': j[f.key] = *(int *)f.p; break;
      case 'b': j[f.key] = *(bool *)f.p; break;
      case 'u': j[f.key] = *(unsigned long long *)f.p; break;
      case 'c': j[f.key] = vec3_to_json((float *)f.p); break;
    }
  }
  return j;
}

void environment_from_json(const json &j, const GraphIdMap &idmap) {
  RenderSettings &rs = render_settings();
  for (const EnvField &f : env_fields(rs)) {
    if (!j.contains(f.key)) continue; // an older file keeps the default
    try {
      switch (f.kind) {
        case 'f': *(float *)f.p = j[f.key].get<float>(); break;
        case 'i': *(int *)f.p = j[f.key].get<int>(); break;
        case 'b': *(bool *)f.p = j[f.key].get<bool>(); break;
        case 'u':
          *(unsigned long long *)f.p =
              remap_id(j[f.key].get<uint64_t>(), idmap);
          break;
        case 'c': vec3_from_json(j[f.key], (float *)f.p); break;
      }
    } catch (const std::exception &) {
      // one mistyped field keeps its default; the rest of the file still loads
    }
  }
}

} // namespace studio
