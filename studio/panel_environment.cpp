// Geekatplay TerraForge — Environment panel: sun (manual/geographic),
// atmosphere, fog/haze/pollution, water materials.
#include "app.hpp"
#include "wheel_widgets.hpp"
#include "prop_lengths.hpp"
#include "gpu_timer.hpp"
#include "render_settings.hpp"
#include "renderer_instances.hpp"
#include <imgui.h>

namespace studio {

static void section_sun(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen)) return;
  ImGui::RadioButton("Manual", &rs.sun_mode, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Location & time", &rs.sun_mode, 1);
  if (rs.sun_mode == 0) {
    studio::SliderFloatW("Azimuth", &rs.sun_azimuth, 0.f, 360.f, "%.0f\xC2\xB0");
    studio::SliderFloatW("Altitude", &rs.sun_altitude, 1.f, 89.f, "%.0f\xC2\xB0");
  } else {
    studio::DragFloatW("Latitude", &rs.latitude, 0.1f, -89.f, 89.f, "%.2f\xC2\xB0");
    studio::DragFloatW("Longitude", &rs.longitude, 0.1f, -180.f, 180.f, "%.2f\xC2\xB0");
    studio::DragFloatW("UTC offset", &rs.utc_offset, 0.25f, -12.f, 14.f, "%.2f h");
    studio::SliderIntW("Month", &rs.month, 1, 12);
    studio::SliderIntW("Day", &rs.day, 1, 31);
    studio::SliderFloatW("Local time", &rs.hour, 0.f, 24.f, "%.2f h");
    float dir[3];
    compute_sun_dir(rs, dir);
    float alt = std::asin(std::clamp(dir[1], -1.f, 1.f)) * 57.29578f;
    ImGui::TextDisabled("computed sun altitude: %.1f\xC2\xB0", alt);
  }
  ImGui::ColorEdit3("Sun color", rs.sun_color);
  studio::SliderFloatW("Sun intensity", &rs.sun_intensity, 0.2f, 8.f);
}

static void section_atmosphere(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Atmosphere", ImGuiTreeNodeFlags_DefaultOpen)) return;
  studio::SliderFloatW("Density", &rs.atmosphere_density, 0.05f, 3.f);
  drag_length("Height", &rs.atmosphere_height, 1.f, 0.f, 1e9f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How high the air reaches over the world's surface, whatever\n"
                      "its shape - a globe, a ring, a flat world. Beyond it is space:\n"
                      "from high enough the sky thins to stars and the world shows a\n"
                      "blue rim; on a ring the air is a band inside the ring. 0 keeps\n"
                      "the old rule, sky everywhere thinning with distance.");
  studio::SliderFloatW("Ambient light", &rs.ambient_intensity, 0.f, 2.f);
  ImGui::ColorEdit3("Sky zenith", rs.sky_zenith);
  ImGui::ColorEdit3("Sky horizon", rs.sky_horizon);
  studio::SliderFloatW("Exposure", &rs.exposure, 0.3f, 3.f);
}

// Deep space behind the air lives in panel_environment_space.cpp: it grew
// a palette, a march and a way to fill the sky, and this file has the
// 500-line rule to keep.
void section_space(RenderSettings &rs);

static void section_fog(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Fog / Haze", ImGuiTreeNodeFlags_DefaultOpen)) return;
  ImGui::Combo("Type", &rs.fog_type, "Off\0Haze\0Fog\0Pollution\0");
  if (rs.fog_type != 0) {
    studio::SliderFloatW("Density", &rs.fog_density, 0.f, 6.f);
    studio::SliderFloatW("Level (height)", &rs.fog_level, 0.f, 1.f);
    studio::SliderFloatW("Vertical falloff", &rs.fog_falloff, 0.5f, 24.f);
    ImGui::ColorEdit3("Fog color", rs.fog_color);
    ImGui::ColorEdit3("Light absorption", rs.absorption_color);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("What survives per channel through the fog, raised to the\n"
                        "optical depth: a bluish absorber makes distant things\n"
                        "go warm, the way real haze does.");
    studio::SliderFloatW("Sun scattering", &rs.fog_sun_scatter, 0.f, 1.f);
    studio::SliderFloatW("Scattering albedo", &rs.fog_albedo, 0.f, 1.f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Of the light the fog stops, how much it scatters on\n"
                        "rather than absorbs. Water droplets are near 1;\n"
                        "smoke and pollution much lower.");
    studio::SliderFloatW("Anisotropy", &rs.fog_anisotropy, -0.9f, 0.95f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The Henyey-Greenstein phase: 0 scatters evenly, toward 1\n"
                        "the light carries on forward, which is the glow around\n"
                        "the sun seen through mist.");
    studio::SliderFloatW("Heterogeneity", &rs.fog_heterogeneity, 0.f, 1.f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Breaks the fog into drifts with noise. Needs ray steps\n"
                        "above 1 to be seen; at 0 the closed form is used and\n"
                        "the fog costs nothing.");
    studio::SliderIntW("Ray steps", &rs.fog_steps, 1, 64);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How many samples the view ray takes through broken fog,\n"
                        "each with a short march toward the sun for shadowing.\n"
                        "The march stops early once 99%% of the light is gone,\n"
                        "so this is a ceiling, not a cost you always pay.");
  }
}

