// Geekatplay TerraForge - TerrainImprint: the ground meets the objects
// standing on it.
//
// A house dropped on a procedural hillside floats over one edge and sinks
// into the other. This node takes the footprints of the objects placed on
// the terrain (the studio writes them, see studio/imprint.cpp) and moulds
// the ground to each one: flat at the object's base inside the footprint,
// then a blend back to the natural terrain over a chosen width. Lower the
// object and the ground dips into a hollow; raise it and a mound rises to
// meet it. An object may be allowed to sink: within `sink` of its base the
// ground is left as it is (a boulder half buried), and only deeper than
// that is it hollowed. The original small relief can be kept under the
// blend, so the mould still looks like the same ground rather than a
// smooth patch.
//
// A footprint is the convex hull of the object's base, one per line:
//   "base sink margin blend n x0 z0 x1 z1 ..."
// base and sink in heightmap units, margin (the flat patch reaches this
// far past the walls) and blend (how far the ground responds; <= 0 means
// `width` times the footprint's radius) in tile fractions, then n hull
// points in tile fractions.
#include "gpx/imprint.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace gpx {

namespace {

struct Footprint {
  std::vector<float> xz; // hull, counter-clockwise
  float base = 0.f, sink = 0.f, margin = 0.f, blend = 0.f;
  float radius = 0.f;                   // sqrt(area / pi)
  float x0 = 0, z0 = 0, x1 = 0, z1 = 0; // bounds, margin and blend included
  // signed distance to the polygon: negative inside
  float distance(float px, float pz) const {
    const size_t n = xz.size() / 2;
    float best = -1e30f;
    bool inside = true;
    float dmin = 1e30f;
    for (size_t i = 0; i < n; ++i) {
      size_t j = (i + 1) % n;
      float ax = xz[i * 2], az = xz[i * 2 + 1], bx = xz[j * 2], bz = xz[j * 2 + 1];
      float ex = bx - ax, ez = bz - az;
      float len2 = ex * ex + ez * ez;
      // side: the hull is counter-clockwise, so inside is to the left
      float side = ex * (pz - az) - ez * (px - ax);
      if (side < 0.f) inside = false;
      float t = len2 > 1e-20f ? std::clamp(((px - ax) * ex + (pz - az) * ez) / len2, 0.f, 1.f) : 0.f;
      float qx = ax + ex * t - px, qz = az + ez * t - pz;
      dmin = std::min(dmin, qx * qx + qz * qz);
      best = std::max(best, side);
    }
    (void)best;
    float d = std::sqrt(dmin);
    return inside ? -d : d;
  }
};

std::vector<Footprint> parse_footprints(const std::string &text, float width_mult) {
  std::vector<Footprint> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    Footprint f;
    int n = 0;
    if (!(ls >> f.base >> f.sink >> f.margin >> f.blend >> n) || n < 3) continue;
    f.xz.resize((size_t)n * 2);
    bool ok = true;
    for (int i = 0; i < n * 2 && ok; ++i) ok = (bool)(ls >> f.xz[(size_t)i]);
    if (!ok) continue;
    // orientation: make it counter-clockwise (positive area)
    double A = 0;
    for (int i = 0; i < n; ++i) {
      int j = (i + 1) % n;
      A += (double)f.xz[i * 2] * f.xz[j * 2 + 1] - (double)f.xz[j * 2] * f.xz[i * 2 + 1];
    }
    if (A < 0) {
      std::vector<float> r;
      for (int i = n - 1; i >= 0; --i) { r.push_back(f.xz[i * 2]); r.push_back(f.xz[i * 2 + 1]); }
      f.xz = r;
      A = -A;
    }
    f.radius = (float)std::sqrt(0.5 * A / 3.14159265358979);
    f.margin = std::max(f.margin, 0.f);
    f.sink = std::max(f.sink, 0.f);
    if (f.blend <= 0.f) f.blend = width_mult * std::max(f.radius, 1e-4f);
    f.x0 = f.z0 = 1e30f; f.x1 = f.z1 = -1e30f;
    for (int i = 0; i < n; ++i) {
      f.x0 = std::min(f.x0, f.xz[i * 2]); f.x1 = std::max(f.x1, f.xz[i * 2]);
      f.z0 = std::min(f.z0, f.xz[i * 2 + 1]); f.z1 = std::max(f.z1, f.xz[i * 2 + 1]);
    }
    const float reach = f.margin + f.blend + 1e-4f;
    f.x0 -= reach; f.z0 -= reach; f.x1 += reach; f.z1 += reach;
    out.push_back(f);
  }
  return out;
}

// a separable box blur, three passes for a near-gaussian bell
void blur(const Heightmap &in, Heightmap &out, int radius) {
  out = in;
  if (radius < 1) return;
  Heightmap tmp(in.w, in.h);
  const float inv = 1.f / (2 * radius + 1);
  for (int pass = 0; pass < 3; ++pass) {
    parallel_rows(in.h, [&](int y0, int y1) {
      for (int y = y0; y < y1; ++y) {
        float sum = 0;
        for (int x = -radius; x <= radius; ++x) sum += out.atc(x, y);
        for (int x = 0; x < in.w; ++x) {
          tmp.at(x, y) = sum * inv;
          sum += out.atc(x + radius + 1, y) - out.atc(x - radius, y);
        }
      }
    });
    parallel_rows(in.w, [&](int x0, int x1) {
      for (int x = x0; x < x1; ++x) {
        float sum = 0;
        for (int y = -radius; y <= radius; ++y) sum += tmp.atc(x, y);
        for (int y = 0; y < in.h; ++y) {
          out.at(x, y) = sum * inv;
          sum += tmp.atc(x, y + radius + 1) - tmp.atc(x, y - radius);
        }
      }
    });
  }
}

} // namespace

