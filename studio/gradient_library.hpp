// Geekatplay TerraForge — a library of colour gradients.
//
// A gradient is the whole look of a colour map, and a good one - the greens
// giving way to rock and then snow of an alpine slope, the ochres of a desert
// - takes real fiddling to arrive at. It should be arrived at once. This keeps
// them: a set built in (natural gradients, named for what they are), and the
// user's own, saved as small JSON files beside the material library.
#pragma once
#include "gpx/attribute.hpp"
#include <string>
#include <vector>

namespace studio {

struct GradientPreset {
  std::string name;
  std::vector<gpx::GradientStop> stops;
  bool builtin = false; // shipped with the application, cannot be deleted
  std::string path;     // the file, for the user's own
};

// Where the user's gradients live (created on demand).
std::string gradient_library_dir();

// The built-in natural gradients followed by the user's saved ones, by name.
// The listing is cached; `gradient_library_rescan()` rereads the directory.
const std::vector<GradientPreset> &gradient_library();
void gradient_library_rescan();

// Save a gradient under a name (sanitised for the file system; an existing
// file of that name is replaced). Returns the path, or "" with `err` set.
std::string gradient_library_save(const std::string &name,
                                  const std::vector<gpx::GradientStop> &stops,
                                  std::string &err);
// Remove a saved gradient (never a built-in). True if it was removed.
bool gradient_library_erase(const std::string &name, std::string &err);

// The preset with this name, or nullptr.
const GradientPreset *gradient_library_find(const std::string &name);

} // namespace studio