static void section_clouds(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Clouds (volumetric)", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  studio::Checkbox("Enabled", &rs.clouds_on);
  if (!rs.clouds_on) return;
  ImGui::Combo("Type", &rs.cloud_type, "Stratus\0Cumulus\0Cumulonimbus\0");
  studio::SliderFloatW("Coverage", &rs.cloud_coverage, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("0 = clear sky, 1 = fully overcast.");
  studio::SliderFloatW("Density", &rs.cloud_density, 0.1f, 3.f);
  studio::SliderFloatW("Detail erosion", &rs.cloud_detail, 0.f, 1.f);
  if (rs.cloud_type == 2)
    studio::SliderFloatW("Anvil spread", &rs.cloud_anvil, 0.f, 1.f);
  studio::SliderFloatW("Altitude", &rs.cloud_altitude, 0.2f, 2.f);
  studio::SliderFloatW("Thickness", &rs.cloud_thickness, 0.05f, 1.5f);
  ImGui::ColorEdit3("Color", rs.cloud_color);
  studio::SliderFloatW("Sky light", &rs.cloud_ambient, 0.f, 2.f);
  studio::SliderFloatW("Wind speed", &rs.cloud_wind_speed, 0.f, 0.3f, "%.3f");
  studio::SliderFloatW("Wind direction", &rs.cloud_wind_dir, 0.f, 360.f, "%.0f\xC2\xB0");
  ImGui::SeparatorText("Second layer");
  studio::Checkbox("Second layer on", &rs.cloud2_on);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Another cloud layer at its own height - a cirrus veil\n"
                      "above the cumulus, or a low stratus sheet under it.\n"
                      "Drawn back to front from the camera.");
  if (rs.cloud2_on) {
    ImGui::Combo("Type##2", &rs.cloud2_type, "Stratus\0Cumulus\0Cumulonimbus\0");
    studio::SliderFloatW("Coverage##2", &rs.cloud2_coverage, 0.f, 1.f);
    studio::SliderFloatW("Density##2", &rs.cloud2_density, 0.1f, 3.f);
    studio::SliderFloatW("Altitude##2", &rs.cloud2_altitude, 0.2f, 4.f);
    studio::SliderFloatW("Thickness##2", &rs.cloud2_thickness, 0.05f, 1.5f);
  }
  ImGui::SeparatorText("Quality");
  ImGui::Combo("Quality", &rs.cloud_quality, "Draft\0Normal\0High\0");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Raymarch step count: higher is smoother but slower.");
  studio::SliderIntW("Scattering bounces", &rs.cloud_scatter_octaves, 1, 4);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("1 stops light at its first hit, which makes dense\n"
                      "cloud read as plastic. Each extra bounce reuses the\n"
                      "shadow ray already computed with lower extinction and\n"
                      "a more even phase, so it costs very little.");
  if (rs.cloud_scatter_octaves > 1) {
    studio::SliderFloatW("Bounce depth", &rs.cloud_scatter_depth, 0.4f, 0.99f,
                       "%.2f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How much further each bounce reaches into the cloud.\n"
                        "Lower lifts the shadowed interior more, and past\n"
                        "about 0.7 it starts flattening the cloud's shape\n"
                        "unless you lower Density to match.");
  }
}

