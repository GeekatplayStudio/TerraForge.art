// Geekatplay TerraForge - Properties: the selected object, and the scene.
//
// Every length here is a real one. A world unit is the home terrain tile,
// which is render_settings().terrain_size_m metres across, and nothing in
// this file shows a bare fraction of it: positions, sizes and altitudes are
// converted to metres (or feet) and given a unit that reads well at the size
// in question, so a 30 cm rock is "30 cm" and not "0.0003".
#include "anim_widgets.hpp"
#include "app.hpp"
#include "gizmo.hpp"
#include "imprint.hpp"
#include "toolbar_internal.hpp"
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
  // the animation circle before each keyable row; an edit with Autokey on
  // writes the key (anim_widgets.hpp)
  auto circ = [&](const char *path, int comp) {
    if (const AnimProp *p = anim_find_prop(o, path)) anim_circle(a, o, *p, comp);
  };
  auto autokey = [&](const char *path, int comp) {
    if (ImGui::IsItemDeactivatedAfterEdit())
      if (const AnimProp *p = anim_find_prop(o, path)) anim_autokey(a, o, *p, comp);
  };
  if (prop_filter_match("Position")) {
    ImGui::SeparatorText("Position");
    circ("pos", 0); drag_length("X", &o.pos[0]); autokey("pos", 0);
    circ("pos", 1); drag_length("Altitude", &o.pos[1], hs); autokey("pos", 1);
    circ("pos", 2); drag_length("Z", &o.pos[2]); autokey("pos", 2);
  }
  if (prop_filter_match("Rotation")) {
    ImGui::SeparatorText("Rotation");
    circ("rot", 0); ImGui::DragFloat("Heading", &o.yaw, 0.5f, -180.f, 180.f, "%.1f°"); autokey("rot", 0);
    circ("rot", 1); ImGui::DragFloat("Pitch", &o.pitch, 0.5f, -180.f, 180.f, "%.1f°"); autokey("rot", 1);
    circ("rot", 2); ImGui::DragFloat("Bank", &o.roll, 0.5f, -180.f, 180.f, "%.1f°"); autokey("rot", 2);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Heading turns about the vertical axis, pitch tips the\n"
                        "nose up and down, bank rolls it - applied in that\n"
                        "order (HPB).");
  }
  if (prop_filter_match("Size")) {
    ImGui::SeparatorText("Size");
    circ("scale", -1); drag_length("Size", &o.scale, 1.f, 0.0005f, 1e6f); autokey("scale", -1);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The object's largest dimension, as a real length.");
    circ("scl", -1); ImGui::DragFloat3("Squeeze", o.scl, 0.005f, 0.01f, 20.f, "%.3f"); autokey("scl", -1);
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
  if (o.type == SceneObject::Mesh && prop_filter_match("Deform")) {
    // Vue's Twist in the Numerics tab, and the bend, skew and taper the
    // gizmos drag: the same numbers, typed
    ImGui::SeparatorText("Deform");
    gpx::Deform &d = o.deform;
    circ("deform.twist", -1); ImGui::DragFloat3("Twist", d.twist, 0.5f, -720.f, 720.f, "%.1f°"); autokey("deform.twist", -1);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Turns the far end about each axis; the base stays. Vue's Twist.");
    circ("deform.bend", -1); ImGui::DragFloat("Bend", &d.bend, 0.5f, -180.f, 180.f, "%.1f°"); autokey("deform.bend", -1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::Combo("##bendaxis", &d.bend_axis, "X\0Y\0Z\0");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The axis the object curls around, from its base.");
    circ("deform.shear", -1); ImGui::DragFloat3("Skew", d.shear, 0.005f, -4.f, 4.f, "%.3f"); autokey("deform.shear", -1);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("X and Z slide the top sideways by that fraction of the width;\nY slides one side up.");
    circ("deform.taper", -1); ImGui::DragFloat("Taper", &d.taper, 0.005f, -1.f, 3.f, "%.3f"); autokey("deform.taper", -1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("-1 brings the top to a point; 1 doubles it.");
    if (!d.identity() && ImGui::SmallButton("Reset deformations")) d = gpx::Deform();
  }
  // The transform tool, with the object it acts on: Move, Rotate, Scale,
  // the deformers for a mesh, and this object's own gizmo switch. The same
  // buttons are in the left tool column; here they sit beside the numbers
  // they drive.
  // Built-in primitives can be rebuilt at any resolution. They used to be
  // fixed at 24 segments, which is a sphere whose facets you can count as
  // soon as it fills any part of the frame - and which has nowhere near
  // enough vertices for a displacement material to have anything to move.
  if (o.type == SceneObject::Mesh && o.path.rfind("primitive:", 0) == 0 &&
      prop_filter_match("Resolution")) {
    ImGui::SeparatorText("Resolution");
    int detail = o.primitive_detail;
    // A drag with a floor and no ceiling. The only cap is inside the
    // generator, at a size that would exhaust memory, and it says so.
    if (ImGui::DragInt("Segments", &detail, 0.5f, 3, 0)) {
      detail = std::max(detail, 3);
      if (detail != o.primitive_detail &&
          scene_primitive_verts(o.path.substr(10), o.verts, detail)) {
        o.primitive_detail = detail;
        o.vert_count = (int)(o.verts.size() / 6);
        o.gpu_dirty = true;
      }
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How finely the shape is built: segments round a round\n"
                        "one, grid squares across a flat one. Raise it before\n"
                        "putting a displacement material on the object - a\n"
                        "displacement can only move vertices that are there.");
    ImGui::TextDisabled("%d triangles", o.vert_count / 3);
  }
  ImGui::SeparatorText("Transform tool");
  gizmo_transform_tools();
  if (o.type == SceneObject::Mesh) {
    tool_sep();
    gizmo_deform_tools();
  }
  ImGui::NewLine();
  gizmo_space_tools();
  tool_sep();
  if (tool_icon(Icon::Fit, "##objgizmo",
                "Show the gizmo on this object\n\nUnder the global Gizmos switch (Ctrl+G).",
                o.show_gizmo))
    o.show_gizmo = !o.show_gizmo;
  ImGui::NewLine();
  ImGui::TextDisabled("tool: %s, %s coordinates", gizmo_mode_name(gizmo_mode()),
                      gizmo_space_name(gizmo_space()));
}

} // namespace

// ---------------------------------------------------------------- ground
// A mesh on the terrain: place it there, hold its base on the surface, and
// the settings of the TerrainImprint node that moulds the ground to it.
void ground_ui(App &a, SceneObject &o) {
  ImGui::SeparatorText("Ground");
  const int ground = imprint_ground_of(o);
  if (ground < 0) {
    if (ImGui::Button("Place on terrain")) {
      int idx = imprint_place_on_terrain(a, scene().selected);
      if (idx < 0) a.status = "no terrain object to place it on";
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Make this object a child of the terrain. It then stands on\n"
                        "the surface and the ground moulds itself to its base:\n"
                        "flat under it, blended around it.");
    return;
  }
  bool lock = o.ground_lock;
  if (studio::Checkbox("Locked to the surface", &lock)) o.ground_lock = lock;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The base follows the ground as the object is moved across it.\n"
                      "Drag it up or down to set how far above or below the ground\n"
                      "it sits; the ground rises or dips to meet it.");
  const float m = std::max(render_settings().height_scale, 1e-5f) * render_settings().terrain_size_m;
  const float tile_m = render_settings().terrain_size_m;
  // Drag speeds scaled to the world, not to a number somebody typed once.
  // A flat margin whose slider moves 0.1 m per pixel needs five thousand
  // pixels of dragging to reach 500 m on a 5 km tile, which is why these
  // controls read as though they had no range: they had the range and no
  // way to get there.
  const float dh = std::max(m * 0.002f, 0.01f);   // heights
  const float dt = std::max(tile_m * 0.002f, 0.01f); // distances across the tile

  float off_m = o.ground_offset * m;
  if (ImGui::DragFloat("Height over surface", &off_m, dh, -1e6f, 1e6f, "%.2f m"))
    o.ground_offset = off_m / m;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Where the object sits relative to the ground under it.\n"
                      "Zero rests its base on the natural surface; negative\n"
                      "puts it below, positive above.\n\n"
                      "The Y in Transform is the same height by another name:\n"
                      "set either and the other follows. Whether the ground\n"
                      "comes with it is the two settings further down.");

  // The gap. An object seated on the highest ground under its footprint
  // never intersects the terrain - and on any slope that means it touches at
  // one corner and hangs over the rest, which reads as floating.
  float sunk_m = o.ground_sunk * m;
  if (ImGui::DragFloat("Sink into the ground", &sunk_m, dh, 0.f, 0.f, "%.2f m"))
    o.ground_sunk = sunk_m / m;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Moves the object without the terrain following.\n\n"
                      "Positive sinks it into the ground, negative lifts it\n"
                      "out. Either way the terrain stays exactly as it was:\n"
                      "no hollow under a buried object, no mound under a\n"
                      "raised one. Sink a boulder a hundred metres and the\n"
                      "hill it is in does not change.\n\n"
                      "At zero it sits on the highest ground under its base,\n"
                      "so on a slope it touches at one corner and hangs over\n"
                      "the rest. Sink it by the unevenness shown beside this\n"
                      "and it touches everywhere.\n\n"
                      "Signed and unlimited. The height above, by contrast,\n"
                      "is one the ground DOES follow.");
  ImGui::SameLine();
  ImGui::TextDisabled("(ground under it varies by %.2f m)", o.ground_uneven * m);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Highest minus lowest ground under the base. Sink by at\n"
                      "least this much and no corner is left in the air.");
  float margin_m = o.ground_margin * tile_m;
  if (ImGui::DragFloat("Flat margin", &margin_m, dt, 0.f, 1e6f, "%.2f m"))
    o.ground_margin = std::max(margin_m, 0.f) / tile_m;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How far past the base's walls the flat patch reaches -\n"
                      "the ground under the whole base, and a little around it.\n"
                      "Widen it for a terrace, a courtyard, a road bed.");
  float blend_m = o.ground_blend * tile_m;
  if (ImGui::DragFloat("Blend distance", &blend_m, dt, 0.f, 1e6f, blend_m > 0.f ? "%.2f m" : "auto"))
    o.ground_blend = std::max(blend_m, 0.f) / tile_m;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How far around the object the ground responds. Zero lets the\n"
                      "TerrainImprint node choose a multiple of the footprint's size.");
  float sink_m = o.ground_sink * m;
  if (ImGui::DragFloat("May sink", &sink_m, dh, 0.f, 1e6f, "%.2f m"))
    o.ground_sink = std::max(sink_m, 0.f) / m;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A dead band: the ground is left alone while the object sits\n"
                      "no deeper than this into it. A boulder half buried keeps the\n"
                      "slope it settled into; zero makes the ground meet the base\n"
                      "exactly.");

  // How far the ground is allowed to travel to meet the object at all. This
  // is what makes "put it where I said" possible: with both at zero the
  // object holds its height and the terrain simply passes through it.
  ImGui::Separator();
  const float BIG = 1e8f;
  bool free_lift = o.ground_lift >= BIG, free_dig = o.ground_dig >= BIG;
  float lift_m = free_lift ? 0.f : o.ground_lift * m;
  float dig_m = free_dig ? 0.f : o.ground_dig * m;

  if (studio::Checkbox("Ground may rise to meet it", &free_lift))
    o.ground_lift = free_lift ? 1e9f : std::max(lift_m, 0.f) / m;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("On: lifting the object raises a mound under it.\n"
                      "Off, or limited below: the object hangs in the air where\n"
                      "you put it - an arch, a bridge deck, a boulder perched on\n"
                      "a ledge.");
  if (!free_lift) {
    if (ImGui::DragFloat("  Rise at most", &lift_m, dh, 0.f, 1e6f, "%.2f m"))
      o.ground_lift = std::max(lift_m, 0.f) / m;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The mound reaches this far up and no further. Past it the\n"
                        "object floats and the ground keeps its own shape.");
  }
  if (studio::Checkbox("Ground may dig out under it", &free_dig))
    o.ground_dig = free_dig ? 1e9f : std::max(dig_m, 0.f) / m;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("On: lowering the object hollows the ground out around it.\n"
                      "Off, or limited below: the object stays buried where you put\n"
                      "it and the ground closes over it - a half-sunk ruin, a rock\n"
                      "with only its cap showing.");
  if (!free_dig) {
    if (ImGui::DragFloat("  Dig at most", &dig_m, dh, 0.f, 1e6f, "%.2f m"))
      o.ground_dig = std::max(dig_m, 0.f) / m;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The hollow reaches this far down and no further. Past it\n"
                        "the object is simply buried.");
  }
  ImGui::Separator();
  // the node's settings, right here where the object is
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  gpx::Node *node = nullptr;
  if (lk.owns_lock())
    for (auto &n : a.graph.nodes)
      if (n->type == "TerrainImprint") node = n.get();
  if (!node) {
    ImGui::TextDisabled("The TerrainImprint node appears in the graph on the\nnext frame.");
    return;
  }
  ImGui::TextDisabled("Terrain blending (TerrainImprint node)");
  bool changed = false;
  auto slider = [&](const char *key, const char *label, const char *tip) {
    gpx::Attribute *at = node->attrs.find(key);
    if (!at) return;
    float v = at->f;
    if (ImGui::SliderFloat(label, &v, at->fmin, at->fmax)) {
      at->f = v;
      changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
  };
  slider("width", "Blend width", "How far around an object the ground responds, as a multiple\nof its footprint - for objects whose Blend distance is auto.");
  slider("smoothness", "Smoothness", "0 is a firm shoulder, 1 a long soft tail.");
  slider("retain", "Keep relief", "How much of the ground's own small relief survives\ninside the blend.");
  slider("flatten", "Flatten under", "How flat the ground is made under the object itself.");
  slider("strength", "Strength", "Dial the whole effect back without losing it.");
  if (changed) {
    a.graph.mark_dirty(node->id);
    a.request_eval();
  }
}

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
  if (const AnimProp *vp = anim_find_prop(o, "visible")) anim_circle(a, o, *vp, -1);
  if (studio::Checkbox("Visible", &o.visible))
    if (const AnimProp *vp = anim_find_prop(o, "visible")) anim_autokey(a, o, *vp, -1);
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
      if (prop_filter_match("Ground")) ground_ui(a, o);
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
          // a population with species: this mesh stands for one of them
          {
            gpx::Node *sn = a.graph.find_node(o.scatter_node);
            const int species = sn ? sn->attrs.get_i("species", 0) : 0;
            if (species > 1) {
              int sp = o.scatter_species + 1; // 0 = every species
              std::string lbl = sp == 0 ? "All" : "Species " + std::to_string(sp);
              if (ImGui::BeginCombo("Species", lbl.c_str())) {
                for (int s = 0; s <= species; ++s) {
                  std::string l = s == 0 ? "All" : "Species " + std::to_string(s);
                  if (ImGui::Selectable(l.c_str(), sp == s)) {
                    o.scatter_species = s - 1;
                    ch = true;
                  }
                }
                ImGui::EndCombo();
              }
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Which of the layer's species this mesh is. Bind one mesh per species.");
            }
          }
          if (ch) a.request_eval();
          ImGui::TextDisabled("%d copies", o.inst_count());
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
