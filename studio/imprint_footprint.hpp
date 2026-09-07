// Geekatplay TerraForge - the footprint of an object on the terrain.
//
// The convex hull of the object's base - the vertices in the lowest band of
// its height, deformed and placed as the viewport places them, projected
// onto the tile - rather than the ellipse of its bounding box. A house's
// corners, an L-shaped barn, a round tower: the flat patch the ground makes
// under each is the shape of the thing standing on it, and a margin widens
// it by a real distance. Pure: no App, no GL, so the tests link it alone.
#pragma once
#include <string>
#include <vector>

namespace studio {

struct SceneObject;

struct Footprint {
  std::vector<float> xz; // convex hull, tile fractions, counter-clockwise
  float cx = 0.f, cz = 0.f; // centroid
  float radius = 0.f;       // sqrt(area / pi): the equivalent round footprint
  float base = 0.f;         // the base's height in heightmap units
  bool valid() const { return xz.size() >= 6; }
};

// `band` is the fraction of the object's height that counts as its base
// (0.12 = the lowest twelve percent). An object with no vertices uses its
// bounding box.
Footprint imprint_footprint(const SceneObject &o, float height_scale, float band = 0.12f);

// One line of the TerrainImprint node's footprints text:
//   "base sink margin blend n x0 z0 x1 z1 ..."
// base and sink in heightmap units, margin and blend in tile fractions
// (blend <= 0 lets the node choose from the footprint's radius).
std::string imprint_footprint_line(const Footprint &f, float sink, float margin,
                                   float blend, float lift, float dig);

// Andrew's monotone chain; points in, hull out (counter-clockwise). Exposed
// for the tests.
std::vector<float> convex_hull_xz(std::vector<float> pts);

} // namespace studio
