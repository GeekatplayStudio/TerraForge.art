// Geekatplay TerraForge - ray test against the planets (no GL). Split from
// planet_renderer.cpp for the 500-line module rule.
#include "planet_renderer.hpp"
#include "scene.hpp"
#include <cmath>

namespace studio {

int planet_pick(const float ro[3], const float rd[3], float &t_out) {
  SceneState &sc = scene();
  int best = -1;
  float best_t = 1e30f;
  for (int idx : scene_planet_indices()) {
    const SceneObject &o = sc.objects[idx];
    if (!sc.object_visible(o)) continue;
    float r = o.planet.radius * (1.f + o.planet.relief * 0.5f);
    float oc[3] = {ro[0] - o.pos[0], ro[1] - o.pos[1], ro[2] - o.pos[2]};
    float b = oc[0] * rd[0] + oc[1] * rd[1] + oc[2] * rd[2];
    float c = oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2] - r * r;
    float disc = b * b - c;
    if (disc < 0) continue;
    float t = -b - std::sqrt(disc);
    if (t < 0) t = -b + std::sqrt(disc);
    if (t > 0 && t < best_t) {
      best_t = t;
      best = idx;
    }
  }
  t_out = best_t;
  return best;
}


} // namespace studio
