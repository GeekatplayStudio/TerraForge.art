// Geekatplay TerraForge — deep space by the handful: named looks, and one
// action that fills the sky with them.
//
// Placing eight nebulas by hand, each with a direction, a size, a seed and
// two colours, is not how anyone wants to arrive at a backdrop. A preset
// sets the star field, the band and the palette; Fill the sky then scatters
// nebulas and galaxies through it, spread apart, varied in kind and size,
// and coloured from wherever the realism dial stands. Both are one call, so
// the panel, the assistant, a script and MCP reach them the same way.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace studio {

struct SpacePreset {
  const char *key;  // what a script names it by
  const char *name; // what the panel shows
  const char *tip;
};

// The looks on offer, in the order the panel lists them.
const std::vector<SpacePreset> &space_presets();

// Set the star field, the band and the palette to a named look. A preset
// that wants nebulas fills the sky itself. False, with `err`, for a name
// that is not one of them.
bool space_preset_apply(const std::string &key, std::string &err);

// Scatter `count` nebulas and galaxies over the sky, spread apart and
// varied, coloured from the realism dial. Any earlier scattered ones are
// cleared first; nebulas placed by hand are left alone. Returns how many
// were made, or -1 with `err`. `style` is "mixed" (the default), "nebulas",
// "galaxies" or "dark".
int space_populate(int count, int seed, const std::string &style, std::string &err);

// The two colours a nebula of `type` is born with at this point of the
// realism dial: hot ionised gas in `c1`, the cooler gas around it in `c2`.
// 1 is what a camera records, 0 what a film paints.
void space_nebula_colors(int type, float realism, uint32_t seed, float c1[3], float c2[3]);

} // namespace studio
