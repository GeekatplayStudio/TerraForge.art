// Geekatplay TerraForge — camera properties: real lens, sensor format,
// exposure triangle, film stock, per-camera render assignment, and a
// through-the-lens preview.
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "gpx/camera_math.hpp"
#include <imgui.h>
#include <cstdio>
#include <string>

namespace studio {

// exposure + film grading of the active camera, pushed to the renderer
void camera_apply_film() {
  SceneState &sc = scene();
  int active = scene_active_camera();
  static const float neutral[3] = {1.f, 1.f, 1.f};
  if (active < 0 || active >= (int)sc.objects.size() ||
      sc.objects[active].type != SceneObject::Camera) {
    renderer_set_film(neutral, 1.f, 1.f);
    return;
  }
  const CameraData &cd = sc.objects[active].cam;
  int nf = 0;
  const gpx::cam::FilmStock *F = gpx::cam::film_stocks(&nf);
  const gpx::cam::FilmStock &f = F[std::clamp(cd.film, 0, nf - 1)];
  float mult = gpx::cam::exposure_multiplier(cd.aperture, cd.shutter, cd.iso);
  renderer_set_film(f.tint, f.saturation, mult);
}

static const char *SHUTTER_LABELS[] = {"1/1000", "1/500", "1/250", "1/125",
                                       "1/60",   "1/30",  "1/15",  "1/8",
                                       "1/4",    "1/2",   "1s",    "2s"};
static const float SHUTTER_VALUES[] = {
    1.f / 1000, 1.f / 500, 1.f / 250, 1.f / 125, 1.f / 60, 1.f / 30,
    1.f / 15,   1.f / 8,   1.f / 4,   1.f / 2,   1.f,      2.f};
static const int SHUTTER_N = 12;

static const float FSTOPS[] = {1.2f, 1.4f, 2.f, 2.8f, 4.f, 5.6f,
                               8.f,  11.f, 16.f, 22.f};
static const int FSTOP_N = 10;

void camera_properties_ui(App &a, SceneObject &obj) {
  CameraData &cd = obj.cam;
  SceneState &sc = scene();
  int self = -1;
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    if (&sc.objects[i] == &obj) self = i;

  // ---- activation ----
  bool is_active = scene_active_camera() == self;
  if (is_active)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
  if (ImGui::Button(is_active ? "Looking through this camera"
                              : "Look through this camera",
                    ImVec2(-1, 0))) {
    scene_active_camera() = is_active ? -1 : self;
    if (!is_active) scene_last_used_camera() = self;
  }
  if (is_active) ImGui::PopStyleColor();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Perspective views render through the active camera.\n"
                      "Click again to return to the free viewport camera.");

