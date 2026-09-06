// Geekatplay TerraForge - GrassDisplacement: grass as displacement, the way
// Terragen grows it before a population is placed: a dense field of tufts
// - domes on a jittered cellular lattice, gathered into clumps by a larger
// noise, ragged at the edges by a finer one - raised only on ground flat
// enough to hold it. As the displacement of a material layer it puts the
// grass exactly where the layer's presence says (a flat, low, well-lit
// meadow) and nowhere else; the same field, thresholded, is the mask the
// layer's colour follows.
//
// Every value is a hash of position and seed, so a frame is bit-identical
// on any thread count, and the node is a raster node: its detail is the
// terrain's resolution. Blade-scale fuzz below that belongs to the GPU
// surface detail (TerrainSurface), not to the heightmap.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

REGISTER_NODE(
    GrassDisplacement, "Primitive",
    "Grass as displacement: a field of tufts in clumps, on ground flat enough to hold it",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      n.add_out("displacement");
      n.add_out("grass_mask");
      add_float(n.attrs, "tuft_scale", "Tuft size", 0.004f, 0.0005f, 0.05f, "Grass")
          .tooltip = "The size of one tuft as a fraction of the terrain's width.\n"
                     "Smaller is denser grass.";
      add_float(n.attrs, "height", "Height", 0.004f, 0.f, 0.05f, "Grass")
          .tooltip = "How tall the tufts stand, as a fraction of the terrain's\n"
                     "own height range.";
      add_float(n.attrs, "density", "Density", 0.75f, 0.f, 1.f, "Grass")
          .tooltip = "How much of the ground the clumps cover.";
      add_float(n.attrs, "clumping", "Clumping", 0.5f, 0.f, 1.f, "Grass")
          .tooltip = "0 an even lawn, 1 tufts gathered into patches with bare\n"
                     "ground between.";
      add_float(n.attrs, "clump_scale", "Clump size", 12.f, 1.f, 64.f, "Grass")
          .tooltip = "Size of the patches, in tufts.";
      add_float(n.attrs, "roughness", "Raggedness", 0.5f, 0.f, 1.f, "Grass")
          .tooltip = "How uneven the tops of the tufts are.";
      add_seed(n.attrs, "seed", "Seed", 0, "Grass");
      add_range(n.attrs, "slope_band", "Grows on slopes", 0.f, 0.35f, 0.f, 1.f, "Placement")
          .tooltip = "Grass takes only ground whose slope is inside this band\n"
                     "(0 flat .. 1 vertical).";
      add_float(n.attrs, "slope_fuzz", "Slope fade", 0.1f, 0.f, 0.5f, "Placement");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &disp = n.out_hmap("displacement");
      Heightmap &gmask = n.out_hmap("grass_mask");
      out = *in;
      disp = Heightmap(in->w, in->h);
      gmask = Heightmap(in->w, in->h);
      const float tuft = std::max(n.attrs.get_f("tuft_scale", 0.004f), 1e-5f);
      const float height = n.attrs.get_f("height", 0.004f);
      const float density = n.attrs.get_f("density", 0.75f);
      const float clumping = n.attrs.get_f("clumping", 0.5f);
      const float clump_scale = std::max(n.attrs.get_f("clump_scale", 12.f), 1.f);
      const float rough = n.attrs.get_f("roughness", 0.5f);
      const uint32_t seed = n.attrs.get_seed("seed");
      float slo, shi;
      n.attrs.get_range("slope_band", slo, shi);
      const float sfz = n.attrs.get_f("slope_fuzz", 0.1f);
      const Heightmap *mask = n.in_hmap("mask");
      if (mask && mask->empty()) mask = nullptr;
      float mn, mx;
      in->minmax(mn, mx);
      const float hamp = (mx - mn) > 1e-9f ? mx - mn : 1.f;
      const float cells = 1.f / tuft;
      noise::FbmParams cf;
      cf.octaves = 3;
      auto band = [](float x, float lo, float hi, float fuzz) {
        if (fuzz <= 1e-6f) return (x >= lo && x <= hi) ? 1.f : 0.f;
        float a = std::clamp((x - (lo - fuzz)) / (2.f * fuzz), 0.f, 1.f);
        float b = std::clamp(((hi + fuzz) - x) / (2.f * fuzz), 0.f, 1.f);
        a = a * a * (3.f - 2.f * a);
        b = b * b * (3.f - 2.f * b);
        return std::min(a, b);
      };
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            const float u = (x + 0.5f) / out.w, v = (y + 0.5f) / out.h;
            // where grass may grow: slope band, then the mask
            float gx, gy;
            in->gradient_at(x, y, gx, gy);
            float slope = std::atan(std::sqrt(gx * gx + gy * gy) * in->w / hamp) * 0.63662f;
            float where = band(slope, slo, shi, sfz);
            if (mask) where *= std::clamp(mask->sample(u, v), 0.f, 1.f);
            if (where <= 1e-4f) continue;
            // clumps: a low noise thresholded by density, soft-edged
            float cn = noise::fbm(u * cells / clump_scale, v * cells / clump_scale, seed ^ 0x9e37u, cf) * 0.5f + 0.5f;
            float thresh = 1.f - density;
            float clump = clumping > 0.f ? std::clamp((cn - thresh) / std::max(0.35f * clumping, 1e-3f) + 0.5f, 0.f, 1.f) : density;
            clump = clump * clumping + density * (1.f - clumping);
            if (clump <= 1e-4f) continue;
            // the tuft: a dome on the nearest jittered cell centre
            float f1, f2;
            noise::worley(u * cells, v * cells, seed, f1, f2, 0.9f);
            float dome = std::clamp(1.f - f1 * 1.6f, 0.f, 1.f);
            dome = dome * dome * (3.f - 2.f * dome);
            // ragged tops: a fine noise takes a bite from each tuft
            float rag = 1.f - rough * 0.5f * (1.f - std::fabs(noise::perlin(u * cells * 3.1f, v * cells * 3.1f, seed ^ 0x51u)));
            float t = dome * rag * clump * where;
            disp.at(x, y) = t * height * hamp;
            gmask.at(x, y) = std::clamp(t * 1.5f, 0.f, 1.f);
            out.at(x, y) = in->at(x, y) + disp.at(x, y);
          }
      });
      apply_mask_blend(nullptr, *in, out);
    })

} // namespace gpx
