// Geekatplay TerraForge - the camera's optical simulation and framing guides.
// Split from panel_camera.cpp, which is already carrying the exposure
// triangle, the film stocks and the per-camera render assignment.
//
// Vue calls this the Advanced Camera Options (manual p333). The shape is the
// same: one switch that turns a perfect lens into a real one, and every part
// of what a real lens does adjustable underneath - because "cinematic" is a
// look, and a look has to be dialled rather than decreed.
#include "app.hpp"
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
    changed |= ImGui::SliderFloat("Distortion", &cd.distortion, -0.10f, 0.20f,
                                  "%.3f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Positive barrels, negative pincushions.\n"
                        "This lens would be %.3f", autok);
  }

  // ---- vignetting: what the aperture implies, scaled by taste
  ImGui::SetNextItemWidth(-140);
  changed |= ImGui::SliderFloat("Vignetting", &cd.vignette, 0.f, 2.f, "%.2f");
  ImGui::TextDisabled("f/%.1f falls off %.0f%% in the corners at 1.00",
                      cd.aperture, gpx::cam::lens_vignette(cd.aperture) * 100.f);

  // ---- chromatic aberration: off until asked for
  ImGui::SetNextItemWidth(-140);
  changed |= ImGui::SliderFloat("Chromatic aberration", &cd.chromatic, 0.f, 2.f,
                                "%.2f");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Lateral fringing: the channels focus at slightly\n"
                      "different scales, so colour separates toward the\n"
                      "corners and vanishes in the middle. 0 is off.");

  // ---- shutter-driven motion blur
  ImGui::SetNextItemWidth(-140);
  changed |= ImGui::SliderFloat("Shutter blur", &cd.motion_blur, 0.f, 1.f,
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
    changed |= ImGui::SliderFloat("Flare strength", &cd.flare_strength, 0.f,
                                  1.5f, "%.2f");
    ImGui::TextDisabled("Ghosts and a halo, only while the sun is in frame.");
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