static void section_water(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Water", ImGuiTreeNodeFlags_DefaultOpen)) return;
  studio::Checkbox("Enabled", &rs.show_water);
  if (!rs.show_water) return;
  studio::SliderFloatW("Level", &rs.water_level, 0.f, 1.f);
  ImGui::ColorEdit3("Deep color", rs.water_deep_color);
  ImGui::ColorEdit3("Shallow color", rs.water_shallow_color);
  studio::SliderFloatW("Clarity", &rs.water_clarity, 1.f, 60.f);
  studio::SliderFloatW("Opacity", &rs.water_opacity, 0.3f, 1.f);
  studio::SliderFloatW("Wave amplitude", &rs.water_wave_amp, 0.f, 4.f);
  studio::SliderFloatW("Wave scale", &rs.water_wave_scale, 0.2f, 6.f);
  studio::SliderFloatW("Wave speed", &rs.water_wave_speed, 0.f, 5.f);
  ImGui::SeparatorText("Foam");
  studio::Checkbox("Foam enabled", &rs.water_foam);
  if (rs.water_foam) {
    ImGui::ColorEdit3("Foam color", rs.foam_color);
    studio::SliderFloatW("Shoreline foam", &rs.foam_amount, 0.f, 2.f);
    studio::SliderFloatW("Crest foam", &rs.foam_crests, 0.f, 1.f);
    studio::SliderFloatW("Foam scale", &rs.foam_scale, 0.5f, 10.f);
  }
}

static void section_planet(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Planet & fractal detail",
                               ImGuiTreeNodeFlags_DefaultOpen))
    return;
  studio::SliderFloatW("Fractal detail", &rs.fractal_detail, 0.f, 0.02f, "%.4f");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Procedural relief added on top of the heightmap.\n"
                      "It keeps resolving as the camera moves closer, so\n"
                      "the surface stays fractal instead of turning into\n"
                      "flat grid cells.");
  studio::SliderFloatW("Detail scale", &rs.fractal_scale, 8.f, 400.f, "%.0f");
  // In metres, from one metre up: a small planet wraps the terrain tile
  // round itself (equirectangular), a large one is the curved horizon.
  {
    bool flat = rs.planet_radius <= 0.f;
    if (studio::Checkbox("Flat world (no planet)", &flat))
      rs.planet_radius = flat ? 0.f : 1275.f;
    if (!flat) {
      float m = rs.planet_radius * rs.terrain_size_m;
      if (studio::SliderFloatW("Planet radius", &m, 1e-4f, 1e12f, "%.4g m",
                             ImGuiSliderFlags_Logarithmic))
        rs.planet_radius = std::max(m, 1e-4f) / rs.terrain_size_m;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The sphere the terrain lies on. Earth is about\n"
                          "6 371 000 m. Below the tile's own circumference\n"
                          "the tile wraps the whole globe - a 1 m planet made\n"
                          "from the heightmap.");
      float circ = rs.planet_radius * 6.2831853f;
      if (circ < 1.f)
        ImGui::TextDisabled("the tile wraps the whole planet");
      else
        ImGui::TextDisabled("horizon about %.1f tiles away at eye height",
                            std::sqrt(2.f * rs.planet_radius * 0.02f));
    }
  }
}

