// Geekatplay TerraForge - TerrainImprint: the ground meets the objects
// standing on it.
//
// A house dropped on a procedural hillside floats over one edge and sinks
// into the other. This node takes the footprints of the objects placed on
// the terrain (the studio writes them, see studio/imprint.cpp) and moulds
// the ground to each one: flat at the object's base inside the footprint,
// then a blend back to the natural terrain over a chosen width. Lower the
// object and the ground dips into a hollow; raise it and a mound rises to
// meet it. The original small relief can be kept under the blend, so the
// mould still looks like the same ground rather than a smooth patch.
//
// Footprints arrive as text, one per line: "cx cz rx rz rot base", in tile
// fractions (0..1), rotation in degrees, base in heightmap units.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

namespace gpx {

namespace {

struct Footprint {
  float cx, cz, rx, rz, cos_r, sin_r, base;
};

std::vector<Footprint> parse_footprints(const std::string &text) {
  std::vector<Footprint> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    float cx, cz, rx, rz, rot, base;
    if (std::sscanf(line.c_str(), "%f %f %f %f %f %f", &cx, &cz, &rx, &rz, &rot, &base) != 6)
      continue;
    const float r = rot * 0.017453292519943295f;
    out.push_back({cx, cz, std::max(rx, 1e-4f), std::max(rz, 1e-4f), std::cos(r), std::sin(r), base});
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
                     "terrain (Properties > Ground). One per line:\n"
                     "cx cz rx rz rotation base.";
      add_float(n.attrs, "width", "Blend width", 1.5f, 0.f, 6.f, "Imprint")
          .tooltip = "How far around each object the ground responds, as a\n"
                     "multiple of the object's footprint.";
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
      imask = Heightmap(in->w, in->h);
      const std::vector<Footprint> fps = parse_footprints(n.attrs.get_s("footprints"));
      if (fps.empty()) return;

      const float width = n.attrs.get_f("width", 1.5f);
      const float smooth = n.attrs.get_f("smoothness", 0.5f);
      const float retain = n.attrs.get_f("retain", 0.35f);
      const float flatten = n.attrs.get_f("flatten", 1.f);
      const float strength = n.attrs.get_f("strength", 1.f);

      // the relief to keep: the terrain minus its own broad shape, at a
      // scale set by the footprints themselves
      float mean_r = 0.f;
      for (const Footprint &f : fps) mean_r += 0.5f * (f.rx + f.rz);
      mean_r /= (float)fps.size();
      Heightmap broad;
      blur(*in, broad, std::max(1, (int)(mean_r * in->w * 0.5f)));

      const float shape = 0.6f + smooth * 1.6f; // the falloff's exponent
      parallel_rows(in->h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
          const float v = (y + 0.5f) / (float)in->h;
          for (int x = 0; x < in->w; ++x) {
            const float u = (x + 0.5f) / (float)in->w;
            // the footprint that claims this pixel most strongly wins
            float best_w = 0.f, best_inside = 0.f;
            float target = 0.f;
            for (const Footprint &f : fps) {
              float dx = u - f.cx, dz = v - f.cz;
              float lx = dx * f.cos_r + dz * f.sin_r;
              float lz = -dx * f.sin_r + dz * f.cos_r;
              float d = std::sqrt((lx / f.rx) * (lx / f.rx) + (lz / f.rz) * (lz / f.rz));
              float w, inside;
              if (d <= 1.f) {
                w = 1.f;
                inside = 1.f;
              } else if (width > 1e-4f && d < 1.f + width) {
                float t = 1.f - (d - 1.f) / width;   // 1 at the footprint, 0 at the rim
                float s = t * t * (3.f - 2.f * t);   // smoothstep
                w = std::pow(s, shape);
                inside = 0.f;
              } else {
                continue;
              }
              if (w > best_w) {
                best_w = w;
                best_inside = inside;
                target = f.base;
              }
            }
            if (best_w <= 0.f) continue;
            const size_t i = (size_t)y * in->w + x;
            const float h = in->v[i];
            // under the object the ground is made flat (by `flatten`); in
            // the blend the natural small relief rides on top of the mould
            const float relief = (h - broad.v[i]) * retain * (1.f - best_inside * flatten);
            const float moulded = target + relief;
            const float k = best_w * strength;
            out.v[i] = h + (moulded - h) * k;
            imask.v[i] = k;
          }
        }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

} // namespace gpx
