// Geekatplay TerraForge - what the plant tables hold.
//
// Not a public header. plant_knowledge.cpp turns words into a
// PlantDescription; the plants themselves live in plant_knowledge_table.cpp
// (the trees) and plant_knowledge_table2.cpp (everything smaller), because
// one table of a hundred and forty plants is longer than a file is allowed
// to be. This is the contract between the three, and the archetype
// defaults every one of them is written on top of.
//
// An entry says only what makes its plant that plant: how tall it grows,
// what its crown and bark look like, its leaf and its colours. Everything
// else - how many levels of branching, at what angle, with what flowers -
// comes from the archetype's defaults unless `extra` says otherwise, so a
// birch entry is three lines rather than thirty.
#pragma once
#include "gpx/plant.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace gpx {
namespace plant {
namespace know {

struct Plant {
  const char *key;     // the names it answers to, lowercase, "|" between them
  const char *name;    // what to call it
  const char *latin;   // its botanical name, which becomes the note
  const char *arch;    // the archetype that grows it
  const char *group;   // the shelf it belongs on
  float height;        // typical mature height in metres
  const char *crown;   // PlantDescription::crown
  const char *bark;    // PlantDescription::bark
  uint32_t bark_rgb;   // 0xRRGGBB, sRGB as a person would pick it
  const char *leaf;    // PlantDescription::leaf_shape
  float leaf_cm;
  uint32_t leaf_rgb;
  uint32_t autumn_rgb; // 0 when the leaves never turn
  // Words and key=value pairs, read by plant_knowledge.cpp: evergreen,
  // flowers, fruits, opposite/whorled/alternate/spiral, weeping, gnarled,
  // dense, sparse, angle=, levels=, trunk=, density=, leafd=, droop=,
  // straight=, width=, forks=, cluster=, fshape=, fcolor=, fsize=,
  // fseason=a,b, frcolor=, frsize=, frseason=a,b, age=, maxage=, var=.
  const char *extra;
};

// The whole table, built once from the two files below.
const std::vector<Plant> &table();
void table_trees(std::vector<Plant> &v); // plant_knowledge_table.cpp
void table_small(std::vector<Plant> &v); // plant_knowledge_table2.cpp

// The archetype these words name or suggest ("conifer", "a pine", "tree"),
// or "" when nothing in them suggests one.
std::string archetype_guess(const std::string &words);
// The archetype's own usual plant, before any species or modifier.
void archetype_defaults(const std::string &arch, PlantDescription &d);

} // namespace know
} // namespace plant
} // namespace gpx
