// Geekatplay TerraForge — scene and environment persistence. See scene_io.hpp.
#include "scene_io.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <cstring>

using json = nlohmann::json;

namespace studio {

// ------------------------------------------------------------ field table
// One table, both directions. A field listed here round-trips; a field not
// listed here visibly resets on load, which is how an omission gets noticed.
// ViewConfig (per-view camera/shading toggles) is deliberately absent: those
// are how you happened to be looking, not what the project is.
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
      {"sun_size_deg", 'f', &rs.sun_angle_deg},
      {"sun_glow", 'f', &rs.sun_glow},
      {"sun_glow_size", 'f', &rs.sun_glow_size},
      // atmosphere
      {"atmosphere_density", 'f', &rs.atmosphere_density},
      {"atmosphere_height", 'f', &rs.atmosphere_height},
      {"atmosphere_falloff", 'f', &rs.atmosphere_falloff},
      {"space_on", 'b', &rs.space.on},
      {"space_brightness", 'f', &rs.space.brightness},
      {"space_realism", 'f', &rs.space.realism},
      {"space_quality", 'i', &rs.space.quality},
      {"space_glow", 'f', &rs.space.glow},
      {"space_stars", 'b', &rs.space.stars},
      {"star_density", 'f', &rs.space.star_density},
      {"star_brightness", 'f', &rs.space.star_brightness},
      {"star_size", 'f', &rs.space.star_size},
      {"star_temperature", 'f', &rs.space.star_temperature},
      {"star_spikes", 'f', &rs.space.star_spikes},
      {"star_halo", 'f', &rs.space.star_halo},
      {"star_clump", 'f', &rs.space.star_clump},
      {"star_seed", 'i', &rs.space.star_seed},
      {"star_spike_points", 'i', &rs.space.star_spike_points},
      {"star_spike_angle", 'f', &rs.space.star_spike_angle},
      {"star_spike_chroma", 'f', &rs.space.star_spike_chroma},
      {"star_saturation", 'f', &rs.space.star_saturation},
      {"star_glow", 'f', &rs.space.star_glow},
      {"star_bright_share", 'f', &rs.space.star_bright_share},
      {"star_clusters", 'f', &rs.space.star_clusters},
      {"star_cluster_size", 'f', &rs.space.star_cluster_size},
      {"galaxy_on", 'b', &rs.space.galaxy_on},
      {"galaxy_intensity", 'f', &rs.space.galaxy_intensity},
      {"galaxy_width", 'f', &rs.space.galaxy_width},
      {"galaxy_yaw", 'f', &rs.space.galaxy_yaw},
      {"galaxy_pitch", 'f', &rs.space.galaxy_pitch},
      {"galaxy_core", 'f', &rs.space.galaxy_core},
      {"galaxy_dust", 'f', &rs.space.galaxy_dust},
      {"galaxy_grain", 'f', &rs.space.galaxy_grain},
      {"galaxy_color", 'c', rs.space.galaxy_color},
      {"galaxy_seed", 'i', &rs.space.galaxy_seed},
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
      {"fog_albedo", 'f', &rs.fog_albedo},
      {"fog_anisotropy", 'f', &rs.fog_anisotropy},
      {"fog_heterogeneity", 'f', &rs.fog_heterogeneity},
      {"fog_steps", 'i', &rs.fog_steps},
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
      {"water_displaced", 'b', &rs.water_displaced},
      // the scene's wind, which the clouds, the sea, the fog and the plants
      // all read (render_settings.hpp). The drift is where the air has got to
      // and is worked out again from the clock, so it is not saved.
      {"wind_speed_ms", 'f', &rs.wind.speed_ms},
      {"wind_direction_deg", 'f', &rs.wind.direction_deg},
      {"wind_gust_strength", 'f', &rs.wind.gust_strength},
      {"wind_gust_frequency", 'f', &rs.wind.gust_frequency},
      {"wind_gust_size_m", 'f', &rs.wind.gust_size_m},
      {"wind_turbulence_deg", 'f', &rs.wind.turbulence_deg},
      {"wind_shear", 'f', &rs.wind.shear},
      {"cloud_wind_follow", 'b', &rs.cloud_wind_follow},
      {"water_wind_follow", 'b', &rs.water_wind_follow},
      {"water_wind_speed", 'f', &rs.water_wind_speed},
      {"water_wind_dir", 'f', &rs.water_wind_dir},
      {"water_choppiness", 'f', &rs.water_choppiness},
      {"water_foam", 'b', &rs.water_foam},
      {"foam_color", 'c', rs.foam_color},
      {"foam_amount", 'f', &rs.foam_amount},
      {"foam_scale", 'f', &rs.foam_scale},
      {"foam_crests", 'f', &rs.foam_crests},
      {"foam_depth_m", 'f', &rs.foam_depth_m},
      {"foam_coverage", 'f', &rs.foam_coverage},
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
      {"cloud2_on", 'b', &rs.cloud2_on},
      {"cloud2_type", 'i', &rs.cloud2_type},
      {"cloud2_coverage", 'f', &rs.cloud2_coverage},
      {"cloud2_density", 'f', &rs.cloud2_density},
      {"cloud2_altitude", 'f', &rs.cloud2_altitude},
      {"cloud2_thickness", 'f', &rs.cloud2_thickness},
      {"id_mode", 'i', &rs.id_mode},
      {"cloud_thickness", 'f', &rs.cloud_thickness},
      {"cloud_type", 'i', &rs.cloud_type},
      {"cloud_detail", 'f', &rs.cloud_detail},
      {"cloud_wind_speed", 'f', &rs.cloud_wind_speed},
      {"cloud_wind_dir", 'f', &rs.cloud_wind_dir},
      {"cloud_color", 'c', rs.cloud_color},
      {"cloud_ambient", 'f', &rs.cloud_ambient},
      {"cloud_quality", 'i', &rs.cloud_quality},
      {"cloud_volumetric", 'b', &rs.cloud_volumetric},
      {"cloud_weather", 'f', &rs.cloud_weather},
      {"cloud_weather_scale", 'f', &rs.cloud_weather_scale},
      {"cloud_scale", 'f', &rs.cloud_scale},
      {"cloud_anvil", 'f', &rs.cloud_anvil},
      {"cloud_scatter_octaves", 'i', &rs.cloud_scatter_octaves},
      {"cloud_scatter_depth", 'f', &rs.cloud_scatter_depth},
      // level of detail
      {"scatter_lod_full_m", 'f', &rs.scatter_lod_full_m},
      {"scatter_lod_far_m", 'f', &rs.scatter_lod_far_m},
      {"scatter_lod_cull_m", 'f', &rs.scatter_lod_cull_m},
      {"scatter_lod_billboard_m", 'f', &rs.scatter_lod_billboard_m},
      {"scatter_lod_min_keep", 'f', &rs.scatter_lod_min_keep},
      {"terrain_lod", 'f', &rs.terrain_lod},
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
      {"fractal_gain", 'f', &rs.fractal_gain},
      {"field_displacement", 'f', &rs.field_displacement},
      // tessellation & world shape
      {"tessellation", 'b', &rs.tessellation},
      {"tess_pixels", 'f', &rs.tess_pixels},
      {"tess_min", 'f', &rs.tess_min},
      {"tess_max", 'f', &rs.tess_max},
      {"frustum_cull", 'b', &rs.frustum_cull},
      {"planet_radius", 'f', &rs.planet_radius},
      {"world_shape", 'i', &rs.world_shape},
      {"world_inside", 'b', &rs.world_inside},
      {"world_width", 'f', &rs.world_width},
      {"world_sun_inside", 'b', &rs.world_sun_inside},
      {"world_thickness", 'f', &rs.world_thickness},
      {"world_outline", 'i', &rs.world_outline},
      {"place_on_planet", 'b', &rs.place_on_planet},
      {"place_edge", 'f', &rs.place_edge},
      {"place_flatten", 'f', &rs.place_flatten},
      {"place_presence", 'f', &rs.place_presence},
      {"place_ground", 'f', &rs.place_ground},
      {"place_gradient", 'f', &rs.place_gradient},
      {"place_round", 'f', &rs.place_round},
      {"place_wander", 'f', &rs.place_wander},
      {"place_mode", 'i', &rs.place_mode},
      {"terrain_shape", 'i', &rs.terrain_shape},
      {"terrain_aspect", 'f', &rs.terrain_aspect},
      // global
      {"height_scale", 'f', &rs.height_scale},
      {"exposure", 'f', &rs.exposure},
      {"use_albedo", 'b', &rs.use_albedo},
      {"shadows", 'b', &rs.shadows},
      {"shadow_softness", 'f', &rs.shadow_softness},
      // render editor: backdrop dome
      {"backdrop_enabled", 'b', &rs.backdrop.enabled},
      {"backdrop_file", 's', &rs.backdrop.file},
      {"backdrop_mapping", 'i', &rs.backdrop.mapping},
      {"backdrop_vfov", 'f', &rs.backdrop.vfov},
      {"backdrop_flip", 'b', &rs.backdrop.flip},
      {"backdrop_yaw", 'f', &rs.backdrop.yaw},
      {"backdrop_pitch", 'f', &rs.backdrop.pitch},
      {"backdrop_exposure_ev", 'f', &rs.backdrop.exposure_ev},
      {"backdrop_tint", 'c', rs.backdrop.tint},
      {"backdrop_blend", 'f', &rs.backdrop.blend},
      {"backdrop_haze", 'f', &rs.backdrop.haze},
      {"backdrop_hide_sun", 'b', &rs.backdrop.hide_sun},
      // render editor: passes, output, post
      {"pass_mask", 'i', &rs.pass_mask},
      {"render_format", 'i', &rs.render_format},
      {"render_engine", 'i', &rs.render_engine},
      {"render_width", 'i', &rs.render_width},
      {"render_height", 'i', &rs.render_height},
      {"render_samples", 'i', &rs.render_samples},
      {"render_path", 's', &rs.render_path},
      {"post_exposure", 'f', &rs.post_exposure},
      {"post_saturation", 'f', &rs.post_saturation},
      {"post_tint", 'c', rs.post_tint},
      {"post_vignette", 'f', &rs.post_vignette},
  };
}

