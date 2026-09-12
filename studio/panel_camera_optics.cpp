// Geekatplay TerraForge - the camera's optical simulation and framing guides.
// Split from panel_camera.cpp, which is already carrying the exposure
// triangle, the film stocks and the per-camera render assignment.
//
// Vue calls this the Advanced Camera Options (manual p333). The shape is the
// same: one switch that turns a perfect lens into a real one, and every part
// of what a real lens does adjustable underneath - because "cinematic" is a
// look, and a look has to be dialled rather than decreed.
#include "app.hpp"
#include "wheel_widgets.hpp"
#include "gpx/camera_math.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace studio {

bool prop_filter_match(const char *label);

bool camera_optics_ui(App &a, CameraData &cd) {
  bool changed = false;
  if (!prop_filter_match("Optics lens distortion vignette chromatic flare "
                         "shutter motion blur simulation"))
    return false;
  ImGui::SeparatorText("Optical simulation");

  changed |= studio::Checkbox("Full lens simulation", &cd.optics);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "Off, the lens is perfect: straight lines stay straight, the\n"
        "corners are as bright as the middle, and no colour fringes.\n"
        "On, the picture is put through what this lens would do to it.");
  if (!cd.optics) {
    ImGui::TextDisabled("A perfect lens. Nothing below is applied.");
    return changed;
  }

  int nf = 0;
  const gpx::cam::SensorFormat *F = gpx::cam::sensor_formats(&nf);
  const float sensor_w = F ? F[std::clamp(cd.format, 0, nf - 1)].width_mm : 36.f;
  const float autok = gpx::cam::lens_distortion_k1(cd.focal_mm, sensor_w);

  // ---- distortion, which is the part that follows the focal length
  changed |= studio::Checkbox("Distortion from focal length",
                              &cd.distortion_auto);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A wide lens bows straight lines outward (barrel), a\n"
                      "long lens bows them inward (pincushion). Untick to\n"
                      "dial a particular lens by hand.");
  if (cd.distortion_auto) {
    ImGui::TextDisabled("%.0f mm on %s: %s %.1f%%", cd.focal_mm,
                        F ? F[std::clamp(cd.format, 0, nf - 1)].name : "35mm",
                        autok > 0.f   ? "barrel"
                        : autok < 0.f ? "pincushion"
                                      : "rectilinear",
                        std::fabs(autok) * 100.f);
  } else {
    ImGui::SetNextItemWidth(-140);
    changed |= studio::SliderFloatW("Distortion", &cd.distortion, -0.10f, 0.20f,
                                  "%.3f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Positive barrels, negative pincushions.\n"
                        "This lens would be %.3f", autok);
  }

  // ---- vignetting: what the aperture implies, scaled by taste
  ImGui::SetNextItemWidth(-140);
  changed |= studio::SliderFloatW("Vignetting", &cd.vignette, 0.f, 2.f, "%.2f");
  ImGui::TextDisabled("f/%.1f falls off %.0f%% in the corners at 1.00",
                      cd.aperture, gpx::cam::lens_vignette(cd.aperture) * 100.f);

  // ---- chromatic aberration: off until asked for
  ImGui::SetNextItemWidth(-140);
  changed |= studio::SliderFloatW("Chromatic aberration", &cd.chromatic, 0.f, 2.f,
                                "%.2f");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Lateral fringing: the channels focus at slightly\n"
                      "different scales, so colour separates toward the\n"
                      "corners and vanishes in the middle. 0 is off.");

  // ---- shutter-driven motion blur
  ImGui::SetNextItemWidth(-140);
  changed |= studio::SliderFloatW("Shutter blur", &cd.motion_blur, 0.f, 1.f,
                                "%.2f");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "How much of the camera's own movement the shutter smears into\n"
        "the frame, scaled by the shutter speed above. The viewport blurs\n"
        "what the camera did, not what moved in front of it; the render\n"
        "engines get the shutter itself and blur both.");

  // ---- flare, optional and tied to the sun actually being in shot
  changed |= studio::Checkbox("Lens flare", &cd.flare);
  if (cd.flare) {
    ImGui::SetNextItemWidth(-140);
    changed |= studio::SliderFloatW("Flare strength", &cd.flare_strength, 0.f,
                                  1.5f, "%.2f");
    static const char *styles[] = {"Classic ghosts", "Cinematic", "Anamorphic"};
    ImGui::SetNextItemWidth(-140);
    changed |= ImGui::Combo("Flare style", &cd.flare_style, styles, 3);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Classic: a few ghosts and a halo. Cinematic: a hot\n"
                        "core, a starburst of rays, a ring and a chain of\n"
                        "tinted reflections. Anamorphic: the same through an\n"
                        "anamorphic lens, with a streak across the frame.");
    if (cd.flare_style > 0) {
      ImGui::SetNextItemWidth(-140);
      changed |= studio::SliderFloatW("Rays", &cd.flare_rays, 0.f, 2.f, "%.2f");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The starburst: thin rays of different lengths from\n"
                          "the aperture's blades, faintly coloured at the tips.");
      ImGui::SetNextItemWidth(-140);
      changed |= studio::SliderFloatW("Streak", &cd.flare_streak, 0.f, 2.f, "%.2f");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The horizontal streak an anamorphic lens draws\n"
                          "through a bright light; long across the frame in\n"
                          "the Anamorphic style, short in the Cinematic one.");
      ImGui::SetNextItemWidth(-140);
      changed |= studio::SliderFloatW("Ghosts", &cd.flare_ghosts, 0.f, 2.f, "%.2f");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The reflections between the lens elements: tinted\n"
                          "discs in the aperture's shape, strung along the line\n"
                          "from the sun through the middle of the frame.");
      ImGui::SetNextItemWidth(-140);
      changed |= studio::SliderFloatW("Halo", &cd.flare_halo, 0.f, 2.f, "%.2f");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The thin coloured ring about the sun.");
      if (ImGui::TreeNode("Flare shape")) {
        ImGui::SetNextItemWidth(-140);
        changed |= studio::SliderFloatW("Core", &cd.flare_core, 0.f, 2.f, "%.2f");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("The white-hot middle and the bloom round it.");
        ImGui::SetNextItemWidth(-140);
        changed |= studio::SliderIntW("Ray count", &cd.flare_ray_count, 4, 64);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("How many long rays the starburst throws; a set of\n"
                            "short ones between them follows the same count.");
        ImGui::SetNextItemWidth(-140);
        changed |= studio::SliderFloatW("Ray length", &cd.flare_ray_length, 0.2f, 3.f, "%.2f");
        ImGui::SetNextItemWidth(-140);
        changed |= studio::SliderFloatW("Streak length", &cd.flare_streak_length, 0.1f, 3.f, "%.2f");
        ImGui::SetNextItemWidth(-140);
        changed |= ImGui::ColorEdit3("Streak tint", cd.flare_streak_tint);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("The colour the streak cools to, and of the faint\n"
                            "parallel lines an anamorphic lens adds: blue for most\n"
                            "anamorphic glass, amber for some.");
        ImGui::SetNextItemWidth(-140);
        changed |= studio::SliderFloatW("Halo radius", &cd.flare_halo_radius, 0.05f, 0.5f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The ring's radius, in frame heights.");
        ImGui::SetNextItemWidth(-140);
        changed |= studio::SliderIntW("Ghost count", &cd.flare_ghost_count, 0, 12);
        ImGui::SetNextItemWidth(-140);
        changed |= studio::SliderIntW("Aperture blades", &cd.flare_blades, 5, 9);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("The ghosts take the aperture's shape: five blades a\n"
                            "pentagon, six a hexagon, nine nearly round.");
        ImGui::SetNextItemWidth(-140);
        changed |= studio::SliderFloatW("Colour parting", &cd.flare_chroma, 0.f, 2.f, "%.2f");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("How far the colours separate on the ring and at the\n"
                            "tips of the rays - the glass's dispersion.");
        ImGui::SetNextItemWidth(-140);
        changed |= studio::DragIntW("Flare seed", &cd.flare_seed, 1, 0, 1 << 20);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Another arrangement of rays and ghosts, the same lens.");
        ImGui::TreePop();
      }
    }
    ImGui::TextDisabled("Only while the sun is in frame, and dimmed by whatever\n"
                        "stands in front of it.");
  }

  // ---- bloom, the glow round everything bright
  ImGui::SetNextItemWidth(-140);
  changed |= studio::SliderFloatW("Bloom", &cd.bloom, 0.f, 2.f, "%.2f");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The glow a lens and a sensor spread round everything\n"
                      "bright: the sun, a glint on water, snow in sunlight, the\n"
                      "brightest stars. 0 is off.");
  if (cd.bloom > 0.f) {
    ImGui::SetNextItemWidth(-140);
    changed |= studio::SliderFloatW("Bloom from", &cd.bloom_threshold, 0.f, 0.99f, "%.2f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How bright a part of the picture has to be before it\n"
                        "glows, as a share of white. Lower and the whole bright\n"
                        "sky softens; higher and only the sun and the glints do.");
    ImGui::SetNextItemWidth(-140);
    changed |= studio::SliderFloatW("Bloom size", &cd.bloom_size, 0.f, 1.f, "%.2f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How far the glow spreads: a tight halo at 0, a wide\n"
                        "haze across the frame at 1.");
  }

  (void)a;
  return changed;
}

