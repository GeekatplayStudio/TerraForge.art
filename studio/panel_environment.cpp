// Geekatplay TerraForge — Environment panel: sun (manual/geographic),
// atmosphere, fog/haze/pollution, water materials.
#include "app.hpp"
#include "render_settings.hpp"
#include <imgui.h>

namespace studio {

static void section_sun(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen)) return;
  ImGui::RadioButton("Manual", &rs.sun_mode, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Location & time", &rs.sun_mode, 1);
  if (rs.sun_mode == 0) {
    ImGui::SliderFloat("Azimuth", &rs.sun_azimuth, 0.f, 360.f, "%.0f\xC2\xB0");
    ImGui::SliderFloat("Altitude", &rs.sun_altitude, 1.f, 89.f, "%.0f\xC2\xB0");
  } else {
    ImGui::DragFloat("Latitude", &rs.latitude, 0.1f, -89.f, 89.f, "%.2f\xC2\xB0");
    ImGui::DragFloat("Longitude", &rs.longitude, 0.1f, -180.f, 180.f, "%.2f\xC2\xB0");
    ImGui::DragFloat("UTC offset", &rs.utc_offset, 0.25f, -12.f, 14.f, "%.2f h");
    ImGui::SliderInt("Month", &rs.month, 1, 12);
    ImGui::SliderInt("Day", &rs.day, 1, 31);
    ImGui::SliderFloat("Local time", &rs.hour, 0.f, 24.f, "%.2f h");
    float dir[3];
    compute_sun_dir(rs, dir);
    float alt = std::asin(std::clamp(dir[1], -1.f, 1.f)) * 57.29578f;
    ImGui::TextDisabled("computed sun altitude: %.1f\xC2\xB0", alt);
  }
  ImGui::ColorEdit3("Sun color", rs.sun_color);
  ImGui::SliderFloat("Sun intensity", &rs.sun_intensity, 0.2f, 8.f);
}

static void section_atmosphere(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Atmosphere", ImGuiTreeNodeFlags_DefaultOpen)) return;
  ImGui::SliderFloat("Density", &rs.atmosphere_density, 0.05f, 3.f);
  ImGui::SliderFloat("Ambient light", &rs.ambient_intensity, 0.f, 2.f);
  ImGui::ColorEdit3("Sky zenith", rs.sky_zenith);
  ImGui::ColorEdit3("Sky horizon", rs.sky_horizon);
  ImGui::SliderFloat("Exposure", &rs.exposure, 0.3f, 3.f);
}

static void section_fog(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Fog / Haze", ImGuiTreeNodeFlags_DefaultOpen)) return;
  ImGui::Combo("Type", &rs.fog_type, "Off\0Haze\0Fog\0Pollution\0");
  if (rs.fog_type != 0) {
    ImGui::SliderFloat("Density", &rs.fog_density, 0.f, 6.f);
    ImGui::SliderFloat("Level (height)", &rs.fog_level, 0.f, 1.f);
    ImGui::SliderFloat("Vertical falloff", &rs.fog_falloff, 0.5f, 24.f);
    ImGui::ColorEdit3("Fog color", rs.fog_color);
    ImGui::ColorEdit3("Light absorption", rs.absorption_color);
    ImGui::SliderFloat("Sun scattering", &rs.fog_sun_scatter, 0.f, 1.f);
  }
}

static void section_clouds(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Clouds (volumetric)", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  ImGui::Checkbox("Enabled", &rs.clouds_on);
  if (!rs.clouds_on) return;
  ImGui::Combo("Type", &rs.cloud_type, "Stratus\0Cumulus\0Cumulonimbus\0");
  ImGui::SliderFloat("Coverage", &rs.cloud_coverage, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("0 = clear sky, 1 = fully overcast.");
  ImGui::SliderFloat("Density", &rs.cloud_density, 0.1f, 3.f);
  ImGui::SliderFloat("Detail erosion", &rs.cloud_detail, 0.f, 1.f);
  if (rs.cloud_type == 2)
    ImGui::SliderFloat("Anvil spread", &rs.cloud_anvil, 0.f, 1.f);
  ImGui::SliderFloat("Altitude", &rs.cloud_altitude, 0.2f, 2.f);
  ImGui::SliderFloat("Thickness", &rs.cloud_thickness, 0.05f, 1.5f);
  ImGui::ColorEdit3("Color", rs.cloud_color);
  ImGui::SliderFloat("Sky light", &rs.cloud_ambient, 0.f, 2.f);
  ImGui::SliderFloat("Wind speed", &rs.cloud_wind_speed, 0.f, 0.3f, "%.3f");
  ImGui::SliderFloat("Wind direction", &rs.cloud_wind_dir, 0.f, 360.f, "%.0f\xC2\xB0");
  ImGui::Combo("Quality", &rs.cloud_quality, "Draft\0Normal\0High\0");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Raymarch step count: higher is smoother but slower.");
}

static void section_water(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Water", ImGuiTreeNodeFlags_DefaultOpen)) return;
  ImGui::Checkbox("Enabled", &rs.show_water);
  if (!rs.show_water) return;
  ImGui::SliderFloat("Level", &rs.water_level, 0.f, 1.f);
  ImGui::ColorEdit3("Deep color", rs.water_deep_color);
  ImGui::ColorEdit3("Shallow color", rs.water_shallow_color);
  ImGui::SliderFloat("Clarity", &rs.water_clarity, 1.f, 60.f);
  ImGui::SliderFloat("Opacity", &rs.water_opacity, 0.3f, 1.f);
  ImGui::SliderFloat("Wave amplitude", &rs.water_wave_amp, 0.f, 4.f);
  ImGui::SliderFloat("Wave scale", &rs.water_wave_scale, 0.2f, 6.f);
  ImGui::SliderFloat("Wave speed", &rs.water_wave_speed, 0.f, 5.f);
  ImGui::SeparatorText("Foam");
  ImGui::Checkbox("Foam enabled", &rs.water_foam);
  if (rs.water_foam) {
    ImGui::ColorEdit3("Foam color", rs.foam_color);
    ImGui::SliderFloat("Shoreline foam", &rs.foam_amount, 0.f, 2.f);
    ImGui::SliderFloat("Crest foam", &rs.foam_crests, 0.f, 1.f);
    ImGui::SliderFloat("Foam scale", &rs.foam_scale, 0.5f, 10.f);
  }
}

static void section_render(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Render")) return;
  ImGui::SliderFloat("Height scale", &rs.height_scale, 0.02f, 0.8f);
  ImGui::Checkbox("Shadows", &rs.shadows);
  ImGui::SliderFloat("Shadow softness", &rs.shadow_softness, 0.5f, 5.f);
  ImGui::Checkbox("Wireframe", &rs.wireframe);
  ImGui::SameLine();
  ImGui::Checkbox("Use graph albedo", &rs.use_albedo);
}

// World tab of the Properties editor (sun, sky, clouds, fog, water)
void world_properties_ui(App &a) {
  RenderSettings &rs = render_settings();
  bool driven = false;
  {
    std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
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
  if (prop_filter_match("Clouds")) section_clouds(rs);
  if (prop_filter_match("Fog haze")) section_fog(rs);
  if (prop_filter_match("Water foam")) section_water(rs);
  if (prop_filter_match("Render shadows")) section_render(rs);
}

} // namespace studio
