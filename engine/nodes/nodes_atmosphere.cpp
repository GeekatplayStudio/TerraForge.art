// Geekatplay TerraForge — atmosphere & render nodes (Terragen-style).
// These nodes carry scene parameters rather than heightmaps: after each
// evaluation the studio reads them and drives the renderer, so the sky,
// clouds, lighting and render setup are all node-driven and saved with the
// project like any other node.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"

namespace gpx {

REGISTER_NODE(
    SunLight, "Atmosphere", "Sun: direction (manual or geographic), color, intensity",
    [](Node &n) {
      n.add_out("sun");
      add_choice(n.attrs, "mode", "Direction mode", {"Manual", "Location & time"}, 0,
                 "Direction");
      add_float(n.attrs, "azimuth", "Azimuth", 135.f, 0.f, 360.f, "Direction");
      add_float(n.attrs, "altitude", "Altitude", 35.f, 1.f, 89.f, "Direction");
      add_float(n.attrs, "latitude", "Latitude", 40.7f, -89.f, 89.f, "Location");
      add_float(n.attrs, "longitude", "Longitude", -111.9f, -180.f, 180.f, "Location");
      add_float(n.attrs, "utc_offset", "UTC offset", -7.f, -12.f, 14.f, "Location");
      add_int(n.attrs, "month", "Month", 6, 1, 12, "Location");
      add_int(n.attrs, "day", "Day", 21, 1, 31, "Location");
      add_float(n.attrs, "hour", "Local time", 14.f, 0.f, 24.f, "Location");
      add_float(n.attrs, "intensity", "Intensity", 2.6f, 0.2f, 8.f, "Light");
      add_float(n.attrs, "color_r", "Color R", 1.f, 0.f, 1.f, "Light");
      add_float(n.attrs, "color_g", "Color G", 0.93f, 0.f, 1.f, "Light");
      add_float(n.attrs, "color_b", "Color B", 0.82f, 0.f, 1.f, "Light");
      add_bool(n.attrs, "shadows", "Cast shadows", true, "Light");
    },
    [](Node &n) { (void)n; })

REGISTER_NODE(
    AtmosphereSettings, "Atmosphere", "Sky colors, density, haze/fog and light absorption",
    [](Node &n) {
      n.add_out("atmosphere");
      add_float(n.attrs, "density", "Atmosphere density", 1.f, 0.05f, 3.f, "Sky");
      add_float(n.attrs, "ambient", "Ambient light", 0.7f, 0.f, 2.f, "Sky");
      add_float(n.attrs, "zenith_r", "Zenith R", 0.18f, 0.f, 1.f, "Sky");
      add_float(n.attrs, "zenith_g", "Zenith G", 0.32f, 0.f, 1.f, "Sky");
      add_float(n.attrs, "zenith_b", "Zenith B", 0.58f, 0.f, 1.f, "Sky");
      add_float(n.attrs, "horizon_r", "Horizon R", 0.62f, 0.f, 1.f, "Sky");
      add_float(n.attrs, "horizon_g", "Horizon G", 0.65f, 0.f, 1.f, "Sky");
      add_float(n.attrs, "horizon_b", "Horizon B", 0.70f, 0.f, 1.f, "Sky");
      add_choice(n.attrs, "fog_type", "Fog type", {"Off", "Haze", "Fog", "Pollution"},
                 1, "Fog");
      add_float(n.attrs, "fog_density", "Fog density", 0.9f, 0.f, 6.f, "Fog");
      add_float(n.attrs, "fog_level", "Fog level", 0.25f, 0.f, 1.f, "Fog");
      add_float(n.attrs, "fog_falloff", "Vertical falloff", 6.f, 0.5f, 24.f, "Fog");
      add_float(n.attrs, "fog_r", "Fog R", 0.55f, 0.f, 1.f, "Fog");
      add_float(n.attrs, "fog_g", "Fog G", 0.63f, 0.f, 1.f, "Fog");
      add_float(n.attrs, "fog_b", "Fog B", 0.75f, 0.f, 1.f, "Fog");
      add_float(n.attrs, "fog_scatter", "Sun scattering", 0.5f, 0.f, 1.f, "Fog");
    },
    [](Node &n) { (void)n; })

REGISTER_NODE(
    CloudLayer, "Atmosphere", "Volumetric cloud layer: type, coverage, altitude, wind",
    [](Node &n) {
      n.add_out("clouds");
      add_bool(n.attrs, "enabled", "Enabled", true, "Layer");
      add_choice(n.attrs, "type", "Cloud type",
                 {"Stratus", "Cumulus", "Cumulonimbus"}, 1, "Layer");
      add_float(n.attrs, "coverage", "Coverage", 0.55f, 0.f, 1.f, "Layer")
          .tooltip = "0 = clear sky, 1 = solid overcast.";
      add_float(n.attrs, "density", "Density", 1.f, 0.1f, 3.f, "Layer");
      add_float(n.attrs, "altitude", "Base altitude", 1.4f, 0.2f, 4.f, "Layer");
      add_float(n.attrs, "thickness", "Thickness", 0.8f, 0.05f, 2.f, "Layer");
      add_float(n.attrs, "detail", "Detail erosion", 0.6f, 0.f, 1.f, "Shape");
      add_float(n.attrs, "anvil", "Anvil spread", 0.3f, 0.f, 1.f, "Shape");
      add_float(n.attrs, "wind_speed", "Wind speed", 0.02f, 0.f, 0.3f, "Motion");
      add_float(n.attrs, "wind_dir", "Wind direction", 45.f, 0.f, 360.f, "Motion");
      add_float(n.attrs, "ambient", "Sky light", 0.55f, 0.f, 2.f, "Lighting");
      add_float(n.attrs, "color_r", "Color R", 1.f, 0.f, 1.f, "Lighting");
      add_float(n.attrs, "color_g", "Color G", 1.f, 0.f, 1.f, "Lighting");
      add_float(n.attrs, "color_b", "Color B", 1.f, 0.f, 1.f, "Lighting");
      add_choice(n.attrs, "quality", "Quality", {"Draft", "Normal", "High"}, 1,
                 "Lighting");
    },
    [](Node &n) { (void)n; })

REGISTER_NODE(
    WaterLayer, "Atmosphere", "Water body: level, colors, waves and foam",
    [](Node &n) {
      n.add_out("water");
      add_bool(n.attrs, "enabled", "Enabled", true, "Body");
      add_float(n.attrs, "level", "Level", 0.08f, 0.f, 1.f, "Body");
      add_float(n.attrs, "clarity", "Clarity", 18.f, 1.f, 60.f, "Body");
      add_float(n.attrs, "opacity", "Opacity", 0.92f, 0.3f, 1.f, "Body");
      add_float(n.attrs, "deep_r", "Deep R", 0.02f, 0.f, 1.f, "Color");
      add_float(n.attrs, "deep_g", "Deep G", 0.08f, 0.f, 1.f, "Color");
      add_float(n.attrs, "deep_b", "Deep B", 0.12f, 0.f, 1.f, "Color");
      add_float(n.attrs, "shallow_r", "Shallow R", 0.10f, 0.f, 1.f, "Color");
      add_float(n.attrs, "shallow_g", "Shallow G", 0.26f, 0.f, 1.f, "Color");
      add_float(n.attrs, "shallow_b", "Shallow B", 0.36f, 0.f, 1.f, "Color");
      add_float(n.attrs, "wave_amp", "Wave amplitude", 1.f, 0.f, 4.f, "Waves");
      add_float(n.attrs, "wave_scale", "Wave scale", 1.f, 0.2f, 6.f, "Waves");
      add_float(n.attrs, "wave_speed", "Wave speed", 1.f, 0.f, 5.f, "Waves");
      add_bool(n.attrs, "foam", "Foam", true, "Foam");
      add_float(n.attrs, "foam_amount", "Shoreline foam", 0.6f, 0.f, 2.f, "Foam");
      add_float(n.attrs, "foam_crests", "Crest foam", 0.35f, 0.f, 1.f, "Foam");
      add_float(n.attrs, "foam_scale", "Foam scale", 3.f, 0.5f, 10.f, "Foam");
    },
    [](Node &n) { (void)n; })

REGISTER_NODE(
    RenderCamera, "Render", "Camera and tone mapping for the render",
    [](Node &n) {
      n.add_out("camera");
      add_float(n.attrs, "exposure", "Exposure", 1.1f, 0.3f, 3.f, "Tone");
      add_float(n.attrs, "height_scale", "Terrain height scale", 0.22f, 0.02f, 0.8f,
                "Scene");
      add_float(n.attrs, "terrain_size_m", "Terrain size (m)", 5000.f, 100.f, 100000.f,
                "Scene");
    },
    [](Node &n) { (void)n; })

REGISTER_NODE(
    RenderQuality, "Render", "Offline render engine, resolution and sampling",
    [](Node &n) {
      n.add_out("quality");
      add_choice(n.attrs, "engine", "Engine",
                 {"Mitsuba 3", "Blender Cycles", "LuxCoreRender", "OpenGL viewport"},
                 0, "Engine");
      add_int(n.attrs, "width", "Width", 1920, 64, 8192, "Output");
      add_int(n.attrs, "height", "Height", 1080, 64, 8192, "Output");
      add_int(n.attrs, "samples", "Samples", 128, 8, 4096, "Output");
      add_filename(n.attrs, "path", "Output file", "terraforge_render.png", "Output");
    },
    [](Node &n) { (void)n; })

} // namespace gpx