static void section_subdivision(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Surface detail", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  studio::Checkbox("Adaptive subdivision", &rs.tessellation);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Subdivide the terrain to whatever the camera needs\n"
                      "instead of to a fixed grid, so displacement keeps\n"
                      "resolving as you approach. Off falls back to the\n"
                      "fixed grid.");
  if (rs.tessellation) {
    studio::SliderFloatW("Target edge (px)", &rs.tess_pixels, 2.f, 32.f, "%.0f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How long a triangle edge should be on screen.\n"
                        "Smaller means more triangles and finer relief.");
    studio::SliderFloatW("Minimum subdivision", &rs.tess_min, 1.f, 32.f, "%.0f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Never go coarser than this, however far away the\n"
                        "ground is. 8 matches the fixed grid exactly, so the\n"
                        "adaptive path can only ever add detail.");
    studio::SliderFloatW("Limit subdivision", &rs.tess_max, 1.f, 64.f, "%.0f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The ceiling, for when detail is not worth the cost.");
    ImGui::TextDisabled("%s", renderer_tess_status().c_str());
    studio::SliderFloatW("Relief detail by distance", &rs.terrain_lod, 0.f, 1.f, "%.2f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Far ground reads a calmer, averaged relief so stone fields\n"
                        "and grass at the horizon stop shimmering. 0 keeps every\n"
                        "texel at every distance.");
    ImGui::SeparatorText("Scattered copies");
    studio::DragFloatW("Full detail within (m)", &rs.scatter_lod_full_m, 1.f, 1.f, 1e6f, "%.0f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Every copy is drawn, from the mesh itself, inside this.");
    studio::DragFloatW("Thinned by (m)", &rs.scatter_lod_far_m, 10.f, 1.f, 1e7f, "%.0f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("By this distance only the share below is drawn, and the\n"
                        "survivors grow so the ground stays as covered.");
    studio::SliderFloatW("Far crowd share", &rs.scatter_lod_min_keep, 0.01f, 1.f, "%.2f");
    studio::DragFloatW("Cards beyond (m)", &rs.scatter_lod_billboard_m, 10.f, 1.f, 1e8f, "%.0f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Past this a copy is a flat card facing the camera,\n"
                        "baked from the mesh, instead of geometry.");
    studio::DragFloatW("Nothing beyond (m)", &rs.scatter_lod_cull_m, 10.f, 1.f, 1e8f, "%.0f");
    {
      int drawn = 0, total = 0, cards = 0;
      renderer_instance_stats(drawn, total, &cards);
      if (total)
        ImGui::TextDisabled("%d of %d copies drawn last frame, %d as cards", drawn, total, cards);
    }
    Checkbox("Cull patches off screen", &rs.frustum_cull);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Discard a patch before subdividing it when its whole\n"
                        "bounding box is outside the view. Off submits all\n"
                        "4096 every frame, which is what this replaced.");
    ImGui::TextDisabled("%s", renderer_cull_status().c_str());
    // GPU time for the terrain pass. Wall-clock frame time is pinned to the
    // refresh rate, so it cannot show what any of the settings above cost.
    if (double t = gpu_timer_ms("terrain"))
      ImGui::TextDisabled("terrain pass: %.3f ms on the GPU", t);
  }

  ImGui::Separator();
  studio::SliderFloatW("Graph displacement", &rs.field_displacement, -1.f, 1.f,
                     "%.3f");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How far a TerrainDisplacement node's field moves the\n"
                      "surface, in world units. The node's own Strength sets\n"
                      "this whenever the graph is evaluated.");
  const char *ferr = renderer_field_error();
  if (ferr && *ferr) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.25f, 1.f));
    ImGui::TextWrapped("displacement shader: %s", ferr);
    ImGui::PopStyleColor();
  }
}

static void section_render(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Render")) return;
  studio::SliderFloatW("Height scale", &rs.height_scale, 0.02f, 0.8f);
  studio::Checkbox("Shadows", &rs.shadows);
  studio::SliderFloatW("Shadow softness", &rs.shadow_softness, 0.5f, 5.f);
  studio::Checkbox("Wireframe", &rs.wireframe);
  ImGui::SameLine();
  studio::Checkbox("Use graph albedo", &rs.use_albedo);
}

// World tab of the Properties editor (sun, sky, clouds, fog, water)
void world_properties_ui(App &a) {
  RenderSettings &rs = render_settings();
  bool driven = false;
  {
    std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::try_to_lock);
    if (lk.owns_lock())
      for (auto &n : a.graph.nodes)
        if (n->type == "SunLight" || n->type == "AtmosphereSettings" ||
            n->type == "CloudLayer" || n->type == "WaterLayer")
          driven = true;
  }
  if (driven) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.47f, 0.19f, 1.f));
    ImGui::TextWrapped("Atmosphere nodes in the graph drive these settings and "
                       "overwrite them on every evaluation. Edit the nodes to "
                       "make changes stick.");
    ImGui::PopStyleColor();
    ImGui::Separator();
  }
  if (prop_filter_match("Sun")) section_sun(rs);
  if (prop_filter_match("Atmosphere sky")) section_atmosphere(rs);
  if (prop_filter_match("Space stars galaxy nebula")) section_space(rs);
  if (prop_filter_match("Clouds")) section_clouds(rs);
  if (prop_filter_match("Fog haze")) section_fog(rs);
  if (prop_filter_match("Water foam")) section_water(rs);
  if (prop_filter_match("Planet fractal detail")) section_planet(rs);
  if (prop_filter_match("Surface detail subdivision tessellation displacement"))
    section_subdivision(rs);
  if (prop_filter_match("Render shadows")) section_render(rs);
}

} // namespace studio

