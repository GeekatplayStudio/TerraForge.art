// Geekatplay TerraForge — category and port colours.
#include "theme_colors.hpp"
#include "gpx/field.hpp"

namespace studio {
namespace theme {

// The categories, grouped by what they are *for* rather than alphabetically,
// so related work shares a family of hues and the eye can follow a chain:
//
//   greens   sources — where terrain comes from
//   blues    shaping — filters and transforms that change what is there
//   browns   simulation — erosion, hydrology, the slow physical processes
//   violets  the field domain — the resolution-independent half of the graph
//   ambers   surface — materials and texture, what it looks like
//   greys    plumbing — logic, groups, sinks
ImU32 category_color(const std::string &c) {
  // sources
  if (c == "Primitive") return IM_COL32(0x6f, 0x8f, 0x4a, 0xff);
  // shaping
  if (c == "Filter") return IM_COL32(0x46, 0x6e, 0x94, 0xff);
  if (c == "Transform") return IM_COL32(0x3f, 0x82, 0x86, 0xff);
  if (c == "Operator") return IM_COL32(0x51, 0x74, 0xa8, 0xff);
  if (c == "Effect") return IM_COL32(0x5a, 0x86, 0xb4, 0xff);
  // simulation
  if (c == "Erosion") return IM_COL32(0x9a, 0x5c, 0x3a, 0xff);
  if (c == "Analysis") return IM_COL32(0x87, 0x6a, 0x3e, 0xff);
  // the field domain — one family, so it reads as a domain rather than as
  // seven unrelated categories that happen to start with the same word
  if (c == "Field Input") return IM_COL32(0x6b, 0x5c, 0xa8, 0xff);
  if (c == "Field Math") return IM_COL32(0x7a, 0x5c, 0xb0, 0xff);
  if (c == "Field Noise") return IM_COL32(0x8a, 0x5c, 0xb8, 0xff);
  if (c == "Field Color") return IM_COL32(0x9a, 0x5c, 0xb0, 0xff);
  if (c == "Field Displace") return IM_COL32(0x64, 0x50, 0xa0, 0xff);
  if (c == "Field Material") return IM_COL32(0xa0, 0x5c, 0x9c, 0xff);
  if (c == "Field Bridge") return IM_COL32(0x55, 0x60, 0xb0, 0xff);
  // surface
  if (c == "Texture") return IM_COL32(0xbf, 0x78, 0x33, 0xff);
  if (c == "Material") return IM_COL32(0xa8, 0x6a, 0x2e, 0xff);
  // world
  if (c == "Atmosphere") return IM_COL32(0x4e, 0x93, 0xa8, 0xff);
  if (c == "Render") return IM_COL32(0x8a, 0x8a, 0x5c, 0xff);
  // plumbing
  if (c == "Mask") return IM_COL32(0x6d, 0x4f, 0x8c, 0xff);
  if (c == "Logic") return IM_COL32(0x7a, 0x7a, 0x7a, 0xff);
  if (c == "Group") return IM_COL32(0x5f, 0x6b, 0x78, 0xff);
  if (c == "Export") return IM_COL32(0x55, 0x55, 0x55, 0xff);
  return IM_COL32(0x66, 0x66, 0x66, 0xff);
}

// Field links already have a colour convention we follow Vue on (manual
// p772-773): blue number, green colour, violet vector, purple texcoord. Raster
// links keep the older pair — bone for heightmaps, orange for textures — so
// the two domains stay distinguishable at a glance, which is the whole point
// of having two.
ImU32 port_color(bool is_texture, bool is_field, unsigned field_type,
                 bool is_points) {
  if (is_field) return gpx::field_type_color((gpx::FieldType)field_type);
  if (is_points) return IM_COL32(0x86, 0xb8, 0x5c, 0xff); // scatter green
  return is_texture ? IM_COL32(0xc8, 0x78, 0x30, 0xff)
                    : IM_COL32(0xcf, 0xcb, 0xc2, 0xff);
}

} // namespace theme
} // namespace studio
