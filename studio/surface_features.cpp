// Geekatplay TerraForge - the surface features on the placed ground. See the header.
#include "surface_features.hpp"

namespace studio {

void surface_features_apply(gpx::Heightmap &ground, const SurfaceFeatures &f, gpx::Heightmap *natural) {
  if (f.displacement && !f.displacement->empty() && !ground.empty()) {
    const gpx::Heightmap &d = *f.displacement;
    if (d.w == ground.w && d.h == ground.h) {
      for (size_t i = 0; i < ground.v.size(); ++i) ground.v[i] += d.v[i];
    } else {
      for (int y = 0; y < ground.h; ++y)
        for (int x = 0; x < ground.w; ++x)
          ground.at(x, y) += d.sample((x + 0.5f) / ground.w, (y + 0.5f) / ground.h);
    }
  }
  if (natural) *natural = ground;
  if (!f.footprints.empty() && !ground.empty()) gpx::imprint_apply(ground, f.footprints, f.imprint, nullptr);
}

} // namespace studio
