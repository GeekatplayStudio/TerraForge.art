// Geekatplay TerraForge - Properties: a nebula or a galaxy in deep space
// (scene.hpp NebulaData, drawn by studio/shaders_space.cpp). Split from
// panel_properties_object.cpp for the 500-line module rule.
#include "app.hpp"
#include "wheel_widgets.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "space_presets.hpp"
#include <algorithm>
#include <imgui.h>

namespace studio {

void object_properties_nebula_ui(App &a, SceneObject &o) {
  (void)a;
  NebulaData &N = o.nebula;
  ImGui::SeparatorText("Deep space");
  ImGui::TextDisabled("At infinity, behind the air: seen at night, from high up,\n"
                      "or wherever the atmosphere is thin - beyond a ring's rim.\n"
                      "The Environment tab's Space section has the stars and the\n"
                      "galaxy band; the Atmosphere's height decides where space\n"
                      "shows.");
  int type = std::clamp(N.type, 0, 4);
  if (ImGui::Combo("Kind", &type,
                   "Nebula (glowing gas)\0Dark nebula\0Spiral galaxy\0Elliptical galaxy\0"
                   "Planetary nebula\0"))
    N.type = type;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A cloud of glowing gas with filaments and young stars; a\n"
                      "cloud of dust that hides the stars behind it; a spiral\n"
                      "galaxy with arms, a bulge and dust lanes; a smooth\n"
                      "elliptical galaxy; a ring of gas round a dying star.");
  ImGui::SeparatorText("Place in the sky");
  studio::DragFloatW("Azimuth", &N.azimuth, 0.5f, -360.f, 360.f, "%.1f\xC2\xB0");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Compass heading, the sun's frame.");
  studio::DragFloatW("Elevation", &N.elevation, 0.5f, -90.f, 90.f, "%.1f\xC2\xB0");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Degrees above the horizon.");
  studio::DragFloatW("Size", &N.size_deg, 0.2f, 0.2f, 180.f, "%.1f\xC2\xB0");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Angular diameter. The full moon is half a degree; the\n"
                      "Andromeda galaxy three, the Orion nebula one.");
  studio::DragFloatW("Rotation", &N.rotation_deg, 0.5f, -180.f, 180.f, "%.1f\xC2\xB0");
  if (N.type == 2 || N.type == 3) {
    studio::DragFloatW("Tilt", &N.tilt_deg, 0.5f, 0.f, 85.f, "%.1f\xC2\xB0");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How far the disc is turned from face-on: 0 a round\n"
                        "galaxy, 80 a thin edge.");
  }
  ImGui::SeparatorText("Look");
  int seed = (int)N.seed;
  if (studio::DragIntW("Seed", &seed, 1, 1, 1 << 24)) N.seed = (uint32_t)seed;
  studio::SliderFloatW("Brightness", &N.brightness, 0.f, 4.f);
  studio::SliderFloatW(N.type == 2 ? "Dust lanes" : "Density", &N.density, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(N.type == 2 ? "How dark the dust lanes through the arms are."
                                  : "How much of the footprint is cloud.");
  studio::SliderFloatW("Detail", &N.detail, 0.f, 1.f);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fractal fineness: soft to wispy.");
  if (N.type == 2) {
    studio::SliderIntW("Arms", &N.arms, 1, 6);
  }
  if (N.type == 0 || N.type == 1) {
    // A cloud is marched as a volume (shaders_space_neb.cpp), so these are
    // the things a real one is made of rather than dials on a picture.
    ImGui::SeparatorText("The cloud itself");
    studio::SliderFloatW("Dust", &N.dust, 0.f, 1.f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How much dark dust threads through the gas. Dust does\n"
                        "not merely hide what is behind it, it reddens it -\n"
                        "blue is absorbed hardest - and where starlight catches\n"
                        "it, it scatters back blue.");
    studio::SliderFloatW("Shape", &N.warp, 0.f, 1.5f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How far the cloud is pulled out of a ball. 0 is a\n"
                        "sphere of noise; high is arms, hollows and wisps.");
    studio::SliderFloatW("Glow", &N.glow, 0.f, 2.f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The halo it throws around itself, past its own edge.");
    if (N.type == 0) {
      studio::SliderIntW("Hot stars", &N.sources, 1, 4);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How many young stars inside light it. Their glare is\n"
                          "what makes the gas glow at all, and because it falls\n"
                          "off as an inverse square the gas nearest them is\n"
                          "ionised twice over - teal - while the outskirts stay\n"
                          "hydrogen's crimson. Move them with the seed.");
      studio::SliderFloatW("Hot stars shown", &N.source_stars, 0.f, 2.f);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How bright those stars are drawn, spikes and all. 0\n"
                          "leaves the gas lit by stars you cannot see.");
      studio::SliderFloatW("Core glow", &N.core_glow, 0.f, 2.f);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The gas round the hot stars burned out toward white,\n"
                          "the way a long exposure records a nebula's heart.");
    }
    studio::SliderFloatW("Turbulence", &N.turbulence, 0.f, 1.f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Tears the gas into filaments and tendrils with a second,\n"
                        "finer warp. Costs one more look into the noise a step.");
    studio::SliderFloatW("Dust lanes", &N.lanes, 0.f, 1.f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Thin dark ridges of dust laid across the glow - the lanes\n"
                        "a photograph of a nebula is crossed by.");
  }
  ImGui::SeparatorText("Colour");
  ImGui::ColorEdit3(N.type == 2 || N.type == 3 ? "Core" : "Ionised gas", N.color1);
  ImGui::ColorEdit3(N.type == 2 ? "Arms" : (N.type == 3 ? "Halo" : "Cool gas"), N.color2);
  if (N.type == 0) {
    // unset (negative) reads as the cool gas's own colour, so the picker opens there
    bool own = N.color3[0] >= 0.f;
    float c3[3] = {N.color3[0], N.color3[1], N.color3[2]};
    if (!own) for (int k = 0; k < 3; ++k) c3[k] = N.color2[k];
    if (ImGui::ColorEdit3("Outskirts", c3))
      for (int k = 0; k < 3; ++k) N.color3[k] = std::max(c3[k], 0.f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The colour of the barely-lit gas at the cloud's edges -\n"
                        "the pinks and ambers round a nebula's crimson.");
    if (own) {
      ImGui::SameLine();
      if (ImGui::SmallButton("As cool gas")) N.color3[0] = N.color3[1] = N.color3[2] = -1.f;
    }
  }
  if (ImGui::Button("Colours from the Realism dial"))
    space_nebula_colors(N.type, render_settings().space.realism, N.seed, N.color1, N.color2);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Takes both colours from where Realism stands in the\n"
                      "Environment panel's Space section: a photograph's at 1,\n"
                      "a film's at 0. The seed picks among a few of each.");
}

} // namespace studio
