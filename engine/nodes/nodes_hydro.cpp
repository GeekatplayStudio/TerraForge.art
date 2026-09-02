// Geekatplay TerraForge — hydrology nodes: Rivers (flow-traced carving)
// and Coast (beach/bluff shoreline shaping), Gaea/World Machine style.
#include "gpx/node_graph.hpp"
#include "gpx/hydrology.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"
#include <algorithm>
#include <cstdint>
#include <queue>
#include <vector>

namespace gpx {

// Floods every closed basin to the height of its outlet. The filled surface is
// what flow routing needs; filled minus original is the lake standing in the
// hollow. Same answer, read the other way round.
REGISTER_NODE(
    FillBasins, "Hydrology",
    "Floods closed basins to their outlet - filled terrain for flow routing, plus lake depth and mask",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      n.add_out("depth");
      n.add_out("mask");
      add_float(n.attrs, "epsilon", "Drainage slope", 0.f, 0.f, 0.01f, "Fill")
          .tooltip = "A hair of tilt across each filled flat so water still\n"
                     "crosses it toward the outlet. Leave at 0 for true level\n"
                     "lakes; raise it slightly when the filled surface feeds\n"
                     "flow accumulation or erosion.";
      add_float(n.attrs, "min_depth", "Ignore puddles below", 0.f, 0.f, 0.2f,
                "Lakes")
          .tooltip = "Depth and mask ignore anything shallower than this, so\n"
                     "a thousand pinprick hollows do not read as lakes. The\n"
                     "filled terrain is unaffected.";
      add_bool(n.attrs, "normalize_depth", "Normalise depth", true, "Lakes")
          .tooltip = "Scales depth to 0..1 so it can drive a mask or a blend\n"
                     "directly. Off leaves it in terrain units.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      std::vector<float> filled =
          fill_depressions(*in, n.attrs.get_f("epsilon", 0.f));

      // Size the outputs from the input rather than from the graph's
      // resolution: a Resample or an imported heightmap upstream makes those
      // two different, and this loop walks them in lockstep.
      Heightmap &out = n.out_hmap("output");
      Heightmap &depth = n.out_hmap("depth");
      Heightmap &mask = n.out_hmap("mask");
      out = *in;
      depth = *in;
      mask = *in;
      const float min_d = n.attrs.get_f("min_depth", 0.f);
      float deepest = 0.f;
      for (size_t i = 0; i < out.v.size(); ++i) {
        out.v[i] = filled[i];
        float d = filled[i] - in->v[i];
        if (d < min_d) d = 0.f;
        depth.v[i] = d;
        mask.v[i] = d > 0.f ? 1.f : 0.f;
        if (d > deepest) deepest = d;
      }
      if (n.attrs.get_b("normalize_depth", true) && deepest > 1e-9f)
        for (float &d : depth.v) d /= deepest;
    })

REGISTER_NODE(
    Flood, "Hydrology", "Standing water at a set level",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      n.add_out("depth");
      n.add_out("water_mask");
      add_float(n.attrs, "level", "Water level", 0.3f, 0.f, 1.f, "Flood");
      add_choice(n.attrs, "mode", "Fill",
                 {"Everywhere below", "Connected to the edge"}, 1, "Flood");
      add_bool(n.attrs, "normalize_depth", "Normalize depth", true, "Flood");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &depth = n.out_hmap("depth");
      Heightmap &mask = n.out_hmap("water_mask");
      out = *in;
      depth = *in;
      mask = *in;
      float mn, mx;
      in->minmax(mn, mx);
      float span = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      float level = mn + n.attrs.get_f("level", 0.3f) * span;
      int w = in->w, h = in->h;
      std::vector<unsigned char> wet((size_t)w * h, 0);
      if (n.attrs.get_choice("mode") == 0) {
        for (size_t i = 0; i < wet.size(); ++i) wet[i] = in->v[i] < level;
      } else {
        // BFS from every boundary cell below the level: only water that can
        // reach the tile edge floods, so basins above the sea stay dry lakes
        std::vector<size_t> queue;
        auto seed = [&](int x, int y) {
          size_t i = (size_t)y * w + x;
          if (!wet[i] && in->v[i] < level) {
            wet[i] = 1;
            queue.push_back(i);
          }
        };
        for (int x = 0; x < w; ++x) { seed(x, 0); seed(x, h - 1); }
        for (int y = 0; y < h; ++y) { seed(0, y); seed(w - 1, y); }
        for (size_t q = 0; q < queue.size(); ++q) {
          size_t c = queue[q];
          int cx = (int)(c % w), cy = (int)(c / w);
          const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
          for (auto &d : nb) {
            int nx = cx + d[0], ny = cy + d[1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            size_t i = (size_t)ny * w + nx;
            if (!wet[i] && in->v[i] < level) {
              wet[i] = 1;
              queue.push_back(i);
            }
          }
        }
      }
      bool norm = n.attrs.get_b("normalize_depth", true);
      float dmax = 1e-12f;
      for (size_t i = 0; i < wet.size(); ++i)
        if (wet[i]) dmax = std::max(dmax, level - in->v[i]);
      for (size_t i = 0; i < wet.size(); ++i) {
        float d = wet[i] ? level - in->v[i] : 0.f;
        out.v[i] = wet[i] ? level : in->v[i];
        depth.v[i] = norm ? d / dmax : d;
        mask.v[i] = wet[i] ? 1.f : 0.f;
      }
    })

