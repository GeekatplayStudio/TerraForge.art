// Geekatplay TerraForge - the scene's wind, in the Environment panel.
//
// One wind blows over the whole scene and everything in it reads the same
// one: the clouds drift with it, the sea is raised by it, the plants lean and
// gust with it, the fog moves with it. Before this each of them had a wind of
// its own, and a landscape where the cloud shadows cross one way while the
// trees lean another and the waves run a third reads as assembled rather than
// seen, however good each part is on its own.
//
// What stays with each of them is what properly belongs to it - how long the
// sea has been under this wind, how flexible a plant is, how much faster the
// air moves at cloud height - and each can be cut loose for a shot that needs
// it (studio/render_settings.hpp, RenderSettings::WindSettings).
#include "app.hpp"
#include "render_settings.hpp"
#include "wheel_widgets.hpp"
#include <imgui.h>

namespace studio {

void section_wind(RenderSettings &rs) {
  auto tip = [](const char *t) {
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t);
  };
  if (!ImGui::CollapsingHeader("Wind", ImGuiTreeNodeFlags_DefaultOpen)) return;

  studio::SliderFloatW("Speed", &rs.wind.speed_ms, 0.f, 30.f, "%.1f m/s");
  tip("The wind over the whole scene, at ten metres up, the way weather is\n"
      "quoted: 2 m/s is a breath, 5 a breeze that stirs leaves, 11 shakes\n"
      "branches, 20 is a gale. The clouds, the sea, the fog and every plant\n"
      "that follows it are blown by this one number.");
  studio::SliderFloatW("Direction##wind", &rs.wind.direction_deg, 0.f, 360.f, "%.0f\xC2\xB0");
  tip("The way it blows toward, seen from above: 0 toward +X, 90 toward +Z.");

  ImGui::SeparatorText("Gusts");
  studio::SliderFloatW("Strength##gust", &rs.wind.gust_strength, 0.f, 1.5f);
  tip("How much harder it blows when a gust arrives. 0 is a steady wind; at\n"
      "1 the gusts are as strong again as the wind between them.");
  studio::SliderFloatW("Frequency##gust", &rs.wind.gust_frequency, 0.f, 1.f, "%.2f /s");
  tip("How often the gusts come. 0.1 is one every ten seconds, which is\n"
      "about what open weather does.");
  studio::SliderFloatW("Gust size", &rs.wind.gust_size_m, 10.f, 3000.f, "%.0f m");
  tip("How large one gust is as it crosses the ground, so a squall travels\n"
      "through a wood rather than shaking all of it at once.");
  studio::SliderFloatW("Turbulence", &rs.wind.turbulence_deg, 0.f, 60.f, "%.0f\xC2\xB0");
  tip("How far the direction wanders either side of the mean as the gusts\n"
      "arrive. Real wind backs and veers rather than strengthening along a\n"
      "fixed line.");

  ImGui::SeparatorText("Aloft");
  studio::SliderFloatW("Shear", &rs.wind.shear, 1.f, 8.f, "%.1fx");
  tip("How much faster the air moves at cloud height than at the ground.\n"
      "Real air does; without it a cloud deck crawls at the speed of a\n"
      "breeze through grass.");

  ImGui::SeparatorText("What follows it");
  studio::Checkbox("Clouds##windfollow", &rs.cloud_wind_follow);
  tip("The cloud deck drifts with the wind, sheared for its height. Off\n"
      "leaves the clouds on their own speed and direction under Clouds.");
  studio::Checkbox("Water##windfollow", &rs.water_wind_follow);
  tip("The sea is raised by the wind. It takes the mean and not the gust: a\n"
      "sea state is built over hours and does not answer a single squall.\n"
      "Off leaves the waves on their own wind under Water.");
  ImGui::TextDisabled("Plants follow it unless a species says otherwise\n"
                      "(its Wind group, \"Follow the world's wind\").");
}

} // namespace studio
