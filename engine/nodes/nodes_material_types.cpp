// Geekatplay TerraForge - the two material types that are not about colour.
//
// Vue's material editor has EcoSystem materials: a material whose presence
// rule also decides where the plants stand. That is DistributionLayer here -
// the same presence mask that shades the ground places the objects, so the
// two never disagree about where the grass is.
//
// EffectorLayer is a new idea. A material that does not colour anything but
// *influences*: it publishes a typed field - pressure, wind, light, heat,
// moisture - that other systems read. Nothing consumes most of these yet;
// the point is that the material editor is where an artist paints "the wind
// is strong here" or "this ground is trodden", and the systems that arrive
// later find that field already waiting, by kind.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include "gpx/points.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

namespace {

inline uint32_t hash_u32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}
inline float unit(uint32_t i, uint32_t channel, uint32_t seed) {
  return (hash_u32(i * 0x9E3779B1u + channel * 0x85EBCA77u + seed) & 0xffffffu) /
         16777216.f;
}

inline float sample01(const Heightmap *hm, float x, float y) {
  int ix = std::clamp((int)(x * hm->w), 0, hm->w - 1);
  int iy = std::clamp((int)(y * hm->h), 0, hm->h - 1);
  return hm->v[(size_t)iy * hm->w + ix];
}

} // namespace

REGISTER_NODE(
    DistributionLayer, "Material",
    "Distribution layer: the presence that shades also places objects",
    [](Node &n) {
      n.add_in("albedo", DataType::Texture, true);
      n.add_in("presence", DataType::Heightmap, true);
      n.add_out("albedo", DataType::Texture);
      n.add_out("presence", DataType::Heightmap);
      n.add_out("points", DataType::Points);
      add_int(n.attrs, "count", "Instances", 800, 1, 50000, "Population")
          .tooltip = "How many objects the presence places at full presence.";
      add_float(n.attrs, "min_dist", "Min spacing", 0.015f, 0.001f, 0.3f,
                "Population");
      add_float(n.attrs, "threshold", "Presence threshold", 0.15f, 0.f, 1.f,
                "Population")
          .tooltip = "Presence below this places nothing at all.";
      add_seed(n.attrs, "seed", "Seed", 0, "Population");
      add_choice(n.attrs, "size_from", "Instance size from",
                 {"Uniform", "Presence", "Power law"}, 1, "Population")
          .tooltip = "The point value: the same for all, following the "
                     "presence (strong presence, big plant), or many small "
                     "and a few large.";
    },
    [](Node &n) {
      // colour passes straight through: this layer shades nothing itself
      TextureRGBA &alb = n.out_tex("albedo");
      if (const TextureRGBA *in = n.in_tex("albedo"); in && !in->empty())
        alb = *in;
      const Heightmap *pres = n.in_hmap("presence");
      Heightmap &pout = n.out_hmap("presence");
      if (pres) pout = *pres;
      PointCloud &pts = n.out_points("points");
      pts.clear();
      // No presence connected means presence everywhere: a distribution with
      // no rule yet still distributes, so there is something to look at.
      const bool everywhere = !pres || pres->empty();

      const int count = n.attrs.get_i("count", 800);
      const float md = std::max(n.attrs.get_f("min_dist", 0.015f), 1e-3f);
      const float thr = n.attrs.get_f("threshold", 0.15f);
      const uint32_t seed = n.attrs.get_seed("seed");
      const int size_from = n.attrs.get_choice("size_from");
      // Dart throwing with the presence as the acceptance probability, and a
      // coarse grid so the spacing test stays linear. Candidates come from a
      // hash of their index, so the same settings give the same population
      // on every machine - and a small change to the presence moves only the
      // points it concerns.
      const int gs = std::max(1, (int)(1.f / md));
      std::vector<std::vector<int>> cells((size_t)gs * gs);
      uint32_t tries = 0, budget = (uint32_t)count * 40u;
      while ((int)pts.size() < count && tries < budget) {
        uint32_t i = tries++;
        float px = unit(i, 1, seed), py = unit(i, 2, seed);
        float p = everywhere ? 1.f : sample01(pres, px, py);
        if (p < thr || unit(i, 3, seed) > p) continue;
        int cx = std::min((int)(px * gs), gs - 1);
        int cy = std::min((int)(py * gs), gs - 1);
        bool near = false;
        for (int dy = -1; dy <= 1 && !near; ++dy)
          for (int dx = -1; dx <= 1 && !near; ++dx) {
            int nx = cx + dx, ny = cy + dy;
            if (nx < 0 || ny < 0 || nx >= gs || ny >= gs) continue;
            for (int q : cells[(size_t)ny * gs + nx]) {
              float ddx = pts.x[(size_t)q] - px, ddy = pts.y[(size_t)q] - py;
              if (ddx * ddx + ddy * ddy < md * md) { near = true; break; }
            }
          }
        if (near) continue;
        cells[(size_t)cy * gs + cx].push_back((int)pts.size());
        float v = 1.f;
        if (size_from == 1)
          v = p;
        else if (size_from == 2)
          v = std::pow(unit(i, 4, seed), 2.f);
        pts.add(px, py, v);
      }
    })

