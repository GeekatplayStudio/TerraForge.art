// Geekatplay TerraForge — the Environment panel's Space section.
//
// Split out of panel_environment.cpp when deep space grew from two sliders
// into a backdrop with a palette, a march and a way to fill the sky in one
// press (space_presets.hpp, shaders_space.cpp). The nebulas and galaxies
// themselves are scene objects with their own tab; what lives here belongs
// to the sky as a whole.
#include "app.hpp"
#include "space_presets.hpp"
#include "render_settings.hpp"
#include "wheel_widgets.hpp"
#include <imgui.h>
#include <string>

namespace studio {

void section_space(RenderSettings &rs) {
  if (!ImGui::CollapsingHeader("Space", ImGuiTreeNodeFlags_DefaultOpen)) return;
  SpaceSettings &s = rs.space;
  static std::string note;
  ImGui::TextDisabled("Seen where the atmosphere lets space through: at night,\n"
                      "from above the Atmosphere's height, beyond a ring's rim.\n"
                      "Every part of it is worked out from a direction and a\n"
                      "seed, so it holds its detail at any focal length and\n"
                      "costs no memory - there is no picture to run out of.");
  studio::Checkbox("Draw deep space", &s.on);

  ImGui::SeparatorText("A whole sky at once");
  // The presets are the fastest way to a backdrop: each sets the star
  // field, the band and the palette, and the ones that want nebulas
  // scatter them too.
  const float half =
      (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  int col = 0;
  for (const SpacePreset &p : space_presets()) {
    if (col % 2) ImGui::SameLine();
    ++col;
    if (ImGui::Button(p.name, ImVec2(half, 0.f))) {
      std::string err;
      note = space_preset_apply(p.key, err) ? std::string("applied ") + p.name : err;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tip);
  }
  static int fill_count = 4, fill_seed = 1, fill_style = 0;
  studio::SliderIntW("How many", &fill_count, 0, 8);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How many nebulas and galaxies to scatter over the sky.\n"
                      "Eight is the most the sky can hold at once.");
  ImGui::Combo("Of what", &fill_style, "A mixture\0Nebulas\0Galaxies\0Dark clouds\0");
  studio::DragIntW("Arrangement", &fill_seed, 1, 1, 1 << 20);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Change this for a different scattering of the same\n"
                      "number of the same things.");
  if (ImGui::Button("Fill the sky")) {
    static const char *STYLES[] = {"mixed", "nebulas", "galaxies", "dark"};
    std::string err;
    const int n = space_populate(fill_count, fill_seed, STYLES[fill_style], err);
    note = n >= 0 ? std::to_string(n) + " scattered over the sky" : err;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Scatters them apart, varied in kind and size, coloured\n"
                      "from where Realism stands. Filling again replaces what\n"
                      "this made and leaves anything you placed by hand.");
  if (!note.empty()) ImGui::TextDisabled("%s", note.c_str());

