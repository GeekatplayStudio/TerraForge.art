// Geekatplay TerraForge - the point-cloud domain: a set of 2D points over
// the unit tile, each with a value. The third port payload next to rasters
// and fields; scatter nodes make them, filters thin them, stamp nodes turn
// them back into rasters. Coordinates are 0..1 like every mask.
#pragma once
#include <cstdint>
#include <vector>

namespace gpx {

struct PointCloud {
  // parallel arrays: position (0..1 tile space) and a per-point value
  // (amplitude, radius scale, species id... whatever the consumer reads)
  std::vector<float> x, y, v;
  size_t size() const { return x.size(); }
  void add(float px, float py, float pv) {
    x.push_back(px);
    y.push_back(py);
    v.push_back(pv);
  }
  void clear() {
    x.clear();
    y.clear();
    v.clear();
  }
};

} // namespace gpx