  // ---- through-the-lens preview ----
  if (prop_filter_match("Preview")) {
    ImGui::SeparatorText("Through the lens");
    int prev_active = scene_active_camera();
    scene_active_camera() = self; // render this camera regardless of selection
    camera_apply_film();
    float avail = ImGui::GetContentRegionAvail().x;
    int nf = 0;
    const gpx::cam::SensorFormat *FMT = gpx::cam::sensor_formats(&nf);
    const gpx::cam::SensorFormat &fmt = FMT[std::clamp(cd.format, 0, nf - 1)];
    float aspect = fmt.width_mm / std::max(fmt.height_mm, 1e-3f);
    int pw = (int)std::min(avail, 320.f);
    int ph = (int)std::max(pw / std::max(aspect, 0.2f), 40.f);
    RenderSettings::ViewConfig vc = render_settings().views[0];
    vc.camera = 0;
    vc.outlines = false;
    unsigned tex = renderer_draw_view(4, vc, pw, ph, 0.f);
    scene_active_camera() = prev_active;
    ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2((float)pw, (float)ph),
                 ImVec2(0, 1), ImVec2(1, 0));
    ImGui::TextDisabled("%s  %.0fmm  f/%.1f", fmt.name, cd.focal_mm, cd.aperture);
  }

  bool changed = false;

  // ---- lens ----
  if (prop_filter_match("Lens focal")) {
    ImGui::SeparatorText("Lens");
    int nf = 0;
    const gpx::cam::SensorFormat *FMT = gpx::cam::sensor_formats(&nf);
    std::string items;
    for (int i = 0; i < nf; ++i) {
      items += FMT[i].name;
      items += '\0';
    }
    ImGui::SetNextItemWidth(-140);
    changed |= ImGui::Combo("Sensor format", &cd.format, items.c_str());
    ImGui::SetNextItemWidth(-140);
    changed |= ImGui::SliderFloat("Focal length", &cd.focal_mm, 8.f, 400.f,
                                  "%.0f mm", ImGuiSliderFlags_Logarithmic);
    // quick primes
    const float primes[] = {14.f, 24.f, 35.f, 50.f, 85.f, 135.f, 200.f};
    for (float p : primes) {
      char lbl[16];
      snprintf(lbl, sizeof lbl, "%.0f", p);
      if (ImGui::SmallButton(lbl)) {
        cd.focal_mm = p;
        changed = true;
      }
      ImGui::SameLine();
    }
    ImGui::NewLine();
    const gpx::cam::SensorFormat &fmt = FMT[std::clamp(cd.format, 0, nf - 1)];
    ImGui::TextDisabled("%.1f x %.1f mm  |  %.1f deg horizontal, %.1f deg vertical",
                        fmt.width_mm, fmt.height_mm,
                        gpx::cam::fov_x_deg(cd.focal_mm, fmt.width_mm),
                        gpx::cam::fov_y_deg(cd.focal_mm, fmt.height_mm));
  }

  // ---- exposure ----
  if (prop_filter_match("Exposure aperture shutter ISO")) {
    ImGui::SeparatorText("Exposure");
    ImGui::SetNextItemWidth(-140);
    changed |= ImGui::SliderFloat("Aperture", &cd.aperture, 1.f, 32.f, "f/%.1f",
                                  ImGuiSliderFlags_Logarithmic);
    for (int i = 0; i < FSTOP_N; ++i) {
      char lbl[16];
      snprintf(lbl, sizeof lbl, "%.1f", FSTOPS[i]);
      if (ImGui::SmallButton(lbl)) {
        cd.aperture = FSTOPS[i];
        changed = true;
      }
      if (i % 5 != 4) ImGui::SameLine();
    }
    int sh = 3;
    for (int i = 0; i < SHUTTER_N; ++i)
      if (std::fabs(SHUTTER_VALUES[i] - cd.shutter) <
          std::fabs(SHUTTER_VALUES[sh] - cd.shutter))
        sh = i;
    std::string sitems;
    for (int i = 0; i < SHUTTER_N; ++i) {
      sitems += SHUTTER_LABELS[i];
      sitems += '\0';
    }
    ImGui::SetNextItemWidth(-140);
    if (ImGui::Combo("Shutter", &sh, sitems.c_str())) {
      cd.shutter = SHUTTER_VALUES[std::clamp(sh, 0, SHUTTER_N - 1)];
      changed = true;
    }
    ImGui::SetNextItemWidth(-140);
    changed |= ImGui::SliderFloat("ISO", &cd.iso, 25.f, 6400.f, "%.0f",
                                  ImGuiSliderFlags_Logarithmic);
    float ev = gpx::cam::ev100(cd.aperture, cd.shutter, cd.iso);
    float mult = gpx::cam::exposure_multiplier(cd.aperture, cd.shutter, cd.iso);
    ImGui::TextDisabled("EV100 %.1f  |  exposure x%.2f", ev, mult);
    // light meter: how far the triangle sits from a correct daylight exposure
    const float DAYLIGHT_EV = 13.0f; // f/8, 1/125, ISO 100 in open sun
    float stops = DAYLIGHT_EV - ev;
    if (std::fabs(stops) < 0.5f) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.75f, 0.45f, 1.f));
      ImGui::Text("meter: correct for daylight");
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.55f, 0.24f, 1.f));
      ImGui::Text("meter: %+.1f stops (%s for daylight)", stops,
                  stops > 0 ? "over" : "under");
    }
    ImGui::PopStyleColor();
    if (std::fabs(stops) >= 0.5f) {
      ImGui::SameLine();
      if (ImGui::SmallButton("balance")) {
        // keep the chosen aperture and ISO, solve for the shutter
        float want = cd.aperture * cd.aperture /
                     (std::pow(2.f, DAYLIGHT_EV) * (cd.iso / 100.f));
        cd.shutter = std::clamp(want, 1.f / 8000.f, 30.f);
        changed = true;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Solve the shutter speed for a correct daylight\n"
                          "exposure at the current aperture and ISO.");
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Aperture, shutter and ISO drive image brightness\n"
                        "exactly like a real camera: f/8, 1/125s, ISO 100\n"
                        "is the neutral reference.");
  }

  // ---- film ----
  if (prop_filter_match("Film stock")) {
    ImGui::SeparatorText("Film");
    int nfilm = 0;
    const gpx::cam::FilmStock *F = gpx::cam::film_stocks(&nfilm);
    std::string fitems;
    for (int i = 0; i < nfilm; ++i) {
      fitems += F[i].name;
      fitems += '\0';
    }
    ImGui::SetNextItemWidth(-140);
    changed |= ImGui::Combo("Stock", &cd.film, fitems.c_str());
    const gpx::cam::FilmStock &f = F[std::clamp(cd.film, 0, nfilm - 1)];
    ImGui::TextDisabled("tint %.2f/%.2f/%.2f  saturation %.2f  grain %.2f",
                        f.tint[0], f.tint[1], f.tint[2], f.saturation, f.grain);
  }

  // ---- transform ----
  if (prop_filter_match("Position transform")) {
    ImGui::SeparatorText("Transform");
    ImGui::SetNextItemWidth(-140);
    changed |= ImGui::DragFloat3("Position", cd.eye, 0.01f);
    ImGui::SetNextItemWidth(-140);
    changed |= ImGui::DragFloat3("Look at", cd.target, 0.01f);
    if (ImGui::Button("Frame the terrain", ImVec2(-1, 0))) {
      cd.target[0] = 0.5f;
      cd.target[1] = render_settings().height_scale * 0.4f;
      cd.target[2] = 0.5f;
      cd.eye[0] = 0.5f;
      cd.eye[1] = render_settings().height_scale * 1.6f;
      cd.eye[2] = 2.1f;
      changed = true;
    }
  }

  // ---- per-camera render ----
  if (prop_filter_match("Render output")) {
    ImGui::SeparatorText("Render assigned to this camera");
    ImGui::SetNextItemWidth(-140);
    ImGui::Combo("Engine", &cd.render.engine,
                 "Mitsuba 3 (path tracer)\0Blender Cycles\0LuxCoreRender\0"
                 "appleseed\0OpenGL viewport (instant)\0");
    ImGui::SetNextItemWidth(-140);
    ImGui::InputInt("Width", &cd.render.width);
    ImGui::SetNextItemWidth(-140);
    ImGui::InputInt("Height", &cd.render.height);
    ImGui::SetNextItemWidth(-140);
    ImGui::SliderInt("Samples", &cd.render.samples, 8, 1024);
    char buf[512];
    snprintf(buf, sizeof buf, "%s", cd.render.output.c_str());
    ImGui::SetNextItemWidth(-140);
    if (ImGui::InputText("Output file", buf, sizeof buf)) cd.render.output = buf;
    if (ImGui::Button("Render this camera", ImVec2(-1, 0))) {
      scene_active_camera() = self;
      scene_last_used_camera() = self;
      a.request_camera_render = self;
    }
    ImGui::TextDisabled("Each camera keeps its own engine, format and file.");
  }

  if (changed) scene_last_used_camera() = self;
}

} // namespace studio