  ImGui::SeparatorText("The look");
  studio::SliderFloatW("Realism", &s.realism, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("1 is a photograph: hydrogen's crimson, doubly-ionised\n"
                      "oxygen's teal where the gas is hardest lit, brown dust,\n"
                      "the colours a camera records.\n"
                      "0 is what a film paints: cyan and magenta, gold cores,\n"
                      "a glow round everything.\n"
                      "It grades what is already in the sky, and it chooses\n"
                      "the colours anything new is born with.");
  studio::SliderFloatW("Brightness", &s.brightness, 0.f, 4.f);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Everything beyond the air at once.");
  studio::SliderFloatW("Glow", &s.glow, 0.f, 2.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The halo a long exposure spreads round bright gas.");
  ImGui::Combo("Quality", &s.quality, "Draft\0Normal\0Fine\0Exhaustive\0");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How far the march through a nebula steps: 12, 22, 40\n"
                      "or 72 samples. Only the pixels a nebula covers pay for\n"
                      "it, and only where space is visible at all - a daylit\n"
                      "scene costs nothing.");

  ImGui::SeparatorText("Stars");
  studio::Checkbox("Star field", &s.stars);
  studio::SliderFloatW("Star density", &s.star_density, 0.f, 1.f);
  studio::SliderFloatW("Star brightness", &s.star_brightness, 0.f, 4.f);
  studio::SliderFloatW("Star size", &s.star_size, 0.f, 4.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How large a star's spot is, in pixels. A bright star is\n"
                      "mostly brighter than a faint one, not much bigger.");
  studio::SliderFloatW("Colour spread", &s.star_temperature, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("0 every star white; 1 the full run of the Planckian\n"
                      "locus, from a red dwarf's orange to a blue giant's.");
  studio::SliderFloatW("Diffraction spikes", &s.star_spikes, 0.f, 2.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The arms a telescope's vanes cut across the brightest\n"
                      "stars. 0 for the naked eye, which sees none.");
  {
    int pts = s.star_spike_points <= 4 ? 0 : (s.star_spike_points <= 6 ? 1 : 2);
    if (ImGui::Combo("Spike points", &pts, "4 (two vanes)\0" "6 (three vanes)\0" "8 (four vanes)\0"))
      s.star_spike_points = 4 + 2 * pts;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How many arms every spike has: four behind a two-vane\n"
                        "spider, six behind a hexagonal mirror, eight behind a\n"
                        "four-vane one.");
  }
  studio::SliderFloatW("Spike angle", &s.star_spike_angle, -90.f, 90.f, "%.0f\xC2\xB0");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The angle the arms lie at. The same for every star: the\n"
                      "spikes belong to the camera, not to the stars.");
  studio::SliderFloatW("Spike colour fringe", &s.star_spike_chroma, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Each colour reaches a little further along the arms than\n"
                      "the last, the rainbow a real lens leaves on them.");
  studio::SliderFloatW("Star saturation", &s.star_saturation, 0.f, 2.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How strongly the stars show their colour: 0 all white,\n"
                      "1 as recorded, 2 a film's richer blues and ambers.");
  studio::SliderFloatW("Star glow", &s.star_glow, 0.f, 2.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A wide soft bloom round the brightest stars, the light a\n"
                      "long exposure spreads through the lens.");
  studio::SliderFloatW("Halo", &s.star_halo, 0.f, 2.f);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("The soft ring round a bright star.");
  studio::SliderFloatW("Clumping", &s.star_clump, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Stars gather into associations and leave the sky between\n"
                      "them nearly empty. 0 sprinkles them evenly, which no\n"
                      "real sky does.");
  studio::SliderFloatW("Bright stars", &s.star_bright_share, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How many of the stars are bright ones. A real sky has a very\n"
                      "few bright stars among a great many faint ones; 0 makes the\n"
                      "bright ones rarer still, 1 a crowded, glittering field.");
  studio::SliderFloatW("Star clusters", &s.star_clusters, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Crowds of stars born together, dense at the middle and\n"
                      "thinning to an edge: loose blue open clusters and tight\n"
                      "yellow globular ones. 0 none.");
  studio::SliderFloatW("Cluster size", &s.star_cluster_size, 0.2f, 5.f, "%.1f\xC2\xB0");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How wide a cluster is across the sky, degrees; each one\n"
                      "varies around it.");
  studio::DragIntW("Star seed", &s.star_seed, 1, 1, 1 << 24);

  ImGui::SeparatorText("Galaxy band");
  studio::Checkbox("Milky Way", &s.galaxy_on);
  studio::SliderFloatW("Intensity", &s.galaxy_intensity, 0.f, 4.f);
  studio::SliderFloatW("Width", &s.galaxy_width, 1.f, 60.f, "%.0f\xC2\xB0");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("The band's half-height, degrees.");
  studio::DragFloatW("Pole heading", &s.galaxy_yaw, 0.5f, -360.f, 360.f, "%.1f\xC2\xB0");
  studio::DragFloatW("Pole elevation", &s.galaxy_pitch, 0.5f, -90.f, 90.f, "%.1f\xC2\xB0");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Where the band's pole points: the band is the great circle\n"
                      "90 degrees from it. Elevation 90 lays it along the horizon.");
  studio::DragFloatW("Core position", &s.galaxy_core, 0.5f, -360.f, 360.f, "%.1f\xC2\xB0");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where along the band the bright core sits.");
  studio::SliderFloatW("Dark rifts", &s.galaxy_dust, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The lanes of dust lying in the plane. They do not merely\n"
                      "darken the band, they redden it: dust absorbs blue\n"
                      "hardest, which is why it goes amber where it is thickest.");
  studio::SliderFloatW("Star haze", &s.galaxy_grain, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The stars the band is made of, too faint and too many to\n"
                      "tell apart. This is what makes it milky rather than a\n"
                      "smear of light.");
  ImGui::ColorEdit3("Tint", s.galaxy_color);
  studio::DragIntW("Galaxy seed", &s.galaxy_seed, 1, 1, 1 << 24);
}

} // namespace studio
