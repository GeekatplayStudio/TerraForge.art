// Geekatplay TerraForge - Properties: the selected object, and the scene.
//
// Every length here is a real one. A world unit is the home terrain tile,
// which is render_settings().terrain_size_m metres across, and nothing in
// this file shows a bare fraction of it: positions, sizes and altitudes are
// converted to metres (or feet) and given a unit that reads well at the size
// in question, so a 30 cm rock is "30 cm" and not "0.0003".
#include "app.hpp"
#include "ai_assist.hpp"
#include "icons.hpp"
#include "planet_place.hpp"
#include "prop_lengths.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <mutex>
#include <vector>
#include <string>

namespace studio {

namespace {

// ------------------------------------------------------------- transform
// Position, rotation and size, in the same block for every object that has
// one. Rotation is HPB (heading about Y, pitch about X, bank about Z) - the
// Cinema 4D convention, and the reason "tilt" is a number here rather than
// something you can only do by dragging in a viewport.
void transform_ui(App &a, SceneObject &o) {
  (void)a;
  const float hs = render_settings().height_scale;
  // the lock, and then everything below reads but does not write
  {
    bool locked = o.locked;
    if (studio::Checkbox("Locked", &locked)) o.locked = locked;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("No gizmo, no dragging, and these fields are read-only\n"
                        "until unlocked. Also in the Objects tree.");
  }
  ImGui::BeginDisabled(o.locked);
  struct EndDisabledAtExit {
    ~EndDisabledAtExit() { ImGui::EndDisabled(); }
  } end_disabled_at_exit;
  if (prop_filter_match("Position")) {
    ImGui::SeparatorText("Position");
    drag_length("X", &o.pos[0]);
    drag_length("Altitude", &o.pos[1], hs);
    drag_length("Z", &o.pos[2]);
  }
  if (prop_filter_match("Rotation")) {
    ImGui::SeparatorText("Rotation");
    ImGui::DragFloat("Heading", &o.yaw, 0.5f, -180.f, 180.f, "%.1f°");
    ImGui::DragFloat("Pitch", &o.pitch, 0.5f, -180.f, 180.f, "%.1f°");
    ImGui::DragFloat("Bank", &o.roll, 0.5f, -180.f, 180.f, "%.1f°");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Heading turns about the vertical axis, pitch tips the\n"
                        "nose up and down, bank rolls it - applied in that\n"
                        "order (HPB).");
  }
  if (prop_filter_match("Size")) {
    ImGui::SeparatorText("Size");
    drag_length("Size", &o.scale, 1.f, 0.0005f, 1e6f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The object's largest dimension, as a real length.");
    ImGui::DragFloat3("Squeeze", o.scl, 0.005f, 0.01f, 20.f, "%.3f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Scales X, Y and Z independently around the size\n"
                        "above: 1, 0.4, 1 flattens without shrinking.");
    float w = o.scale * o.scl[0] * render_settings().terrain_size_m;
    float ht = o.scale * o.scl[1] * render_settings().terrain_size_m;
    float d = o.scale * o.scl[2] * render_settings().terrain_size_m;
    LenUnit u = pick_unit(std::fmax(w, std::fmax(ht, d)));
    ImGui::TextDisabled("%.*f x %.*f x %.*f %s", u.decimals, w * u.per_m,
                        u.decimals, ht * u.per_m, u.decimals, d * u.per_m,
                        u.suffix);
  }
}

} // namespace

// ---- scene object properties (Cinema-4D-style attribute manager) ----------
void object_properties_ui(App &a) {
  SceneState &sc = scene();
  RenderSettings &rs = render_settings();
  const float hs = rs.height_scale;
  if (sc.selected < 0 || sc.selected >= (int)sc.objects.size()) {
    ImGui::TextDisabled("nothing selected");
    return;
  }
  SceneObject &o = sc.objects[sc.selected];
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.55f, 0.24f, 1.f));
  ImGui::Text("%s", o.name.c_str());
  ImGui::PopStyleColor();
  ImGui::SameLine();
  const char *kind = o.type == SceneObject::Terrain ? "Terrain object"
                   : o.type == SceneObject::Water   ? "Water object"
                   : o.type == SceneObject::Sun     ? "Light"
                   : o.type == SceneObject::Atmosphere ? "Environment"
                   : o.type == SceneObject::Camera ? "Camera"
                   : o.type == SceneObject::Group  ? "Group"
                   : o.type == SceneObject::Planet ? "Planet"
                   : o.type == SceneObject::InfiniteSurface ? "Infinite terrain"
                                                   : "Mesh object";
  ImGui::TextDisabled("· %s", kind);
  studio::Checkbox("Visible", &o.visible);
  ImGui::Separator();

  if (o.type == SceneObject::Camera) {
    camera_properties_ui(a, o);
    return;
  }
  if (o.type == SceneObject::Group) {
    ImGui::TextDisabled("Group — expand it in the Outliner to reach its members.");
    int n = 0;
    for (const auto &c : sc.objects)
      if (&c != &o && c.parent >= 0 && &sc.objects[c.parent] == &o) ++n;
    ImGui::TextDisabled("%d child object%s", n, n == 1 ? "" : "s");
    return;
  }
  switch (o.type) {
    case SceneObject::Terrain:
      if (prop_filter_match("Size")) {
        ImGui::SeparatorText("Size");
        ImGui::TextUnformatted("Across");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##across", &rs.terrain_size_m, 50.f, 10.f, 1e7f,
                         "%.0f m");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("How much ground the terrain tile covers. Every\n"
                            "other length in the interface is measured\n"
                            "against this.");
        text_length("Highest possible point",
                    rs.height_scale * rs.terrain_size_m);
      }
      ImGui::SeparatorText("Shape");
      labeled_scalar("Height scale", "hs", &rs.height_scale, 0.02f, 0.8f);
      ImGui::TextDisabled("Terrain shape is built in the node graph\n"
                          "(Terrain workspace). Material lives in the\n"
                          "Material tab.");
      ImGui::SeparatorText("Surface");
      labeled_scalar("Roughness", "ro", &rs.mat_roughness, 0.02f, 1.f);
      labeled_scalar("Reflection", "rf", &rs.mat_reflection, 0.f, 1.f);
      if (prop_filter_match("Placement")) {
        // How the tile sits on the planet (studio/planet_place.cpp). The
        // numbers here are what a user can reason about: a length for the
        // feather, a length for what counts as a feature, and how much of
        // the planet survives underneath.
        ImGui::SeparatorText("Placement on planet");
        studio::Checkbox("Place on planet surface", &rs.place_on_planet);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("The planet's own landscape shows through where\n"
                            "this tile is flat, and is levelled under\n"
                            "whatever the graph builds; the joins are\n"
                            "feathered so nothing steps. Off: the tile is\n"
                            "shown exactly as the graph made it.");
        if (rs.place_on_planet) {
          drag_length("Edge blend", &rs.place_edge, 1.f, 0.f,
                      0.5f * rs.terrain_size_m);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How far a feature's footprint, and the tile's\n"
                              "own border, fade into the planet.");
          labeled_scalar("Flatten beneath", "pf", &rs.place_flatten, 0.f, 1.f);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("1: the planet is levelled under a feature, so\n"
                              "a mountain stands on its own ground. 0: the\n"
                              "feature is added on top of the planet's relief.");
          drag_length("Feature threshold", &rs.place_presence, hs, 0.f,
                      rs.height_scale * rs.terrain_size_m);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Anything this far above or below the tile's\n"
                              "ground level counts as a feature - a hole\n"
                              "dug deeper than this becomes a basin, and the\n"
                              "water fills it where it reaches below the\n"
                              "water level.");
          drag_length("Ground level", &rs.place_ground, hs, -hs * rs.terrain_size_m,
                      hs * rs.terrain_size_m);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The planet's ground level: the altitude its\n"
                              "relief is built around, and the level this\n"
                              "tile's own ground is settled to. The water\n"
                              "level (Water object) decides how much of it\n"
                              "is sea.");
          const PlaceResult &pr = planet_place_last();
          if (pr.placed) {
            text_length("Tile ground level",
                        pr.tile_ground * hs * rs.terrain_size_m);
            ImGui::TextDisabled("%.0f%% of the tile is feature, the rest is\n"
                                "the planet showing through.",
                                pr.coverage * 100.f);
          } else {
            ImGui::TextDisabled("No planet surface layers: nothing to place\n"
                                "the tile on.");
          }
        }
      }
      break;
    case SceneObject::Water:
      ImGui::SeparatorText("Level");
      labeled_scalar("Water level", "wl", &rs.water_level, 0.f, 1.f);
      ImGui::TextDisabled("Colors, waves and foam: Material tab.");
      break;
    case SceneObject::Sun:
      ImGui::SeparatorText("Direction");
      ImGui::RadioButton("Manual", &rs.sun_mode, 0);
      ImGui::SameLine();
      ImGui::RadioButton("Location & time", &rs.sun_mode, 1);
      if (rs.sun_mode == 0) {
        labeled_scalar("Azimuth", "az", &rs.sun_azimuth, 0.f, 360.f);
        labeled_scalar("Altitude", "al", &rs.sun_altitude, 1.f, 89.f);
      } else {
        ImGui::DragFloat("Latitude", &rs.latitude, 0.1f, -89.f, 89.f, "%.2f");
        ImGui::DragFloat("Longitude", &rs.longitude, 0.1f, -180.f, 180.f, "%.2f");
        ImGui::DragFloat("UTC offset", &rs.utc_offset, 0.25f, -12.f, 14.f, "%.2f h");
        ImGui::SliderInt("Month", &rs.month, 1, 12);
        ImGui::SliderInt("Day", &rs.day, 1, 31);
        ImGui::SliderFloat("Local time", &rs.hour, 0.f, 24.f, "%.2f h");
      }
      ImGui::SeparatorText("Light");
      ImGui::ColorEdit3("Color", rs.sun_color);
      labeled_scalar("Intensity", "si", &rs.sun_intensity, 0.2f, 8.f);
      studio::Checkbox("Casts shadows", &rs.shadows);
      break;
    case SceneObject::Atmosphere:
      ImGui::SeparatorText("Sky");
      labeled_scalar("Density", "ad", &rs.atmosphere_density, 0.05f, 3.f);
      ImGui::ColorEdit3("Zenith", rs.sky_zenith);
      ImGui::ColorEdit3("Horizon", rs.sky_horizon);
      ImGui::SeparatorText("Fog");
      ImGui::Combo("Type", &rs.fog_type, "Off\0Haze\0Fog\0Pollution\0");
      labeled_scalar("Density", "fd", &rs.fog_density, 0.f, 6.f);
      ImGui::SeparatorText("Clouds");
      studio::Checkbox("Volumetric clouds", &rs.clouds_on);
      labeled_scalar("Coverage", "cc", &rs.cloud_coverage, 0.f, 1.f);
      ImGui::TextDisabled("Full atmosphere controls: Environment tab.");
      break;
    case SceneObject::Light:
      ImGui::SeparatorText("Point light");
      drag_length("X", &o.pos[0]);
      drag_length("Y", &o.pos[1]);
      drag_length("Z", &o.pos[2]);
      ImGui::ColorEdit3("Color", o.color);
      ImGui::SliderFloat("Intensity", &o.light_intensity, 0.f, 10.f);
      ImGui::SliderFloat("Reach", &o.light_radius, 0.01f, 2.f);
      ImGui::Combo("Type", &o.light_type, "Point\0Spot\0");
      if (o.light_type == 1) {
        ImGui::SliderFloat("Cone", &o.light_cone, 5.f, 160.f, "%.0f\xC2\xB0");
        ImGui::SliderFloat("Heading", &o.yaw, -180.f, 180.f, "%.0f\xC2\xB0");
        ImGui::SliderFloat("Pitch", &o.pitch, -90.f, 90.f, "%.0f\xC2\xB0");
        ImGui::TextDisabled("Pitch -90 aims straight down.");
      }
      ImGui::TextDisabled("Lights the terrain and every mesh within reach.");
      break;
    case SceneObject::Mesh:
      transform_ui(a, o);
      if (prop_filter_match("Color")) {
        ImGui::SeparatorText("Surface");
        ImGui::ColorEdit3("Color", o.color);
      }
      if (prop_filter_match("Scatter")) {
        ImGui::SeparatorText("Scatter");
        // pick any node with a point-cloud output; None turns it off
        {
          const char *cur = "None";
          std::string cur_label;
          gpx::Node *curn = a.graph.find_node(o.scatter_node);
          if (curn) {
            cur_label = curn->type + " #" + std::to_string(curn->id);
            cur = cur_label.c_str();
          }
          if (ImGui::BeginCombo("Points node", cur)) {
            if (ImGui::Selectable("None", o.scatter_node == 0)) {
              o.scatter_node = 0;
              o.inst.clear();
            }
            for (auto &cand : a.graph.nodes) {
              bool has_pts = false;
              for (const gpx::Port &p : cand->ports)
                has_pts = has_pts || (p.dir == gpx::PortDir::Out &&
                                      p.type == gpx::DataType::Points);
              if (!has_pts) continue;
              std::string label =
                  cand->type + " #" + std::to_string(cand->id);
              if (ImGui::Selectable(label.c_str(),
                                    o.scatter_node == cand->id)) {
                o.scatter_node = cand->id;
                a.request_eval();
              }
            }
            ImGui::EndCombo();
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies of this mesh appear at every point of\n"
                              "the chosen cloud, standing on the terrain.");
        }
        if (o.scatter_node) {
          bool ch = false;
          ch |= ImGui::SliderFloat("Size", &o.scatter_scale, 0.05f, 4.f);
          ch |= ImGui::SliderFloat("Size jitter", &o.scatter_jitter, 0.f, 1.f);
          ImGui::SliderFloat("Wind sway", &o.scatter_sway, 0.f, 0.3f);
          ch |= ImGui::SliderFloat("Size from value", &o.scatter_value_size,
                                   0.f, 1.f);
          int sd = (int)o.scatter_seed;
          if (ImGui::DragInt("Seed", &sd, 1, 0, 1 << 24)) {
            o.scatter_seed = (unsigned)sd;
            ch = true;
          }
          if (ch) a.request_eval();
          ImGui::TextDisabled("%d copies", (int)(o.inst.size() / 8));
        }
      }
      if (prop_filter_match("Info")) {
        ImGui::SeparatorText("Info");
        ImGui::TextDisabled("%d triangles", o.vert_count / 3);
        ImGui::TextDisabled("%s", o.path.c_str());
      }
      break;
    case SceneObject::Planet:
      object_properties_planet_ui(a, o);
      break;
    case SceneObject::InfiniteSurface:
      object_properties_surface_ui(a, o);
      break;
  }
}

} // namespace studio
