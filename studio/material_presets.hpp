// Geekatplay TerraForge — the base materials.
//
// A material is built up from a base, and the bases every scene reaches for
// are always the same handful: a metal, a glass, water, plain colour, ground,
// rock, ice, snow... Each is a small graph - a colour source into a
// MaterialOutput with the surface set to what that substance does with light
// - dropped into the project ready to be changed. They are made, not loaded:
// there is no file to lose and nothing to install.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace studio {
struct App;

struct MaterialPresetInfo {
  const char *name;  // "Copper"
  const char *group; // "Metal", "Glass & liquid", "Ground", ...
  const char *blurb; // one line on what it is for
};

// Every base material, in display order.
const std::vector<MaterialPresetInfo> &material_presets();

// Build the named preset in the graph and return its MaterialOutput id, or 0
// with `err` set. Takes the graph lock itself; pushes one undo step.
uint64_t material_preset_create(App &a, const std::string &name, std::string &err);
// The same for a caller that already holds the graph lock (a panel drawing
// under a GraphLease). Taking the lock again on the same thread only times
// out, which is how "click does nothing" happened in the browser.
uint64_t material_preset_create_locked(App &a, const std::string &name, std::string &err);

// What the preset looks like, for a thumbnail, without making it: the
// surface parameters and the colour as a tint. No graph, no lock.
struct MaterialPreviewSpec;
MaterialPreviewSpec material_preset_preview_spec(const std::string &name);

} // namespace studio