REGISTER_NODE(
    EffectorLayer, "Material",
    "Effector layer: a typed influence field for other systems to read",
    [](Node &n) {
      n.add_in("albedo", DataType::Texture, true);
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("albedo", DataType::Texture);
      n.add_out("effector", DataType::Heightmap);
      add_choice(n.attrs, "kind", "Effector kind",
                 {"Pressure", "Wind", "Light", "Heat", "Moisture", "Custom"}, 0,
                 "Effector")
          .tooltip = "What this field means to the systems that read it. "
                     "Pressure: trodden or loaded ground. Wind: local air "
                     "movement. Light and Heat: exposure. Moisture: wetness. "
                     "Custom: whatever a script assigns it.";
      add_float(n.attrs, "strength", "Strength", 1.f, 0.f, 4.f, "Effector");
      add_float(n.attrs, "falloff", "Falloff", 1.f, 0.1f, 6.f, "Effector")
          .tooltip = "A power on the mask: above 1 the field concentrates "
                     "where the mask is strongest, below 1 it spreads.";
      add_bool(n.attrs, "invert", "Invert", false, "Effector");
      add_bool(n.attrs, "show", "Tint the material by the field", false,
               "Effector")
          .tooltip = "Blends the field into the colour so it can be seen in "
                     "the viewport while it is being painted. Off, the "
                     "material's colour passes through untouched.";
    },
    [](Node &n) {
      const Heightmap *mask = n.in_hmap("mask");
      Heightmap &out = n.out_hmap("effector");
      const float strength = n.attrs.get_f("strength", 1.f);
      const float falloff = n.attrs.get_f("falloff", 1.f);
      const bool invert = n.attrs.get_b("invert");
      if (mask && !mask->empty()) {
        out = *mask;
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i) {
            float m = std::clamp(out.v[i], 0.f, 1.f);
            if (invert) m = 1.f - m;
            out.v[i] = std::clamp(std::pow(m, falloff) * strength, 0.f, 4.f);
          }
        });
      } else {
        out.v.clear();
        out.w = out.h = 0;
      }
      TextureRGBA &alb = n.out_tex("albedo");
      const TextureRGBA *in = n.in_tex("albedo");
      if (in && !in->empty()) alb = *in;
      // Only when asked: the field shown in the colour, so painting it has
      // something to look at. The tint says which kind it is.
      if (n.attrs.get_b("show") && !alb.empty() && !out.empty() &&
          alb.w == out.w && alb.h == out.h) {
        static const float tint[6][3] = {{0.9f, 0.3f, 0.2f}, {0.4f, 0.7f, 0.95f},
                                         {1.f, 0.9f, 0.4f},  {1.f, 0.5f, 0.1f},
                                         {0.2f, 0.5f, 0.9f}, {0.8f, 0.4f, 0.9f}};
        const float *t = tint[std::clamp(n.attrs.get_choice("kind"), 0, 5)];
        parallel_rows(alb.h, [&](int y0, int y1) {
          for (int y = y0; y < y1; ++y)
            for (int x = 0; x < alb.w; ++x) {
              float *p = alb.px(x, y);
              float f = std::clamp(out.v[(size_t)y * out.w + x], 0.f, 1.f) * 0.6f;
              for (int c = 0; c < 3; ++c) p[c] = p[c] * (1.f - f) + t[c] * f;
            }
        });
      }
    })

} // namespace gpx