REGISTER_NODE(
    Rivers, "Erosion", "Trace rivers from headwaters and carve channels; outputs river + depth masks",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      n.add_out("river_mask");
      n.add_out("water_depth");
      add_int(n.attrs, "headwaters", "Headwaters", 24, 2, 200, "Rivers")
          .tooltip = "Number of river source points seeded on high ground\n"
                     "with strong drainage; streams merge downstream.";
      add_float(n.attrs, "width", "River width", 0.006f, 0.001f, 0.05f, "Rivers");
      add_float(n.attrs, "depth", "Carve depth", 0.05f, 0.005f, 0.3f, "Rivers");
      add_float(n.attrs, "valley_width", "Valley width", 0.02f, 0.f, 0.15f, "Rivers")
          .tooltip = "Soft V-shaped valley carved around the channel.";
      add_float(n.attrs, "widen_downstream", "Widen downstream", 0.6f, 0.f, 1.f,
                "Rivers");
      add_seed(n.attrs);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &rmask = n.out_hmap("river_mask");
      Heightmap &wdepth = n.out_hmap("water_depth");
      out = *in;
      int w = out.w, h = out.h;
      int heads = n.attrs.get_i("headwaters", 24);
      float width = n.attrs.get_f("width", 0.006f) * w;
      float carve = n.attrs.get_f("depth", 0.05f);
      float vwidth = n.attrs.get_f("valley_width", 0.02f) * w;
      float widen = n.attrs.get_f("widen_downstream", 0.6f);
      uint32_t seed = n.attrs.get_seed("seed");
      float mn, mx;
      in->minmax(mn, mx);
      float hamp = (mx - mn) > 1e-9f ? mx - mn : 1.f;

      // flow accumulation (D8) to find good headwaters and guide tracing
      static const int DX8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
      static const int DY8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
      std::vector<int> order((size_t)w * h), receiver((size_t)w * h, -1);
      std::vector<float> area((size_t)w * h, 1.f);
      for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
      std::sort(order.begin(), order.end(),
                [&](int a2, int b2) { return in->v[a2] > in->v[b2]; });
      for (int idx : order) {
        int x = idx % w, y = idx / w;
        float hgt = in->v[idx];
        float best = 0;
        for (int k = 0; k < 8; ++k) {
          int nx2 = x + DX8[k], ny2 = y + DY8[k];
          if (nx2 < 0 || nx2 >= w || ny2 < 0 || ny2 >= h) continue;
          float drop = (hgt - in->at(nx2, ny2)) / ((DX8[k] && DY8[k]) ? 1.414f : 1.f);
          if (drop > best) {
            best = drop;
            receiver[idx] = ny2 * w + nx2;
          }
        }
        if (receiver[idx] >= 0) area[receiver[idx]] += area[idx];
      }

      // pick headwaters: high cells with moderate accumulation, spread by seed
      std::vector<int> sources;
      for (int c = 0; c < heads * 8 && (int)sources.size() < heads; ++c) {
        int x = (int)(noise::hash01(c, 5, seed) * (w - 4)) + 2;
        int y = (int)(noise::hash01(c, 9, seed) * (h - 4)) + 2;
        int idx = y * w + x;
        float t = (in->v[idx] - mn) / hamp;
        if (t > 0.45f) sources.push_back(idx);
      }

      // trace each river down the receiver chain, accumulating strength
      std::vector<float> strength((size_t)w * h, 0.f);
      for (int s : sources) {
        int cur = s;
        float str = 1.f;
        int guard = w * h;
        while (cur >= 0 && guard-- > 0) {
          strength[cur] = std::max(strength[cur], str);
          str += widen * 0.01f * std::log1p(area[cur]);
          cur = receiver[cur];
        }
      }

      // carve: distance-based falloff around river cells (splat channels)
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            if (strength[idx] <= 0) continue;
            float rw = width * (0.6f + 0.4f * strength[idx]);
            float rv = vwidth * (0.6f + 0.4f * strength[idx]);
            int rad = (int)std::ceil(std::max(rw, rv)) + 1;
            float cd = carve * hamp * std::min(strength[idx], 2.f);
            for (int dy = -rad; dy <= rad; ++dy)
              for (int dx = -rad; dx <= rad; ++dx) {
                int nx2 = x + dx, ny2 = y + dy;
                if (nx2 < 0 || nx2 >= w || ny2 < 0 || ny2 >= h) continue;
                float d = std::sqrt((float)(dx * dx + dy * dy));
                // channel: parabolic cross-section
                if (d < rw) {
                  float prof = (1.f - (d / rw) * (d / rw)) * cd;
                  size_t ni = (size_t)ny2 * w + nx2;
                  float cut = in->v[ni] - prof;
                  // note: concurrent min writes race benignly across bands
                  if (cut < out.v[ni]) out.v[ni] = cut;
                  if (prof > wdepth.v[ni]) wdepth.v[ni] = prof;
                  rmask.v[ni] = std::max(rmask.v[ni], 1.f - d / rw);
                } else if (d < rv) {
                  float t = (d - rw) / std::max(rv - rw, 1e-4f);
                  float prof = (1.f - t) * (1.f - t) * cd * 0.5f;
                  size_t ni = (size_t)ny2 * w + nx2;
                  float cut = in->v[ni] - prof;
                  if (cut < out.v[ni]) out.v[ni] = cut;
                }
              }
          }
      });
      // enforce downstream monotonic water surface along traced paths
      for (int s : sources) {
        int cur = s;
        float level = out.v[s];
        int guard = w * h;
        while (cur >= 0 && guard-- > 0) {
          level = std::min(level, out.v[cur]);
          if (out.v[cur] > level) out.v[cur] = level;
          cur = receiver[cur];
        }
      }
      rmask.remap(0.f, 1.f);
      wdepth.remap(0.f, 1.f);
    })