namespace {

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
    case SceneObject::Light: return "light";
    case SceneObject::Nebula: return "nebula";
  }
  return "mesh";
}

bool kind_from_name(const std::string &s, SceneObject::Type &t) {
  if (s == "light") { t = SceneObject::Light; return true; }
  if (s == "nebula") { t = SceneObject::Nebula; return true; }
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

  j["layers"] = layers_to_json(sc); // scene_io_object.cpp

  json objs = json::array();
  for (const SceneObject &o : sc.objects) {
    json jo = {
        {"kind", kind_name(o.type)},
        {"name", o.name},
        {"locked", o.locked},
        {"builtin", o.builtin},
        {"layer", o.layer},
        {"parent", o.parent},
        {"expanded", o.expanded},
    };
    if (o.side) jo["side"] = o.side; // world_shape.hpp: 1 outside, 2 inside
    object_visibility_to_json(jo, o); // scene_io_object.cpp
    object_transform_to_json(jo, o);
    if (o.driver_node) jo["driver_node"] = o.driver_node;
    // Any object may wear a material, not only a mesh. This used to be
    // written inside the Mesh branch below, so assigning a material to the
    // terrain - which is what the Materials panel's Assign button does by
    // default - survived until the project was saved and then quietly did
    // not. Written for every type now, and only when there is one, so a
    // scene of objects that have no material is not padded with zeroes.
    if (o.material_node) jo["material_node"] = o.material_node;
    if (o.type == SceneObject::Light) {
      jo["light_intensity"] = o.light_intensity;
      jo["light_radius"] = o.light_radius;
      jo["light_type"] = o.light_type;
      jo["light_cone"] = o.light_cone;
    }
    if (o.type == SceneObject::Mesh) {
      jo["path"] = o.path;
      jo["material_node"] = o.material_node;
      jo["scatter_node"] = o.scatter_node;
      jo["scatter_scale"] = o.scatter_scale;
      jo["scatter_jitter"] = o.scatter_jitter;
      jo["scatter_seed"] = o.scatter_seed;
      jo["scatter_sway"] = o.scatter_sway;
      jo["scatter_value_size"] = o.scatter_value_size;
      jo["scatter_species"] = o.scatter_species;
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
          {"optics", c.optics},
          {"distortion_auto", c.distortion_auto},
          {"distortion", c.distortion},
          {"vignette", c.vignette},
          {"chromatic", c.chromatic},
          {"flare", c.flare},
          {"flare_strength", c.flare_strength},
          {"flare_style", c.flare_style},
          {"flare_rays", c.flare_rays},
          {"flare_streak", c.flare_streak},
          {"flare_ghosts", c.flare_ghosts},
          {"flare_halo", c.flare_halo},
          {"flare_core", c.flare_core},
          {"flare_ray_count", c.flare_ray_count},
          {"flare_ray_length", c.flare_ray_length},
          {"flare_streak_length", c.flare_streak_length},
          {"flare_streak_tint", vec3_to_json(c.flare_streak_tint)},
          {"flare_halo_radius", c.flare_halo_radius},
          {"flare_ghost_count", c.flare_ghost_count},
          {"flare_blades", c.flare_blades},
          {"flare_chroma", c.flare_chroma},
          {"flare_seed", c.flare_seed},
          {"bloom", c.bloom},
          {"bloom_threshold", c.bloom_threshold},
          {"bloom_size", c.bloom_size},
          {"motion_blur", c.motion_blur},
          {"render",
           {{"engine", c.render.engine},
            {"width", c.render.width},
            {"height", c.render.height},
            {"samples", c.render.samples},
            {"output", c.render.output},
            {"passes", c.render.passes},
            {"panorama", c.render.panorama},
            {"preset", c.render.preset}}},
      };
      // camera animation tracks, only when they hold keys
      {
        json anim;
        const char *axes[3] = {"x", "y", "z"};
        for (int k = 0; k < 3; ++k) {
          if (!c.anim_eye[k].empty())
            anim[std::string("eye_") + axes[k]] =
                gpx::track_to_string(c.anim_eye[k]);
          if (!c.anim_target[k].empty())
            anim[std::string("target_") + axes[k]] =
                gpx::track_to_string(c.anim_target[k]);
        }
        if (!anim.empty()) jo["camera"]["anim"] = std::move(anim);
      }
    } else if (o.type == SceneObject::Planet) {
      const PlanetData &P = o.planet;
      jo["planet"] = {
          {"radius", P.radius},       {"relief", P.relief},
          {"seed", P.seed},           {"sea_level", P.sea_level},
          {"snow_line", P.snow_line}, {"spin_deg", P.spin_deg},
          {"atmo_density", P.atmo_density},
          {"clouds", P.clouds},
          {"home", P.home},
          {"surface_node", P.surface_node},
          {"rock_low", vec3_to_json(P.rock_low)},
          {"rock_high", vec3_to_json(P.rock_high)},
          {"water_color", vec3_to_json(P.water_color)},
          {"atmo_color", vec3_to_json(P.atmo_color)},
      };
    } else if (o.type == SceneObject::Nebula) {
      const NebulaData &N = o.nebula;
      jo["nebula"] = {
          {"type", N.type},           {"azimuth", N.azimuth},
          {"elevation", N.elevation}, {"size_deg", N.size_deg},
          {"tilt_deg", N.tilt_deg},   {"rotation_deg", N.rotation_deg},
          {"seed", N.seed},           {"brightness", N.brightness},
          {"density", N.density},     {"detail", N.detail},
          {"arms", N.arms},           {"dust", N.dust},
          {"warp", N.warp},           {"glow", N.glow},
          {"sources", N.sources},
          {"turbulence", N.turbulence}, {"lanes", N.lanes},
          {"core_glow", N.core_glow}, {"source_stars", N.source_stars},
          {"color1", vec3_to_json(N.color1)},
          {"color2", vec3_to_json(N.color2)},
      };
      if (N.color3[0] >= 0.f) jo["nebula"]["color3"] = vec3_to_json(N.color3);
    } else if (o.type == SceneObject::InfiniteSurface) {
      const gpx::planet::Layer &L = o.surf.layer;
      jo["surface"] = {
          {"seed", L.seed},         {"type", L.type},
          {"frequency", L.frequency}, {"amplitude", L.amplitude},
          {"octaves", L.octaves},   {"coverage", L.coverage},
          {"mask_scale", L.mask_scale},
          {"height_scale", o.surf.height_scale},
          {"surface_node", o.surf.surface_node},
      };
    }
    objs.push_back(std::move(jo));
  }
  j["objects"] = objs;
  scene_anim_to_json(j, sc);
  j["selected"] = sc.selected;
  {
    json presets = json::array();
    for (const RenderPreset &p : sc.render_presets)
      presets.push_back({{"name", p.name},         {"engine", p.assign.engine},
                         {"width", p.assign.width}, {"height", p.assign.height},
                         {"samples", p.assign.samples}, {"output", p.assign.output},
                         {"passes", p.assign.passes}, {"panorama", p.assign.panorama}});
    j["render_presets"] = std::move(presets);
  }
  j["active_camera"] = scene_active_camera();
  j["last_used_camera"] = scene_last_used_camera();
  return j;
}

