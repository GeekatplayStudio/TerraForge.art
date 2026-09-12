// Geekatplay TerraForge — applies Atmosphere/Render nodes to the renderer.
// Terragen-style: if the graph contains SunLight / AtmosphereSettings /
// CloudLayer / WaterLayer / RenderCamera / RenderQuality nodes, they drive
// the scene (and are saved with the project). Without them the Properties
// panels remain the source of truth.
#include "app.hpp"
#include "render_settings.hpp"
#include <string>

namespace studio {

void apply_object_nodes(App &a); // scene_nodes_objects.cpp

bool scene_nodes_present(App &a, const char *type) {
  for (auto &n : a.graph.nodes)
    if (n->type == type) return true;
  return false;
}

void apply_scene_nodes(App &a) {
  RenderSettings &rs = render_settings();
  // CloudLayer nodes: the first drives the main cloud settings as it always
  // did; every one after it is a further layer of its own, so a sky is built
  // by adding nodes - low stratus, cumulus, a cirrus veil - with no limit but
  // the eight the sky pass marches.
  rs.cloud_layers.clear();
  // A sky has the clouds someone put in it and no others. Without this the
  // default scene came up overcast with nothing in the tree to account for
  // it, and no way to be rid of it but to find a checkbox.
  bool any_cloud_layer = false;
  // And the picture that says where the cloud is, from whichever layer names
  // one. It shapes the cover every layer reads, so it is a property of the
  // sky rather than of one deck - taking it from the first layer that has
  // one means it works wherever it was set, instead of silently only on the
  // deck that happens to drive the main settings.
  rs.cloud_shape_map.clear();
  rs.cloud_shape_amount = 1.f;
  rs.cloud_shape_size = 4.f;
  rs.cloud_shape_tiled = false;
  rs.cloud_shape_center[0] = 0.5f;
  rs.cloud_shape_center[1] = 0.5f;
  for (const auto &np : a.graph.nodes) {
    if (np->type != "CloudLayer" || !np->attrs.get_b("enabled", true)) continue;
    any_cloud_layer = true;
    const std::string map = np->attrs.get_s("shape_map");
    if (map.empty() || !rs.cloud_shape_map.empty()) continue;
    rs.cloud_shape_map = map;
    rs.cloud_shape_amount = np->attrs.get_f("shape_amount", 1.f);
    // the attribute is in kilometres, the shader works in world units
    rs.cloud_shape_size = np->attrs.get_f("shape_size_km", 40.f) * 1000.f /
                          (rs.terrain_size_m > 1.f ? rs.terrain_size_m : 1.f);
    rs.cloud_shape_tiled = np->attrs.get_b("shape_tiled", false);
    const float to_world = 1000.f / (rs.terrain_size_m > 1.f ? rs.terrain_size_m : 1.f);
    rs.cloud_shape_center[0] = 0.5f + np->attrs.get_f("shape_x_km", 0.f) * to_world;
    rs.cloud_shape_center[1] = 0.5f + np->attrs.get_f("shape_z_km", 0.f) * to_world;
  }
  rs.clouds_on = any_cloud_layer;
  // FogLayer nodes: every one is a band of air, and the sky and the surfaces
  // see all of them at once - haze to the horizon under a valley fog under a
  // brown layer over a town. The AtmosphereSettings node's own fog is still
  // the first layer, so a scene with no FogLayer node behaves as it always
  // did.
  rs.fog_layers.clear();
  bool first_cloud = true;
  for (auto &np : a.graph.nodes) {
    gpx::Node &n = *np;
    const gpx::AttrSet &at = n.attrs;
    if (n.type == "CloudLayer" && !first_cloud) {
      if (!at.get_b("enabled", true)) continue;
      if ((int)rs.cloud_layers.size() >= RenderSettings::MAX_CLOUD_LAYERS) continue;
      RenderSettings::CloudLayerSettings L;
      L.type = at.get_choice("type");
      L.coverage = at.get_f("coverage", 0.5f);
      L.density = at.get_f("density", 1.f);
      L.altitude = at.get_f("altitude", 1.4f);
      L.thickness = at.get_f("thickness", 0.8f);
      rs.cloud_layers.push_back(L);
      continue;
    }
    if (n.type == "FogLayer") {
      if (!at.get_b("enabled", true)) continue;
      if ((int)rs.fog_layers.size() >= RenderSettings::MAX_FOG_LAYERS) continue;
      RenderSettings::FogLayerSettings L;
      L.type = at.get_choice("type");
      L.density = at.get_f("density", 0.6f);
      L.level = at.get_f("level", 0.2f);
      L.falloff = at.get_f("falloff", 6.f);
      L.color[0] = at.get_f("color_r", 0.62f);
      L.color[1] = at.get_f("color_g", 0.68f);
      L.color[2] = at.get_f("color_b", 0.76f);
      L.sun_scatter = at.get_f("sun_scatter", 0.5f);
      L.albedo = at.get_f("albedo", 0.85f);
      L.anisotropy = at.get_f("anisotropy", 0.55f);
      L.drift = at.get_f("drift", 0.6f);
      rs.fog_layers.push_back(L);
      continue;
    }
    if (n.type == "CloudLayer") first_cloud = false;
    if (n.type == "SunLight") {
      rs.sun_mode = at.get_choice("mode");
      rs.sun_azimuth = at.get_f("azimuth", rs.sun_azimuth);
      rs.sun_altitude = at.get_f("altitude", rs.sun_altitude);
      rs.latitude = at.get_f("latitude", rs.latitude);
      rs.longitude = at.get_f("longitude", rs.longitude);
      rs.utc_offset = at.get_f("utc_offset", rs.utc_offset);
      rs.month = at.get_i("month", rs.month);
      rs.day = at.get_i("day", rs.day);
      rs.hour = at.get_f("hour", rs.hour);
      rs.sun_intensity = at.get_f("intensity", rs.sun_intensity);
      rs.sun_color[0] = at.get_f("color_r", rs.sun_color[0]);
      rs.sun_color[1] = at.get_f("color_g", rs.sun_color[1]);
      rs.sun_color[2] = at.get_f("color_b", rs.sun_color[2]);
      rs.shadows = at.get_b("shadows", rs.shadows);
    } else if (n.type == "AtmosphereSettings") {
      rs.atmosphere_density = at.get_f("density", rs.atmosphere_density);
      if (at.find("height_km"))
        rs.atmosphere_height = at.get_f("height_km", 100.f) * 1000.f /
                               (rs.terrain_size_m > 1.f ? rs.terrain_size_m : 1.f);
      rs.ambient_intensity = at.get_f("ambient", rs.ambient_intensity);
      rs.sky_zenith[0] = at.get_f("zenith_r", rs.sky_zenith[0]);
      rs.sky_zenith[1] = at.get_f("zenith_g", rs.sky_zenith[1]);
      rs.sky_zenith[2] = at.get_f("zenith_b", rs.sky_zenith[2]);
      rs.sky_horizon[0] = at.get_f("horizon_r", rs.sky_horizon[0]);
      rs.sky_horizon[1] = at.get_f("horizon_g", rs.sky_horizon[1]);
      rs.sky_horizon[2] = at.get_f("horizon_b", rs.sky_horizon[2]);
      rs.fog_type = at.get_choice("fog_type");
      rs.fog_density = at.get_f("fog_density", rs.fog_density);
      rs.fog_level = at.get_f("fog_level", rs.fog_level);
      rs.fog_falloff = at.get_f("fog_falloff", rs.fog_falloff);
      rs.fog_color[0] = at.get_f("fog_r", rs.fog_color[0]);
      rs.fog_color[1] = at.get_f("fog_g", rs.fog_color[1]);
      rs.fog_color[2] = at.get_f("fog_b", rs.fog_color[2]);
      rs.fog_sun_scatter = at.get_f("fog_scatter", rs.fog_sun_scatter);
    } else if (n.type == "CloudLayer") {
      rs.clouds_on = at.get_b("enabled", rs.clouds_on);
      rs.cloud_type = at.get_choice("type");
      rs.cloud_coverage = at.get_f("coverage", rs.cloud_coverage);
      rs.cloud_density = at.get_f("density", rs.cloud_density);
      rs.cloud_altitude = at.get_f("altitude", rs.cloud_altitude);
      rs.cloud_thickness = at.get_f("thickness", rs.cloud_thickness);
      rs.cloud_detail = at.get_f("detail", rs.cloud_detail);
      rs.cloud_anvil = at.get_f("anvil", rs.cloud_anvil);
      rs.cloud_wind_speed = at.get_f("wind_speed", rs.cloud_wind_speed);
      rs.cloud_wind_dir = at.get_f("wind_dir", rs.cloud_wind_dir);
      rs.cloud_ambient = at.get_f("ambient", rs.cloud_ambient);
      rs.cloud_color[0] = at.get_f("color_r", rs.cloud_color[0]);
      rs.cloud_color[1] = at.get_f("color_g", rs.cloud_color[1]);
      rs.cloud_color[2] = at.get_f("color_b", rs.cloud_color[2]);
      rs.cloud_quality = at.get_choice("quality");
    } else if (n.type == "WaterLayer") {
      rs.show_water = at.get_b("enabled", rs.show_water);
      rs.water_level = at.get_f("level", rs.water_level);
      rs.water_clarity = at.get_f("clarity", rs.water_clarity);
      rs.water_opacity = at.get_f("opacity", rs.water_opacity);
      rs.water_deep_color[0] = at.get_f("deep_r", rs.water_deep_color[0]);
      rs.water_deep_color[1] = at.get_f("deep_g", rs.water_deep_color[1]);
      rs.water_deep_color[2] = at.get_f("deep_b", rs.water_deep_color[2]);
      rs.water_shallow_color[0] = at.get_f("shallow_r", rs.water_shallow_color[0]);
      rs.water_shallow_color[1] = at.get_f("shallow_g", rs.water_shallow_color[1]);
      rs.water_shallow_color[2] = at.get_f("shallow_b", rs.water_shallow_color[2]);
      rs.water_wave_amp = at.get_f("wave_amp", rs.water_wave_amp);
      rs.water_wave_scale = at.get_f("wave_scale", rs.water_wave_scale);
      rs.water_wave_speed = at.get_f("wave_speed", rs.water_wave_speed);
      rs.water_foam = at.get_b("foam", rs.water_foam);
      rs.foam_amount = at.get_f("foam_amount", rs.foam_amount);
      rs.foam_crests = at.get_f("foam_crests", rs.foam_crests);
      rs.foam_scale = at.get_f("foam_scale", rs.foam_scale);
      rs.water_displaced = at.get_b("displaced", rs.water_displaced);
      rs.water_wind_speed = at.get_f("wind_speed", rs.water_wind_speed);
      rs.water_wind_dir = at.get_f("wind_dir", rs.water_wind_dir);
      rs.water_choppiness = at.get_f("choppiness", rs.water_choppiness);
      rs.foam_depth_m = at.get_f("foam_depth", rs.foam_depth_m);
      rs.foam_coverage = at.get_f("foam_coverage", rs.foam_coverage);
    } else if (n.type == "RenderCamera") {
      rs.exposure = at.get_f("exposure", rs.exposure);
      rs.height_scale = at.get_f("height_scale", rs.height_scale);
      rs.terrain_size_m = at.get_f("terrain_size_m", rs.terrain_size_m);
    } else if (n.type == "RenderQuality") {
      // its engine list has no appleseed entry: 3 there is the viewport
      int e = at.get_choice("engine");
      rs.render_engine = e == 3 ? 4 : e;
      rs.render_width = at.get_i("width", rs.render_width);
      rs.render_height = at.get_i("height", rs.render_height);
      rs.render_samples = at.get_i("samples", rs.render_samples);
      std::string p = at.get_s("path");
      if (!p.empty()) rs.render_path = p;
    } else if (n.type == "RenderOutput") {
      std::string p = at.get_s("path");
      if (!p.empty()) rs.render_path = p;
      rs.render_format = at.get_choice("format");
      rs.render_width = at.get_i("width", rs.render_width);
      rs.render_height = at.get_i("height", rs.render_height);
      rs.render_engine = at.get_choice("engine");
      rs.render_samples = at.get_i("samples", rs.render_samples);
    } else if (n.type == "RenderPasses") {
      int m = 0;
      for (int i = 0; i < RENDER_PASS_COUNT; ++i)
        if (at.get_b(render_pass_name(i))) m |= 1 << i;
      rs.pass_mask = m;
    } else if (n.type == "RenderBackdrop") {
      RenderSettings::Backdrop &b = rs.backdrop;
      b.enabled = at.get_b("enabled", b.enabled);
      b.file = at.get_s("file");
      b.mapping = at.get_choice("mapping");
      b.vfov = at.get_f("vfov", b.vfov);
      b.flip = at.get_b("flip", b.flip);
      b.yaw = at.get_f("yaw", b.yaw);
      b.pitch = at.get_f("pitch", b.pitch);
      b.exposure_ev = at.get_f("exposure", b.exposure_ev);
      if (const gpx::Attribute *c = at.find("tint"))
        for (int k = 0; k < 3; ++k) b.tint[k] = c->col[k];
      b.blend = at.get_f("blend", b.blend);
      b.haze = at.get_f("haze", b.haze);
      b.hide_sun = at.get_b("hide_sun", b.hide_sun);
    } else if (n.type == "PostProcess") {
      rs.post_exposure = at.get_f("exposure", rs.post_exposure);
      rs.post_saturation = at.get_f("saturation", rs.post_saturation);
      if (const gpx::Attribute *c = at.find("tint"))
        for (int k = 0; k < 3; ++k) rs.post_tint[k] = c->col[k];
      rs.post_vignette = at.get_f("vignette", rs.post_vignette);
    }
  }
  // lights, cameras, objects, planets, the sequence: scene_nodes_objects.cpp
  apply_object_nodes(a);
}

} // namespace studio