REGISTER_NODE(
    Coast, "Erosion", "Coastal shaping: flat beach band, wave planation, bluff",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      n.add_out("beach_mask");
      add_float(n.attrs, "water_level", "Water level", 0.12f, 0.f, 0.8f, "Coast");
      add_float(n.attrs, "beach_width", "Beach height band", 0.04f, 0.005f, 0.2f,
                "Coast")
          .tooltip = "Heights within this band above water are planed\n"
                     "into a gently sloping beach.";
      add_float(n.attrs, "beach_slope", "Beach slope", 0.25f, 0.02f, 1.f, "Coast");
      add_float(n.attrs, "bluff", "Bluff sharpness", 0.5f, 0.f, 1.f, "Coast")
          .tooltip = "Steepens the cut where the terrain rises out of\n"
                     "the beach band — wave-cut bluffs.";
      add_float(n.attrs, "underwater_smooth", "Underwater smoothing", 0.4f, 0.f, 1.f,
                "Coast");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &bmask = n.out_hmap("beach_mask");
      out = *in;
      float mn, mx;
      in->minmax(mn, mx);
      float hamp = (mx - mn) > 1e-9f ? mx - mn : 1.f;
      float wl = mn + n.attrs.get_f("water_level", 0.12f) * hamp;
      float bw = n.attrs.get_f("beach_width", 0.04f) * hamp;
      float bslope = n.attrs.get_f("beach_slope", 0.25f);
      float bluff = n.attrs.get_f("bluff", 0.5f);
      float usm = n.attrs.get_f("underwater_smooth", 0.4f);
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float hgt = in->v[i];
          float rel = hgt - wl;
          if (rel >= 0 && rel <= bw) {
            // plane into gentle beach with a bluff shoulder at the top
            float t = rel / bw;
            float beach = wl + rel * bslope;
            float shoulder = t > 0.7f ? std::pow((t - 0.7f) / 0.3f, 1.f + bluff * 3.f)
                                      : 0.f;
            out.v[i] = beach + shoulder * (hgt - beach);
            bmask.v[i] = 1.f - t;
          } else if (rel < 0) {
            // smooth shallow seabed toward the waterline
            float depth01 = std::clamp(-rel / (bw * 2.f), 0.f, 1.f);
            out.v[i] = hgt + (wl - bw * 0.3f - hgt) * usm * (1.f - depth01) * 0.5f;
            bmask.v[i] = 0.f;
          } else {
            bmask.v[i] = 0.f;
          }
        }
      });
    })

} // namespace gpx