void imprint_apply(Heightmap &ground, const std::string &footprints, const ImprintParams &prm,
                   Heightmap *mask) {
  if (ground.empty()) return;
  const std::vector<Footprint> fps = parse_footprints(footprints, prm.width);
  if (mask) *mask = Heightmap(ground.w, ground.h);
  if (fps.empty()) return;
  const Heightmap in = ground; // the natural ground the mould is measured from
  const float smooth = prm.smoothness, retain = prm.retain, flatten = prm.flatten, strength = prm.strength;

  // the relief to keep: the terrain minus its own broad shape, at a scale
  // set by the footprints themselves
  float mean_r = 0.f;
  for (const Footprint &f : fps) mean_r += f.radius;
  mean_r /= (float)fps.size();
  Heightmap broad;
  blur(in, broad, std::max(1, (int)(mean_r * in.w * 0.5f)));

  const float shape = 0.6f + smooth * 1.6f; // the falloff's exponent
  // only the pixels a footprint can reach are visited
  parallel_rows(in.h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
      const float v = (y + 0.5f) / (float)in.h;
      for (int x = 0; x < in.w; ++x) {
        const float u = (x + 0.5f) / (float)in.w;
        // the footprint that claims this pixel most strongly wins
        float best_w = 0.f, best_inside = 0.f;
        float target = 0.f;
        const size_t i = (size_t)y * in.w + x;
        const float h = in.v[i];
        for (const Footprint &f : fps) {
          if (u < f.x0 || u > f.x1 || v < f.z0 || v > f.z1) continue;
          const float d = f.distance(u, v) - f.margin; // 0 at the widened wall
          float w, inside;
          if (d <= 0.f) {
            w = 1.f;
            inside = 1.f;
          } else if (d < f.blend) {
            float t = 1.f - d / f.blend;        // 1 at the wall, 0 at the rim
            float s = t * t * (3.f - 2.f * t);  // smoothstep
            w = std::pow(s, shape);
            inside = 0.f;
          } else {
            continue;
          }
          if (w > best_w) {
            best_w = w;
            best_inside = inside;
            // the ground may keep its height while the object sits no
            // deeper than `sink` into it; below the base it rises to meet
            // it, above base + sink it is dug down
            target = std::clamp(h, f.base, f.base + f.sink);
          }
        }
        if (best_w <= 0.f) continue;
        // under the object the ground is made flat (by `flatten`); in the
        // blend the natural small relief rides on top of the mould
        const float relief = (h - broad.v[i]) * retain * (1.f - best_inside * flatten);
        const float moulded = target + relief;
        const float k = best_w * strength;
        ground.v[i] = h + (moulded - h) * k;
        if (mask) mask->v[i] = k;
      }
    }
  });
}

REGISTER_NODE(
    TerrainImprint, "Effect",
    "Mould the ground to the objects standing on it - flat under each, blended around it",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      n.add_out("imprint_mask");
      add_text(n.attrs, "footprints", "Footprints", "", "Imprint")
          .tooltip = "Written by the studio from the objects placed on the\n"
                     "terrain (Properties > Ground). One per line: base sink\n"
                     "margin blend, then the base's convex hull.";
      add_float(n.attrs, "width", "Blend width", 1.5f, 0.f, 6.f, "Imprint")
          .tooltip = "How far around an object the ground responds, as a\n"
                     "multiple of the footprint's radius - for objects that\n"
                     "do not set their own blend distance.";
      add_float(n.attrs, "smoothness", "Smoothness", 0.5f, 0.f, 1.f, "Imprint")
          .tooltip = "The shape of the blend: 0 is a firm shoulder, 1 a long\n"
                     "soft tail.";
      add_float(n.attrs, "retain", "Keep relief", 0.35f, 0.f, 1.f, "Imprint")
          .tooltip = "How much of the terrain's own small relief survives\n"
                     "inside the blend, so the mould still looks like the\n"
                     "same ground.";
      add_float(n.attrs, "flatten", "Flatten under", 1.f, 0.f, 1.f, "Imprint")
          .tooltip = "How flat the ground is made inside the footprint itself.";
      add_float(n.attrs, "strength", "Strength", 1.f, 0.f, 1.f, "Imprint")
          .tooltip = "Dial the whole effect back without losing it.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &imask = n.out_hmap("imprint_mask");
      out = *in;
      ImprintParams prm;
      prm.width = n.attrs.get_f("width", 1.5f);
      prm.smoothness = n.attrs.get_f("smoothness", 0.5f);
      prm.retain = n.attrs.get_f("retain", 0.35f);
      prm.flatten = n.attrs.get_f("flatten", 1.f);
      prm.strength = n.attrs.get_f("strength", 1.f);
      imprint_apply(out, n.attrs.get_s("footprints"), prm, &imask);
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

} // namespace gpx