void scene_from_json(const json &j, const GraphIdMap &idmap,
                     std::string &warnings) {
  SceneState &sc = scene();

  layers_from_json(j.value("layers", json::array()), sc); // scene_io_object.cpp

  // The array is rebuilt verbatim, in file order, so every parent and layer
  // index in it still means what it meant when it was written.
  sc.objects.clear();
  scene_anim_from_json(j, sc);
  sc.render_presets.clear();
  for (const auto &jp : j.value("render_presets", json::array())) {
    if (!jp.is_object()) continue;
    RenderPreset p;
    p.name = jp.value("name", std::string());
    if (p.name.empty()) continue;
    p.assign.engine = jp.value("engine", p.assign.engine);
    p.assign.width = jp.value("width", p.assign.width);
    p.assign.height = jp.value("height", p.assign.height);
    p.assign.samples = jp.value("samples", p.assign.samples);
    p.assign.output = jp.value("output", p.assign.output);
    p.assign.passes = jp.value("passes", p.assign.passes);
    p.assign.panorama = jp.value("panorama", p.assign.panorama);
    p.assign.preset = p.name;
    sc.render_presets.push_back(p);
  }
  for (const json &jo : j.value("objects", json::array())) {
    SceneObject o;
    if (!kind_from_name(jo.value("kind", std::string()), o.type)) continue;
    o.name = jo.value("name", std::string("object"));
    object_visibility_from_json(jo, o); // scene_io_object.cpp
    o.locked = jo.value("locked", false);
    o.builtin = jo.value("builtin", false);
    o.layer = jo.value("layer", 0);
    o.parent = jo.value("parent", -1);
    o.side = jo.value("side", 0);
    o.expanded = jo.value("expanded", true);
    object_transform_from_json(jo, o); // scene_io_object.cpp
    o.driver_node = remap_id(jo.value("driver_node", 0ull), idmap);
    o.material_node = remap_id(jo.value("material_node", 0ull), idmap);

    if (o.type == SceneObject::Light) {
      o.light_intensity = jo.value("light_intensity", 1.f);
      o.light_radius = jo.value("light_radius", 0.35f);
      o.light_type = jo.value("light_type", 0);
      o.light_cone = jo.value("light_cone", 40.f);
    }
    if (o.type == SceneObject::Mesh) {
      o.path = jo.value("path", std::string());
      o.scatter_node = remap_id(jo.value("scatter_node", 0ull), idmap);
      o.scatter_scale = jo.value("scatter_scale", 1.f);
      o.scatter_jitter = jo.value("scatter_jitter", 0.4f);
      o.scatter_seed = jo.value("scatter_seed", 0u);
      o.scatter_sway = jo.value("scatter_sway", 0.f);
      o.scatter_value_size = jo.value("scatter_value_size", 0.f);
      o.scatter_species = jo.value("scatter_species", -1);
      std::string err;
      if (o.path.rfind("primitive:", 0) == 0 &&
          scene_primitive_build(o.path.substr(10), o.primitive_detail, o)) {
        // a built-in primitive regenerates from its kind, no file needed
      } else if (o.path.empty() || !scene_load_mesh(o.path, o, err)) {
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
      c.optics = jc.value("optics", c.optics);
      c.distortion_auto = jc.value("distortion_auto", c.distortion_auto);
      c.distortion = jc.value("distortion", c.distortion);
      c.vignette = jc.value("vignette", c.vignette);
      c.chromatic = jc.value("chromatic", c.chromatic);
      c.flare = jc.value("flare", c.flare);
      c.flare_strength = jc.value("flare_strength", c.flare_strength);
      c.flare_style = std::clamp(jc.value("flare_style", c.flare_style), 0, 2);
      c.flare_rays = jc.value("flare_rays", c.flare_rays);
      c.flare_streak = jc.value("flare_streak", c.flare_streak);
      c.flare_ghosts = jc.value("flare_ghosts", c.flare_ghosts);
      c.flare_halo = jc.value("flare_halo", c.flare_halo);
      c.flare_core = jc.value("flare_core", c.flare_core);
      c.flare_ray_count = std::clamp(jc.value("flare_ray_count", c.flare_ray_count), 4, 64);
      c.flare_ray_length = jc.value("flare_ray_length", c.flare_ray_length);
      c.flare_streak_length = jc.value("flare_streak_length", c.flare_streak_length);
      if (jc.contains("flare_streak_tint")) vec3_from_json(jc["flare_streak_tint"], c.flare_streak_tint);
      c.flare_halo_radius = jc.value("flare_halo_radius", c.flare_halo_radius);
      c.flare_ghost_count = std::clamp(jc.value("flare_ghost_count", c.flare_ghost_count), 0, 12);
      c.flare_blades = std::clamp(jc.value("flare_blades", c.flare_blades), 5, 9);
      c.flare_chroma = jc.value("flare_chroma", c.flare_chroma);
      c.flare_seed = jc.value("flare_seed", c.flare_seed);
      c.bloom = std::clamp(jc.value("bloom", c.bloom), 0.f, 4.f);
      c.bloom_threshold = std::clamp(jc.value("bloom_threshold", c.bloom_threshold), 0.f, 0.99f);
      c.bloom_size = std::clamp(jc.value("bloom_size", c.bloom_size), 0.f, 1.f);
      c.motion_blur = jc.value("motion_blur", c.motion_blur);
      if (jc.contains("render")) {
        const json &jr = jc["render"];
        c.render.engine = jr.value("engine", c.render.engine);
        c.render.width = jr.value("width", c.render.width);
        c.render.height = jr.value("height", c.render.height);
        c.render.samples = jr.value("samples", c.render.samples);
        c.render.output = jr.value("output", c.render.output);
        c.render.passes = jr.value("passes", c.render.passes);
        c.render.panorama = jr.value("panorama", c.render.panorama);
        c.render.preset = jr.value("preset", c.render.preset);
      }
      if (jc.contains("anim")) {
        const json &ja = jc["anim"];
        const char *axes[3] = {"x", "y", "z"};
        for (int k = 0; k < 3; ++k) {
          std::string ek = std::string("eye_") + axes[k];
          std::string tk = std::string("target_") + axes[k];
          if (ja.contains(ek))
            gpx::track_from_string(c.anim_eye[k], ja[ek].get<std::string>());
          if (ja.contains(tk))
            gpx::track_from_string(c.anim_target[k], ja[tk].get<std::string>());
        }
      }
    } else if (o.type == SceneObject::Planet && jo.contains("planet")) {
      const json &jp = jo["planet"];
      PlanetData &P = o.planet;
      P.radius = jp.value("radius", P.radius);
      P.home = jp.value("home", P.home);
      P.relief = jp.value("relief", P.relief);
      P.seed = jp.value("seed", P.seed);
      P.sea_level = jp.value("sea_level", P.sea_level);
      P.snow_line = jp.value("snow_line", P.snow_line);
      P.spin_deg = jp.value("spin_deg", P.spin_deg);
      P.atmo_density = jp.value("atmo_density", P.atmo_density);
      P.clouds = std::clamp(jp.value("clouds", P.clouds), 0.f, 1.f);
      P.surface_node = remap_id(jp.value("surface_node", 0ull), idmap);
      if (jp.contains("rock_low")) vec3_from_json(jp["rock_low"], P.rock_low);
      if (jp.contains("rock_high")) vec3_from_json(jp["rock_high"], P.rock_high);
      if (jp.contains("water_color"))
        vec3_from_json(jp["water_color"], P.water_color);
      if (jp.contains("atmo_color"))
        vec3_from_json(jp["atmo_color"], P.atmo_color);
    } else if (o.type == SceneObject::Nebula && jo.contains("nebula")) {
      const json &jn = jo["nebula"];
      NebulaData &N = o.nebula;
      N.type = jn.value("type", N.type);
      N.azimuth = jn.value("azimuth", N.azimuth);
      N.elevation = jn.value("elevation", N.elevation);
      N.size_deg = jn.value("size_deg", N.size_deg);
      N.tilt_deg = jn.value("tilt_deg", N.tilt_deg);
      N.rotation_deg = jn.value("rotation_deg", N.rotation_deg);
      N.seed = jn.value("seed", N.seed);
      N.brightness = jn.value("brightness", N.brightness);
      N.density = jn.value("density", N.density);
      N.detail = jn.value("detail", N.detail);
      N.arms = jn.value("arms", N.arms);
      N.dust = jn.value("dust", N.dust);
      N.warp = jn.value("warp", N.warp);
      N.glow = jn.value("glow", N.glow);
      N.sources = jn.value("sources", N.sources);
      N.turbulence = jn.value("turbulence", N.turbulence);
      N.lanes = jn.value("lanes", N.lanes);
      N.core_glow = jn.value("core_glow", N.core_glow);
      N.source_stars = jn.value("source_stars", N.source_stars);
      if (jn.contains("color1")) vec3_from_json(jn["color1"], N.color1);
      if (jn.contains("color2")) vec3_from_json(jn["color2"], N.color2);
      if (jn.contains("color3")) vec3_from_json(jn["color3"], N.color3);
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
      o.surf.surface_node = remap_id(js.value("surface_node", 0ull), idmap);
    }
    sc.objects.push_back(std::move(o));
  }

  // If the file somehow carried nothing, fall back to a working scene rather
  // than an empty one nothing can be done with.
  if (sc.objects.empty()) scene_init_builtins();
  scene_ensure_home_planet(); // an older project: the world gets its planet

  auto clamp_idx = [&](int v) {
    return (v >= 0 && v < (int)sc.objects.size()) ? v : -1;
  };
  sc.selected = clamp_idx(j.value("selected", 0));
  if (sc.selected < 0) sc.selected = 0;
  sc.selection.clear();
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
      case 's': j[f.key] = *(std::string *)f.p; break;
    }
  }
  return j;
}

void scene_remap_node_ids(SceneState &sc, RenderSettings &rs, const GraphIdMap &idmap) {
  for (SceneObject &o : sc.objects) {
    o.driver_node = remap_id(o.driver_node, idmap);
    o.material_node = remap_id(o.material_node, idmap);
    o.scatter_node = remap_id(o.scatter_node, idmap);
    o.planet.surface_node = remap_id(o.planet.surface_node, idmap);
    o.surf.surface_node = remap_id(o.surf.surface_node, idmap);
  }
  for (const EnvField &f : env_fields(rs))
    if (f.kind == 'u') {
      auto *id = (unsigned long long *)f.p;
      *id = remap_id(*id, idmap);
    }
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
        case 's': *(std::string *)f.p = j[f.key].get<std::string>(); break;
      }
    } catch (const std::exception &) {
      // one mistyped field keeps its default; the rest of the file still loads
    }
  }
}

} // namespace studio
