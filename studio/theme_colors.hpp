// Geekatplay TerraForge — the interface palette, as a token graph.
//
// Cinema 4D's scheme system is the model here. Its colour slots are typed
// RGB / Bitmap / *Reference*, where a Reference makes one slot's colour
// "dependent on the colors of other elements" — so a scheme is not a flat list
// of two hundred values, it is a handful of lead colours with almost
// everything else defined in terms of them. That is why C4D stays coherent
// when a user edits it, and it is the part worth copying.
//
// So: a few LEADS, and everything else derived from them. Re-tinting the whole
// application is then a matter of changing three or four numbers, and nothing
// can drift out of step because nothing else holds a literal.
//
// (Maxon publishes no slot names and no hex values anywhere in the
// documentation — checked across six versions. The values here are read off
// the reference screenshots, not from the docs, and are ours.)
#pragma once
#include <imgui.h>
#include <string>

namespace studio {
namespace theme {

// ------------------------------------------------------------------- leads
// Change these and the rest follows.
inline constexpr ImU32 LEAD_SURFACE = IM_COL32(0x2b, 0x2b, 0x2b, 0xff); // panels
inline constexpr ImU32 LEAD_INK     = IM_COL32(0xd6, 0xd3, 0xcd, 0xff); // text
inline constexpr ImU32 LEAD_ACCENT  = IM_COL32(0xd9, 0x8c, 0x33, 0xff); // dim orange

// Shift a colour's brightness. Derivation is multiplicative so a re-tint keeps
// the relationships between shades intact rather than flattening them.
inline ImU32 shade(ImU32 c, float k) {
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  v.x = v.x * k;
  v.y = v.y * k;
  v.z = v.z * k;
  return ImGui::ColorConvertFloat4ToU32(
      ImVec4(v.x > 1.f ? 1.f : v.x, v.y > 1.f ? 1.f : v.y,
             v.z > 1.f ? 1.f : v.z, v.w));
}
inline ImU32 fade(ImU32 c, float a) {
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  v.w *= a;
  return ImGui::ColorConvertFloat4ToU32(v);
}

// ---------------------------------------------------------------- derived
inline ImU32 window_bg()   { return shade(LEAD_SURFACE, 0.62f); }
inline ImU32 panel_bg()    { return LEAD_SURFACE; }
inline ImU32 node_bg()     { return shade(LEAD_SURFACE, 0.78f); }
inline ImU32 node_border() { return shade(LEAD_SURFACE, 0.35f); }
inline ImU32 node_selected() { return LEAD_ACCENT; }
inline ImU32 text()        { return LEAD_INK; }
inline ImU32 text_dim()    { return shade(LEAD_INK, 0.58f); }
inline ImU32 text_on_header() { return IM_COL32(0xf2, 0xf0, 0xec, 0xff); }
inline ImU32 accent()      { return LEAD_ACCENT; }
inline ImU32 error()       { return IM_COL32(0xd8, 0x50, 0x3c, 0xff); }

// ------------------------------------------------------- node header colours
// One colour per node category, the way Cinema 4D and Cycles 4D colour node
// headers: the header is the category, so a graph is readable at a glance and
// from a distance, before any label is legible.
//
// Maxon does not publish its category-to-colour mapping (confirmed absent in
// every documented version), so these are chosen to be distinguishable at
// small size and to sit on a dark ground without glowing.
ImU32 category_color(const std::string &category);

// Ports are coloured by what flows through them, which is the one thing you
// need to know before making a connection.
ImU32 port_color(bool is_texture, bool is_field, unsigned field_type);

} // namespace theme
} // namespace studio