// Vue's camera panel copies settings between cameras (p333); so does this.
// Everything that describes the lens and the film travels; where the camera
// stands and what it looks at does not, because that is the shot, not the
// camera.
bool camera_copy_ui(App &a, int this_index) {
  if (!prop_filter_match("Copy settings cameras")) return false;
  SceneState &sc = scene();
  int others = 0;
  for (const SceneObject &o : sc.objects)
    if (o.type == SceneObject::Camera) ++others;
  if (others < 2) return false;

  ImGui::SeparatorText("Copy settings");
  bool changed = false;
  if (ImGui::Button("Copy to every other camera")) {
    const CameraData &src = sc.objects[(size_t)this_index].cam;
    int n = 0;
    for (size_t i = 0; i < sc.objects.size(); ++i) {
      if ((int)i == this_index || sc.objects[i].type != SceneObject::Camera)
        continue;
      CameraData &dst = sc.objects[i].cam;
      // the lens and the film, not the shot
      dst.focal_mm = src.focal_mm;
      dst.format = src.format;
      dst.aperture = src.aperture;
      dst.shutter = src.shutter;
      dst.iso = src.iso;
      dst.film = src.film;
      dst.optics = src.optics;
      dst.distortion_auto = src.distortion_auto;
      dst.distortion = src.distortion;
      dst.vignette = src.vignette;
      dst.chromatic = src.chromatic;
      dst.flare = src.flare;
      dst.flare_strength = src.flare_strength;
      dst.flare_style = src.flare_style;
      dst.flare_rays = src.flare_rays;
      dst.flare_streak = src.flare_streak;
      dst.flare_ghosts = src.flare_ghosts;
      dst.flare_halo = src.flare_halo;
      dst.flare_core = src.flare_core;
      dst.flare_ray_count = src.flare_ray_count;
      dst.flare_ray_length = src.flare_ray_length;
      dst.flare_streak_length = src.flare_streak_length;
      for (int k = 0; k < 3; ++k) dst.flare_streak_tint[k] = src.flare_streak_tint[k];
      dst.flare_halo_radius = src.flare_halo_radius;
      dst.flare_ghost_count = src.flare_ghost_count;
      dst.flare_blades = src.flare_blades;
      dst.flare_chroma = src.flare_chroma;
      dst.flare_seed = src.flare_seed;
      dst.bloom = src.bloom;
      dst.bloom_threshold = src.bloom_threshold;
      dst.bloom_size = src.bloom_size;
      dst.motion_blur = src.motion_blur;
      ++n;
    }
    a.status = "copied lens and film to " + std::to_string(n) +
               (n == 1 ? " camera" : " cameras");
    changed = true;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Focal length, format, exposure, film and the optical\n"
                      "simulation. Position and target stay as they are.");
  return changed;
}

} // namespace studio
