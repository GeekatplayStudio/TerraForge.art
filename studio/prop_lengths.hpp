// Geekatplay TerraForge - real lengths in the Properties panels.
//
// A world unit is the home terrain tile, render_settings().terrain_size_m
// metres across, and no panel shows a bare fraction of it: positions, sizes
// and altitudes are converted to metres (or feet) and given a unit that
// reads well at the size in question, so a 30 cm rock is "30 cm" and a
// planet is "6371.000 km". Shared by panel_properties_object*.cpp.
#pragma once
#include "app.hpp"
#include "render_settings.hpp"
#include <cmath>
#include <cstdio>
#include <imgui.h>

namespace studio {

// ---------------------------------------------------------- metric lengths
// The unit a length is shown in follows its magnitude, the way a person would
// say it: centimetres for a pebble, metres for a building, kilometres for a
// mountain range. Imperial does the same with inches, feet and miles.
struct LenUnit {
  const char *suffix;
  float per_m;   // display value = metres * per_m
  int decimals;
};

inline LenUnit pick_unit(float metres) {
  float m = std::fabs(metres);
  if (render_settings().units == 1) { // imperial
    if (m < 0.3048f) return {"in", 39.37008f, 2};
    if (m < 1609.344f) return {"ft", 3.280840f, m < 30.f ? 2 : 1};
    return {"mi", 1.f / 1609.344f, 3};
  }
  if (m < 0.01f) return {"mm", 1000.f, 3};
  if (m < 1.f) return {"cm", 100.f, 1};
  if (m < 1000.f) return {"m", 1.f, m < 10.f ? 2 : 1};
  return {"km", 0.001f, 3};
}

// `v` is in world units; `world_scale` converts it to world units of length
// (height_scale for altitudes, 1 for everything else).
inline bool drag_length(const char *label, float *v, float world_scale = 1.f,
                 float lo_m = -1e7f, float hi_m = 1e7f) {
  const float tile = render_settings().terrain_size_m;
  const float k = tile * (world_scale != 0.f ? world_scale : 1.f);
  float metres = *v * k;
  LenUnit u = pick_unit(metres);
  float shown = metres * u.per_m;
  char fmt[24];
  std::snprintf(fmt, sizeof fmt, "%%.%df %s", u.decimals, u.suffix);
  float speed = std::fmax(std::fabs(shown) * 0.01f, 0.01f);
  bool ch = ImGui::DragFloat(label, &shown, speed, lo_m * u.per_m,
                             hi_m * u.per_m, fmt);
  if (ch && k != 0.f) *v = (shown / u.per_m) / k;
  return ch;
}

// A read-only length, for things the user cannot drag here.
inline void text_length(const char *label, float metres) {
  LenUnit u = pick_unit(metres);
  ImGui::TextDisabled("%s: %.*f %s", label, u.decimals, metres * u.per_m,
                      u.suffix);
}

// A slider with its name above it rather than beside it. A trailing label is
// the first thing to be clipped when the panel is narrow or the font is
// large, and this panel is often both.
inline void labeled_scalar(const char *label, const char *id, float *v, float mn,
                    float mx) {
  ImGui::TextUnformatted(label);
  scalar_float(id, v, mn, mx);
}


} // namespace studio
